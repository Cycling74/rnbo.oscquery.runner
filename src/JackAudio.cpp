#include "JackAudio.h"
#include "Config.h"
#include "MIDIMap.h"

#include <jack/midiport.h>
#include <jack/uuid.h>
#include <jack/jslist.h>

#include <readerwriterqueue/readerwriterqueue.h>

#include <ossia/network/generic/generic_device.hpp>
#include <ossia/network/generic/generic_parameter.hpp>

#include <boost/optional.hpp>
#include <boost/filesystem.hpp>

#include <atomic>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <set>

namespace fs = boost::filesystem;

using std::chrono::steady_clock;

namespace {
	const auto card_poll_period = std::chrono::seconds(2);
	const auto port_poll_timeout = std::chrono::milliseconds(20);

	const std::string CONTROL_CLIENT_NAME("rnbo-control");

	boost::optional<std::string> ns("jack");

#ifdef __APPLE__
	const static std::string jack_driver_name = "coreaudio";
	const static std::string jack_midi_driver_name = "coremidi";
#else
	const static std::string jack_driver_name = "alsa";
	const static std::string jack_midi_driver_name;
#endif

	template <typename T>
	boost::optional<T> jconfig_get(const std::string& key) {
		return config::get<T>(key, ns);
	}

	template <typename T>
	void jconfig_set(const T& v, const std::string& key) {
		return config::set<T>(v, key, ns);
	}

	template boost::optional<int> jconfig_get(const std::string& key);
	template boost::optional<double> jconfig_get(const std::string& key);
	template boost::optional<bool> jconfig_get(const std::string& key);

	const static std::regex alsa_card_regex(R"X(\s*(\d+)\s*\[([^\[]+?)\s*\]:\s*([^;]+?)\s*;\s*([^;]+?)\s*;)X");

	std::atomic<bool> sync_transport = true;

	const std::string bpm_property_key("http://www.x37v.info/jack/metadata/bpm");
	const char * bpm_property_type = "https://www.w3.org/2001/XMLSchema#decimal";

	static int processJackProcess(jack_nframes_t nframes, void *arg) {
		reinterpret_cast<ProcessAudioJack *>(arg)->process(nframes);
		return 0;
	}

	static void processJackPortRenamed(jack_port_id_t port, const char *old_name, const char *new_name, void *arg) {
		reinterpret_cast<ProcessAudioJack *>(arg)->portRenamed(port, old_name, new_name);
	}

	static int processJackInstance(jack_nframes_t nframes, void *arg) {
		reinterpret_cast<InstanceAudioJack *>(arg)->process(nframes);
		return 0;
	}

	static void jackPortRegistration(jack_port_id_t id, int reg, void *arg) {
		reinterpret_cast<InstanceAudioJack *>(arg)->jackPortRegistration(id, reg);
	}

	static void jackInstancePortConnection(jack_port_id_t a, jack_port_id_t b, int connect, void *arg) {
		reinterpret_cast<InstanceAudioJack *>(arg)->portConnected(a, b, connect != 0);
	}

	static void processJackPortRegistration(jack_port_id_t id, int reg, void *arg) {
		reinterpret_cast<ProcessAudioJack *>(arg)->jackPortRegistration(id, reg);
	}

	static void processJackPortConnection(jack_port_id_t a, jack_port_id_t b, int connect, void *arg) {
		reinterpret_cast<ProcessAudioJack *>(arg)->portConnected(a, b, connect != 0);
	}

	void iterate_connections(jack_port_t * port, std::function<void(std::string)> func) {
		auto connections = jack_port_get_connections(port);

		if (connections != nullptr) {
			for (int i = 0; connections[i] != nullptr; i++) {
				func(std::string(connections[i]));
			}
			jack_free(connections);
		}
	}

	//sorts and compares
	bool sort_compare(const std::vector<std::string>& oldnames, std::vector<std::string> newnames) {
		std::sort(newnames.begin(), newnames.end());
		if (oldnames.size() == newnames.size()) {
			bool same = true;
			for (auto i = 0; i < oldnames.size(); i++) {
				if (oldnames[i].compare(newnames[i]) != 0) {
					same = false;
					break;
				}
			}
			return !same;
		}
		return true;
	}

#if JACK_SERVER
	jackctl_driver_t * jackctl_server_get_driver(jackctl_server_t * server, const std::string& name) {
		auto n = jackctl_server_get_drivers_list(server);
		while (n) {
			jackctl_driver_t * driver = reinterpret_cast<jackctl_driver_t *>(n->data);
			std::string dname = jackctl_driver_get_name(driver);
			if (name.compare(dname) == 0) {
				return driver;
			}
			n = jack_slist_next(n);
		}
		return nullptr;
	}
#endif

	bool is_through(const char * name) {
		std::string lower(name);
		transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
		return lower.find("through") != std::string::npos || lower.find("virtual") != std::string::npos;
	};
}

