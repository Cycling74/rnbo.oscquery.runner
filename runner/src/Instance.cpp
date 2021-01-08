#include <memory>
#include <iostream>
#include <functional>
#include <filesystem>
#include <sndfile.hh>

#include "Config.h"
#include "Instance.h"
#include "JackAudio.h"
#include "PatcherFactory.h"

using RNBO::ParameterIndex;
using RNBO::ParameterInfo;
using RNBO::ParameterType;
using RNBO::ParameterValue;

namespace fs = std::filesystem;

namespace {
	static const std::chrono::milliseconds command_wait_timeout(10);
}

//helper for c-style callback from ossia, so that we can use std func with captures.
class Instance::ValueCallbackHelper {
	public:
		ValueCallbackHelper(std::function<void(const opp::value& val)> func) : mFunc(func) { }
		void call(const opp::value& val) {
			mFunc(val);
		}
	private:
		std::function<void(const opp::value& val)> mFunc;
};

void Instance::valueCallbackTrampoline(void* context, const opp::value& val) {
	Instance::ValueCallbackHelper * helper = reinterpret_cast<Instance::ValueCallbackHelper *>(context);
	if (helper)
		helper->call(val);
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

	builder([this](opp::node root) {
		//setup parameters
		auto params = root.create_child("params");
		params.set_description("parameter get/set");
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

			//set the callback, using our helper
			auto h = std::make_shared<ValueCallbackHelper>([this, index](const opp::value& val) {
					if (val.is_float())
						mCore->setParameterValue(index, val.to_float());
			});
			p.set_value_callback(valueCallbackTrampoline, h.get());
			mValueCallbackHelpers.push_back(h);
			mIndexToNode[index] = p;
			mNodes.push_back(p);
		}
		mNodes.push_back(params);

		auto dataRefs = root.create_child("data_refs");
		for (auto index = 0; index < mCore->getNumExternalDataRefs(); index++) {
			auto id = mCore->getExternalDataId(index);
			std::string name(id);
			auto d = dataRefs.create_string(name);
			auto h = std::make_shared<ValueCallbackHelper>([this, id](const opp::value& val) {
					if (val.is_string())
						mDataRefCommandQueue.push(DataRefCommand(val.to_string(), id));
			});
			d.set_value_callback(valueCallbackTrampoline, h.get());
			mValueCallbackHelpers.push_back(h);
			mDataRefNodes[id] = d;
		}

		//indicate the presets
		auto presets = root.create_child("presets");
		auto entries = presets.create_list("entries");
		entries.set_access(opp::access_mode::Get);
		std::vector<opp::value> names;
		for (auto &kv : mPresets) {
			names.push_back(opp::value(kv.first));
		}
		entries.set_value(opp::value(names));

		auto load = presets.create_string("load");
		load.set_access(opp::access_mode::Set);
		auto h = std::make_shared<ValueCallbackHelper>([this](const opp::value& val) {
			//TODO do we want to move this to another thread?
			if (val.is_string()) {
				loadPreset(val.to_string());
			}
		});
		load.set_value_callback(valueCallbackTrampoline, h.get());
		mValueCallbackHelpers.push_back(h);

		mNodes.push_back(presets);
		mNodes.push_back(entries);
		mNodes.push_back(load);
	});

	//auto load data refs
	auto datarefs = conf["datarefs"];
	if (datarefs.is_object()) {
		for (auto it = datarefs.begin(); it != datarefs.end(); ++it) {
			std::string value = it.value();
			if (value.size() > 0)
				mDataRefCommandQueue.push(DataRefCommand(value, it.key().c_str()));
		}
	}
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

void Instance::processDataRefCommands() {
	fs::path dataFileDir = config::get<fs::path>(config::key::DataFileDir);
	fs::create_directories(dataFileDir);

	while (mDataRefProcessCommands.load()) {
		auto cmdOpt = mDataRefCommandQueue.popTimeout(command_wait_timeout);
		if (!cmdOpt.has_value())
			continue;
		auto cmd = cmdOpt.value();
		mCore->releaseExternalData(cmd.id.c_str());
		mDataRefs.erase(cmd.id);

		if (!cmd.fileName.empty()) {
			auto filePath = dataFileDir / fs::path(cmd.fileName);
			if (!fs::exists(filePath)) {
				std::cerr << "no file at " << filePath << std::endl;
				//TODO clear node value?
				continue;
			}
			SndfileHandle sndfile(filePath.u8string());
			if (!sndfile) {
				std::cerr << "couldn't open as sound file " << filePath << std::endl;
				//TODO clear node value?
				continue;
			}

			//actually read in audio and set the data
			auto data = std::make_shared<std::vector<float>>(static_cast<size_t>(sndfile.channels()) * static_cast<size_t>(sndfile.frames()));
			auto framesRead = sndfile.readf(&data->front(), sndfile.frames());

			//TODO store file name so we don't double load file?
			mDataRefs[cmd.id] = data;

			//set the dataref data
			RNBO::Float32AudioBuffer bufferType(sndfile.channels(), static_cast<double>(sndfile.samplerate()));
			mCore->setExternalData(cmd.id.c_str(), reinterpret_cast<char *>(&data->front()), sizeof(float) * framesRead * sndfile.channels(), bufferType, [data](RNBO::ExternalDataId, char*) mutable {
					//hold onto data shared_ptr until rnbo stops using it
					data.reset();
			});
		}
	}
}

void Instance::processEvents() {
	mEventHandler->processEvents();
	mAudio->poll();
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
}
