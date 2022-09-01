#include <memory>
#include <iostream>
#include <functional>

#include <sndfile.hh>
#include <readerwriterqueue/readerwriterqueue.h>
#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

#include <ossia/network/generic/generic_device.hpp>
#include <ossia/network/generic/generic_parameter.hpp>

#include "Config.h"
#include "Instance.h"
#include "JackAudio.h"
#include "PatcherFactory.h"

using RNBO::ParameterIndex;
using RNBO::ParameterInfo;
using RNBO::ParameterType;
using RNBO::ParameterValue;
using RNBO::MessageEvent;

namespace fs = boost::filesystem;

namespace {
	static const std::chrono::milliseconds command_wait_timeout(10);
	static const std::string initial_preset_key = "preset_initial";
	static const std::string last_preset_key = "preset_last";
	static const std::string preset_midi_channel_key = "preset_midi_channel";
	static const std::map<std::string, int> preset_midi_channel_values = {
		{"omni", 0},
		{"1", 1},
		{"2", 2},
		{"3", 3},
		{"4", 4},
		{"5", 5},
		{"6", 6},
		{"7", 7},
		{"8", 8},
		{"9", 9},
		{"10", 10},
		{"11", 11},
		{"12", 12},
		{"13", 13},
		{"14", 14},
		{"15", 15},
		{"16", 16},
		{"none", 17} //17 will never be valid
	};

	//recursively get values, if we can
	boost::optional<ossia::value> get_value(const RNBO::Json& meta) {
		boost::optional<ossia::value> value = boost::none;
		if (meta.is_boolean()){
			value = ossia::value(meta.get<bool>());
		} else if (meta.is_number()) {
			value = ossia::value(meta.get<double>());
		} else if (meta.is_string()) {
			value = ossia::value(meta.get<std::string>());
		} else if (meta.is_null()) {
			value = ossia::value(ossia::impulse());
		} else if (meta.is_array()) {
			//if all children are simple values, we can append
			std::vector<ossia::value> values;
			for (auto i: meta) {
				auto v = get_value(i);
				if (v) {
					values.push_back(*v);
				} else {
					return boost::none;
				}
			}
			value = ossia::value(values);
		} //else, object, not a simple value
		return value;
	}

	void recurse_add_meta(const RNBO::Json& meta, ossia::net::node_base * parent) {
		if (meta.is_array()) {
			//if we can get an ossia::value from the array, we can just create a LIST param
			auto value = get_value(meta);
			if (value) {
				auto p = parent->create_parameter(ossia::val_type::LIST);
				p->push_value(*value);
			} else {
				int index = 0;
				for (auto i: meta) {
					auto c = parent->create_child(std::to_string(index++));
					recurse_add_meta(i, c);
					c->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				}
			}
		} else if (meta.is_object()) {
			for (auto it = meta.begin(); it != meta.end(); ++it) {
				auto c = parent->create_child(it.key());
				recurse_add_meta(it.value(), c);
				c->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
			}
		} else if (meta.is_boolean()) {
			auto p = parent->create_parameter(ossia::val_type::BOOL);
			p->push_value(meta.get<bool>());
		} else if (meta.is_string()) {
			auto p = parent->create_parameter(ossia::val_type::STRING);
			p->push_value(meta.get<std::string>());
		} else if (meta.is_number()) {
			auto p = parent->create_parameter(ossia::val_type::FLOAT);
			p->push_value(meta.get<double>());
		} else if (meta.is_null()) {
			auto p = parent->create_parameter(ossia::val_type::IMPULSE);
			p->push_value(ossia::impulse());
		} else {
			//assert?
			return;
		}
	}

	void add_meta_to_param(RNBO::Json& meta, ossia::net::node_base& param) {
		if (meta.is_object() && meta.size()) {
			auto m = param.create_child("meta");
			auto p = m->create_parameter(ossia::val_type::STRING);
			p->push_value(meta.dump());
			m->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
			recurse_add_meta(meta, m);
		}
	}
}