ProcessAudioJack::ProcessAudioJack(NodeBuilder builder, std::function<void(ProgramChange)> progChangeCallback) :
	mBuilder(builder), mJackClient(nullptr), mTransportBPMPropLast(100.0), mBPMClientUUID(0),
	mProgramChangeCallback(progChangeCallback)
{
	mPortQueue = RNBO::make_unique<moodycamel::ReaderWriterQueue<std::pair<jack_port_id_t, JackPortChange>, 32>>(32);
	mProgramChangeQueue = RNBO::make_unique<moodycamel::ReaderWriterQueue<ProgramChange, 32>>(32);

	//init aliases
	mJackPortAliases[0] = new char[jack_port_name_size()];
	mJackPortAliases[1] = new char[jack_port_name_size()];

	//read in config
	{
		mSampleRate = jconfig_get<double>("sample_rate").get_value_or(48000.);
		mPeriodFrames = jconfig_get<int>("period_frames").get_value_or(256);
#ifndef __APPLE__
		mCardName = jconfig_get<std::string>("card_name").get_value_or("");
		mNumPeriods = jconfig_get<int>("num_periods").get_value_or(2);
		mMIDISystem = jconfig_get<std::string>("midi_system_name").get_value_or("seq");
#endif
	}

	mBuilder([this](ossia::net::node_base * root) {
			mInfoNode = root->create_child("info");

			auto connections = root->create_child("connections");
			connections->set(ossia::net::description_attribute{}, "Jack connections: sources and their connected sinks, disconnect and connect utilities");
			mPortAudioSourceConnectionsNode = connections->create_child("audio");
			mPortMIDISourceConnectionsNode = connections->create_child("midi");

			//ease of use connect/disconnect params
			{
				auto n = connections->create_child("connect");
				n->set(ossia::net::description_attribute{}, "connect 2 jack ports by name: source destination");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
				auto param = n->create_parameter(ossia::val_type::LIST);
				param->add_callback([this](const ossia::value& val) {
					if (val.get_type() == ossia::val_type::LIST) {
						auto l = val.get<std::vector<ossia::value>>();
						std::vector<std::string> names;
						for (auto it: l) {
							if (it.get_type() == ossia::val_type::STRING) {
								names.push_back(it.get<std::string>());
							}
						}
						if (names.size() == 2) {
							jack_connect(mJackClient, names[0].c_str(), names[1].c_str());
						}
					}
				});
			}
			{
				auto n = connections->create_child("disconnect");
				n->set(ossia::net::description_attribute{}, "disconnect 2 jack ports by name: source destination");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
				auto param = n->create_parameter(ossia::val_type::LIST);
				param->add_callback([this](const ossia::value& val) {
					if (val.get_type() == ossia::val_type::LIST) {
						auto l = val.get<std::vector<ossia::value>>();
						std::vector<std::string> names;
						for (auto it: l) {
							if (it.get_type() == ossia::val_type::STRING) {
								names.push_back(it.get<std::string>());
							}
						}
						if (names.size() == 2) {
							jack_disconnect(mJackClient, names[0].c_str(), names[1].c_str());
						}
					}
				});
			}

			mPortInfoNode = mInfoNode->create_child("ports");

			{
				auto audio = mPortInfoNode->create_child("audio");
				auto midi = mPortInfoNode->create_child("midi");

				auto build = [](ossia::net::node_base * parent, const std::string name) -> ossia::net::parameter_base * {
					auto n = parent->create_child(name);
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
					auto p = n->create_parameter(ossia::val_type::LIST);
					return p;
				};

				mPortAudioSinksParam = build(audio, "sinks");
				mPortAudioSourcesParam = build(audio, "sources");
				mPortMidiSinksParam = build(midi, "sinks");
				mPortMidiSourcesParam = build(midi, "sources");

				mPortAliases = mPortInfoNode->create_child("aliases");
				mPortAliases->set(ossia::net::description_attribute{}, "Ports and a list of their aliases");
			}

			auto conf = root->create_child("config");
			conf->set(ossia::net::description_attribute{}, "Jack configuration parameters");

			auto control = root->create_child("control");

#ifndef __APPLE__
			mCardListNode = mInfoNode->create_child("alsa_cards");
			mCardNode = conf->create_child("card");

			auto card = mCardNode->create_parameter(ossia::val_type::STRING);
			mCardNode->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
			mCardNode->set(ossia::net::description_attribute{}, "ALSA device name");

			updateCards();
			mCardsPollNext = std::chrono::steady_clock::now() + card_poll_period;
			updateCardNodes();

			if (!mCardName.empty()) {
				card->push_value(mCardName);
			} else if (mCardNamesAndDescriptions.size()) {
				//set card name to first found non Headphones, it is our best guess
				mCardName = mCardNamesAndDescriptions.begin()->first;
				//don't pick dummy by default if we can
				if (mCardNamesAndDescriptions.size() > 1) {
					for (auto& kv: mCardNamesAndDescriptions) {
						if (kv.first != "hw:Dummy") {
							mCardName = kv.first;
							break;
						}
					}
				}
				jconfig_set(mCardName, "card_name");
			}

			//start callback and threading
			card->add_callback([this](const ossia::value& val) {
				if (val.get_type() == ossia::val_type::STRING) {
					mCardName = val.get<std::string>();
					jconfig_set(mCardName, "card_name");
				}
			});

			{
				auto n = conf->create_child("num_periods");
				mNumPeriodsParam = n->create_parameter(ossia::val_type::INT);
				n->set(ossia::net::description_attribute{}, "Number of periods of playback latency");
				mNumPeriodsParam->push_value(mNumPeriods);

				auto dom = ossia::init_domain(ossia::val_type::INT);
				ossia::set_values(dom, { 2, 3, 4});
				n->set(ossia::net::domain_attribute{}, dom);
				n->set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);

				mNumPeriodsParam->add_callback([this](const ossia::value& val) {
					//TODO clamp?
					if (val.get_type() == ossia::val_type::INT) {
						mNumPeriods = val.get<int>();
						jconfig_set(mNumPeriods, "num_periods");
					}
				});
			}

			{
				auto n = conf->create_child("midi_system");
				auto p = n->create_parameter(ossia::val_type::STRING);
				n->set(ossia::net::description_attribute{}, "Which ALSA MIDI system to provide access to.");

				p->push_value(mMIDISystem);

				auto dom = ossia::init_domain(ossia::val_type::STRING);
				ossia::set_values(dom, { "seq", "raw" });
				n->set(ossia::net::domain_attribute{}, dom);
				n->set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);

				p->add_callback([this](const ossia::value& val) {
					if (val.get_type() == ossia::val_type::STRING) {
						auto s = val.get<std::string>();
						if (s == "raw" || s == "seq") {
							mMIDISystem = s;
							jconfig_set(mMIDISystem, "midi_system_name");
						}
					}
				});
			}
#endif
			{
				//accepted is a list of 2**n (32,... 1024)
				std::vector<ossia::value> accepted;
				for (int i = 5; i <= 10; i++) {
					accepted.push_back(1 << i);
				}
				auto n = conf->create_child("period_frames");
				mPeriodFramesParam = n->create_parameter(ossia::val_type::INT);
				n->set(ossia::net::description_attribute{}, "Frames per period");
				mPeriodFramesParam->push_value(mPeriodFrames);

				auto dom = ossia::init_domain(ossia::val_type::INT);
				ossia::set_values(dom, accepted);
				n->set(ossia::net::domain_attribute{}, dom);
				n->set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);

				mPeriodFramesParam->add_callback([this](const ossia::value& val) {
					//TODO clamp?
					if (val.get_type() == ossia::val_type::INT) {
						mPeriodFrames = val.get<int>();
						jconfig_set(mPeriodFrames, "period_frames");
					}
				});
			}

			{
				auto n = conf->create_child("sample_rate");
				mSampleRateParam = n->create_parameter(ossia::val_type::FLOAT);
				n->set(ossia::net::description_attribute{}, "Sample rate");
				mSampleRateParam->push_value(mSampleRate);

				auto dom = ossia::init_domain(ossia::val_type::FLOAT);
				dom.set_min(44100.0 / 2);
				n->set(ossia::net::domain_attribute{}, dom);
				n->set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);

				mSampleRateParam->add_callback([this](const ossia::value& val) {
					//TODO clamp?
					if (val.get_type() == ossia::val_type::FLOAT) {
						mSampleRate = val.get<float>();
						jconfig_set(mSampleRate, "sample_rate");
					}
				});
			}

			{
				auto n = control->create_child("midi_in");
				n->set(ossia::net::description_attribute{}, "MIDI connection to use for control (patcher selection)");
				mMidiInParam = n->create_parameter(ossia::val_type::LIST);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
				mMidiInParam->add_callback([this](const ossia::value& val) {
					mMidiPortConnectionsChanged = true;
				});
			}
	});
	createClient(false);
}

ProcessAudioJack::~ProcessAudioJack() {
	setActive(false);

	delete [] mJackPortAliases[0];
	delete [] mJackPortAliases[1];
}

void ProcessAudioJack::process(jack_nframes_t nframes) {
	{
		auto midi_buf = jack_port_get_buffer(mJackMidiIn, nframes);
		jack_nframes_t count = jack_midi_get_event_count(midi_buf);
		jack_midi_event_t evt;
		for (auto i = 0; i < count; i++) {
			jack_midi_event_get(&evt, midi_buf, i);
			if (mProgramChangeQueue && evt.size == 2 && (evt.buffer[0] & 0xF0) == 0xC0) {
				mProgramChangeQueue->enqueue(ProgramChange { .chan = static_cast<uint8_t>(evt.buffer[0] & 0x0F), .prog = static_cast<uint8_t>(evt.buffer[1]) });
			}
		}
	}

	{
		//communicate transport state changes
		auto state = jack_transport_query(mJackClient, nullptr);
		//we only care about rolling and stopped
		bool rolling = false;
		switch (state) {
			case jack_transport_state_t::JackTransportRolling:
				rolling = true;
				//fallthrough
			case jack_transport_state_t::JackTransportStopped:
				if (rolling != mTransportRollingUpdate.load())
					mTransportRollingUpdate.store(rolling);
				break;
			default:
				break;
		}
	}
}

bool ProcessAudioJack::connect(const RNBO::Json& config) {
	if (config.is_object() && mJackClient) {
		for (auto& [key, value]: config.items()) {
			bool input = value["input"].get<bool>();
			for (auto o: value["connections"]) {
				std::string name = o.get<std::string>();
				if (input) {
					jack_connect(mJackClient, name.c_str(), key.c_str());
				} else {
					jack_connect(mJackClient, key.c_str(), name.c_str());
				}
			}
		}
		return true;
	}
	return false;
}

RNBO::Json ProcessAudioJack::connections() {
	RNBO::Json conf;
	const char ** ports = nullptr;
	if ((ports = jack_get_ports(mJackClient, nullptr, nullptr, 0)) != nullptr) {
		for (size_t i = 0; ports[i] != nullptr; i++) {
			std::string name(ports[i]);
			jack_port_t * port = jack_port_by_name(mJackClient, name.c_str());
			std::vector<std::string> connections;
			iterate_connections(port, [&connections](std::string n) {
					connections.push_back(n);
			});

			if (connections.size()) {
				auto flags = jack_port_flags(port);

				RNBO::Json entry;
				entry["connections"] = connections;
				entry["physical"] = static_cast<bool>(JackPortIsPhysical & flags);
				entry["input"] = static_cast<bool>(JackPortIsInput & flags);
				entry["output"] = static_cast<bool>(JackPortIsOutput & flags);

				conf[name] = entry;
			}
		}
		jack_free(ports);
	}

	//TODO filter any dupes

	return conf;
}

bool ProcessAudioJack::isActive() {
	std::lock_guard<std::mutex> guard(mMutex);
	return mJackClient != nullptr;
}

