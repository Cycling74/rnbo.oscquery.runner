#include "JackAudio.h"
#include "Config.h"

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

namespace fs = boost::filesystem;

namespace {
	const auto card_poll_period = std::chrono::seconds(2);

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

	static int processJackInstance(jack_nframes_t nframes, void *arg) {
		reinterpret_cast<InstanceAudioJack *>(arg)->process(nframes);
		return 0;
	}

	static void jackPortRegistration(jack_port_id_t id, int reg, void *arg) {
		reinterpret_cast<InstanceAudioJack *>(arg)->jackPortRegistration(id, reg);
	}

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
}

ProcessAudioJack::ProcessAudioJack(NodeBuilder builder) : mBuilder(builder), mJackClient(nullptr), mTransportBPMPropLast(100.0), mBPMClientUUID(0) {
	//read in config
	{
		mSampleRate = jconfig_get<double>("sample_rate").get_value_or(48000.);
		mPeriodFrames = jconfig_get<int>("period_frames").get_value_or(256);
#ifndef __APPLE__
		mCardName = jconfig_get<std::string>("card_name").get_value_or("");
		mNumPeriods = jconfig_get<int>("num_periods").get_value_or(2);
#endif
	}

	mBuilder([this](ossia::net::node_base * root) {
			mInfoNode = root->create_child("info");

			mPortInfoNode = mInfoNode->create_child("ports");

			auto conf = root->create_child("config");
			conf->set(ossia::net::description_attribute{}, "Jack configuration parameters");

#ifndef __APPLE__
			mCardListNode = mInfoNode->create_child("alsa_cards");
			mCardNode = conf->create_child("card");

			auto card = mCardNode->create_parameter(ossia::val_type::STRING);
			mCardNode->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
			mCardNode->set(ossia::net::description_attribute{}, "ALSA device name");

			updateCards();
			mCardsUpdated.store(false);
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
			mCardThread = std::thread([this]() {
					while (mPollCards.load()) {
						updateCards();
						std::this_thread::sleep_for(card_poll_period);
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

				mSampleRateParam->add_callback( [this](const ossia::value& val) {
					//TODO clamp?
					if (val.get_type() == ossia::val_type::FLOAT) {
						mSampleRate = val.get<float>();
						jconfig_set(mSampleRate, "sample_rate");
					}
				});
			}
	});
	createClient(false);
}

ProcessAudioJack::~ProcessAudioJack() {
	mPollCards.store(false);
	if (mCardThread.joinable()) {
		mCardThread.join();
	}
	setActive(false);
}

void ProcessAudioJack::process(jack_nframes_t nframes) {
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

bool ProcessAudioJack::isActive() {
	std::lock_guard<std::mutex> guard(mMutex);
	return mJackClient != nullptr;
}

bool ProcessAudioJack::setActive(bool active) {
	if (active) {
		return createClient(true);
	} else {
		mBuilder([this](ossia::net::node_base * root) {
			if (mTransportNode) {
				if (root->remove_child("transport")) {
					mTransportNode = nullptr;
				}
			}
		});
		std::lock_guard<std::mutex> guard(mMutex);
		if (mJackClient) {
			jack_client_close(mJackClient);
			mJackClient = nullptr;
		}
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
		return false;
	}
}

//Controller is holding onto build mutex, so feel free to build and don't lock it
void ProcessAudioJack::processEvents() {
	if (auto _lock = std::unique_lock<std::mutex> (mMutex, std::try_to_lock)) {
		//handle cards changing
		if (mCardsUpdated.exchange(false)) {
			updateCardNodes();
		}

		if (!mTransportBPMParam || !mJackClient) {
			return;
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
}

void ProcessAudioJack::updateCards() {
	fs::path alsa_cards("/proc/asound/cards");
	if (!fs::exists(alsa_cards)) {
		return;
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

	{
		std::lock_guard<std::mutex> guard(mCardMutex);
		if (nameDesc != mCardNamesAndDescriptions) {
			mCardNamesAndDescriptions.swap(nameDesc);
			mCardsUpdated.store(true);
		}
	}
}

void ProcessAudioJack::updateCardNodes() {
	if (!mCardNode || !mCardListNode) {
		return;
	}

	std::lock_guard<std::mutex> guard(mCardMutex);
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
		mJackClient = jack_client_open("rnbo-info", JackOptions::JackNoStartServer, &status);
		if (status == 0 && mJackClient) {
			jack_set_process_callback(mJackClient, processJackProcess, this);

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
			updatePorts();
			jack_activate(mJackClient);
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

void ProcessAudioJack::updatePorts() {
	mBuilder([this](ossia::net::node_base *) {
			const char ** ports = nullptr;

			char * aliases[2] = { nullptr, nullptr };
			aliases[0] = new char[jack_port_name_size()];
			aliases[1] = new char[jack_port_name_size()];

			if (mPortAudioSinksParam == nullptr) {
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
			}

			std::vector<std::tuple<ossia::net::parameter_base *, const char *, unsigned long>> portTypes = {
				{mPortAudioSourcesParam, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput},
				{mPortAudioSinksParam, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput},
				{mPortMidiSourcesParam, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput},
				{mPortMidiSinksParam, JACK_DEFAULT_MIDI_TYPE, JackPortIsInput},
			};

			for (auto info: portTypes) {
				auto p = std::get<0>(info);
				std::vector<ossia::value> names;

				if ((ports = jack_get_ports(mJackClient, NULL, std::get<1>(info), std::get<2>(info))) != NULL) {
					for (size_t i = 0; ports[i] != nullptr; i++) {
						jack_port_t * port = jack_port_by_name(mJackClient, ports[i]);
						if (port) {
							if (jack_port_get_aliases(port, aliases)) {
								names.push_back(std::string(aliases[0])); //do we ever want the 2nd one?
							} else {
								names.push_back(std::string(ports[i]));
							}
						}
					}
					jack_free(ports);
				}

				p->push_value(names);
			}

			delete [] aliases[0];
			delete [] aliases[1];
	});
}

//XXX expects to have mutex already
bool ProcessAudioJack::createServer() {
	mJackServer = jackctl_server_create(NULL, NULL);
	if (mJackServer == nullptr) {
		std::cerr << "failed to create jack server" << std::endl;
		return false;
	}

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

		std::string midi("-Xseq");
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
		std::function<void(ProgramChange)> progChangeCallback
		) : mCore(core), mInstanceConf(conf), mRunning(false), mProgramChangeCallback(progChangeCallback) {
	//get jack client, fail early if we can't
	mJackClient = jack_client_open(name.c_str(), JackOptions::JackNoStartServer, nullptr);
	if (!mJackClient)
		throw new std::runtime_error("couldn't create jack client");

	jack_set_process_callback(mJackClient, processJackInstance, this);

	//setup queues, these might come from different threads?
	mPortQueue = RNBO::make_unique<moodycamel::ReaderWriterQueue<jack_port_id_t, 32>>(32);
	mProgramChangeQueue = RNBO::make_unique<moodycamel::ReaderWriterQueue<ProgramChange, 32>>(32);

	//init aliases
	mJackPortAliases[0] = new char[jack_port_name_size()];
	mJackPortAliases[1] = new char[jack_port_name_size()];

	//zero out transport position
	std::memset(&mTransportPosLast, 0, sizeof(jack_position_t));

	builder([this](ossia::net::node_base * root) {
		//setup jack
		auto jack = root->create_child("jack");

		//create i/o
		{
			std::vector<ossia::value> names;
			for (auto i = 0; i < mCore->getNumInputChannels(); i++) {
				auto port = jack_port_register(mJackClient,
						("in" + std::to_string(i + 1)).c_str(),
						JACK_DEFAULT_AUDIO_TYPE,
						JackPortFlags::JackPortIsInput,
						0
				);
				names.push_back(std::string(jack_port_name(port)));
				mSampleBufferPtrIn.push_back(nullptr);
				mJackAudioPortIn.push_back(port);
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
				names.push_back(std::string(jack_port_name(port)));
				mSampleBufferPtrOut.push_back(nullptr);
				mJackAudioPortOut.push_back(port);
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
			midi_ins->push_value(ossia::value({ossia::value(std::string(jack_port_name(mJackMidiIn)))}));
		}
		{
			mJackMidiOut = jack_port_register(mJackClient,
					"midiout1",
					JACK_DEFAULT_MIDI_TYPE,
					JackPortFlags::JackPortIsOutput,
					0
			);
			auto n = jack->create_child("midi_outs");
			auto midi_outs = n->create_parameter(ossia::val_type::LIST);
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
			midi_outs->push_value(ossia::value({ossia::value(std::string(jack_port_name(mJackMidiOut)))}));
		}
	});

	double sr = jack_get_sample_rate(mJackClient);
	jack_nframes_t bs = jack_get_buffer_size(mJackClient);
	mCore->prepareToProcess(sr, bs);
	mFrameMillis = 1000.0 / sr;
	mMilliFrame = sr / 1000.0;
}

InstanceAudioJack::~InstanceAudioJack() {
	stop(); //stop locks
	{
		std::lock_guard<std::mutex> guard(mMutex);
		if (mJackClient) {
			jack_client_close(mJackClient);
			mJackClient = nullptr;
		}
		//TODO unregister ports?
	}
	delete [] mJackPortAliases[0];
	delete [] mJackPortAliases[1];
}

void InstanceAudioJack::start() {
	std::lock_guard<std::mutex> guard(mMutex);
	//protect against double activate or deactivate
	if (!mRunning) {
		if (jack_set_port_registration_callback(mJackClient, ::jackPortRegistration, this) != 0) {
			std::cerr << "failed to jack_set_port_registration_callback" << std::endl;
		}
		jack_activate(mJackClient);
		//only connects what the config indicates
		connectToHardware();
		mRunning = true;
	}
}

void InstanceAudioJack::stop() {
	std::lock_guard<std::mutex> guard(mMutex);
	if (mRunning) {
		jack_deactivate(mJackClient);
		mRunning = false;
	}
}

bool InstanceAudioJack::isActive() {
	return mRunning;
}

void InstanceAudioJack::processEvents() {
	//process events from audio thread and notifications

	jack_port_id_t id;
	while (mPortQueue->try_dequeue(id)) {
		connectToMidiIf(jack_port_by_id(mJackClient, id));
	}

	ProgramChange c;
	while (mProgramChangeQueue->try_dequeue(c)) {
		mProgramChangeCallback(c);
	}
}

void InstanceAudioJack::connectToHardware() {
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


	if (config::get<bool>(config::key::InstanceAutoConnectMIDI)) {
		if ((ports = jack_get_ports(mJackClient, NULL, JACK_DEFAULT_MIDI_TYPE, JackPortIsPhysical)) != NULL) {
			for (auto ptr = ports; *ptr != nullptr; ptr++) {
				connectToMidiIf(jack_port_by_name(mJackClient, *ptr));
			}
			jack_free(ports);
		}
	}
}

void InstanceAudioJack::connectToMidiIf(jack_port_t * port) {
	std::lock_guard<std::mutex> guard(mPortMutex);
	//if we can get the port, it isn't ours and it is a midi port
	if (port && !jack_port_is_mine(mJackClient, port) && std::string(jack_port_type(port)) == std::string(JACK_DEFAULT_MIDI_TYPE)) {
		//ignore through and virtual
		auto is_through = [](const char * name) -> bool {
			std::string lower(name);
			transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
			return lower.find("through") != std::string::npos || lower.find("virtual") != std::string::npos;
		};
		auto name = jack_port_name(port);
		//ditch if the port is a through or is already connected
		if (is_through(name) || jack_port_connected_to(mJackMidiOut, name) || jack_port_connected_to(mJackMidiIn, name))
			return;
		//check aliases, ditch if it is a virtual or through
		auto count = jack_port_get_aliases(port, mJackPortAliases);
		for (auto i = 0; i < count; i++) {
			if (is_through(mJackPortAliases[i]))
				return;
		}
		auto flags = jack_port_flags(port);
		if (flags & JackPortFlags::JackPortIsInput) {
			jack_connect(mJackClient, jack_port_name(mJackMidiOut), name);
		} else if (flags & JackPortFlags::JackPortIsOutput) {
			jack_connect(mJackClient, name, jack_port_name(mJackMidiIn));
		}
	}
}

void InstanceAudioJack::process(jack_nframes_t nframes) {
	auto midiOutBuf = jack_port_get_buffer(mJackMidiOut, nframes);
	jack_midi_clear_buffer(midiOutBuf);

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

	//get the buffers
	for (auto i = 0; i < mSampleBufferPtrIn.size(); i++)
		mSampleBufferPtrIn[i] = reinterpret_cast<jack_default_audio_sample_t *>(jack_port_get_buffer(mJackAudioPortIn[i], nframes));
	for (auto i = 0; i < mSampleBufferPtrOut.size(); i++)
		mSampleBufferPtrOut[i] = reinterpret_cast<jack_default_audio_sample_t *>(jack_port_get_buffer(mJackAudioPortOut[i], nframes));

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
		for (const auto& e : mMIDIOutList) {
			jack_nframes_t frame = std::max(0.0, e.getTime() - nowms) * mMilliFrame;
			jack_midi_event_write(midiOutBuf, frame, e.getData(), e.getLength());
		}
		mMIDIOutList.clear();
	}
}

void InstanceAudioJack::jackPortRegistration(jack_port_id_t id, int reg) {
	//auto connect to midi
	//we only care about new registrations (non zero) as jack will auto disconnect unreg
	if (mPortQueue && reg != 0 && config::get<bool>(config::key::InstanceAutoConnectMIDI)) {
		mPortQueue->enqueue(id);
	}
}
