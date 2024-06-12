#include <memory>
#include <iostream>
#include <functional>

#include <sndfile.hh>
#include <readerwriterqueue/readerwriterqueue.h>
#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

#include <ossia/network/generic/generic_device.hpp>
#include <ossia/network/generic/generic_parameter.hpp>

#include "Defines.h"
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

	//construct preset name, add in set name if it exists
	std::string presetName(std::string name, std::string setname) {
		if (setname.size()) {
			return setname + "/" + name;
		}
		return name;
	}

	//referece count nodes created via metadata as they might be shared, this lets us cleanup
	std::unordered_map<std::string, unsigned int> node_reference_count;
	std::mutex node_reference_count_mutex;

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

	std::pair<ossia::net::node_base *, ossia::net::parameter_base *> add_meta_to_param(ossia::net::node_base& param) {
		auto m = param.create_child("meta");
		auto p = m->create_parameter(ossia::val_type::STRING);
		m->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
		return std::make_pair(m, p);
	}

	void add_node_ref(const std::string& addr, bool exists) {
		std::lock_guard<std::mutex> guard(node_reference_count_mutex);

		auto it = node_reference_count.find(addr);
		if (it != node_reference_count.end()) {
			it->second++;
		} else {
			node_reference_count.insert({addr, exists ? 2 : 1});
		}
	}

	void cleanup_param(const std::string& addr, ossia::net::node_base * node, ossia::net::parameter_base * param) {
		std::lock_guard<std::mutex> guard(node_reference_count_mutex);
		auto it = node_reference_count.find(addr);
		if (it == node_reference_count.end()) {
			//don't know what to do
			return;
		}
		if (it->second > 1) {
			it->second--;
			return;
		}
		//do cleanup
		node_reference_count.erase(it);

		if (param->callback_count() == 0) {
			node->remove_parameter();
			auto parent = node->get_parent();
			while (node && parent && node->children().size() == 0 && node->get_parameter() == nullptr) {
				auto n = node;
				auto p = parent;

				p->remove_child(*n);
				parent = p->get_parent();
				node = p;
			}
		}
	}
}