bool ProcessAudioJack::setActive(bool active) {
	if (active) {
		//only force a server if we have done it before or we've never created a client
		auto createServer = (mHasCreatedServer || !mHasCreatedClient);
		return createClient(createServer);
	} else {
		mBuilder([this](ossia::net::node_base * root) {
			if (mTransportNode) {
				if (root->remove_child("transport")) {
					mTransportNode = nullptr;
				}
			}

			std::vector<ossia::value> empty;
			mPortAudioSinksParam->push_value(empty);
			mPortAudioSourcesParam->push_value(empty);
			mPortMidiSinksParam->push_value(empty);
			mPortMidiSourcesParam->push_value(empty);
			mPortAliases->clear_children();
			mPortAudioSourceConnectionsNode->clear_children();
			mPortMIDISourceConnectionsNode->clear_children();
		});

		std::lock_guard<std::mutex> guard(mMutex);
		if (mJackClient) {
			jack_client_close(mJackClient);
			mJackClient = nullptr;
		}
#if JACK_SERVER
		if (mJackServer) {
			jackctl_server_stop(mJackServer);
			if (jack_midi_driver_name.size()) {
				jackctl_driver_t * midiDriver = jackctl_server_get_driver(mJackServer, jack_midi_driver_name);
				if (midiDriver != nullptr && jackctl_driver_get_type(midiDriver) == JackSlave) {
					jackctl_server_remove_slave(mJackServer, midiDriver);
				}
			}
			jackctl_server_close(mJackServer);
			jackctl_server_destroy(mJackServer);
			mJackServer = nullptr;
		}
#endif
		return false;
	}
}

//Controller is holding onto build mutex, so feel free to build and don't lock it
void ProcessAudioJack::processEvents() {
	auto now = steady_clock::now();

#ifndef __APPLE__
		if (mCardsPollNext < now) {
			mCardsPollNext = std::chrono::steady_clock::now() + card_poll_period;
			if (updateCards()) {
				updateCardNodes();
			}
		}
#endif

	{
		std::lock_guard<std::mutex> guard(mMutex);
		if (mJackClient == nullptr)
			return;

		{
			std::pair<jack_port_id_t, JackPortChange> entry;
			while (mPortQueue->try_dequeue(entry)) {
				if (entry.second == JackPortChange::Register) {
					connectToMidiIf(jack_port_by_id(mJackClient, entry.first));
				}

				if (entry.second == JackPortChange::Connection) {
					mPortConnectionUpdates.insert(entry.first);
					mPortConnectionPoll = now + port_poll_timeout;
				} else {
					mPortPoll = now + port_poll_timeout;
				}
			}
		}

		//manage port connections/disconnections to and from oscquery
		auto doConnectDisconnectFromParam = [this](const std::string& portname, jack_port_t * port, bool isSource, ossia::net::parameter_base * param) {
			std::vector<ossia::value> values; //accumulate "good" values in case we need to update the param
			std::set<std::string> toConnect;
			auto val = param->value();
			if (val.get_type() == ossia::val_type::LIST) {
				auto l = val.get<std::vector<ossia::value>>();
				for (auto it: l) {
					if (it.get_type() == ossia::val_type::STRING) {
						toConnect.insert(it.get<std::string>());
					}
				}
			}

			//check existing connections, disconnect anything that is connected but not in the list
			iterate_connections(port, [&toConnect, &portname, this, isSource, &values](std::string n) {
					if (toConnect.count(n)) {
						values.push_back(n);
						toConnect.erase(n);
					} else if (isSource) {
						jack_disconnect(mJackClient, portname.c_str(), n.c_str());
					} else {
						jack_disconnect(mJackClient, n.c_str(), portname.c_str());
					}
			});

			bool updateParam = false;
			for (auto& n: toConnect) {
				if (!jack_port_connected_to(port, n.c_str())) {
					int r;
					if (isSource) {
						r = jack_connect(mJackClient, portname.c_str(), n.c_str());
					} else {
						r = jack_connect(mJackClient, n.c_str(), portname.c_str());
					}

					//connection can't be made, need to remove it from the param
					if (r == 0 || r == EEXIST) {
						values.push_back(n);
					} else {
						updateParam = true;
					}
				}
			}
			if (updateParam) {
				param->push_value(values);
			}
		};

		if (mMidiPortConnectionsChanged) {
			mMidiPortConnectionsChanged = false;
			std::string name(jack_port_name(mJackMidiIn));
			doConnectDisconnectFromParam(name, mJackMidiIn, false, mMidiInParam);
		}

		//update source connections from param
		auto doUpdate = [this, &doConnectDisconnectFromParam](std::set<std::string>& updates, ossia::net::node_base * parent) {
			for (auto name: updates) {
				auto node = parent->find_child(name);
				if (node == nullptr)
					continue;
				auto param = node->get_parameter();
				if (param == nullptr)
					continue;
				auto port = jack_port_by_name(mJackClient, name.c_str());
				if (port != nullptr) {
					doConnectDisconnectFromParam(name, port, true, param);
				}
			}
			updates.clear();
		};
		doUpdate(mSourceAudioPortConnectionUpdates, mPortAudioSourceConnectionsNode);
		doUpdate(mSourceMIDIPortConnectionUpdates, mPortMIDISourceConnectionsNode);

		{
			auto updateParamFromJack = [this](const std::string& portname, jack_port_t * port, bool isSource, ossia::net::parameter_base * param) {
				//get the current param values
				std::set<std::string> notInJack;
				auto val = param->value();
				if (val.get_type() == ossia::val_type::LIST) {
					auto l = val.get<std::vector<ossia::value>>();
					for (auto it: l) {
						if (it.get_type() == ossia::val_type::STRING) {
							notInJack.insert(it.get<std::string>());
						}
					}
				}

				bool inJackNotParam = false;
				std::vector<ossia::value> values; //accumulate "good" values in case we need to update the param
				iterate_connections(port, [&values, &inJackNotParam, &notInJack](std::string n) {
						inJackNotParam = inJackNotParam || notInJack.erase(n) == 0;
						values.push_back(n);
				});

				//if there are any remaining names in the set that we didn't see via jack
				//or there are any params that jack sees but aren't in the param list
				//push an update
				if (!notInJack.empty() || inJackNotParam) {
					param->push_value(values);
				}
			};

			if (mPortPoll && mPortPoll.get() < now) {
				mPortPoll.reset();
				updatePorts();
			}
			if (mPortConnectionPoll && mPortConnectionPoll.get() < now) {
				mPortConnectionPoll.reset();
				//find ports that have connection updates, query jakc and update param if needed
				for (auto id: mPortConnectionUpdates) {
					auto port = jack_port_by_id(mJackClient, id);
					if (port == nullptr) {
						continue;
					}

					std::string name(jack_port_name(port));
					if (port == mJackMidiIn) {
						updateParamFromJack(name, port, false, mMidiInParam);
					} else {
						auto flags = jack_port_flags(port);
						auto type = jack_port_type(port);
						//only care about outputs "sources"
						if (flags & JackPortIsOutput) {
							ossia::net::node_base * parent = nullptr;

							if (strcmp(type, JACK_DEFAULT_AUDIO_TYPE) == 0) {
								parent = mPortAudioSourceConnectionsNode;
							} else if (strcmp(type, JACK_DEFAULT_MIDI_TYPE) == 0) {
								parent = mPortMIDISourceConnectionsNode;
							} else {
								continue;
							}
							auto node = parent->find_child(name);
							if (node == nullptr)
								continue;
							auto param = node->get_parameter();
							if (param != nullptr) {
								updateParamFromJack(name, port, true, param);
							}
						}
					}
				}
				mPortConnectionUpdates.clear();
			}
		}

		auto bpmClient = mBPMClientUUID.load();
		bool hasProperty = !jack_uuid_empty(bpmClient);
		//manage BPM between 2 incoming async sources, prefer OSCQuery
		//OSCQuery and Jack Properties
		auto v = mTransportBPMParam->value();
		float bpm = (v.get_type() == ossia::val_type::FLOAT) ? v.get<float>() : 100.0;

		//if the incoming OSCQuery value has changed, report the property
		if (mTransportBPMLast != bpm) {
			mTransportBPMLast = bpm;
			if (hasProperty) {
				std::string bpms = std::to_string(bpm);
				mTransportBPMPropLast.store(bpm);
				jack_set_property(mJackClient, bpmClient, bpm_property_key.c_str(), bpms.c_str(), bpm_property_type);
			}
		} else if (hasProperty) {
			//property value changed, report out
			bpm = mTransportBPMPropLast.load();
			if (mTransportBPMLast != bpm) {
				mTransportBPMLast = bpm;
				mTransportBPMParam->push_value(bpm);
			}
		}

		//report transport start/stop state
		bool rolling = mTransportRollingUpdate.load();
		if (rolling != mTransportRollingLast.load()) {
			mTransportRollingLast.store(rolling);
			mTransportRollingParam->push_value(rolling);
		}
	}

	//without mMutex in case it calls back into something that needs it
	ProgramChange c;
	while (mProgramChangeQueue->try_dequeue(c)) {
		mProgramChangeCallback(c);
	}
}

