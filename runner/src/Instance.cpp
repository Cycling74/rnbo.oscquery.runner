#include <memory>
#include <iostream>
#include <functional>
#include <filesystem>
#include <sndfile.hh>
#include <readerwriterqueue/readerwriterqueue.h>

#include "Config.h"
#include "Instance.h"
#include "JackAudio.h"
#include "PatcherFactory.h"
#include "ValueCallbackHelper.h"

using RNBO::ParameterIndex;
using RNBO::ParameterInfo;
using RNBO::ParameterType;
using RNBO::ParameterValue;

namespace fs = std::filesystem;

namespace {
	static const std::chrono::milliseconds command_wait_timeout(10);
	static const std::string initial_preset_key = "preset_initial";
	static const std::string last_preset_key = "preset_last";
}

Instance::Instance(std::shared_ptr<PatcherFactory> factory, std::string name, NodeBuilder builder, RNBO::Json conf) : mPatcherFactory(factory), mDataRefProcessCommands(true) {
	//RNBO is telling us we have a parameter update, tell ossia
	auto paramCallback = [this](RNBO::ParameterIndex index, RNBO::ParameterValue value) {
		auto it = mIndexToNode.find(index);
		if (it != mIndexToNode.end()) {
			it->second.set_value(value);
		}
	};
	mEventHandler = std::unique_ptr<EventHandler>(new EventHandler(paramCallback));
	mCore = std::make_shared<RNBO::CoreObject>(mPatcherFactory->createInstance(), mEventHandler.get());
	mAudio = std::unique_ptr<InstanceAudioJack>(new InstanceAudioJack(mCore, name, builder));

	mPresetSavedQueue = std::make_unique<moodycamel::ReaderWriterQueue<std::pair<std::string, RNBO::ConstPresetPtr>, 2>>(2);

	//parse out presets
	auto presets = conf["presets"];
	if (presets.is_object()) {
		for (auto it = presets.begin(); it != presets.end(); ++it) {
			auto presetJson = it.value();
			RNBO::PresetPtr preset = std::make_shared<RNBO::Preset>();
			RNBO::convertJSONObjToPreset(presetJson, *preset);
			mPresets[it.key()] = std::move(preset);
		}
	}

	//grab the initial preset and store it, so we can use it as a tree value
	{
		auto presetInit = conf[initial_preset_key];
		if (presetInit.is_string())
			mPresetInitial = presetInit;
	}

	builder([this](opp::node root) {
		//setup parameters
		auto params = root.create_child("params");
		params.set_description("Parameter get/set");
		for (RNBO::ParameterIndex index = 0; index < mCore->getNumParameters(); index++) {
			ParameterInfo info;
			mCore->getParameterInfo(index, &info);
			//only supporting numbers right now
			//don't bind invisible or debug
			if (info.type != ParameterType::ParameterTypeNumber || !info.visible || info.debug)
				continue;

			//set parameter access, range, etc etc
			auto p = params.create_float(mCore->getParameterId(index));
			p.set_access(opp::access_mode::Bi);
			p.set_value(info.initialValue);
			p.set_min(info.min);
			p.set_max(info.max);
			p.set_bounding(opp::bounding_mode::Clip);

			ValueCallbackHelper::setCallback(
				p, mValueCallbackHelpers,
				[this, index](const opp::value& val) {
				if (val.is_float())
					mCore->setParameterValue(index, val.to_float());
				});
			mIndexToNode[index] = p;
			mNodes.push_back(p);
		}
		mNodes.push_back(params);

		auto dataRefs = root.create_child("data_refs");
		for (auto index = 0; index < mCore->getNumExternalDataRefs(); index++) {
			auto id = mCore->getExternalDataId(index);
			std::string name(id);
			auto d = dataRefs.create_string(name);
			ValueCallbackHelper::setCallback(
				d, mValueCallbackHelpers,
					[this, id](const opp::value& val) {
						if (val.is_string())
							mDataRefCommandQueue.push(DataRefCommand(val.to_string(), id));
				});
			mDataRefNodes[id] = d;
		}

		//indicate the presets
		auto presets = root.create_child("presets");
		mPresetEntires = presets.create_list("entries");
		mPresetEntires.set_description("A list of presets that can be loaded");
		mPresetEntires.set_access(opp::access_mode::Get);
		updatePresetEntries();

		//save preset, pass name
		auto save = presets.create_string("save");
		save.set_description("Save the current settings as a preset with the given name");
		save.set_access(opp::access_mode::Set);
		ValueCallbackHelper::setCallback(
			save, mValueCallbackHelpers,
			[this](const opp::value& val) {
				if (val.is_string()) {
					std::string name = val.to_string();
					mCore->getPreset([name, this] (RNBO::ConstPresetPtr preset) {
							mPresetSavedQueue->try_enqueue(std::make_pair(name, preset));
					});
				}
			});

		auto load = presets.create_string("load");
		load.set_description("Load a preset with the given name");
		load.set_access(opp::access_mode::Set);
		ValueCallbackHelper::setCallback(
			load, mValueCallbackHelpers,
			[this](const opp::value& val) {
				//TODO do we want to move this to another thread?
				if (val.is_string()) {
					loadPreset(val.to_string());
				}
			});

		auto init = presets.create_string("initial");
		init.set_description("Indicate a preset, by name, that should be loaded every time this patch is reloaded. Set to an empty string to load the loaded preset instead");
		if (!mPresetInitial.empty())
			init.set_value(mPresetInitial);
		ValueCallbackHelper::setCallback(
			init, mValueCallbackHelpers,
			[this](const opp::value& val) {
				if (val.is_string()) {
					std::lock_guard<std::mutex> guard(mPresetMutex);
					//TODO validate?
					mPresetInitial = val.to_string();
					queueConfigChangeSignal();
				}
			});

		mNodes.push_back(presets);
		mNodes.push_back(load);
		mNodes.push_back(init);
		mNodes.push_back(save);
	});

	//auto load data refs
	auto datarefs = conf["datarefs"];
	if (datarefs.is_object()) {
		for (auto it = datarefs.begin(); it != datarefs.end(); ++it) {
			std::string value = it.value();
			if (value.size() > 0)
				loadDataRef(it.key(), value);
		}
	}

	//load the initial or last preset, if in the config
	if (!mPresetInitial.empty()) {
		loadPreset(mPresetInitial);
	} else if (conf[last_preset_key].is_string()) {
		loadPreset(conf[last_preset_key]);
	}

	//incase we changed it
	mConfigChanged = false;
	mDataRefThread = std::thread(&Instance::processDataRefCommands, this);
}