Instance::Instance(std::shared_ptr<PatcherFactory> factory, std::string name, NodeBuilder builder, RNBO::Json conf, std::shared_ptr<ProcessAudio> processAudio) : mPatcherFactory(factory), mDataRefProcessCommands(true), mConfig(conf) {
	std::unordered_map<std::string, std::string> dataRefMap;

	//load up data ref map so we can set the initial value
	auto datarefs = conf["datarefs"];
	if (datarefs.is_object()) {
		for (auto it = datarefs.begin(); it != datarefs.end(); ++it) {
			dataRefMap[it.key()] = it.value();
		}
	}

	//setup initial preset channel mapping
	try {
		std::string chanName = "omni";
		auto chan = conf[preset_midi_channel_key];
		if (chan.is_string()) {
			chanName = chan.get<std::string>();
		} else if (auto o = config::get<std::string>(config::key::PresetMIDIProgramChangeChannel)) {
			chanName = *o;
		}
		auto it = preset_midi_channel_values.find(chanName);
		if (it != preset_midi_channel_values.end()) {
			mPresetProgramChangeChannel = it->second;
		}
	} catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
	}

	auto msgCallback =
		[this](RNBO::MessageEvent msg) {
			//only send messages that aren't targeted for a specific object
			if (msg.getObjectId() == 0)
				handleOutportMessage(msg);
		};
	auto midiCallback =
		[this](RNBO::MidiEvent e) {
			handleMidiCallback(e);
		};

	auto transportCallback = [processAudio](RNBO::TransportEvent e) {
		processAudio->handleTransportState(e.getState());
	};
	auto tempoCallback = [processAudio](RNBO::TempoEvent e) {
		processAudio->handleTransportTempo(e.getTempo());
	};
	auto beatTimeCallback = [processAudio](RNBO::BeatTimeEvent e) {
		processAudio->handleTransportBeatTime(e.getBeatTime());
	};
	auto timeSigCallback = [processAudio](RNBO::TimeSignatureEvent e) {
		processAudio->handleTransportTimeSig(e.getNumerator(), e.getDenominator());
	};

	mEventHandler = std::unique_ptr<EventHandler>(new EventHandler(
				std::bind(&Instance::handleParamUpdate, this, std::placeholders::_1, std::placeholders::_2),
				msgCallback,
				transportCallback, tempoCallback, beatTimeCallback, timeSigCallback,
				midiCallback));
	mCore = std::make_shared<RNBO::CoreObject>(mPatcherFactory->createInstance(), mEventHandler.get());
	mAudio = std::unique_ptr<InstanceAudioJack>(new InstanceAudioJack(mCore, name, builder, std::bind(&Instance::handleProgramChange, this, std::placeholders::_1)));

	mPresetSavedQueue = RNBO::make_unique<moodycamel::ReaderWriterQueue<std::pair<std::string, RNBO::ConstPresetPtr>, 2>>(2);
	mDataRefCleanupQueue = RNBO::make_unique<moodycamel::ReaderWriterQueue<std::shared_ptr<std::vector<float>>, 32>>(32);

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

	builder([this, &dataRefMap, conf](ossia::net::node_base * root) {

		//setup parameters
		auto params = root->create_child("params");
		RNBO::Json paramConfig;
		if (conf.contains("parameters")) {
			paramConfig = conf["parameters"];
		}

		params->set(ossia::net::description_attribute{}, "Parameter get/set");
		for (RNBO::ParameterIndex index = 0; index < mCore->getNumParameters(); index++) {

			auto add_meta = [index, &paramConfig, this](ossia::net::node_base& param) {
				if (!paramConfig.is_array()) {
					return;
				}
				//find the parameter
				for (auto p: paramConfig) {
					if (p.contains("index") && p["index"].is_number() && static_cast<RNBO::ParameterIndex>(p["index"].get<double>()) == index) {
						if (p.contains("meta")) {
							auto meta = p["meta"];
							add_meta_to_param(meta, param);
						}
					}
				}
			};

			ParameterInfo info;
			mCore->getParameterInfo(index, &info);
			//only supporting numbers right now
			//don't bind invisible or debug
			if (info.type != ParameterType::ParameterTypeNumber || !info.visible || info.debug)
				continue;

			//use a mutex to make sure we don't update in a loop
			auto active = std::make_shared<std::mutex>();

			//create normalized version
			auto cnorm = [this, info, index, active](ossia::net::node_base& param) -> ossia::net::parameter_base * {
				auto n = param.create_child("normalized");
				auto p = n->create_parameter(ossia::val_type::FLOAT);

				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
				n->set(ossia::net::domain_attribute{}, ossia::make_domain(0., 1.));
				n->set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);

				p->push_value(mCore->convertToNormalizedParameterValue(index, info.initialValue));

				//normalized callback
				p->add_callback([this, index, active](const ossia::value& val) mutable {
					if (val.get_type() == ossia::val_type::FLOAT) {
						if (auto _lock = std::unique_lock<std::mutex> (*active, std::try_to_lock)) {
							double f = static_cast<double>(val.get<float>());
							f = mCore->convertFromNormalizedParameterValue(index, f);
							mCore->setParameterValue(index, f);
							handleParamUpdate(index, f);
						}
					}
				});

				return p;
			};

			if (info.enumValues == nullptr) {
				//numerical parameters
				//set parameter access, range, etc etc
				auto& n = ossia::net::create_node(*params, mCore->getParameterId(index));
				auto p = n.create_parameter(ossia::val_type::FLOAT);

				n.set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
				n.set(ossia::net::domain_attribute{}, ossia::make_domain(info.min, info.max));
				n.set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);
				p->push_value(info.initialValue);
				add_meta(n);

				//normalized
				auto norm = cnorm(n);

				//param callback, set norm
				p->add_callback([this, index, active, norm](const ossia::value& val) mutable {
					if (val.get_type() == ossia::val_type::FLOAT) {
						if (auto _lock = std::unique_lock<std::mutex> (*active, std::try_to_lock)) {
							double f = static_cast<double>(val.get<float>());
							mCore->setParameterValue(index, f);
							f = mCore->convertToNormalizedParameterValue(index, f);
							norm->push_value(static_cast<float>(f));
						}
					}
				});

				//setup lookup, no enum
				mIndexToParam[index] = std::make_pair(p, boost::none);

			} else {
				//enumerated parameters
				std::vector<ossia::value> values;

				//build out lookup maps between strings and numbers
				std::unordered_map<std::string, ParameterValue> nameToVal;
				std::unordered_map<int, std::string> valToName;
				for (int e = 0; e < info.steps; e++) {
					std::string s(info.enumValues[e]);
					values.push_back(s);
					nameToVal[s] = static_cast<ParameterValue>(e);
					valToName[e] = s;
				}

				auto& n = ossia::net::create_node(*params, mCore->getParameterId(index));
				auto p = n.create_parameter(ossia::val_type::STRING);

				auto dom = ossia::init_domain(ossia::val_type::STRING);
				ossia::set_values(dom, values);
				n.set(ossia::net::domain_attribute{}, dom);
				n.set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);
				p->push_value(info.enumValues[std::min(std::max(0, static_cast<int>(info.initialValue)), info.steps - 1)]);
				add_meta(n);

				//normalized
				auto norm = cnorm(n);

				p->add_callback([this, index, nameToVal, active, norm](const ossia::value& val) mutable {
					if (val.get_type() == ossia::val_type::STRING) {
						if (auto _lock = std::unique_lock<std::mutex> (*active, std::try_to_lock)) {
							auto f = nameToVal.find(val.get<std::string>());
							if (f != nameToVal.end()) {
								mCore->setParameterValue(index, f->second);
								norm->push_value(static_cast<float>(mCore->convertToNormalizedParameterValue(index, f->second)));
							}
						}
					}
				});
				//set lookup with enum lookup
				mIndexToParam[index] = std::make_pair(p, valToName);
			}

		}

		{
			auto dataRefs = root->create_child("data_refs");
			for (auto index = 0; index < mCore->getNumExternalDataRefs(); index++) {
				auto id = mCore->getExternalDataId(index);
				std::string name(id);
				auto n = dataRefs->create_child(name);
				auto d = n->create_parameter(ossia::val_type::STRING);
				auto it = dataRefMap.find(name);
				if (it != dataRefMap.end()) {
					d->push_value(it->second);
				}
				d->add_callback(
					[this, id](const ossia::value& val) {
						if (val.get_type() == ossia::val_type::STRING)
							mDataRefCommandQueue.push(DataRefCommand(val.get<std::string>(), id));
				});
				mDataRefNodes.emplace(name, d);
			}
		}

		{
			//indicate the presets
			auto presets = root->create_child("presets");
			{
				auto n = presets->create_child("entries");
				mPresetEntries = n->create_parameter(ossia::val_type::LIST);
				n->set(ossia::net::description_attribute{}, "A list of presets that can be loaded");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				updatePresetEntries();
			}

			//save preset, pass name
			{
				auto n = presets->create_child("save");
				auto save = n->create_parameter(ossia::val_type::STRING);
				n->set(ossia::net::description_attribute{}, "Save the current settings as a preset with the given name");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);

				save->add_callback([this](const ossia::value& val) {
					if (val.get_type() == ossia::val_type::STRING) {
						mPresetCommandQueue.push(PresetCommand(PresetCommand::CommandType::Save, val.get<std::string>()));
					}
				});
			}

			{
				auto n = presets->create_child("load");
				auto load = n->create_parameter(ossia::val_type::STRING);

				n->set(ossia::net::description_attribute{}, "Load a preset with the given name");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
				load->add_callback([this](const ossia::value& val) {
					if (val.get_type() == ossia::val_type::STRING) {
						mPresetCommandQueue.push(PresetCommand(PresetCommand::CommandType::Load, val.get<std::string>()));
					}
				});
			}

			{
				auto n = presets->create_child("delete");
				auto del = n->create_parameter(ossia::val_type::STRING);

				n->set(ossia::net::description_attribute{}, "Delete a preset with the given name");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
				del->add_callback([this](const ossia::value& val) {
					if (val.get_type() == ossia::val_type::STRING) {
						mPresetCommandQueue.push(PresetCommand(PresetCommand::CommandType::Delete, val.get<std::string>()));
					}
				});
			}

			{
				auto n = presets->create_child("del");
				auto del = n->create_parameter(ossia::val_type::STRING);

				n->set(ossia::net::description_attribute{}, "Delete a preset with the given name");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
				del->add_callback([this](const ossia::value& val) {
					if (val.get_type() == ossia::val_type::STRING) {
						{
							auto ps = val.get<std::string>();
							std::lock_guard<std::mutex> guard(mPresetMutex);
							if (mPresets.erase(ps)) {
								if (mPresetInitial == ps)
									mPresetInitial.clear();
								if (mPresetLatest == ps)
									mPresetLatest.clear();
							} else {
								return;
							}
						}
						updatePresetEntries();
						queueConfigChangeSignal();
					}
				});
			}

			{
				auto n = presets->create_child("initial");
				mPresetInitialParam = n->create_parameter(ossia::val_type::STRING);
				n->set(ossia::net::description_attribute{}, "Indicate a preset, by name, that should be loaded every time this patch is reloaded. Set to an empty string to load the last loaded preset instead");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);


				mPresetInitialParam->add_callback([this](const ossia::value& val) {
					if (val.get_type() == ossia::val_type::STRING) {
						mPresetCommandQueue.push(PresetCommand(PresetCommand::CommandType::Initial, val.get<std::string>()));
					}
				});
				if (!mPresetInitial.empty() && mPresets.find(mPresetInitial) != mPresets.end())
					mPresetInitialParam->push_value(mPresetInitial);
			}

			{
				auto n = presets->create_child("midi_channel");

				std::vector<ossia::value> values;
				for (auto& kv: preset_midi_channel_values) {
					values.push_back(kv.first);
				}

				mPresetProgramChangeChannelParam = n->create_parameter(ossia::val_type::STRING);

				auto dom = ossia::init_domain(ossia::val_type::STRING);
				ossia::set_values(dom, values);
				n->set(ossia::net::domain_attribute{}, dom);
				n->set(ossia::net::description_attribute{}, "Indicate MIDI channel, none or omni (all) that should be used for changing presets via Program Changes.");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
				n->set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);

				for (auto& kv: preset_midi_channel_values) {
					if (kv.second == mPresetProgramChangeChannel) {
						mPresetProgramChangeChannelParam->push_value(kv.first);
						break;
					}
				}

				mPresetProgramChangeChannelParam->add_callback([this](const ossia::value& val) {
					if (val.get_type() == ossia::val_type::STRING) {
						auto it = preset_midi_channel_values.find(val.get<std::string>());
						if (it != preset_midi_channel_values.end()) {
							mPresetProgramChangeChannel = it->second;
							queueConfigChangeSignal();
						}
					}
				});
			}
		}

		try {
			if (conf.contains("outports") && conf.contains("inports")) {
				auto outports = conf["outports"];
				auto inports = conf["inports"];
				bool hasInports = (inports.is_array() && inports.size() > 0);
				bool hasOutports = (outports.is_array() && outports.size() > 0);
				if (hasInports || hasOutports) {
					auto msgs = root->create_child("messages");
					if (hasInports) {
						auto in = msgs->create_child("in");
						for (auto i: inports) {
							std::string name = i["tag"];
							auto tag = RNBO::TAG(name.c_str());

							auto& n = ossia::net::create_node(*in, name);
							auto p = n.create_parameter(ossia::val_type::LIST);
							n.set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);

							if (i.contains("meta"))
							{
								auto meta = i["meta"];
								add_meta_to_param(meta, n);
							}

							p->add_callback([this, tag](const ossia::value& val) {
								handleInportMessage(tag, val);
							});
						}
					}
					if (hasOutports) {
						auto o = msgs->create_child("out");
						for (auto i: outports) {
							std::string name = i["tag"];
							auto& n = ossia::net::create_node(*o, name);
							auto p = n.create_parameter(ossia::val_type::LIST);
							n.set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);

							if (i.contains("meta"))
							{
								auto meta = i["meta"];
								add_meta_to_param(meta, n);
							}

							mOutportParams[name] = p;
						}
					}
				}
			}
		} catch (const std::exception& e) {
			std::cerr << "exception processing ports: " << e.what() << std::endl;
		}

		//setup virtual midi
		{
			auto vmidi = root->create_child("midi");
			auto n = vmidi->create_child("in");
			auto in = n->create_parameter(ossia::val_type::LIST);
			n->set(ossia::net::description_attribute{}, "midi events in to your RNBO patch");
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
			//handle a virtual midi input, route into RNBO object
			in->add_callback( [this](const ossia::value& val) {
				if (val.get_type() == ossia::val_type::LIST) {
					auto l = val.get<std::vector<ossia::value>>();
					std::vector<uint8_t> bytes;
					for (auto v: l) {
						if (v.get_type() == ossia::val_type::INT) {
							bytes.push_back(static_cast<uint8_t>(v.get<int>()));
						} else if (v.get_type() == ossia::val_type::FLOAT) {
							//shouldn't get float but just in case?
							bytes.push_back(static_cast<uint8_t>(floorf(v.get<float>())));
						} else {
							//XXX cerr
							return;
						}
					}
					mCore->scheduleEvent(RNBO::MidiEvent(0, 0, &bytes.front(), bytes.size()));
				}
			});

			{
				auto n = vmidi->create_child("out");
				mMIDIOutParam = n->create_parameter(ossia::val_type::LIST);
				n->set(ossia::net::description_attribute{}, "midi events out of your RNBO patch");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
			}
		}
	});

	//auto load data refs
	for (auto& kv: dataRefMap) {
		loadDataRefCleanup(kv.first, kv.second);
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
		if (!cmdOpt)
			continue;
		DataRefCommand cmd = cmdOpt.get();
		if (loadDataRefCleanup(cmd.id, cmd.fileName)) {
			queueConfigChangeSignal();
		}
	}
}