bool ProcessAudioJack::updateCards() {
	fs::path alsa_cards("/proc/asound/cards");
	if (!fs::exists(alsa_cards)) {
		return false;
	}

	std::map<std::string, std::string> nameDesc;
	std::ifstream i(alsa_cards.string());

	//get all the lines, use semi instead of new line because regex doesn't support newline
	std::string value;
	std::string line;
	while (std::getline(i, line)) {
		value += (line + ";");
	}
	value += ";";
	std::smatch m;
	while (std::regex_search(value, m, alsa_card_regex)) {
		auto desc = m[3].str() + "\n" + m[4].str();
		auto name = std::string("hw:") + m[2].str();
		auto index = std::string("hw:") + m[1].str();
		value = m.suffix().str();
		nameDesc.insert({name, desc});
		nameDesc.insert({index, desc});
	}

	if (nameDesc != mCardNamesAndDescriptions) {
		mCardNamesAndDescriptions.swap(nameDesc);
		return true;
	}
	return false;
}

void ProcessAudioJack::updateCardNodes() {
	if (!mCardNode || !mCardListNode) {
		return;
	}

	std::vector<ossia::value> accepted;
	mCardListNode->clear_children();
	for (auto& kv: mCardNamesAndDescriptions) {
		accepted.push_back(kv.first);
		auto n = mCardListNode->create_child(kv.first);
		auto c = n->create_parameter(ossia::val_type::STRING);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
		c->push_value(kv.second);
	}

	//update card enum
	auto dom = ossia::init_domain(ossia::val_type::STRING);
	ossia::set_values(dom, accepted);
	mCardNode->set(ossia::net::domain_attribute{}, dom);
	mCardNode->set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);
}

bool ProcessAudioJack::createClient(bool startServer) {
	std::lock_guard<std::mutex> guard(mMutex);
	if (mJackClient == nullptr) {
		//start server
		if (startServer && mJackServer == nullptr && !createServer()) {
			std::cerr << "failed to create jack server" << std::endl;
			return false;
		}

		jack_status_t status;
		mJackClient = jack_client_open(CONTROL_CLIENT_NAME.c_str(), JackOptions::JackNoStartServer, &status);
		if (status == 0 && mJackClient) {
			mHasCreatedClient = true;

			mJackMidiIn = jack_port_register(mJackClient,
					"midiin",
					JACK_DEFAULT_MIDI_TYPE,
					JackPortFlags::JackPortIsInput,
					0
			);

			jack_set_process_callback(mJackClient, processJackProcess, this);
			jack_set_port_rename_callback(mJackClient, processJackPortRenamed, this);
			jack_set_port_registration_callback(mJackClient, processJackPortRegistration, this);
			jack_set_port_connect_callback(mJackClient, processJackPortConnection, this);

			mBuilder([this](ossia::net::node_base * root) {
				if (mIsRealTimeParam == nullptr) {
					auto n = mInfoNode->create_child("is_realtime");
					mIsRealTimeParam = n->create_parameter(ossia::val_type::BOOL);
					n->set(ossia::net::description_attribute{}, "indicates if jack is running in realtime mode or not");
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				}
				mIsRealTimeParam->push_value(jack_is_realtime(mJackClient) != 0);
				if (mIsOwnedParam == nullptr) {
					auto n = mInfoNode->create_child("owns_server");
					mIsOwnedParam = n->create_parameter(ossia::val_type::BOOL);
					n->set(ossia::net::description_attribute{}, "indicates if the runner manages/owns the server or not");
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				}
				mIsOwnedParam->push_value(mJackServer != nullptr);

				double sr = jack_get_sample_rate(mJackClient);
				jack_nframes_t bs = jack_get_buffer_size(mJackClient);
				if (sr != mSampleRate) {
					mSampleRateParam->push_value(static_cast<float>(sr));
				}
				if (bs != mPeriodFrames) {
					mPeriodFramesParam->push_value(static_cast<int>(bs));
				}

				if (!mTransportNode) {
					auto transport = mTransportNode = root->create_child("transport");
					{
						auto n = transport->create_child("bpm");
						mTransportBPMParam = n->create_parameter(ossia::val_type::FLOAT);
						mTransportBPMParam->push_value(100.0); //default
					}

					{
						const std::string key("sync_transport");
						//get from config
						bool sync = jconfig_get<bool>(key).get_value_or(true);
						sync_transport.store(sync);

						auto n = transport->create_child("sync");
						n->set(ossia::net::description_attribute{}, "should the runner sync to/from jack's transport or not");
						auto p = n->create_parameter(ossia::val_type::BOOL);
						p->push_value(sync);
						p->add_callback([this, key](const ossia::value& val) {
							if (val.get_type() == ossia::val_type::BOOL) {
								auto v = val.get<bool>();
								sync_transport.store(v);
								jconfig_set(v, key);
							}
						});
					}

					{
						auto n = transport->create_child("rolling");
						mTransportRollingParam = n->create_parameter(ossia::val_type::BOOL);
						auto state = jack_transport_query(mJackClient, nullptr);
						bool rolling = state != jack_transport_state_t::JackTransportStopped;
						mTransportRollingLast.store(rolling);
						mTransportRollingUpdate.store(rolling);

						mTransportRollingParam->push_value(rolling);

						mTransportRollingParam->add_callback([this](const ossia::value& val) {
							if (val.get_type() == ossia::val_type::BOOL) {
								bool was = mTransportRollingLast.load();
								bool rolling = val.get<bool>();
								if (was != rolling) {
									mTransportRollingLast.store(rolling);
									if (rolling) {
										jack_transport_start(mJackClient);
									} else {
										jack_transport_stop(mJackClient);
									}
								}
							}
						});
					}
				}

				//set property change callback, if we can
				{
					//try to get our uuid, if we can get it, we set the property and property callback
					char * uuids;
					if ((uuids = jack_get_uuid_for_client_name(mJackClient, jack_get_client_name(mJackClient))) != nullptr
							&& jack_uuid_parse(uuids, &mJackClientUUID) == 0) {
						jack_set_property_change_callback(mJackClient, ProcessAudioJack::jackPropertyChangeCallback, this);

						//find the current bpm
						jack_description_t * descriptions = nullptr;
						auto cnt = jack_get_all_properties(&descriptions);
						jack_uuid_t bpmClient = 0;
						if (cnt > 0) {
							for (auto i = 0; i < cnt; i++) {
								auto des = descriptions[i];
								for (auto j = 0; j < des.property_cnt && jack_uuid_empty(bpmClient); j++) {
									auto prop = des.properties[j];
									//find bpm key and attempt to convert data to double
									if (bpm_property_key.compare(prop.key) == 0) {
										char* pEnd = nullptr;
										//using floats because ossia doesn't do double
										float bpm = static_cast<float>(std::strtod(prop.data, &pEnd));
										if (*pEnd == 0) {
											bpmClient = des.subject;
											//update all the values
											mTransportBPMPropLast.store(bpm);
											mTransportBPMParam->push_value(bpm);
											mTransportBPMLast = bpm;
										}
									}
								}
								jack_free_description(&des, 0);
							}
							jack_free(descriptions);
						}
						mBPMClientUUID.store(bpmClient);
					}
				}
			});

			jack_activate(mJackClient);

			updatePorts();

			{
				const char ** ports;
				if ((ports = jack_get_ports(mJackClient, NULL, JACK_DEFAULT_MIDI_TYPE, JackPortIsPhysical)) != NULL) {
					for (auto ptr = ports; *ptr != nullptr; ptr++) {
						connectToMidiIf(jack_port_by_name(mJackClient, *ptr));
					}
					jack_free(ports);
				}
			}

		}
	}
	return mJackClient != nullptr;
}

void ProcessAudioJack::jackPropertyChangeCallback(jack_uuid_t subject, const char *key, jack_property_change_t change, void *arg) {
	reinterpret_cast<ProcessAudioJack *>(arg)->jackPropertyChangeCallback(subject, key, change);
}

void ProcessAudioJack::jackPropertyChangeCallback(jack_uuid_t subject, const char *key, jack_property_change_t change) {
	bool key_match = bpm_property_key.compare(key) == 0;
	jack_uuid_t bpmClient = mBPMClientUUID.load();
	//update the client uuid in case we don't already have it
	if (key_match && !jack_uuid_empty(subject)) {
		auto newId = change == jack_property_change_t::PropertyDeleted ? 0 : subject;
		if (newId != bpmClient) {
			mBPMClientUUID.store(newId);
		}
	}
	//if the subject is 'all' or matches the bpm subject
	if (!jack_uuid_empty(bpmClient) && (jack_uuid_empty(subject) || subject == bpmClient)) {
		//if the key is 'all' or matches the bpm key and it isn't a delete
		//grab the info
		if (!key || key_match) {
			if (change != jack_property_change_t::PropertyDeleted) {
				char * values = nullptr;
				char * types = nullptr;
				if (0 == jack_get_property(bpmClient, bpm_property_key.c_str(), &values, &types)) {
					//convert to double and store if success
					char* pEnd = nullptr;
					float bpm = static_cast<float>(std::strtod(values, &pEnd));
					if (*pEnd == 0) {
						mTransportBPMPropLast.store(bpm);
					}
					//free
					if (values)
						jack_free(values);
					if (types)
						jack_free(types);
				}
			}
		}
	}
}