Instance::Instance(std::shared_ptr<DB> db, std::shared_ptr<PatcherFactory> factory, std::string name, NodeBuilder builder, RNBO::Json conf, std::shared_ptr<ProcessAudio> processAudio, unsigned int index) : mPatcherFactory(factory), mDataRefProcessCommands(true), mConfig(conf), mIndex(index), mName(name), mDB(db) {

	std::unordered_map<std::string, std::string> dataRefMap;

	//load up data ref map so we can set the initial value
	if (conf.contains("datarefs") && conf["datarefs"].is_object()) {
		auto datarefs = conf["datarefs"];
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
		auto it = config_midi_channel_values.find(chanName);
		if (it != config_midi_channel_values.end()) {
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
				std::bind(&Instance::handlePresetEvent, this, std::placeholders::_1),
				midiCallback));
	mCore = std::make_shared<RNBO::CoreObject>(mPatcherFactory->createInstance());
	mParamInterface = mCore->createParameterInterface(RNBO::ParameterEventInterface::MultiProducer, mEventHandler.get());

	std::string audioName = name + "-" + std::to_string(mIndex);
	mAudio = std::unique_ptr<InstanceAudioJack>(new InstanceAudioJack(mCore, conf, audioName, builder, std::bind(&Instance::handleProgramChange, this, std::placeholders::_1), mMIDIMapMutex, mMIDIMap));
	mAudio->registerConfigChangeCallback([this]() {
			if (mConfigChangeCallback != nullptr)
			mConfigChangeCallback();
	});

	mDataRefCleanupQueue = RNBO::make_unique<moodycamel::ReaderWriterQueue<std::shared_ptr<std::vector<float>>, 32>>(32);
	mPresetSaveQueue = RNBO::make_unique<moodycamel::ReaderWriterQueue<std::tuple<std::string, RNBO::ConstPresetPtr, std::string>, 32>>(32);

	mDB->presets(mName, [this](const std::string& name, bool initial) {
			if (initial) {
				mPresetInitial = name;
			}
	});

	builder([this, &dataRefMap, conf](ossia::net::node_base * root) {
		//get namespace root so we can add OSC at it with metadata later
		mOSCRoot = root;
		while (auto r = mOSCRoot->get_parent()) {
			mOSCRoot = r;
		}

		//set name
		if (conf.contains("name") && conf["name"].is_string()) {
			auto n = root->create_child("name");
			auto p = n->create_parameter(ossia::val_type::STRING);
			n->set(ossia::net::description_attribute{}, "The name of the loaded patcher");
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
			p->push_value(conf["name"].get<std::string>());
		}

		//get overrides
		RNBO::Json paramMetaOverride = RNBO::Json::array();
		RNBO::Json inportMetaOverride = RNBO::Json::object();
		RNBO::Json outportMetaOverride = RNBO::Json::object();
		if (conf.contains("metaoverride") && conf["metaoverride"].is_object()) {
			auto& o = conf["metaoverride"];

			if (o.contains("params") && o["params"].is_array()) {
				paramMetaOverride = o["params"];
			}
			if (o.contains("inports") && o["inports"].is_object()) {
				inportMetaOverride = o["inports"];
			}
			if (o.contains("outports") && o["outports"].is_object()) {
				outportMetaOverride = o["outports"];
			}
		}

		//setup parameters
		auto params = root->create_child("params");
		RNBO::Json paramConfig;
		if (conf.contains("parameters")) {
			paramConfig = conf["parameters"];
		}

		params->set(ossia::net::description_attribute{}, "Parameter get/set");
		for (RNBO::ParameterIndex index = 0; index < mCore->getNumParameters(); index++) {

			auto add_meta = [index, &paramConfig, &paramMetaOverride, this](ossia::net::node_base& param) {
				if (!paramConfig.is_array()) {
					return;
				}

				RNBO::Json metaoverride;
				for (auto p: paramMetaOverride) {
					if (p["index"].is_number() && static_cast<RNBO::ParameterIndex>(p["index"].get<double>()) == index) {
						metaoverride = p["meta"];
						break;
					}
				}

				//XXX what if there is no param config entry for this parameter? will there ever be?

				//find the parameter
				for (auto p: paramConfig) {
					if (p.contains("index") && p["index"].is_number() && static_cast<RNBO::ParameterIndex>(p["index"].get<double>()) == index) {
						auto meta = p["meta"]; //might be null
						auto param_meta = add_meta_to_param(param);

						auto on = param_meta.first;
						auto op = param_meta.second;

						//save default and process meta
						if (meta.is_object()) {
							mParamMetaDefault.insert({index, meta.dump()});
						}

						if (metaoverride.is_object()) {
							op->push_value(metaoverride.dump());
							handleMetadataUpdate(MetaUpdateCommand(on, op, index, metaoverride.dump()));
						} else if (meta.is_object()) {
							op->push_value(meta.dump());
							handleMetadataUpdate(MetaUpdateCommand(on, op, index, meta.dump()));
						} else {
							op->push_value("");
						}

						op->add_callback([this, op, on, index](const ossia::value& val) {
								std::string s = val.get_type() == ossia::val_type::STRING ? val.get<std::string>() : std::string();
								mMetaUpdateQueue.push(MetaUpdateCommand(on, op, index, s));
						});
						break;
					}
				}
			};

			ParameterInfo info;

			mCore->getParameterInfo(index, &info);
			//only supporting numbers right now
			//don't bind invisible or debug
			if (info.type != ParameterType::ParameterTypeNumber || !info.visible || info.debug)
				continue;

			ParamOSCUpdateData updateData;

			//create comon, return normalized version
			auto ccommon = [this, info, index](ossia::net::node_base& param) -> ossia::net::parameter_base * {
				{
					auto n = param.create_child("index");

					auto p = n->create_parameter(ossia::val_type::INT);
					n->set(ossia::net::description_attribute{}, "RNBO parameter index");
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
					p->push_value(static_cast<int>(index));
				}

				{
					auto n = param.create_child("normalized");
					auto p = n->create_parameter(ossia::val_type::FLOAT);

					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
					n->set(ossia::net::domain_attribute{}, ossia::make_domain(0., 1.));
					n->set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);

					p->push_value(mCore->convertToNormalizedParameterValue(index, info.initialValue));

					//normalized callback
					p->add_callback([this, index](const ossia::value& val) {
						handleNormalizedFloatParamOscUpdate(index, val);
					});

					return p;
				}
			};

			auto& n = ossia::net::create_node(*params, mCore->getParameterId(index));
			if (info.enumValues == nullptr) {
				//numerical parameters
				//set parameter access, range, etc etc
				auto p = n.create_parameter(ossia::val_type::FLOAT);

				n.set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
				n.set(ossia::net::domain_attribute{}, ossia::make_domain(info.min, info.max));
				n.set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);
				p->push_value(info.initialValue);

				updateData.normparam = ccommon(n);
				updateData.param = p;

				p->add_callback([this, index](const ossia::value& val) { handleFloatParamOscUpdate(index, val); });
			} else {
				//enumerated parameters
				std::vector<ossia::value> values;

				//build out lookup maps between strings and numbers
				for (int e = 0; e < info.steps; e++) {
					std::string s(info.enumValues[e]);
					values.push_back(s);
					updateData.nameToVal[s] = static_cast<ParameterValue>(e);
					updateData.valToName[e] = s;
				}

				auto p = n.create_parameter(ossia::val_type::STRING);

				auto dom = ossia::init_domain(ossia::val_type::STRING);
				ossia::set_values(dom, values);
				n.set(ossia::net::domain_attribute{}, dom);
				n.set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);
				p->push_value(info.enumValues[std::min(std::max(0, static_cast<int>(info.initialValue)), info.steps - 1)]);

				updateData.normparam = ccommon(n);
				updateData.param = p;

				p->add_callback([this, index](const ossia::value& val) { handleEnumParamOscUpdate(index, val); });
			}

			mIndexToParam[index] = updateData;
			//XXX no need for this to be a lambda anymore
			add_meta(n);

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
				auto n = presets->create_child("rename");
				auto rename = n->create_parameter(ossia::val_type::LIST);
				n->set(ossia::net::description_attribute{}, "rename a preset, arguments: oldname, newName");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);

				rename->add_callback([this](const ossia::value& val) {
					if (val.get_type() == ossia::val_type::LIST) {
						auto l = val.get<std::vector<ossia::value>>();
						if (l.size() == 2 && l[0].get_type() == ossia::val_type::STRING && l[1].get_type() == ossia::val_type::STRING) {
							mPresetCommandQueue.push(PresetCommand(PresetCommand::CommandType::Rename, l[0].get<std::string>(), l[1].get<std::string>()));
						}
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
				auto n = presets->create_child("loaded");
				mPresetLoadedParam = n->create_parameter(ossia::val_type::STRING);

				n->set(ossia::net::description_attribute{}, "Indicates that a preset was loaded. A name with a / indiciates a set preset where the format is setname/setpresetname");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
			}

			{

				auto cb = [this](const ossia::value& val) {
					if (val.get_type() == ossia::val_type::STRING) {
						mPresetCommandQueue.push(PresetCommand(PresetCommand::CommandType::Delete, val.get<std::string>()));
					}
				};
				{
					auto n = presets->create_child("delete");
					auto del = n->create_parameter(ossia::val_type::STRING);

					n->set(ossia::net::description_attribute{}, "Delete a preset with the given name");
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);

					del->add_callback(cb);
				}
				//XXX why the dupe?
				{
					auto n = presets->create_child("del");
					auto del = n->create_parameter(ossia::val_type::STRING);

					n->set(ossia::net::description_attribute{}, "Delete a preset with the given name");
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);

					del->add_callback(cb);
				}
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
				if (!mPresetInitial.empty() && mDB->preset(mName, mPresetInitial)) {
					mPresetInitialParam->push_value(mPresetInitial);
				}
			}

			{
				auto n = presets->create_child("midi_channel");

				std::vector<ossia::value> values;
				for (auto& kv: config_midi_channel_values) {
					values.push_back(kv.first);
				}

				mPresetProgramChangeChannelParam = n->create_parameter(ossia::val_type::STRING);

				auto dom = ossia::init_domain(ossia::val_type::STRING);
				ossia::set_values(dom, values);
				n->set(ossia::net::domain_attribute{}, dom);
				n->set(ossia::net::description_attribute{}, "Indicate MIDI channel, none or omni (all) that should be used for changing presets via Program Changes.");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
				n->set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);

				for (auto& kv: config_midi_channel_values) {
					if (kv.second == mPresetProgramChangeChannel) {
						mPresetProgramChangeChannelParam->push_value(kv.first);
						break;
					}
				}

				mPresetProgramChangeChannelParam->add_callback([this](const ossia::value& val) {
					if (val.get_type() == ossia::val_type::STRING) {
						auto it = config_midi_channel_values.find(val.get<std::string>());
						if (it != config_midi_channel_values.end()) {
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

							//add meta
							{
								auto meta = i["meta"];
								auto metaoverride = inportMetaOverride[name];
								auto param_meta = add_meta_to_param(n);
								auto on = param_meta.first;
								auto op = param_meta.second;

								if (meta.is_object()) {
									mInportMetaDefault.insert({name, meta.dump()});
								}

								if (metaoverride.is_object()) {
									op->push_value(metaoverride.dump());
									handleMetadataUpdate(MetaUpdateCommand(on, op, MetaUpdateCommand::Subject::Inport, name, metaoverride.dump()));
								} else if (meta.is_object()) {
									op->push_value(meta.dump());
									handleMetadataUpdate(MetaUpdateCommand(on, op, MetaUpdateCommand::Subject::Inport, name, meta.dump()));
								} else {
									op->push_value("");
								}

								op->add_callback([this, op, on, name](const ossia::value& val) {
										std::string s = val.get_type() == ossia::val_type::STRING ? val.get<std::string>() : std::string();
										mMetaUpdateQueue.push(MetaUpdateCommand(on, op, MetaUpdateCommand::Subject::Inport, name, s));
								});
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

							//we may have another param associated with this outport but the default outport is at the front of the list
							mOutportParams[name] = { p };

							//add meta
							{
								auto meta = i["meta"];
								auto metaoverride = outportMetaOverride[name];
								auto param_meta = add_meta_to_param(n);
								auto on = param_meta.first;
								auto op = param_meta.second;

								if (meta.is_object()) {
									mOutportMetaDefault.insert({name, meta.dump()});
								}

								if (metaoverride.is_object()) {
									op->push_value(metaoverride.dump());
									handleMetadataUpdate(MetaUpdateCommand(on, op, MetaUpdateCommand::Subject::Outport, name, metaoverride.dump()));
								} else if (meta.is_object()) {
									op->push_value(meta.dump());
									handleMetadataUpdate(MetaUpdateCommand(on, op, MetaUpdateCommand::Subject::Outport, name, meta.dump()));
								} else {
									op->push_value("");
								}

								op->add_callback([this, op, on, name](const ossia::value& val) {
										std::string s = val.get_type() == ossia::val_type::STRING ? val.get<std::string>() : std::string();
										mMetaUpdateQueue.push(MetaUpdateCommand(on, op, MetaUpdateCommand::Subject::Outport, name, s));
								});
							}
						}
					}
				}
			}
		} catch (const std::exception& e) {
			std::cerr << "exception processing ports: " << e.what() << std::endl;
		}

		//setup virtual midi and mapping
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
					//XXX skips MIDI mapping
					mParamInterface->scheduleEvent(RNBO::MidiEvent(0, 0, &bytes.front(), bytes.size()));
				}
			});

			{
				auto n = vmidi->create_child("out");
				mMIDIOutParam = n->create_parameter(ossia::val_type::LIST);
				n->set(ossia::net::description_attribute{}, "midi events out of your RNBO patch");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
			}

			{
				auto last = vmidi->create_child("last");
				{
					auto n = last->create_child("value");
					mMIDILastParam = n->create_parameter(ossia::val_type::STRING);
					n->set(ossia::net::description_attribute{}, "JSON encoded string representing the last MIDI mappable message seen by this instance, only reports if \"report\" is true");
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				}
				{
					auto n = last->create_child("report");
					auto p = n->create_parameter(ossia::val_type::BOOL);
					n->set(ossia::net::description_attribute{}, "Turn on/off publishing to the last \"value\" parameter");
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);

					p->push_value(mMIDILastReport);
					p->add_callback([this](const ossia::value& val) {
						if (val.get_type() == ossia::val_type::BOOL) {
							mMIDILastReport = val.get<bool>();
						}
					});
				}
			}

		}
	});

	//auto load data refs
	for (auto& kv: dataRefMap) {
		loadDataRefCleanup(kv.first, kv.second);
	}

	//load the initial or last preset, if in the config
	if (conf[initial_preset_key].is_string()) {
		//initial preset might be specified by set data
		loadPreset(conf[initial_preset_key].get<std::string>());
	} else if (!mPresetInitial.empty()) {
		//this one comes from the DB
		loadPreset(mPresetInitial);
	} else if (conf[last_preset_key].is_string()) {
		//this is the last one that was loaded
		loadPreset(conf[last_preset_key].get<std::string>());
	}

	//incase we changed it
	mConfigChanged = false;
	mDataRefThread = std::thread(&Instance::processDataRefCommands, this);
}