void Instance::processEvents() {
	mEventHandler->processEvents();
	mAudio->processEvents();
	//clear
	while (mDataRefCleanupQueue->pop()) {
		//clear out/dealloc
	}

	//see if we should signal a change
	auto changed = false;
	{
		std::lock_guard<std::mutex> guard(mConfigChangedMutex);
		changed = mConfigChanged;
		mConfigChanged = false;
	}

	//store any presets that we got, indicate a change if we have one
	std::pair<std::string, RNBO::ConstPresetPtr> namePreset;
	bool updated = false;
	while (mPresetSavedQueue->try_dequeue(namePreset)) {
		std::lock_guard<std::mutex> guard(mPresetMutex);
		changed = updated = true;
		mPresets[namePreset.first] = namePreset.second;
		mPresetLatest = namePreset.first;
	}
	//only process a few events
	auto c = 0;
	while (auto item = mPresetCommandQueue.tryPop()) {
		if (c++ > 10)
			break;
		auto cmd = item.get();
		switch (cmd.type) {
			case PresetCommand::CommandType::Load:
				loadPreset(cmd.preset);
				break;
			case PresetCommand::CommandType::Save:
				mCore->getPreset([cmd, this] (RNBO::ConstPresetPtr preset) {
					mPresetSavedQueue->try_enqueue(std::make_pair(cmd.preset, preset));
				});
				break;
			case PresetCommand::CommandType::Initial:
				{
					std::lock_guard<std::mutex> guard(mPresetMutex);
					if (cmd.preset.size() == 0 || mPresets.find(cmd.preset) != mPresets.end()) {
						if (mPresetInitial != cmd.preset) {
							mPresetInitial = cmd.preset;
							queueConfigChangeSignal();
						}
					} else if (cmd.preset != mPresetInitial) {
						mPresetInitialParam->push_value(mPresetInitial);
					}
				}
				break;
			case PresetCommand::CommandType::Delete:
				{
					std::lock_guard<std::mutex> guard(mPresetMutex);
					auto it = mPresets.find(cmd.preset);
					if (it != mPresets.end()) {
						updated = true;
						mPresets.erase(it);
						//clear out initial and latest if they match
						if (mPresetInitial == cmd.preset) {
							mPresetInitial.clear();
							mPresetInitialParam->push_value(mPresetInitial);
						}
						if (mPresetLatest == cmd.preset) {
							mPresetLatest.clear();
						}
						queueConfigChangeSignal();
					}
				}
				break;
		}
	}
	if (updated)
		updatePresetEntries();

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
	RNBO::UniquePresetPtr preset = RNBO::make_unique<RNBO::Preset>();
	auto shared = it->second;
	RNBO::copyPreset(*shared, *preset);
	mCore->setPreset(std::move(preset));
	mPresetLatest = name;
	queueConfigChangeSignal();
}