//expects to be holding the build mutex
void ProcessAudioJack::updatePorts() {
	const char ** ports = nullptr;

	char * aliases[2] = { nullptr, nullptr };
	aliases[0] = new char[jack_port_name_size()];
	aliases[1] = new char[jack_port_name_size()];

	std::vector<std::tuple<ossia::net::parameter_base *, const char *, unsigned long>> portTypes = {
		{mPortAudioSourcesParam, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput},
		{mPortAudioSinksParam, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput},
		{mPortMidiSourcesParam, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput},
		{mPortMidiSinksParam, JACK_DEFAULT_MIDI_TYPE, JackPortIsInput},
	};

	//get the existing children so we can decide if we need to remove some
	auto achildren = mPortAudioSourceConnectionsNode->children_names();
	std::set<std::string> curAudioSources(achildren.begin(), achildren.end());
	auto mchildren = mPortMIDISourceConnectionsNode->children_names();
	std::set<std::string> curMIDISources(mchildren.begin(), mchildren.end());
	auto aliaschildren = mPortAliases->children_names();
	std::set<std::string> removePortAliases(aliaschildren.begin(), aliaschildren.end());

	for (auto info: portTypes) {
		auto p = std::get<0>(info);
		std::vector<ossia::value> names;

		auto portType = std::get<1>(info);
		auto portDirection = std::get<2>(info);

		if ((ports = jack_get_ports(mJackClient, NULL, portType, portDirection)) != NULL) {
			for (size_t i = 0; ports[i] != nullptr; i++) {

				jack_port_t * port = jack_port_by_name(mJackClient, ports[i]);
				if (port) {
					std::string name(ports[i]);
					names.push_back(name);
					removePortAliases.erase(name);

					//see if we have a connection node for this
					if (portDirection == JackPortIsOutput) {
						bool isAudio = strcmp(portType, JACK_DEFAULT_AUDIO_TYPE) == 0;
						auto parent = isAudio ? mPortAudioSourceConnectionsNode : mPortMIDISourceConnectionsNode;
						std::set<std::string>& cur = isAudio ? curAudioSources : curMIDISources;

						//if parent has it, remove from our current list
						if (parent->find_child(name) != nullptr) {
							cur.erase(name);
						} else {
							auto n = parent->create_child(name);
							auto p = n->create_parameter(ossia::val_type::LIST);
							p->add_callback([this, name, isAudio](const ossia::value& val) {
									if (isAudio) {
										mSourceAudioPortConnectionUpdates.insert(name);
									} else {
										mSourceMIDIPortConnectionUpdates.insert(name);
									}
							});
						}
					}

					auto cnt = jack_port_get_aliases(port, aliases);

					//update aliases
					if (cnt != 0) {
						auto n = mPortAliases->find_child(name);
						if (n == nullptr) {
							n = mPortAliases->create_child(name);
							n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
							n->create_parameter(ossia::val_type::LIST);
						}

						std::vector<ossia::value> aliasValues;
						for (int i = 0; i < cnt; i++) {
							aliasValues.push_back(std::string(aliases[i]));
						}
						n->get_parameter()->push_value(aliasValues);
					} else {
						auto n = mPortAliases->remove_child(name);
					}
				}
			}
			jack_free(ports);
		}

		p->push_value(names);
	}

	//remove any sources or aliases that don't exist anymore
	for (auto& n : curAudioSources) {
		mPortAudioSourceConnectionsNode->remove_child(n);
	}
	for (auto& n : curMIDISources) {
		mPortMIDISourceConnectionsNode->remove_child(n);
	}
	for (auto& n : removePortAliases) {
		mPortAliases->remove_child(n);
	}

	delete [] aliases[0];
	delete [] aliases[1];
}

void ProcessAudioJack::portRenamed(jack_port_id_t id, const char * /*old_name*/, const char * /*new_name*/) {
	if (mPortQueue) {
		mPortQueue->enqueue(std::make_pair(id, JackPortChange::Rename));
	}
}

void ProcessAudioJack::jackPortRegistration(jack_port_id_t id, int reg) {
	if (mPortQueue) {
		mPortQueue->enqueue(std::make_pair(id, reg != 0 ? JackPortChange::Register : JackPortChange::Unregister));
	}
}

void ProcessAudioJack::portConnected(jack_port_id_t a, jack_port_id_t b, bool /*connected*/) {
	if (mPortQueue) {
		mPortQueue->enqueue(std::make_pair(a, JackPortChange::Connection));
		mPortQueue->enqueue(std::make_pair(b, JackPortChange::Connection));
	}
}

//XXX expects to have mutex already
bool ProcessAudioJack::createServer() {
#if JACK_SERVER
	mJackServer = jackctl_server_create(NULL, NULL);
	if (mJackServer == nullptr) {
		std::cerr << "failed to create jack server" << std::endl;
		return false;
	}
	mHasCreatedServer = true;

	//create the server, destroy on failure
	auto create = [this]() -> bool {

		jackctl_driver_t * audioDriver = jackctl_server_get_driver(mJackServer, jack_driver_name);
		if (audioDriver == nullptr) {
			std::cerr << "cannot get driver " << jack_driver_name << " to create jack server" << std::endl;
			return false;
		}

		std::vector<char *> args;
		args.push_back(const_cast<char *>(jack_driver_name.c_str()));

#ifndef __APPLE__
		std::string dev("--device");
		std::string card = mCardName.empty() ? "hw:0" : mCardName;
		args.push_back(const_cast<char *>(dev.c_str()));
		args.push_back(const_cast<char *>(card.c_str()));

		std::string midi = std::string("-X") + mMIDISystem;
		args.push_back(const_cast<char *>(midi.c_str()));

		std::string nperiods("--nperiods");
		std::string nperiodsv = std::to_string(mNumPeriods);
		args.push_back(const_cast<char *>(nperiods.c_str()));
		args.push_back(const_cast<char *>(nperiodsv.c_str()));
#endif

		std::string period = "--period";
		std::string periodv = std::to_string(mPeriodFrames);
		args.push_back(const_cast<char *>(period.c_str()));
		args.push_back(const_cast<char *>(periodv.c_str()));

		std::string rate = "--rate";
		std::string ratev = std::to_string(mSampleRate);
		args.push_back(const_cast<char *>(rate.c_str()));
		args.push_back(const_cast<char *>(ratev.c_str()));

		if (jackctl_driver_params_parse(audioDriver, args.size(), &args.front())) {
			std::cerr << "failed to parse audio driver args ";
			for (auto a: args) {
				std::cerr << a << " ";
			}
			std::cerr << std::endl;
			return false;
		}

		//auto sigmask = jackctl_setup_signals(0);

		if (!jackctl_server_open(mJackServer, audioDriver)) {
			std::cerr << "failed to open jack server" << std::endl;
			return false;
		}

		if (jack_midi_driver_name.size()) {
			jackctl_driver_t * midiDriver = jackctl_server_get_driver(mJackServer, jack_midi_driver_name);
			if (midiDriver != nullptr && jackctl_driver_get_type(midiDriver) == JackSlave) {
				jackctl_server_add_slave(mJackServer, midiDriver);
			} else {
				std::cerr << "couldn't get jack midi driver" << std::endl;
			}
		}

		if (!jackctl_server_start(mJackServer)) {
			std::cerr << "failed to start jack server" << std::endl;
			return false;
		}
		return true;
	};

	if (!create()) {
		jackctl_server_destroy(mJackServer);
		mJackServer = nullptr;
		return false;
	}
	std::this_thread::sleep_for(std::chrono::seconds(2));
	return true;
#else
	return false;
#endif
}