Instance::~Instance() {
	mDataRefProcessCommands.store(false);
	mDataRefThread.join();
	stop();
	mAudio.reset();
	mEventHandler.reset();
	mCore.reset();
	mPatcherFactory.reset();
}

void Instance::start() {
	mAudio->start();
}

void Instance::stop() {
	mAudio->stop();
}

void Instance::registerConfigChangeCallback(std::function<void()> cb) {
	mConfigChangeCallback = cb;
}

void Instance::processDataRefCommands() {
	while (mDataRefProcessCommands.load()) {
		auto cmdOpt = mDataRefCommandQueue.popTimeout(command_wait_timeout);
		if (!cmdOpt.has_value())
			continue;
		auto cmd = cmdOpt.value();
		if (loadDataRef(cmd.id, cmd.fileName))
			queueConfigChangeSignal();
	}
}

void Instance::processEvents() {
	mEventHandler->processEvents();
	mAudio->poll();

	//store any presets that we got
	std::pair<std::string, RNBO::ConstPresetPtr> namePreset;
	bool updated = false;
	while (mPresetSavedQueue->try_dequeue(namePreset)) {
		std::lock_guard<std::mutex> guard(mPresetMutex);
		updated = true;
		mPresets[namePreset.first] = namePreset.second;
		mPresetLatest = namePreset.first;
	}
	if (updated)
		updatePresetEntries();

	//see if we should signal a change
	auto changed = false;
	{
		std::lock_guard<std::mutex> guard(mConfigChangedMutex);
		changed = mConfigChanged;
		mConfigChanged = false;
	}
	if (changed && mConfigChangeCallback != nullptr) {
		mConfigChangeCallback();
	}
}