Instance::~Instance() {
	//cleanup callbacks
	for (auto& kv: mMetaCleanup) {
		kv.second();
	}

	mDataRefProcessCommands.store(false);
	mDataRefThread.join();
	stop();
	mAudio.reset();
	mEventHandler.reset();
	mCore.reset();
	mPatcherFactory.reset();
}

void Instance::activate() {
	mAudio->activate();
}

void Instance::connect() {
	mAudio->connect();
}

void Instance::start(float fadems) {
	mAudio->start(fadems);
}

void Instance::stop(float fadems) {
	mAudio->stop(fadems);
}

AudioState Instance::audioState() {
	return mAudio->state();
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

//build mutex is locked, we can build
void Instance::processEvents() {
	const auto state = audioState();
	const auto active = state == AudioState::Starting || state == AudioState::Running;
	if (active) {
		mEventHandler->processEvents();

		auto key = mAudio->lastMIDIKey();
		if (key != 0 && mMIDILastReport) {
			auto json = midimap::json(key);
			if (json.is_null()) {
				std::cerr << "cannot find json encoding for " << key << std::endl;
			} else {
				//XXX do we really want this much data sent from each node?
				//maybe it isn't a big deal?
				mMIDILastParam->push_value(json.dump());
			}
		}
	}
	mAudio->processEvents();

	//clear
	while (mDataRefCleanupQueue->pop()) {
		//clear out/dealloc
	}

	//handle queued presets
	{
		std::tuple<std::string, RNBO::ConstPresetPtr, std::string> preset;
		while (mPresetSaveQueue->try_dequeue(preset)) {
			RNBO::Json data;
			std::string name = std::get<0>(preset);

			data["runner_preset"] = RNBO::convertPresetToJSONObj(*std::get<1>(preset));

			std::string set_name = std::get<2>(preset);

			//add dataref mapping
			RNBO::Json datarefs = RNBO::Json::object();
			{
				std::lock_guard<std::mutex> bguard(mDataRefFileNameMutex);
				for (auto& kv: mDataRefFileNameMap)
					datarefs[kv.first] = kv.second;
			}
			data["datarefs"] = datarefs;

			if (set_name.size()) {
				mDB->setPresetSave(mName, name, set_name, mIndex, data.dump());
			} else {
				mDB->presetSave(mName, name, data.dump());
			}

			mPresetsDirty = true;
			{
				std::lock_guard<std::mutex> guard(mPresetMutex);
				mPresetLatest = presetName(name, set_name);
			}
			updatePresetEntries();
		}
	}

	//handle meta updates
	while (auto item = mMetaUpdateQueue.tryPop()) {
		auto update = item.get();
		handleMetadataUpdate(update);
	}

	if (active) {
		//see if we should signal a change
		auto changed = false;
		{
			std::lock_guard<std::mutex> guard(mConfigChangedMutex);
			changed = mConfigChanged;
			mConfigChanged = false;
		}

		//store any presets that we got
		bool updated = false;
		//only process a few events
		auto c = 0;
		while (auto item = mPresetCommandQueue.tryPop()) {
			auto cmd = item.get();
			switch (cmd.type) {
				case PresetCommand::CommandType::Load:
					loadPreset(cmd.preset);
					break;
				case PresetCommand::CommandType::Save:
					{
						auto name = cmd.preset;
						mCore->getPreset([name, this] (RNBO::ConstPresetPtr preset) {
							//get preset called in audio thread, queue to do heavy work outside that thread
							mPresetSaveQueue->try_enqueue({name, preset, std::string()});
						});
					}
					break;
				case PresetCommand::CommandType::Initial:
					{
						auto name = cmd.preset;
						mDB->presetSetInitial(mName, name);

						auto preset = mDB->preset(mName, name);

						{
							std::lock_guard<std::mutex> guard(mPresetMutex);
							if (cmd.preset.size() == 0 || preset) {
								if (mPresetInitial != name) {
									mPresetInitial = name;
								}
							} else if (name != mPresetInitial) {
								mPresetInitialParam->push_value(mPresetInitial);
							}
						}
					}
					break;
				case PresetCommand::CommandType::Delete:
					mDB->presetDestroy(mName, cmd.preset);
					{
						mPresetsDirty = true;
						//clear out initial and latest if they match
						std::lock_guard<std::mutex> guard(mPresetMutex);
						if (mPresetInitial == cmd.preset) {
							mPresetInitial.clear();
							mPresetInitialParam->push_value(mPresetInitial);
						}
						if (mPresetLatest == cmd.preset) {
							mPresetLatest.clear();
						}
						updated = true;
					}
					break;
				case PresetCommand::CommandType::Rename:
					mDB->presetRename(mName, cmd.preset, cmd.newname);
					{
						mPresetsDirty = true;
						//update our initial and latest if they match
						std::lock_guard<std::mutex> guard(mPresetMutex);
						if (mPresetInitial == cmd.preset) {
							mPresetInitial = cmd.newname;
							mPresetInitialParam->push_value(mPresetInitial);
						}
						if (mPresetLatest == cmd.preset) {
							mPresetLatest = cmd.newname;
						}
						updated = true;
					}
					break;
			}
			if (c++ > 10)
				break;
		}

		if (updated)
			updatePresetEntries();

		if (changed && mConfigChangeCallback != nullptr) {
			mConfigChangeCallback();
		}
	}
}

void Instance::savePreset(std::string name, std::string set_name) {
	mCore->getPreset([name, set_name, this] (RNBO::ConstPresetPtr preset) {
		//get preset called in audio thread, queue to do heavy work outside that thread
		mPresetSaveQueue->try_enqueue({name, preset, set_name});
	});
}

void Instance::loadPreset(std::string name, std::string set_name) {
	boost::optional<std::string> preset;
	if (set_name.size()) {
		preset = mDB->setPreset(mName, name, set_name, mIndex);
	} else {
		auto p = mDB->preset(mName, name);
		if (!p) {
			try {
				int index = std::stoi(name);
				if (index >= 0) {
					p = mDB->preset(mName, static_cast<unsigned int>(index));
				}
			} catch (...) {
				//do nothing
			}
		}
		if (p) {
			preset = p->first;
			name = p->second;
		}
	}

	if (preset) {
		loadJsonPreset(*preset, name, set_name);
	} else {
		if (set_name.size() == 0 || name != "initial") {
			std::cerr << "couldn't find preset with name or index: " << name;
			if (set_name.size()) {
				std::cerr << " in set " << set_name;
			}
			std::cerr << std::endl;
		}
	}
}

void Instance::loadPreset(unsigned int index) {
	auto preset = mDB->preset(mName, index);
	if (preset) {
		loadJsonPreset(preset->first, preset->second);
	} else {
		std::cerr << "couldn't find preset with index: " << index << std::endl;
	}
}

void Instance::loadPreset(RNBO::UniquePresetPtr preset) {
	mCore->setPreset(std::move(preset));
}

bool Instance::loadJsonPreset(const std::string& preset, const std::string& name, std::string setname) {
	//set preset latest so we can correctly send loaded param
	std::string last;
	{
		std::lock_guard<std::mutex> guard(mPresetMutex);
		last = mPresetLatest;
		mPresetLatest = presetName(name, setname);
	}
	try {
		RNBO::Json j = RNBO::Json::parse(preset);

		//added data to the preset JSON to support datarefs
		//using a special key "runner_preset" to specify this format
		//"runner_preset" contains the actual rnbo formatted JSON preset
		bool trydatarefs = false;
		RNBO::UniquePresetPtr unique = RNBO::make_unique<RNBO::Preset>();
		if (j["runner_preset"].is_object()) {
			trydatarefs = true;
			convertJSONObjToPreset(j["runner_preset"], *unique);
		} else {
			convertJSONObjToPreset(j, *unique);
		}
		mCore->setPreset(std::move(unique));

		if (trydatarefs && j["datarefs"].is_object()) {
			auto datarefs = j["datarefs"];
			//push directly to the nodes so that we queue up changes
			for (auto it = datarefs.begin(); it != datarefs.end(); ++it) {
				if (!it.value().is_string()) {
					std::cerr << "dataref value for key " << it.key() << " is not a string" << std::endl;
					continue;
				}

				auto nodeit = mDataRefNodes.find(it.key());
				if (nodeit != mDataRefNodes.end()) {
					nodeit->second->push_value(it.value().get<std::string>());
				}
			}
		}

		return true;
	} catch (const std::exception& e) {
		std::cerr << "error setting preset " << e.what() << std::endl;
		{
			std::lock_guard<std::mutex> guard(mPresetMutex);
			//revert if preset fails
			mPresetLatest = last;
		}
		return false;
	}
}

RNBO::UniquePresetPtr Instance::getPresetSync() {
	RNBO::UniquePresetPtr preset = RNBO::make_unique<RNBO::Preset>();
	auto shared = mCore->getPresetSync();
	RNBO::copyPreset(*shared, *preset);
	return preset;
}

RNBO::Json Instance::currentConfig() {
	RNBO::Json config = RNBO::Json::object();
	RNBO::Json datarefs = RNBO::Json::object();
	RNBO::Json meta = RNBO::Json::object();

	//store last preset
	{
		std::lock_guard<std::mutex> pguard(mPresetMutex);
		if (!mPresetLatest.empty() && mDB->preset(mName, mPresetLatest)) {
			config[last_preset_key] = mPresetLatest;
		}
	}
	//copy datarefs
	{
		std::lock_guard<std::mutex> bguard(mDataRefFileNameMutex);
		for (auto& kv: mDataRefFileNameMap)
			datarefs[kv.first] = kv.second;
	}
	config["datarefs"] = datarefs;

	//mAudio->addConfig(config);

	//meta mappings
	try {
		std::lock_guard<std::mutex> guard(mMetaMapMutex);
		RNBO::Json params = RNBO::Json::array();
		for (auto& kv: mParamMetaMapped) {
			RNBO::Json entry;
			entry["index"] = kv.first;
			entry["meta"] = RNBO::Json::parse(kv.second);
			params.push_back(entry);
		}
		meta["params"] = params;

		RNBO::Json inports = RNBO::Json::object();
		for (auto& kv: mInportMetaMapped) {
			inports[kv.first] = RNBO::Json::parse(kv.second);
		}
		meta["inports"] = inports;

		RNBO::Json outports = RNBO::Json::object();
		for (auto& kv: mOutportMetaMapped) {
			outports[kv.first] = RNBO::Json::parse(kv.second);
		}
		meta["outports"] = outports;
	} catch (...) {
		std::cerr << "problem creating meta config" << std::endl;
	}
	config["metaoverride"] = meta;

	return config;
}

void Instance::updatePresetEntries() {
	std::lock_guard<std::mutex> guard(mPresetMutex);
	std::vector<ossia::value> names;
	mDB->presets(mName, [&names](const std::string& name, bool /*initial*/) {
			names.push_back(ossia::value(name));
	});
	mPresetEntries->push_value(ossia::value(names));
}

void Instance::handleProgramChange(ProgramChange p) {
	//filter channel
	//0 == omni, 17 == none (won't match)
	if (mPresetProgramChangeChannel == 0 || mPresetProgramChangeChannel == p.chan + 1) {
		loadPreset(static_cast<unsigned int>(p.prog));
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
		mParamInterface->sendMessage(tag);
	} else if (val.get_type() == ossia::val_type::FLOAT) {
		mParamInterface->sendMessage(tag, static_cast<RNBO::number>(val.get<float>()));
	} else if (val.get_type() == ossia::val_type::INT) {
		mParamInterface->sendMessage(tag, static_cast<RNBO::number>(val.get<int>()));
	} else if (val.get_type() == ossia::val_type::LIST) {
		auto list = val.get<std::vector<ossia::value>>();
		//empty list, bang
		if (list.size() == 0 || (list.size() == 1 && list[0].get_type() == ossia::val_type::IMPULSE)) {
			mParamInterface->sendMessage(tag);
		} else if (list.size() == 1) {
			RNBO::number v = 0.0;
			if (list[0].get_type() == ossia::val_type::INT) {
				v = static_cast<RNBO::number>(list[0].get<int>());
			} else if (list[0].get_type() == ossia::val_type::FLOAT) {
				v = static_cast<RNBO::number>(list[0].get<float>());
			} else {
				std::cerr << "only numeric items are allowed in lists, aborting message" << std::endl;
			}
			mParamInterface->sendMessage(tag, v);
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
			mParamInterface->sendMessage(tag, std::move(l));
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

	switch(e.getType()) {
		case MessageEvent::Type::Number:
			for (auto p: it->second) {
				p->push_value(e.getNumValue());
			}
			break;
		case MessageEvent::Type::Bang:
			for (auto p: it->second) {
				p->push_value(ossia::impulse {});
			}
			break;
		case MessageEvent::Type::List:
			{
				std::vector<ossia::value> values;
				std::shared_ptr<const RNBO::list> elist = e.getListValue();
				for (size_t i = 0; i < elist->length; i++) {
					values.push_back(ossia::value(elist->operator[](i)));
				}
				for (auto p: it->second) {
					p->push_value(values);
				}
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
	if (it == mIndexToParam.end()) {
		//XXX error
		return;
	}

	auto& info = it->second;
	//prevent recursion
	if (auto _lock = std::unique_lock<std::mutex> (*info.mutex, std::try_to_lock)) {
		auto norm = static_cast<float>(mCore->convertToNormalizedParameterValue(index, value));
		if (info.valToName.size()) {
			auto it2 = info.valToName.find(static_cast<int>(value));
			if (it2 != info.valToName.end()) {
				info.param->push_value(it2->second);
				info.push_osc(it2->second, norm);
			}
		} else {
			info.param->push_value(value);
			info.push_osc(static_cast<float>(value),  norm);
		}
		info.normparam->push_value(norm);
	}
}

void Instance::handlePresetEvent(const RNBO::PresetEvent& e) {
	if (mPresetLoadedParam && e.getType() == RNBO::PresetEvent::Type::SettingEnd) {
		std::lock_guard<std::mutex> guard(mPresetMutex);
		mPresetLoadedParam->push_value(mPresetLatest);
	}
}

void Instance::handleMetadataUpdate(MetaUpdateCommand update) {
	//assumes build mutex is locked which is the case in processEvents or builder

	//do we default ports named with "/" prefixes to become top level OSC messages?
	const bool portToOSC = config::get<bool>(config::key::InstancePortToOSC).value_or(true);

	//default access mode of a new param if we make one
	ossia::access_mode oscAccessMode = ossia::access_mode::BI;
	ossia::val_type oscValueType = ossia::val_type::LIST;
	std::string oscAddr;

	std::string name;
	std::string cleanupKey;
	ParameterInfo paramInfo;
	RNBO::Json meta;
	bool setDefault = false;
	bool isParam = false;
	bool usenormalized = false;

	//set default meta if length is zero and there is a default
	if (update.meta.length() == 0) {
		update.node->clear_children();
		setDefault = true;
	} else {
		try {
			meta = RNBO::Json::parse(update.meta);
			update.node->clear_children();

			if (meta.is_object()) {
				recurse_add_meta(meta, update.node);
			}
		} catch (...) {
			//XXX clear out??
			std::cerr << "failed to parse meta as json: " << update.meta << std::endl;
			return;
		}
	}

	{
		std::lock_guard<std::mutex> guard(mMetaMapMutex);
		switch (update.subject) {
			case MetaUpdateCommand::Subject::Param:
				{
					isParam = true;
					//empty name and default address, metadata can fill it in though
					oscAccessMode = ossia::access_mode::SET; //listen by default
					cleanupKey = "Param" + std::to_string(update.paramIndex);

					mCore->getParameterInfo(update.paramIndex, &paramInfo);

					//enum are string
					oscValueType = paramInfo.enumValues == nullptr ? ossia::val_type::FLOAT : ossia::val_type::STRING;

					bool isCustom = !setDefault;
					auto it = mParamMetaDefault.find(update.paramIndex);
					if (it != mParamMetaDefault.end()) {
						//push new value but let fall through to unmap meta
						if (setDefault) {
							update.param->push_value(it->second);
						} else {
							isCustom = update.meta != it->second;
						}
					}
					if (isCustom) {
						mParamMetaMapped[update.paramIndex] = update.meta;
					} else {
						mParamMetaMapped.erase(update.paramIndex);
					}
				}
				break;
			case MetaUpdateCommand::Subject::Inport:
				{
					name = update.messageTag;
					oscAccessMode = ossia::access_mode::SET;
					oscAddr = portToOSC ? name : "";
					cleanupKey = "Inport" + name;

					//push new value but let fall through to unmap meta
					bool isCustom = !setDefault;
					auto it = mInportMetaDefault.find(name);
					if (it != mInportMetaDefault.end()) {
						//push new value but let fall through to unmap meta
						if (setDefault) {
							update.param->push_value(it->second);
						} else {
							isCustom = update.meta != it->second;
						}
					}
					if (isCustom) {
						mInportMetaMapped[name] = update.meta;
					} else {
						mInportMetaMapped.erase(name);
					}
				}
				break;
			case MetaUpdateCommand::Subject::Outport:
				{
					name = update.messageTag;
					oscAccessMode = ossia::access_mode::GET;
					oscAddr = portToOSC ? name : "";
					cleanupKey = "Outport" + name;

					bool isCustom = !setDefault;
					auto it = mOutportMetaDefault.find(name);
					if (it != mOutportMetaDefault.end()) {
						//push new value but let fall through to unmap meta
						if (setDefault) {
							update.param->push_value(it->second);
						} else {
							isCustom = update.meta != it->second;
						}
					}
					if (isCustom) {
						mOutportMetaMapped[name] = update.meta;
					} else {
						mOutportMetaMapped.erase(name);
					}
				}
				break;
			default:
				//shouldn't ever happen
				std::cerr << "unknown MetaUpdateCommand subject" << std::endl;
				return;
		}
	}
	queueConfigChangeSignal();

	if (isParam) {
		uint16_t midiKey = 0;
		if (meta.is_object() && meta.contains("midi")) {
			midiKey = midimap::key(meta["midi"]);
		}

		//clear out mapping if it doesn't match our current mapping
		auto it = mMIDIMapLookup.find(update.paramIndex);
		if (it != mMIDIMapLookup.end()) {
			auto key = it->second;

			if (key != midiKey) {
				mMIDIMapLookup.erase(it);
				std::unique_lock<std::mutex> guard(mMIDIMapMutex);
				mMIDIMap.erase(key);
			} else {
				midiKey = 0; //don't change map
			}
		}

		//setup new mapping
		if (midiKey) {
			std::unique_lock<std::mutex> guard(mMIDIMapMutex);
			//TODO what if we already have something at midiKey? we should probably clear it out?

			mMIDIMap[midiKey] = update.paramIndex;
			//set reverse lookup
			mMIDIMapLookup[update.paramIndex] = midiKey;
		}
	}

	//clear out existing OSC and figure out if we need to map new OSC
	{
		auto it = mMetaCleanup.find(cleanupKey);
		if (it != mMetaCleanup.end()) {
			//do cleanup and clear
			it->second();
			mMetaCleanup.erase(it);
		}
	}

	if (meta.is_object() && meta.contains("osc")) {
		if (meta["osc"].is_string()) {
			oscAddr = meta["osc"].get<std::string>();
		} else if (meta["osc"].is_boolean()) {
			if (!meta["osc"].get<bool>())
				oscAddr = "";
			oscAddr = name;
		} else if (meta["osc"].is_object()) {
			auto& osc = meta["osc"];
			if (osc["addr"].is_string()) {
				oscAddr = osc["addr"].get<std::string>();
			}

			//param has direction details
			//by default it is an input
			//it can become an output if out: true
			//it can become bi directional only if in:true, out: true
			if (isParam) {
				bool in = true;
				bool out = false;

				if (osc["in"].is_boolean()) {
					in = osc["in"].get<bool>();
				}
				if (osc["out"].is_boolean()) {
					out = osc["out"].get<bool>();
					//if in is not defined and out is true, set in to false
					if (out && !osc["in"].is_boolean()) {
						in = false;
					}
				}

				if (osc["norm"].is_boolean()) {
					usenormalized = osc["norm"].get<bool>();
					oscValueType = ossia::val_type::FLOAT;
				}

				if (in && out) {
					oscAccessMode = ossia::access_mode::BI;
				} else if (in) {
					oscAccessMode = ossia::access_mode::SET;
				} else if (out) {
					oscAccessMode = ossia::access_mode::GET;
				} else {
					std::cerr << "parameter osc with both in and out false is invalid" << std::endl;
					return;
				}
			}
		}
	}
	if (oscAddr.size() && !oscAddr.starts_with('/')) {
		oscAddr = "/" + oscAddr;
	}

	//XXX should we disallow a /rnbo/ prefix??
	if (oscAddr.starts_with('/')) {
		bool exists = ossia::net::find_node(*mOSCRoot, oscAddr) != nullptr;
		auto& pn = ossia::net::find_or_create_node(*mOSCRoot, oscAddr);
		auto pp = pn.get_parameter();

		if (pp == nullptr) {
			pp = pn.create_parameter(oscValueType);
			pn.set(ossia::net::access_mode_attribute{}, oscAccessMode);
		} else {
			//make sure we have a compatible mode, if not, convert it
			auto mode = ossia::net::get_access_mode(pn);
			if (mode && *mode != oscAccessMode && mode != ossia::access_mode::BI) {
				pn.set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
			}
		}

		//hook up OSC
		std::function<void()> cleanup;

		//setup reference for cleanup
		add_node_ref(oscAddr, exists);
		auto node_ptr = ossia::net::find_node(*mOSCRoot, oscAddr);

		switch (update.subject) {
			case MetaUpdateCommand::Subject::Param:
				{
					auto index = update.paramIndex;
					ossia::callback_container<std::function<void (const ossia::value &)>>::iterator cb;

					//TODO what about normalized?
					//TODO remapping values?
					//can we do both remapping and noramlization with an input and output std::func<float(float)> ?

					{
						auto it = mIndexToParam.find(index);
						if (it == mIndexToParam.end()) {
							std::cerr << "failed to find param mapping info, aborting meta osc mapping" << std::endl;
							return;
						}
						it->second.usenormalized = usenormalized;

						//if the access mode is BI or GET, this means we send messages out so set the oscparam
						if (oscAccessMode == ossia::access_mode::BI || oscAccessMode == ossia::access_mode::GET) {
							it->second.oscparam = pp;
						}
					}

					//if access mode is BI or SET, this means we recieve messages so set the callback
					if (oscAccessMode == ossia::access_mode::BI || oscAccessMode == ossia::access_mode::SET) {
						if (paramInfo.enumValues == nullptr || usenormalized) {
							cb = pp->add_callback([this, index] (const ossia::value& val) {
									auto it = mIndexToParam.find(index);
									if (it != mIndexToParam.end()) {
										auto& info = it->second;
										//this is really just a proxy and we don't want to recurse back
										if (auto _lock = std::unique_lock<std::mutex> (*info.oscmutex, std::try_to_lock)) {
											if (info.usenormalized) {
												info.normparam->push_value(val);
											} else {
												info.param->push_value(val);
											}
										}
									}
							});
						} else {
							cb = pp->add_callback([this, index] (const ossia::value& val) {
									auto it = mIndexToParam.find(index);
									if (it != mIndexToParam.end()) {
										auto& info = it->second;
										//this is really just a proxy and we don't want to recurse back
										if (auto _lock = std::unique_lock<std::mutex> (*info.oscmutex, std::try_to_lock)) {
											info.param->push_value(val);
										}
									}
							});
						}
						cleanup = [this, index, oscAddr, cb, pp, node_ptr]() {
							//clear out the associated oscparam
							//remove callbacks
							//cleanup param
							auto it = mIndexToParam.find(index);
							if (it != mIndexToParam.end()) {
								//this might already be null, but there is no issue setting it null again
								it->second.oscparam = nullptr;
							}
							pp->remove_callback(cb);
							cleanup_param(oscAddr, node_ptr, pp);
						};
					} else {
						//if we're GET only, there is no callback to clear out
						cleanup = [this, index, oscAddr, pp, node_ptr]() {
							//clear out the associated oscparam
							//cleanup param
							auto it = mIndexToParam.find(index);
							if (it != mIndexToParam.end()) {
								it->second.oscparam = nullptr;
							}
							cleanup_param(oscAddr, node_ptr, pp);
						};
					}
				}
				break;
			case MetaUpdateCommand::Subject::Inport:
				{
					auto tag = RNBO::TAG(name.c_str());
					auto cb = pp->add_callback([this, tag](const ossia::value& val) {
						handleInportMessage(tag, val);
					});
					cleanup = [oscAddr, cb, pp, node_ptr]() {
						pp->remove_callback(cb);
						cleanup_param(oscAddr, node_ptr, pp);
					};
				}
				break;
			case MetaUpdateCommand::Subject::Outport:
				{
					{
						auto it = mOutportParams.find(name);
						if (it != mOutportParams.end()) {
							//push the parameter to the list
							it->second.push_back(pp);
						} else {
							//should not happen
							return;
						}
					}
					cleanup = [oscAddr, pp, node_ptr, name, this]() {
						//remove the parameter from our outport list
						auto it = mOutportParams.find(name);
						if (it != mOutportParams.end()) {
							while (it->second.size() > 1) {
								it->second.pop_back();
							}
						} else {
							//XXX shouldn't happen
						}
						cleanup_param(oscAddr, node_ptr, pp);
					};
				}
				break;
			default:
				return;
		}
		mMetaCleanup.insert({cleanupKey, cleanup});
	}
}

void Instance::handleEnumParamOscUpdate(RNBO::ParameterIndex index, const ossia::value& val) {
	auto it = mIndexToParam.find(index);
	if (it == mIndexToParam.end()) {
		//XXX ERROR
		return;
	}

	auto& info = it->second;

	//TODO any other types valid?
	if (val.get_type() == ossia::val_type::STRING) {
		if (auto _lock = std::unique_lock<std::mutex> (*info.mutex, std::try_to_lock)) {
			auto s = val.get<std::string>();
			auto f = info.nameToVal.find(s);
			if (f != info.nameToVal.end()) {
				mParamInterface->setParameterValue(index, f->second);

				auto norm = static_cast<float>(mCore->convertToNormalizedParameterValue(index, f->second));
				info.push_osc(static_cast<float>(f->second), norm);
				info.normparam->push_value(norm);
			}
		}
	}
}

void Instance::handleFloatParamOscUpdate(RNBO::ParameterIndex index, const ossia::value& val) {
	auto it = mIndexToParam.find(index);
	if (it == mIndexToParam.end()) {
		//XXX ERROR
		return;
	}

	auto& info = it->second;

	//support other types?
	double f = 0.0;
	switch(val.get_type()) {
		case ossia::val_type::FLOAT:
			f = static_cast<double>(val.get<float>());
			break;
		case ossia::val_type::INT:
			f = static_cast<double>(val.get<int>());
			break;
		case ossia::val_type::BOOL:
			f = val.get<bool>() ? 1.0 : 0.0;
			break;
		default:
			return;
	}

	if (auto _lock = std::unique_lock<std::mutex> (*info.mutex, std::try_to_lock)) {
		//constrain in case we're getting this from some random OSC source
		f = mCore->constrainParameterValue(index, f);
		mParamInterface->setParameterValue(index, f);
		auto norm = static_cast<float>(mCore->convertToNormalizedParameterValue(index, f));

		info.push_osc(static_cast<float>(f), norm);
		info.normparam->push_value(norm);
	}
}

void Instance::handleNormalizedFloatParamOscUpdate(RNBO::ParameterIndex index, const ossia::value& val) {
	auto it = mIndexToParam.find(index);
	if (it == mIndexToParam.end()) {
		//XXX ERROR
		return;
	}

	auto& info = it->second;
	if (val.get_type() == ossia::val_type::FLOAT) {
		if (auto _lock = std::unique_lock<std::mutex> (*info.mutex, std::try_to_lock)) {
			const double f = static_cast<double>(val.get<float>());

			auto unnorm = mCore->convertFromNormalizedParameterValue(index, f);
			mParamInterface->setParameterValue(index, unnorm);

			//is it enum?
			if (info.valToName.size()) {
				auto name = info.valToName.find(static_cast<int>(unnorm));
				if (name != info.valToName.end()) {
					info.param->push_value(name->second);
					info.push_osc(name->second, f);
				}
			} else {
				info.param->push_value(static_cast<float>(unnorm));
				info.push_osc(static_cast<float>(unnorm), f);
			}
		}
	}
}


Instance::ParamOSCUpdateData::ParamOSCUpdateData() {
	mutex = std::make_shared<std::mutex>();
	oscmutex = std::make_shared<std::mutex>();
}

void Instance::ParamOSCUpdateData::push_osc(ossia::value val, float normval) {
	if (oscparam) {
		if (auto _olock = std::unique_lock<std::mutex> (*oscmutex, std::try_to_lock)) {
			oscparam->push_value(usenormalized ? ossia::value(normval) : val);
		}
	}
}