void ProcessAudioJack::connectToMidiIf(jack_port_t * port) {
	if (config::get<bool>(config::key::ControlAutoConnectMIDI).value_or(false)) {
		if (port && !jack_port_is_mine(mJackClient, port) && std::string(jack_port_type(port)).compare(std::string(JACK_DEFAULT_MIDI_TYPE)) == 0) {
			auto flags = jack_port_flags(port);
			auto name = jack_port_name(port);

			//we don't want through, reconnecting to ports we're already connected to, or inputs
			if (is_through(name) || jack_port_connected_to(mJackMidiIn, name) || flags & JackPortFlags::JackPortIsInput) {
				return;
			}
			//check aliases and don't auto connect to rnbo midi outputs or through
			auto count = jack_port_get_aliases(port, mJackPortAliases);
			for (auto i = 0; i < count; i++) {
				std::string alias(mJackPortAliases[i]);
				if (is_through(mJackPortAliases[i]) || alias.find("rnbomidi") != std::string::npos) {
					return;
				}
			}
			jack_connect(mJackClient, name, jack_port_name(mJackMidiIn));
		}
	}
}

void ProcessAudioJack::handleTransportState(bool running) {
	if (!sync_transport.load()) {
		return;
	}
	if (running) {
		jack_transport_start(mJackClient);
	} else {
		jack_transport_stop(mJackClient);
	}
}

static void reposition(jack_client_t * client, std::function<void(jack_position_t& pos)> func) {
	if (!sync_transport.load()) {
		return;
	}
	jack_position_t pos;
	jack_transport_query(client, &pos);
	if (pos.valid & JackPositionBBT) {
		func(pos);
		jack_transport_reposition(client, &pos);
	}
}

void ProcessAudioJack::handleTransportTempo(double bpm) {
	if (!sync_transport.load()) {
		return;
	}
	//XXX should lock? std::lock_guard<std::mutex> guard(mMutex);
	//we shouldn't actually get this callback if audio isn't active so I don't think so
	auto bpmClient = mBPMClientUUID.load();
	if (jack_uuid_empty(bpmClient)) {
		return;
	}

	std::string bpms = std::to_string(bpm);
	jack_set_property(mJackClient, bpmClient, bpm_property_key.c_str(), bpms.c_str(), bpm_property_type);
}

void ProcessAudioJack::handleTransportBeatTime(double btime) {
	if (btime < 0.0)
		return;
	reposition(mJackClient, [btime](jack_position_t& pos) {
			//beat time is always in quater notes
			double numerator = pos.beats_per_bar;
			double denominator = pos.beat_type;
			if (denominator > 0.0) {
				double mul = denominator / 4.0;
				double bt = btime * mul;

				double bar = std::trunc(bt / numerator);
				double rem = bt - bar * numerator;
				double beat = std::trunc(rem);
				double tick = (rem - beat) * static_cast<double>(pos.ticks_per_beat);

				pos.bar = static_cast<int32_t>(bar) + 1;
				pos.beat = static_cast<int32_t>(beat) + 1;
				pos.tick = static_cast<int32_t>(std::trunc(tick));
			}
	});
}

void ProcessAudioJack::handleTransportTimeSig(double numerator, double denominator) {
	reposition(mJackClient, [numerator, denominator](jack_position_t& pos) {
			pos.beats_per_bar = static_cast<float>(numerator);
			pos.beat_type = static_cast<float>(denominator);
	});
}

InstanceAudioJack::InstanceAudioJack(
		std::shared_ptr<RNBO::CoreObject> core,
		RNBO::Json conf,
		std::string name,
		NodeBuilder builder,
		std::function<void(ProgramChange)> progChangeCallback,
		std::mutex& midiMapMutex,
		std::unordered_map<uint16_t, ossia::net::parameter_base *>& midiMap
		) : mCore(core), mInstanceConf(conf), mProgramChangeCallback(progChangeCallback),
	mMIDIMapMutex(midiMapMutex), mMIDIMap(midiMap)
{

	std::string clientName = name;
	if (conf.contains("jack") && conf["jack"].contains("client_name")) {
		clientName = conf["jack"]["client_name"];
	}

	//get jack client, fail early if we can't
	mJackClient = jack_client_open(clientName.c_str(), JackOptions::JackNoStartServer, nullptr);
	if (!mJackClient)
		throw new std::runtime_error("couldn't create jack client");

	jack_set_process_callback(mJackClient, processJackInstance, this);

	//setup queues, these might come from different threads?
	mPortQueue = RNBO::make_unique<moodycamel::ReaderWriterQueue<jack_port_id_t, 32>>(32);
	mPortConnectedQueue = RNBO::make_unique<moodycamel::ReaderWriterQueue<jack_port_id_t, 32>>(32);
	mProgramChangeQueue = RNBO::make_unique<moodycamel::ReaderWriterQueue<ProgramChange, 32>>(32);

	//init aliases
	mJackPortAliases[0] = new char[jack_port_name_size()];
	mJackPortAliases[1] = new char[jack_port_name_size()];

	//zero out transport position
	std::memset(&mTransportPosLast, 0, sizeof(jack_position_t));

	builder([this](ossia::net::node_base * root) {
		//setup jack
		auto jack = root->create_child("jack");
		auto conn = jack->create_child("connections");

		auto audio = conn->create_child("audio");
		auto midi = conn->create_child("midi");

		auto build_port_param = [this](jack_port_t * port, ossia::net::node_base * parent, const std::string& name, bool input) -> ossia::net::parameter_base * {

			//remove prefix: from string
			auto childname = name;
			auto it = childname.find(':');
			if (it != std::string::npos) {
				childname = childname.substr(it + 1);
			}

			auto n = parent->create_child(childname);

			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
			n->set(ossia::net::description_attribute{}, "Port and its connections, send port names to change, see /rnbo/jack/ports for names to connect");
			auto param = n->create_parameter(ossia::val_type::LIST);

			param->add_callback([port, input, name, this, param](const ossia::value& val) {
				std::set<std::string> add;
				std::set<std::string> remove;

				if (val.get_type() == ossia::val_type::LIST) {
					auto l = val.get<std::vector<ossia::value>>();
					std::vector<ossia::value> valid;
					bool invalidEntry = false;
					for (auto it: l) {
						if (it.get_type() == ossia::val_type::STRING) {
							add.insert(it.get<std::string>());
						} else {
							//ERROR
						}
					}

					iterate_connections(port, [&valid, &add, &remove](std::string n) {
						//we don't need to add connections that are already there
						if (add.count(n)) {
							valid.push_back(n);
							add.erase(n);
						} else {
							remove.insert(n);
						}
					});

					for (auto n: add) {
						int ret = 0;
						if (input) {
							ret = jack_connect(mJackClient, n.c_str(), name.c_str());
						} else {
							ret = jack_connect(mJackClient, name.c_str(), n.c_str());
						}
						//check response and update parameter
						if (ret == 0 || ret == EEXIST) {
							valid.push_back(n);
						} else {
							invalidEntry = true;
						}
					}
					for (auto n: remove) {
						if (input) {
							jack_disconnect(mJackClient, n.c_str(), name.c_str());
						} else {
							jack_disconnect(mJackClient, name.c_str(), n.c_str());
						}
					}
					//update param with valid entries
					if (invalidEntry) {
						//quiet to avoid recursion and deadlock
						param->set_value_quiet(valid);
					}
				}
			});

			return param;
		};

		auto audio_sinks = audio->create_child("sinks");
		auto audio_sources = audio->create_child("sources");
		auto midi_sinks = midi->create_child("sinks");
		auto midi_sources = midi->create_child("sources");


		//client name
		{
			auto n = jack->create_child("name");
			auto p = n->create_parameter(ossia::val_type::STRING);
			std::string name(jack_get_client_name(mJackClient));

			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
			p->push_value(name);
		}

		//create i/o
		{
			std::vector<ossia::value> names;
			for (auto i = 0; i < mCore->getNumInputChannels(); i++) {
				//TODO metadata?
				auto port = jack_port_register(mJackClient,
						("in" + std::to_string(i + 1)).c_str(),
						JACK_DEFAULT_AUDIO_TYPE,
						JackPortFlags::JackPortIsInput,
						0
				);

				std::string name(jack_port_name(port));
				names.push_back(name);

				mSampleBufferPtrIn.push_back(nullptr);
				mJackAudioPortIn.push_back(port);

				mPortParamMap.insert({port, build_port_param(port, audio_sinks, name, true)});
			}

			{
				auto n = jack->create_child("audio_ins");
				auto audio_ins = n->create_parameter(ossia::val_type::LIST);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				audio_ins->push_value(names);
			}
		}
		{
			std::vector<ossia::value> names;
			for (auto i = 0; i < mCore->getNumOutputChannels(); i++) {
				auto port = jack_port_register(mJackClient,
						("out" + std::to_string(i + 1)).c_str(),
						JACK_DEFAULT_AUDIO_TYPE,
						JackPortFlags::JackPortIsOutput,
						0
				);

				std::string name(jack_port_name(port));
				names.push_back(name);

				mSampleBufferPtrOut.push_back(nullptr);
				mJackAudioPortOut.push_back(port);

				mPortParamMap.insert({port, build_port_param(port, audio_sources, name, false)});
			}
			{
				auto n = jack->create_child("audio_outs");
				auto audio_outs = n->create_parameter(ossia::val_type::LIST);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				audio_outs->push_value(names);
			}
		}

		{
			mJackMidiIn = jack_port_register(mJackClient,
					"midiin1",
					JACK_DEFAULT_MIDI_TYPE,
					JackPortFlags::JackPortIsInput,
					0
			);
			auto n = jack->create_child("midi_ins");
			auto midi_ins = n->create_parameter(ossia::val_type::LIST);
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);

			std::string name(jack_port_name(mJackMidiIn));
			mPortParamMap.insert({mJackMidiIn, build_port_param(mJackMidiIn, midi_sinks, name, true)});

			midi_ins->push_value(ossia::value({ossia::value(name)}));
		}
		{
			mJackMidiOut = jack_port_register(mJackClient,
					"midiout1",
					JACK_DEFAULT_MIDI_TYPE,
					JackPortFlags::JackPortIsOutput,
					0
			);
			//add alias so we can avoid connecting control to it
			std::string alias = std::string(jack_get_client_name(mJackClient)) + ":rnbomidiout1";
			jack_port_set_alias(mJackMidiOut, alias.c_str());

			auto n = jack->create_child("midi_outs");
			auto midi_outs = n->create_parameter(ossia::val_type::LIST);
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);

			std::string name(jack_port_name(mJackMidiOut));
			mPortParamMap.insert({mJackMidiOut, build_port_param(mJackMidiOut, midi_sources, name, false)});

			midi_outs->push_value(ossia::value({ossia::value(name)}));
		}
	});

	double sr = jack_get_sample_rate(mJackClient);
	jack_nframes_t bs = jack_get_buffer_size(mJackClient);
	mCore->prepareToProcess(sr, bs);
	mFrameMillis = 1000.0 / sr;
	mMilliFrame = sr / 1000.0;
}