void Instance::loadPreset(RNBO::UniquePresetPtr preset) {
	mCore->setPreset(std::move(preset));
}

RNBO::UniquePresetPtr Instance::getPresetSync() {
	RNBO::UniquePresetPtr preset = RNBO::make_unique<RNBO::Preset>();
	auto shared = mCore->getPresetSync();
	RNBO::copyPreset(*shared, *preset);
	return preset;
}

RNBO::Json Instance::currentConfig() {
	RNBO::Json config = mConfig;

	RNBO::Json presets = RNBO::Json::object();
	RNBO::Json datarefs = RNBO::Json::object();
	//copy presets
	{
		std::lock_guard<std::mutex> pguard(mPresetMutex);
		for (auto& kv: mPresets)
			presets[kv.first] = RNBO::convertPresetToJSONObj(*kv.second);
		//indicate the initial and latest preset if we loaded this config again
		if (!mPresetInitial.empty() && mPresets.find(mPresetInitial) != mPresets.end())
			config[initial_preset_key] = mPresetInitial;
		if (!mPresetLatest.empty() && mPresets.find(mPresetLatest) != mPresets.end())
			config[last_preset_key] = mPresetLatest;
		for (auto& kv: preset_midi_channel_values) {
			if (kv.second == mPresetProgramChangeChannel) {
				config[preset_midi_channel_key] = kv.first;
			}
		}
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
	std::vector<ossia::value> names;
	for (auto &kv : mPresets) {
		names.push_back(ossia::value(kv.first));
	}
	mPresetEntries->push_value(ossia::value(names));
}

void Instance::handleProgramChange(ProgramChange p) {
	//filter channel
	//0 == omni, 17 == none (won't match)
	if (mPresetProgramChangeChannel == 0 || mPresetProgramChangeChannel == p.chan + 1) {
		std::string name;
		{
			std::lock_guard<std::mutex> guard(mPresetMutex);
			uint8_t i = 0;
			for (auto it = mPresets.begin(); it != mPresets.end(); it++, i++) {
				if (i == p.prog) {
					name = it->first;
					break;
				}
			}
		}
		if (name.size()) {
			loadPreset(name);
		}
	}
}

void Instance::queueConfigChangeSignal() {
	std::lock_guard<std::mutex> guard(mConfigChangedMutex);
	mConfigChanged = true;
}

bool Instance::loadDataRef(const std::string& id, const std::string& fileName) {
	mCore->releaseExternalData(id.c_str());
	mDataRefs.erase(id);
	if (fileName.empty())
		return true;
	try {
		auto dataFileDir = config::get<fs::path>(config::key::DataFileDir);
		if (!dataFileDir) {
			std::cerr << config::key::DataFileDir << " not in config" << std::endl;
			return false;
		}
		auto filePath = dataFileDir.get() / fs::path(fileName);
		if (!fs::exists(filePath)) {
			std::cerr << "no file at " << filePath << std::endl;
			//TODO clear node value?
			return false;
		}
		SndfileHandle sndfile(filePath.string());
		if (!sndfile) {
			std::cerr << "couldn't open as sound file " << filePath << std::endl;
			//TODO clear node value?
			return false;
		}

		//sanity check
		if (sndfile.channels() < 1 || sndfile.samplerate() < 1.0 || sndfile.frames() < 1) {
			std::cerr << "sound file needs to have samplerate, frames and channels greater than zero " << fileName <<
				" samplerate: " << sndfile.samplerate() <<
				" channels: " << sndfile.channels() <<
				" frames: " << sndfile.frames() <<
				std::endl;
			return false;
		}

		std::shared_ptr<std::vector<float>> data;
		sf_count_t framesRead = 0;

		//actually read in audio and set the data
		//Some formats have an unknown frame size, so we have to read a bit at a time
		if (sndfile.frames() < SF_COUNT_MAX) {
			data = std::make_shared<std::vector<float>>(static_cast<size_t>(sndfile.channels()) * static_cast<size_t>(sndfile.frames()));
			framesRead = sndfile.readf(&data->front(), sndfile.frames());
		} else {
			const sf_count_t framesToRead = static_cast<sf_count_t>(sndfile.samplerate());
			//blockSize, offset, offsetIncr are in samples, not frames
			const auto blockSize = static_cast<size_t>(sndfile.channels()) * static_cast<size_t>(framesToRead);
			size_t offset = 0;
			size_t offsetIncr = framesToRead * sndfile.channels();
			sf_count_t read = 0;
			//reserve 5 seconds of space
			data = std::make_shared<std::vector<float>>(blockSize * 5);
			do {
				data->resize(offset + blockSize);
				read = sndfile.readf(&data->front() + offset, framesToRead);
				framesRead += read;
				offset += offsetIncr;
			} while (read == framesToRead);
		}

		if (framesRead == 0) {
			std::cerr << "read zero frames from " << fileName << std::endl;
			return false;
		}

		//TODO check mDataRefFileNameMap so we don't double load?
		mDataRefs[id] = data;

		//store the mapping so we can persist
		{
			std::lock_guard<std::mutex> guard(mDataRefFileNameMutex);
			mDataRefFileNameMap[id] = fileName;
		}

		//set the dataref data
		RNBO::Float32AudioBuffer bufferType(sndfile.channels(), static_cast<double>(sndfile.samplerate()));
		mCore->setExternalData(id.c_str(), reinterpret_cast<char *>(&data->front()), sizeof(float) * framesRead * sndfile.channels(), bufferType, [data, this](RNBO::ExternalDataId, char*) mutable {
				//hold onto data shared_ptr until rnbo stops using it, move it to cleanup
				mDataRefCleanupQueue->try_enqueue(data);
				data.reset();
				});
		//std::cout << "loading: " << fileName << " into: " << id << std::endl;
		return true;
	} catch (std::exception& e) {
		std::cerr << "exception reading data ref file: " << e.what() << std::endl;
	}
	return false;
}

bool Instance::loadDataRefCleanup(const std::string& id, const std::string& fileName) {
	if (loadDataRef(id, fileName)) {
		return true;
	}
	auto it = mDataRefNodes.find(id);
	if (it == mDataRefNodes.end()) {
		std::cerr << "cannot find dataref node with id: " << id << std::endl;
		return false;
	}
	it->second->push_value("");
	return false;
}

void Instance::handleInportMessage(RNBO::MessageTag tag, const ossia::value& val) {
	if (val.get_type() == ossia::val_type::IMPULSE) {
		mCore->sendMessage(tag);
	} else if (val.get_type() == ossia::val_type::FLOAT) {
		mCore->sendMessage(tag, static_cast<RNBO::number>(val.get<float>()));
	} else if (val.get_type() == ossia::val_type::INT) {
		mCore->sendMessage(tag, static_cast<RNBO::number>(val.get<int>()));
	} else if (val.get_type() == ossia::val_type::LIST) {
		auto list = val.get<std::vector<ossia::value>>();
		//empty list, bang
		if (list.size() == 0 || (list.size() == 1 && list[0].get_type() == ossia::val_type::IMPULSE)) {
			mCore->sendMessage(tag);
		} else if (list.size() == 1) {
			RNBO::number v = 0.0;
			if (list[0].get_type() == ossia::val_type::INT) {
				v = static_cast<RNBO::number>(list[0].get<int>());
			} else if (list[0].get_type() == ossia::val_type::FLOAT) {
				v = static_cast<RNBO::number>(list[0].get<float>());
			} else {
				std::cerr << "only numeric items are allowed in lists, aborting message" << std::endl;
			}
			mCore->sendMessage(tag, v);
		} else {
			//construct and send list
			auto l = RNBO::make_unique<RNBO::list>();
			for (auto v: list) {
				if (v.get_type() == ossia::val_type::INT)
					l->push(static_cast<RNBO::number>(v.get<int>()));
				else if (v.get_type() == ossia::val_type::FLOAT)
					l->push(static_cast<RNBO::number>(v.get<float>()));
				else {
					std::cerr << "only numeric values are allowed in lists, aborting message" << std::endl;
					return;
				}
			}
			mCore->sendMessage(tag, std::move(l));
		}
	}
}

void Instance::handleOutportMessage(RNBO::MessageEvent e) {
	auto tag = std::string(mCore->resolveTag(e.getTag()));
	auto it = mOutportParams.find(tag);
	if (it == mOutportParams.end()) {
		std::cerr << "couldn't find outport node with tag " << tag << std::endl;
		return;
	}
	auto p = it->second;
	switch(e.getType()) {
		case MessageEvent::Type::Number:
			p->push_value(e.getNumValue());
			break;
		case MessageEvent::Type::Bang:
			p->push_value(ossia::impulse {});
			break;
		case MessageEvent::Type::List:
			{
				std::vector<ossia::value> values;
				std::shared_ptr<const RNBO::list> elist = e.getListValue();
				for (size_t i = 0; i < elist->length; i++)
					values.push_back(ossia::value(elist->operator[](i)));
				p->push_value(values);
			}
			break;
		case MessageEvent::Type::Invalid:
		case MessageEvent::Type::Max_Type:
		default:
			return; //TODO warning message?
	}
}

//from RNBO, report via OSCQuery
void Instance::handleMidiCallback(RNBO::MidiEvent e) {
	if (mMIDIOutParam) {
		auto in = e.getData();
		std::vector<ossia::value> bytes;
		for (int i = 0; i < e.getLength(); i++) {
			bytes.push_back(ossia::value(static_cast<int>(in[i])));
		}
		mMIDIOutParam->push_value(bytes);
	}
}

//from RNBO
void Instance::handleParamUpdate(RNBO::ParameterIndex index, RNBO::ParameterValue value) {
	//RNBO is telling us we have a parameter update, tell ossia
	auto it = mIndexToParam.find(index);
	if (it != mIndexToParam.end()) {
		//enumerated value
		if (it->second.second) {
			auto it2 = it->second.second->find(static_cast<int>(value));
			if (it2 != it->second.second->end()) {
				it->second.first->push_value(it2->second);
			}
		} else {
			it->second.first->push_value(value);
		}
	}
}