void Instance::loadPreset(std::string name) {
	std::lock_guard<std::mutex> guard(mPresetMutex);
	auto it = mPresets.find(name);
	if (it == mPresets.end()) {
		std::cerr << "couldn't find preset with name " << name << std::endl;
		return;
	}
	RNBO::UniquePresetPtr preset = std::make_unique<RNBO::Preset>();
	auto shared = it->second;
	RNBO::copyPreset(*shared, *preset);
	mCore->setPreset(std::move(preset));
	mPresetLatest = name;
	queueConfigChangeSignal();
}

RNBO::Json Instance::currentConfig() {
	RNBO::Json config = RNBO::Json::object();
	RNBO::Json presets = RNBO::Json::object();
	RNBO::Json datarefs = RNBO::Json::object();
	//copy presets
	{
		std::lock_guard<std::mutex> pguard(mPresetMutex);
		for (auto& kv: mPresets)
			presets[kv.first] = RNBO::convertPresetToJSONObj(*kv.second);
		//indicate the initial and latest preset if we loaded this config again
		if (!mPresetInitial.empty())
			config[initial_preset_key] = mPresetInitial;
		if (!mPresetLatest.empty())
			config[last_preset_key] = mPresetLatest;
	}
	//copy datarefs
	{
		std::lock_guard<std::mutex> bguard(mDataRefFileNameMutex);
		for (auto& kv: mDataRefFileNameMap)
			datarefs[kv.first] = kv.second;
	}
	config["presets"] = presets;
	config["datarefs"] = datarefs;
	return config;
}
void Instance::updatePresetEntries() {
	std::lock_guard<std::mutex> guard(mPresetMutex);
	std::vector<opp::value> names;
	for (auto &kv : mPresets) {
		names.push_back(opp::value(kv.first));
	}
	mPresetEntires.set_value(opp::value(names));
}

void Instance::queueConfigChangeSignal() {
	std::lock_guard<std::mutex> guard(mConfigChangedMutex);
	mConfigChanged = true;
}

bool Instance::loadDataRef(const std::string& id, const std::string& fileName) {
	auto dataFileDir = config::get<fs::path>(config::key::DataFileDir);
	mCore->releaseExternalData(id.c_str());
	mDataRefs.erase(id);
	if (fileName.empty())
		return true;
	auto filePath = dataFileDir / fs::path(fileName);
	if (!fs::exists(filePath)) {
		std::cerr << "no file at " << filePath << std::endl;
		//TODO clear node value?
		return false;
	}
	SndfileHandle sndfile(filePath.u8string());
	if (!sndfile) {
		std::cerr << "couldn't open as sound file " << filePath << std::endl;
		//TODO clear node value?
		return false;
	}

	//actually read in audio and set the data
	auto data = std::make_shared<std::vector<float>>(static_cast<size_t>(sndfile.channels()) * static_cast<size_t>(sndfile.frames()));
	auto framesRead = sndfile.readf(&data->front(), sndfile.frames());

	//TODO check mDataRefFileNameMap so we don't double load?
	mDataRefs[id] = data;

	//store the mapping so we can persist
	{
		std::lock_guard<std::mutex> guard(mDataRefFileNameMutex);
		mDataRefFileNameMap[id] = fileName;
	}

	//set the dataref data
	RNBO::Float32AudioBuffer bufferType(sndfile.channels(), static_cast<double>(sndfile.samplerate()));
	mCore->setExternalData(id.c_str(), reinterpret_cast<char *>(&data->front()), sizeof(float) * framesRead * sndfile.channels(), bufferType, [data](RNBO::ExternalDataId, char*) mutable {
			//hold onto data shared_ptr until rnbo stops using it
			data.reset();
	});
	return true;
}