InstanceAudioJack::~InstanceAudioJack() {
	if (mAudioState.load() != AudioState::Stopped) {
		stop(0.0); //stop locks
	}
	{
		std::lock_guard<std::mutex> guard(mMutex);
		if (mJackClient) {
			if (mActivated) {
				jack_deactivate(mJackClient);
				jack_set_port_registration_callback(mJackClient, nullptr, nullptr);
				jack_set_port_connect_callback(mJackClient, nullptr, nullptr);
			}
			jack_client_close(mJackClient);
			mJackClient = nullptr;
		}
		//TODO unregister ports?
	}
	delete [] mJackPortAliases[0];
	delete [] mJackPortAliases[1];
}

void InstanceAudioJack::addConfig(RNBO::Json& conf) {
	conf["jack"]["client_name"] = std::string(jack_get_client_name(mJackClient));
}

void InstanceAudioJack::activate() {
	std::lock_guard<std::mutex> guard(mMutex);
	//protect against double activate or deactivate
	if (!mActivated) {
		if (jack_set_port_registration_callback(mJackClient, ::jackPortRegistration, this) != 0) {
			std::cerr << "failed to jack_set_port_registration_callback" << std::endl;
		}
		if (jack_set_port_connect_callback(mJackClient, ::jackInstancePortConnection, this) != 0) {
			std::cerr << "failed to jack_set_port_connect_callback" << std::endl;
		}
		//only connects what the config indicates
		mActivated = true;
		mAudioState.store(AudioState::Idle);

		jack_activate(mJackClient);
	}
}

float computeFadeIncr(jack_client_t *client, float ms) {
	float sample_rate = static_cast<float>(jack_get_sample_rate(client));
	return 1000.0 / (sample_rate * ms);
}

void InstanceAudioJack::start(float fadems) {
	if (fadems > 0.0f) {
		mFadeIncr.store(computeFadeIncr(mJackClient, fadems));
		mFade.store(0.0f);
		mAudioState.store(AudioState::Starting);
	} else {
		mAudioState.store(AudioState::Running);
	}
}

void InstanceAudioJack::stop(float fadems) {
	std::lock_guard<std::mutex> guard(mMutex);
	if (fadems > 0.0f) {
		//fade out, if mFade is less than 1.0, it'll be quicker
		mFadeIncr.store(-computeFadeIncr(mJackClient, fadems * mFade.load()));
		mAudioState.store(AudioState::Stopping);
	} else {
		mAudioState.store(AudioState::Stopped);
	}
}

void InstanceAudioJack::processEvents() {
	if (!mActivated) {
		return;
	}
	//process events from audio thread and notifications

	//deactivate
	auto state = mAudioState.load();
	if (state == AudioState::Stopped) {
		mActivated = false;
		jack_deactivate(mJackClient);
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		return;
	}

	//only process events while running/starting
	if (state == AudioState::Starting || state == AudioState::Running) {
		jack_port_id_t id;
		while (mPortQueue->try_dequeue(id)) {
			connectToMidiIf(jack_port_by_id(mJackClient, id));
		}

		//react to port connection changes
		std::set<jack_port_id_t> connections;
		while (mPortConnectedQueue->try_dequeue(id)) {
			connections.insert(id);
		}
		bool changed = false;
		for (auto id: connections) {
			auto p = jack_port_by_id(mJackClient, id);
			auto it = mPortParamMap.find(p);
			if (it != mPortParamMap.end()) {
				auto param = it->second;
				std::vector<ossia::value> names;
				iterate_connections(p, [&names](std::string name) {
						names.push_back(name);
				});
				param->push_value(names);
				changed = true;
			}
		}
		if (changed && mConfigChangeCallback != nullptr) {
			mConfigChangeCallback();
		}

		ProgramChange c;
		while (mProgramChangeQueue->try_dequeue(c)) {
			mProgramChangeCallback(c);
		}
	}
}

void InstanceAudioJack::connect() {
	mConnect = true;
	const char ** ports;

	RNBO::Json outlets;
	if (mInstanceConf.contains("outlets") && mInstanceConf["outlets"].is_array()) {
		outlets = mInstanceConf["outlets"];
	}
	RNBO::Json inlets;
	if (mInstanceConf.contains("inlets") && mInstanceConf["inlets"].is_array()) {
		inlets = mInstanceConf["inlets"];
	}

	bool autoConnect = false;
	bool indexed = false;
	{
		auto v = config::get<bool>(config::key::InstanceAutoConnectAudio);
		if (v) {
			autoConnect = *v;
		}
		v = config::get<bool>(config::key::InstanceAutoConnectAudioIndexed);
		if (v) {
			indexed = *v;
		}
	}
	if (autoConnect || indexed) {
		//get the port count, is there a better way?
		auto getPortCount = [](const char ** ptr) -> size_t {
			size_t portCount = 0;
			while (*ptr != nullptr) {
				portCount++;
				ptr++;
			}
			return portCount;
		};

		auto remap = [indexed](RNBO::Json ioletConf, size_t i) -> int {
			int index = static_cast<int>(i);
			if (indexed && ioletConf.is_array()) {
				if (ioletConf.size() <= i) {
					std::cerr << "iolet index << " << i << " out of range" << std::endl;
					return -1;
				}
				auto cur = ioletConf[i];
				if (!(cur.is_object() && cur.contains("index"))) {
					std::cerr << "iolet index << " << i << " data invalid" << std::endl;
					return -1;
				}
				index = cur["index"].get<int>() - 1;
			}
			return index;
		};

		//connect hardware audio outputs to our inputs
		if ((ports = jack_get_ports(mJackClient, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortIsPhysical|JackPortIsOutput)) != NULL) {
			size_t portCount = getPortCount(ports);
			for (size_t i = 0; i < mJackAudioPortIn.size(); i++) {
				int index = remap(inlets, i);
				if (index >= 0 && index < portCount) {
					jack_connect(mJackClient, ports[static_cast<size_t>(index)], jack_port_name(mJackAudioPortIn.at(i)));
				}
			}
			jack_free(ports);
		}

		//connect hardware audio inputs to our outputs
		if ((ports = jack_get_ports(mJackClient, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortIsPhysical|JackPortIsInput)) != NULL) {
			size_t portCount = getPortCount(ports);
			for (size_t i = 0; i < mJackAudioPortOut.size(); i++) {
				int index = remap(outlets, i);
				if (index >= 0 && index < portCount) {
					jack_connect(mJackClient, jack_port_name(mJackAudioPortOut.at(i)), ports[static_cast<size_t>(index)]);
				}
			}

			jack_free(ports);
		}
	}

	if ((ports = jack_get_ports(mJackClient, NULL, JACK_DEFAULT_MIDI_TYPE, JackPortIsPhysical)) != NULL) {
		for (auto ptr = ports; *ptr != nullptr; ptr++) {
			connectToMidiIf(jack_port_by_name(mJackClient, *ptr));
		}
		jack_free(ports);
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

void InstanceAudioJack::connectToMidiIf(jack_port_t * port) {
	if (!mConnect) {
		return;
	}

	bool all = config::get<bool>(config::key::InstanceAutoConnectMIDI).value_or(false);
	bool hardware = config::get<bool>(config::key::InstanceAutoConnectMIDIHardware).value_or(false);
	auto flags = jack_port_flags(port);

	if (all || (hardware && JackPortIsPhysical & flags)) {
		std::lock_guard<std::mutex> guard(mPortMutex);
		//if we can get the port, it isn't ours and it is a midi port
		if (port && !jack_port_is_mine(mJackClient, port) && std::string(jack_port_type(port)) == std::string(JACK_DEFAULT_MIDI_TYPE)) {
			//ignore through and virtual
			auto name = jack_port_name(port);
			std::string name_s(name);

			//ditch if the port is a through, if is already connected or if it is the control port
			if (is_through(name) || jack_port_connected_to(mJackMidiOut, name) || jack_port_connected_to(mJackMidiIn, name) || name_s.find(CONTROL_CLIENT_NAME) != std::string::npos)
				return;
			//check aliases, ditch if it is a virtual or through
			auto count = jack_port_get_aliases(port, mJackPortAliases);
			for (auto i = 0; i < count; i++) {
				if (is_through(mJackPortAliases[i]))
					return;
			}
			if (flags & JackPortFlags::JackPortIsInput) {
				jack_connect(mJackClient, jack_port_name(mJackMidiOut), name);
			} else if (flags & JackPortFlags::JackPortIsOutput) {
				jack_connect(mJackClient, name, jack_port_name(mJackMidiIn));
			}
		}
	}
}

void InstanceAudioJack::process(jack_nframes_t nframes) {
	auto midiOutBuf = jack_port_get_buffer(mJackMidiOut, nframes);
	jack_midi_clear_buffer(midiOutBuf);

	//get the buffers
	for (auto i = 0; i < mSampleBufferPtrIn.size(); i++)
		mSampleBufferPtrIn[i] = reinterpret_cast<jack_default_audio_sample_t *>(jack_port_get_buffer(mJackAudioPortIn[i], nframes));
	for (auto i = 0; i < mSampleBufferPtrOut.size(); i++)
		mSampleBufferPtrOut[i] = reinterpret_cast<jack_default_audio_sample_t *>(jack_port_get_buffer(mJackAudioPortOut[i], nframes));

	const auto state = mAudioState.load();
	if (state == AudioState::Idle || state == AudioState::Stopped) {
		for (auto i = 0; i < mSampleBufferPtrOut.size(); i++) {
			memset(mSampleBufferPtrOut[i], 0, sizeof(jack_default_audio_sample_t) * nframes);
		}
	} else {
		//get the current time
		auto nowms = mCore->getCurrentTime();

		//TODO sync to jack's time?

		//query the jack transport
		if (sync_transport.load()) {
			jack_position_t jackPos;
			auto state = jack_transport_query(mJackClient, &jackPos);

			//only use JackTransportRolling and JackTransportStopped
			bool rolling = state == jack_transport_state_t::JackTransportRolling;
			if (state != mTransportStateLast && (rolling || state == jack_transport_state_t::JackTransportStopped)) {
				RNBO::TransportEvent event(nowms, rolling ? RNBO::TransportState::RUNNING : RNBO::TransportState::STOPPED);
				mCore->scheduleEvent(event);
			}
			//if bbt is valid, check details
			if (jackPos.valid & JackPositionBBT) {
				auto lastValid = mTransportPosLast.valid & JackPositionBBT;

				//TODO check bbt_offset valid and compute

				//tempo
				if (!lastValid || mTransportPosLast.beats_per_minute != jackPos.beats_per_minute) {
					mCore->scheduleEvent(RNBO::TempoEvent(nowms, jackPos.beats_per_minute));
				}

				//time sig
				if (!lastValid || mTransportPosLast.beats_per_bar != jackPos.beats_per_bar || mTransportPosLast.beat_type != jackPos.beat_type) {
					mCore->scheduleEvent(RNBO::TimeSignatureEvent(nowms, static_cast<int>(std::ceil(jackPos.beats_per_bar)), static_cast<int>(std::ceil(jackPos.beat_type))));
				}

				//beat time
				if (!lastValid || mTransportPosLast.beat != jackPos.beat || mTransportPosLast.bar != jackPos.bar || mTransportPosLast.tick != jackPos.tick) {
					if (jackPos.ticks_per_beat > 0.0 && jackPos.beat_type > 0.0) { //should always be true, but just in case
																																				 //beat and bar start a 1
						double beatTime = static_cast<double>(jackPos.beat - 1) * 4.0 / jackPos.beat_type;
						beatTime +=  static_cast<double>(jackPos.bar - 1)  * jackPos.beats_per_bar * 4.0 / jackPos.beat_type;
						beatTime += static_cast<double>(jackPos.tick) / jackPos.ticks_per_beat;
						mCore->scheduleEvent(RNBO::BeatTimeEvent(nowms, beatTime));
					}
				}
			}

			mTransportPosLast = jackPos;
			mTransportStateLast = state;
		}

		//get midi in
		{
			mMIDIInList.clear();
			auto midi_buf = jack_port_get_buffer(mJackMidiIn, nframes);
			jack_nframes_t count = jack_midi_get_event_count(midi_buf);
			jack_midi_event_t evt;
			for (auto i = 0; i < count; i++) {
				jack_midi_event_get(&evt, midi_buf, i);
				//time is in frames since the first frame in this callback
				RNBO::MillisecondTime off = (RNBO::MillisecondTime)evt.time * mFrameMillis;
				mMIDIInList.addEvent(RNBO::MidiEvent(nowms + off, 0, evt.buffer, evt.size));
				//look for program change to change preset
				if (mProgramChangeQueue && evt.size == 2 && (evt.buffer[0] & 0xF0) == 0xC0) {
					mProgramChangeQueue->enqueue(ProgramChange { .chan = static_cast<uint8_t>(evt.buffer[0] & 0x0F), .prog = static_cast<uint8_t>(evt.buffer[1]) });
				}
			}
		}

		//RNBO process
		mCore->process(
				static_cast<jack_default_audio_sample_t **>(mSampleBufferPtrIn.size() == 0 ? nullptr : &mSampleBufferPtrIn.front()), mSampleBufferPtrIn.size(),
				static_cast<jack_default_audio_sample_t **>(mSampleBufferPtrOut.size() == 0 ? nullptr : &mSampleBufferPtrOut.front()), mSampleBufferPtrOut.size(),
				nframes, &mMIDIInList, &mMIDIOutList);

		//process midi out
		if (mMIDIOutList.size()) {
			//TODO do we need to sort the list??
			jack_nframes_t last = 0; //assure that we always got up
			for (const auto& e : mMIDIOutList) {
				auto t = e.getTime();
				jack_nframes_t frame = static_cast<jack_nframes_t>(std::max(0.0, t - nowms) * mMilliFrame);
				last = frame = std::max(last, frame);
				jack_midi_event_write(midiOutBuf, frame, e.getData(), e.getLength());
			}
			mMIDIOutList.clear();
		}

		if (state != AudioState::Running) {
			float fade = mFade.load();
			float incr = mFadeIncr.load();
			for (auto i = 0; i < nframes; i++) {
				float mul = std::clamp(fade, 0.0f, 1.0f);
				for (auto it: mSampleBufferPtrOut) {
					it[i] *= mul;
				}
				fade += incr;
				if (fade >= 1.0f)
					break;
			}
			mFade.store(std::clamp(fade, 0.0f, 1.0f));
			if (fade >= 1.0f || fade <= 0.0) {
				if (state == AudioState::Starting) {
					mAudioState.store(AudioState::Running);
				} else {
					mAudioState.store(AudioState::Stopped);
				}
			}
		}
	}
}

void InstanceAudioJack::jackPortRegistration(jack_port_id_t id, int reg) {
	//auto connect to midi
	//we only care about new registrations (non zero) as jack will auto disconnect unreg
	if (mPortQueue && reg != 0 && config::get<bool>(config::key::InstanceAutoConnectMIDI)) {
		mPortQueue->enqueue(id);
	}
}

void InstanceAudioJack::portConnected(jack_port_id_t a, jack_port_id_t b, bool /*connected*/) {
	mPortConnectedQueue->enqueue(a);
	mPortConnectedQueue->enqueue(b);
}
