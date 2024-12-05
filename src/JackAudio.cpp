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
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/algorithm/string/trim.hpp>

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
	const auto stats_poll_period = std::chrono::seconds(2);
	const auto port_poll_timeout = std::chrono::milliseconds(20);
	const std::string persist_extra_key = "persist_extra";

	const std::string CONTROL_CLIENT_NAME("rnbo-control");

	const std::string PORTGROUPKEY(JACK_METADATA_PORT_GROUP);
	const std::string RNBO_GRAPH_PORTGROUP("rnbo-graph-user-io");


#ifdef __APPLE__
	const static std::string jack_driver_name = "coreaudio";
	const static std::string jack_midi_driver_name = "coremidi";
#else
	const static std::string jack_driver_name = "alsa";
	const static std::string jack_midi_driver_name;
#endif

	const boost::optional<std::string> ns("jack");
	template <typename T>
	boost::optional<T> jconfig_get(const std::string& key) {
		return config::get<T>(key, ns);
	}

	template <typename T>
	void jconfig_set(const T& v, const std::string& key) {
		return config::set<T>(v, key, ns);
	}

	//jackextra are extra args per card name that are stored in the config
	const boost::optional<std::string> extra_ns("jackextra");
	template <typename T>
	boost::optional<T> jextraconfig_get(const std::string& key) {
		return config::get<T>(key, extra_ns);
	}

	template <typename T>
	void jextraconfig_set(const T& v, const std::string& key) {
		return config::set<T>(v, key, extra_ns);
	}

	template boost::optional<int> jconfig_get(const std::string& key);
	template boost::optional<double> jconfig_get(const std::string& key);
	template boost::optional<bool> jconfig_get(const std::string& key);

	template boost::optional<std::string> jextraconfig_get(const std::string& key);

	const static std::regex alsa_card_regex(R"X(\s*(\d+)\s*\[([^\[]+?)\s*\]:\s*([^;]+?)\s*;\s*([^;]+?)\s*;)X");
	const static std::regex raw_midi_regex(R"X(^(in|out)-hw-\d+-\d+-\d+-(.*)$)X");

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

	static int processJackXRun(void * arg) {
		reinterpret_cast<ProcessAudioJack *>(arg)->xrun();
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

	std::set<std::string> mRNBOGraphPortGroupNames;
	std::mutex mRNBOGraphPortGroupNamesMutex;

	std::vector<std::string> port_names(const char ** ports, bool filter = false) {
		std::vector<std::string> names;
		if (ports != NULL) {
			std::lock_guard<std::mutex> guard(mRNBOGraphPortGroupNamesMutex);
			for (auto ptr = ports; *ptr != nullptr; ptr++) {
				std::string name(*ptr);
				if (!filter || mRNBOGraphPortGroupNames.count(name) != 0) {
					names.push_back(name);
				}
			}
			jack_free(ports);
		}

		//TODO sort?

		return names;
	}
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
		mExtraArgs = jconfig_get<std::string>("extra").get_value_or("");
#endif
	}

	mBuilder([this](ossia::net::node_base * root) {
			mInfoNode = root->create_child("info");

			{
				auto n = mInfoNode->create_child("is_active");
				n->set(ossia::net::description_attribute{}, "Gives the actual status of the jack server being active or not");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);

				mAudioActiveParam = n->create_parameter(ossia::val_type::BOOL);
				mAudioActiveParam->push_value(false);
			}

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

				mPortProps = mPortInfoNode->create_child("properties");
				mPortProps->set(ossia::net::description_attribute{}, "Ports and a list of their properties");
			}

			auto conf = root->create_child("config");
			conf->set(ossia::net::description_attribute{}, "Jack configuration parameters");

			auto control = root->create_child("control");

			mStatsPollNext = std::chrono::steady_clock::now() + stats_poll_period;
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

					//if we're presisting extra and we switch to a new card, restore the extra with that card
					const bool persist_extra = jconfig_get<bool>(persist_extra_key).value_or(true);
					if (persist_extra && mCardName.size() && mExtraArgsParam) {
						mExtraArgsParam->push_value(jextraconfig_get<std::string>(mCardName).value_or(""));
					}
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

			{
				const bool persist_extra = jconfig_get<bool>(persist_extra_key).value_or(true);
				{
					auto n = conf->create_child("extra");
					auto p = mExtraArgsParam = n->create_parameter(ossia::val_type::STRING);
					n->set(ossia::net::description_attribute{}, "Extra args to be passed to jack when creating the server, these are appended after the args generated by sample_rate, period_frames, etc.");

					{
						//get out last extra config OR potentially one stored in the extra config for this card
						if (mExtraArgs.size() == 0 && persist_extra && mCardName.size()) {
							mExtraArgs = jextraconfig_get<std::string>(mCardName).value_or("");
						}
						p->push_value(mExtraArgs);
					}

					p->add_callback([this](const ossia::value& val) {
						if (val.get_type() == ossia::val_type::STRING) {
							mExtraArgs = val.get<std::string>();
							jconfig_set(mExtraArgs, "extra");
							if (mCardName.size() && jconfig_get<bool>(persist_extra_key).value_or(true)) {
								jextraconfig_set(mExtraArgs, mCardName);
							}
						}
					});
				}

				{
					auto n = conf->create_child(persist_extra_key);
					auto p = n->create_parameter(ossia::val_type::BOOL);
					n->set(ossia::net::description_attribute{}, "Should the runner save/restore \"extra\" args associated with your \"card\", useful if you switch cards.");

					p->push_value(persist_extra);
					p->add_callback([this](const ossia::value& val) {
						if (val.get_type() == ossia::val_type::BOOL) {
							jconfig_set(val.get<bool>(), persist_extra_key);
						}
					});
				}
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

/*
 * example raw midi aliases
 *  system:midi_capture_4
 *	  in-hw-1-0-0-Faderfox-DJ3-MIDI-1
 *	system:midi_playback_4
 *		 out-hw-1-0-0-Faderfox-DJ3-MIDI-1
 *	system:midi_capture_2
 *		 in-hw-2-0-0-Scarlett-2i4-USB-MIDI-1
 *	system:midi_playback_2
 *		 out-hw-2-0-0-Scarlett-2i4-USB-MIDI-1
 *	system:midi_capture_5
 *		 in-hw-5-0-0-QUNEO-MIDI-1
 *	system:midi_playback_5
 *		 out-hw-5-0-0-QUNEO-MIDI-1
 */

bool ProcessAudioJack::connect(const std::vector<SetConnectionInfo>& connections, bool withControlConnections) {
	if (mJackClient) {
		auto replace_raw = [this](std::string& portname) {
			std::array<std::vector<char>, 2> aliasStrings = {
				std::vector<char>(static_cast<size_t>(jack_port_name_size()), '\0'),
				std::vector<char>(static_cast<size_t>(jack_port_name_size()), '\0')
			};
			std::array<char *, 2> aliases = { aliasStrings[0].data(), aliasStrings[0].data() };

			//work around moving raw_midi aliases
			std::smatch match;
			if (std::regex_match(portname, match, raw_midi_regex)) {
				const std::string prefix = match[1].str();
				const std::string suffix = match[2].str();


				//find alias match
				//search all the ports
				//TODO could filter by type??
				bool found = false;
				auto ports = jack_get_ports(mJackClient, nullptr, nullptr, 0);
				if (ports) {
					for (size_t i = 0; ports[i] != nullptr && !found; i++) {
						std::string name(ports[i]);
						const jack_port_t * port = jack_port_by_name(mJackClient, name.c_str());
						//search aliases, look for a match
						auto cnt = jack_port_get_aliases(port, aliases.data());
						for (auto j = 0; j < cnt && !found; j++) {
							std::string alias(aliases[j]);
							if (std::regex_match(alias, match, raw_midi_regex) && match[1].str() == prefix && match[2].str() == suffix) {
								portname = name;
								found = true;
							}
						}
					}
					jack_free(ports);
				}
			}
		};

		for (auto& info: connections) {
			if (info.sink_name == CONTROL_CLIENT_NAME && !withControlConnections) {
				continue;
			}
			std::string source = info.source_name;
			if (info.source_port_name.size()) {
				source += (std::string(":") + info.source_port_name);
			}
			std::string sink = info.sink_name;
			if (info.sink_port_name.size()) {
				sink += (std::string(":") + info.sink_port_name);
			}

			replace_raw(source);
			replace_raw(sink);

			jack_connect(mJackClient, source.c_str(), sink.c_str());
		}
		return true;
	}
	return false;
}

std::vector<SetConnectionInfo> ProcessAudioJack::connections() {
	std::vector<SetConnectionInfo> conn;

	const char ** sources = nullptr;
	std::array<std::vector<char>, 2> aliasStrings = {
		std::vector<char>(static_cast<size_t>(jack_port_name_size()), '\0'),
		std::vector<char>(static_cast<size_t>(jack_port_name_size()), '\0')
	};
	std::array<char *, 2> aliases = { aliasStrings[0].data(), aliasStrings[0].data() };

	//use a port alias for physical midi ports instead of the system name
	auto use_midi_port_alias = [this, &aliases](std::string name) -> std::string {
		const jack_port_t * port = jack_port_by_name(mJackClient, name.c_str());
		const auto port_type = jack_port_type(port);
		const auto flags = jack_port_flags(port);
		if (strcmp(port_type, JACK_DEFAULT_MIDI_TYPE) == 0 && flags & JackPortIsPhysical) {
			auto cnt = jack_port_get_aliases(port, aliases.data());
			for (auto i = 0; i < cnt; i++) {
				std::string alias(aliases[i]);
				//don't use alsa_pcm: prefix if we don't have to
				if (!alias.starts_with("alsa_pcm") || cnt == 1) {
					name = alias;
				}
			}
		}
		return name;
	};

	auto cleanup_name_info = [](const std::string& name) -> std::vector<std::string> {
		std::vector<std::string> info;
		boost::algorithm::split(info, name, boost::is_any_of(":"));
		//add an empty entry if there is only 1 entry
		if (info.size() == 1) {
			info.push_back("");
		} else {
			//if there are more than 2 entries, build them back into 2
			for (auto j = 2; j < info.size(); j++) {
				info[1] += ":" + info[j];
			}
		}
		return info;
	};

	if ((sources = jack_get_ports(mJackClient, nullptr, nullptr, JackPortIsOutput)) != nullptr) {
		for (size_t i = 0; sources[i] != nullptr; i++) {
			std::string name(sources[i]);
		 	jack_port_t * src = jack_port_by_name(mJackClient, name.c_str());
			name = use_midi_port_alias(name);

			std::vector<std::string> src_info = cleanup_name_info(name);
			iterate_connections(src, [&conn, &src_info, &use_midi_port_alias, &cleanup_name_info](std::string sinkname) {
					sinkname = use_midi_port_alias(sinkname);

					std::vector<std::string> sink_info = cleanup_name_info(sinkname);
					conn.push_back(SetConnectionInfo(src_info[0], src_info[1], sink_info[0], sink_info[1]));
			});
		}
		jack_free(sources);
	}

	return conn;
}

bool ProcessAudioJack::isActive() {
	std::lock_guard<std::mutex> guard(mMutex);
	return mJackClient != nullptr;
}

bool ProcessAudioJack::setActive(bool active, bool withServer) {
	if (active) {
		//only force a server if we have done it before or we've never created a client
		auto createServer = withServer && (mHasCreatedServer || !mHasCreatedClient);
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
			mPortProps->clear_children();
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
		mAudioActiveParam->push_value(false);
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

		//update stats
		{
			if (mStatsPollNext < now) {
				mStatsPollNext = std::chrono::steady_clock::now() + stats_poll_period;

				//report
				auto c = mXRunCount.load();
				if (c != mXRunCountLast) {
					mXRunCountLast = c;
					mXRunCountParam->push_value(c);
				}
				mCPULoadParam->push_value(jack_cpu_load(mJackClient));
			}
		}

		{
			std::pair<jack_port_id_t, JackPortChange> entry;
			while (mPortQueue->try_dequeue(entry)) {
				if (entry.second == JackPortChange::Register) {
					connectToMidiIf(jack_port_by_id(mJackClient, entry.first));
				}

				if (entry.second == JackPortChange::Connection) {
					auto port = jack_port_by_id(mJackClient, entry.first);
					if (port != nullptr) {
						std::string name(jack_port_name(port));

						mPortConnectionUpdates.insert(name);
						mPortConnectionPoll = now + port_poll_timeout;
					}
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

			bool updateParam = false;
			if (val.get_type() == ossia::val_type::LIST) {
				auto l = val.get<std::vector<ossia::value>>();
				for (auto it: l) {
					if (it.get_type() == ossia::val_type::STRING) {
						toConnect.insert(it.get<std::string>());
					}
				}
			} else {
				updateParam = true;
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
				bool update = false;
				if (val.get_type() == ossia::val_type::LIST) {
					auto l = val.get<std::vector<ossia::value>>();
					for (auto it: l) {
						if (it.get_type() == ossia::val_type::STRING) {
							notInJack.insert(it.get<std::string>());
						}
					}
				} else {
					update = true;
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
				if (update || !notInJack.empty() || inJackNotParam) {
					param->push_value(values);
				}
			};

			if (mPortPoll && mPortPoll.get() < now) {
				mPortPoll.reset();
				updatePorts();
			}
			if (mPortConnectionPoll && mPortConnectionPoll.get() < now) {
				std::set<std::string> names;
				std::swap(mPortConnectionUpdates, names);
				mPortConnectionPoll.reset();

				//find ports that have connection updates, query jack and update param if needed
				for (auto name: names) {
					auto port = jack_port_by_name(mJackClient, name.c_str());
					if (port == nullptr) {
						continue;
					}

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
			}
		}

		if (mPortPropertyPoll && mPortPropertyPoll.get() < now) {
			mPortPropertyPoll.reset();

			std::set<std::string> names;
			std::swap(mPortPropertyUpdates, names);

			for (auto name: names) {
				jack_port_t * port = jack_port_by_name(mJackClient, name.c_str());
				if (port != nullptr) {
					updatePortProperties(port);
				}
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

void ProcessAudioJack::updatePortProperties(jack_port_t* port) {
	std::string name(jack_port_name(port));
	bool inPortGroup = false;

	//jack property subjects are often URIs which wouldn't work as names in the OSCQuery name space so we encode the entire
	//blob as JSON and make it a string
	RNBO::Json properties = RNBO::Json::object();

	jack_uuid_t uuid = jack_port_uuid(port);
	if (!jack_uuid_empty(uuid)) {
		jack_description_t description;
		auto cnt = jack_get_properties(uuid, &description);
		for (auto i = 0; i < cnt; i++) {
			char* pEnd = nullptr;
			auto prop = description.properties[i];
			if (prop.key == nullptr || prop.data == nullptr) {
				continue;
			}

			std::string key(prop.key);
			std::string data(prop.data);
			if (prop.type == nullptr || strcmp(prop.type, "https://www.w3.org/2001/XMLSchema#string") == 0 || strcmp(prop.type, "text/plain") == 0) {
				properties[key] = data;
				if (PORTGROUPKEY.compare(key) == 0 && RNBO_GRAPH_PORTGROUP.compare(data) == 0) {
					inPortGroup = true;
				}
			}
			else if (strcmp(prop.type, "http://www.w3.org/2001/XMLSchema#int") == 0) {
				size_t end = 0;
				int value = std::stoi(data, &end);
				if (end == data.size()) {
					properties[key] = value;
				}
			}
			else if (
					strcmp(prop.type, "http://www.w3.org/2001/XMLSchema#double") == 0||
					strcmp(prop.type, "http://www.w3.org/2001/XMLSchema#float") == 0) {
				double value = std::strtod(prop.data, &pEnd);
				if (*pEnd == 0) {
					properties[key] = value;
				}
			}

		}
		if (cnt > 0) {
			jack_free_description(&description, 0);
		}
	}

	auto flags = jack_port_flags(port);
	if (flags & JackPortIsPhysical) {
		properties["physical"] = true;
		//automatically add physical io to the RNBO_GRAPH_PORTGROUP port group if there is no other port group indicated
		if (!properties.contains(PORTGROUPKEY)) {
			properties[PORTGROUPKEY] = RNBO_GRAPH_PORTGROUP;
			inPortGroup = true;
		}
	}
	if (flags & JackPortIsTerminal) {
		properties["terminal"] = true;
	}

	auto n = mPortProps->find_child(name);
	if (properties.size() > 0) {
		if (n == nullptr) {
			n = mPortProps->create_child(name);
			n->set(ossia::net::description_attribute{}, "JSON key/value object indicating Jack properties for this port");
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
			n->create_parameter(ossia::val_type::STRING);
		}
		std::string j(properties.dump());
		n->get_parameter()->push_value(j);
	} else if (n != nullptr) {
		mPortProps->remove_child(name);
	}

	{
		std::lock_guard<std::mutex> guard(mRNBOGraphPortGroupNamesMutex);
		if (inPortGroup) {
			mRNBOGraphPortGroupNames.insert(name);
		} else {
			mRNBOGraphPortGroupNames.erase(name);
		}
	}

}

bool ProcessAudioJack::createClient(bool startServer) {
	std::lock_guard<std::mutex> guard(mMutex);
	if (mJackClient == nullptr) {
		//start server
		if (startServer && mJackServer == nullptr && !createServer()) {
			std::cerr << "failed to create jack server" << std::endl;
			mAudioActiveParam->push_value(false);
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
			jack_set_xrun_callback(mJackClient, processJackXRun, this);

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

				if (mCPULoadParam == nullptr) {
					auto n = mInfoNode->create_child("cpu_load");
					mCPULoadParam = n->create_parameter(ossia::val_type::FLOAT);
					n->set(ossia::net::description_attribute{}, "The current CPU load percent estimated by JACK");
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				}

				if (mXRunCountParam == nullptr) {
					auto n = mInfoNode->create_child("xrun_count");
					mXRunCountParam = n->create_parameter(ossia::val_type::INT);
					n->set(ossia::net::description_attribute{}, "The count of xruns since the last server start");
					n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				}
				mXRunCount = mXRunCountLast = 0;
				mXRunCountParam->push_value(0);

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
	bool active = mJackClient != nullptr;
	mAudioActiveParam->push_value(active);
	return active;
}

void ProcessAudioJack::jackPropertyChangeCallback(jack_uuid_t subject, const char *key, jack_property_change_t change, void *arg) {
	reinterpret_cast<ProcessAudioJack *>(arg)->jackPropertyChangeCallback(subject, key, change);
}

void ProcessAudioJack::xrun() {
	mXRunCount.fetch_add(1, std::memory_order_relaxed);
}

void ProcessAudioJack::jackPropertyChangeCallback(jack_uuid_t subject, const char *key, jack_property_change_t change) {

	//is it a port?
	{
		std::lock_guard<std::mutex> guard(mPortUUIDToNameMutex);
		auto it = mPortUUIDToName.find(subject);
		if (it != mPortUUIDToName.end()) {
			mPortPropertyUpdates.insert(it->second);
			mPortPropertyPoll = steady_clock::now() + port_poll_timeout;
			return;
		}
	}

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
	auto propchildren = mPortProps->children_names();
	std::set<std::string> removePortProps(propchildren.begin(), propchildren.end());

	{
		std::lock_guard<std::mutex> guard(mPortUUIDToNameMutex);
		mPortUUIDToName.clear();
	}

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
					removePortProps.erase(name);

					auto uuid = jack_port_uuid(port);
					if (!jack_uuid_empty(uuid)) {
						std::lock_guard<std::mutex> guard(mPortUUIDToNameMutex);
						mPortUUIDToName.insert({uuid, name});
					}

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
							auto param = n->create_parameter(ossia::val_type::LIST);

							param->add_callback([this, name, isAudio](const ossia::value& val) {
									if (isAudio) {
										mSourceAudioPortConnectionUpdates.insert(name);
									} else {
										mSourceMIDIPortConnectionUpdates.insert(name);
									}
							});

							//check out the jack connections
							mPortConnectionUpdates.insert(name);
						}
					}

					//update aliases
					auto cnt = jack_port_get_aliases(port, aliases);
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

					updatePortProperties(port);
				}
			}
			jack_free(ports);
		}

		mPortConnectionPoll = steady_clock::now() + port_poll_timeout;

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
	for (auto& n : removePortProps) {
		mPortProps->remove_child(n);
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

	std::vector<char *> args;
	auto cleanup = [&args]() {
		for (char * a: args) {
			free(a);
		}
	};

	auto create = [this, &args]() -> bool {
		auto add_arg = [&args](std::string arg, std::string value = std::string()) {
			args.push_back(strdup(arg.c_str()));
			if (value.size()) {
				args.push_back(strdup(value.c_str()));
			}
		};

		jackctl_driver_t * audioDriver = jackctl_server_get_driver(mJackServer, jack_driver_name);
		if (audioDriver == nullptr) {
			std::cerr << "cannot get driver " << jack_driver_name << " to create jack server" << std::endl;
			return false;
		}


		add_arg("-d", jack_driver_name);

#ifndef __APPLE__
		add_arg("--device", mCardName.empty() ? "hw:0" : mCardName);
		add_arg(std::string("-X") + mMIDISystem);
		add_arg("--nperiods", std::to_string(mNumPeriods));
#endif

		add_arg("--period", std::to_string(mPeriodFrames));
		add_arg("--rate", std::to_string(mSampleRate));

		//add extra args
		if (mExtraArgs.size()) {
			std::vector<std::string> extra;

			//need to separate them out, jack seems to want args and values in the same entry in the args list like -o 2 and -i 0
			boost::algorithm::split_regex(extra, mExtraArgs, boost::regex(" -")); //space and 1 (could be 2) -, adding - back

			for (auto i = 0; i < extra.size(); i++) {
				auto e = extra[i];
				boost::algorithm::trim(e);
				if (e.size() == 0) {
					continue;
				}

				//add the - back
				if (i != 0) {
					e = "-" + e;
				}

				std::vector<std::string> v;
				boost::algorithm::split(v, e, boost::is_any_of(" "), boost::token_compress_on);

				//split arg into: option [value]
				if (v.size() == 1) {
					add_arg(v[0]);
				} else if (v.size() == 2) {
					add_arg(v[0], v[1]);
				} else {
					std::cerr << "don't know how to handle extra arg: " << e;
					return false;
				}
			}
		}

		if (jackctl_driver_params_parse(audioDriver, args.size(), const_cast<char **>(&args.front()))) {
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
		cleanup();
		return false;
	}
	cleanup();
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
		std::unordered_map<uint16_t, std::set<RNBO::ParameterIndex>>& midiMap
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

	RNBO::Json inletsInfo = RNBO::Json::array();
	RNBO::Json outletsInfo = RNBO::Json::array();

	if (conf.contains("inlets") && conf["inlets"].is_array()) {
		inletsInfo = conf["inlets"];
	}

	if (conf.contains("outlets") && conf["outlets"].is_array()) {
		outletsInfo = conf["outlets"];
	}

	//zero out transport position
	std::memset(&mTransportPosLast, 0, sizeof(jack_position_t));

	builder([this, &inletsInfo, &outletsInfo](ossia::net::node_base * root) {
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

				//pretty names from comment
				if (inletsInfo.size() > i) {
					auto info = inletsInfo[i];
					if (info.is_object() && info.contains("comment") && info["comment"].is_string()) {
						auto comment = info["comment"].get<std::string>();
						jack_uuid_t uuid = jack_port_uuid(port);
						if (comment.size() > 0 && !jack_uuid_empty(uuid)) {
							jack_set_property(mJackClient, uuid, JACK_METADATA_PRETTY_NAME, comment.c_str(), "text/plain");
						}
					}
					//TODO meta?
				}
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

				//pretty names from comment
				if (outletsInfo.size() > i) {
					auto info = outletsInfo[i];
					if (info.is_object() && info.contains("comment") && info["comment"].is_string()) {
						auto comment = info["comment"].get<std::string>();
						jack_uuid_t uuid = jack_port_uuid(port);
						if (comment.size() > 0 && !jack_uuid_empty(uuid)) {
							jack_set_property(mJackClient, uuid, JACK_METADATA_PRETTY_NAME, comment.c_str(), "text/plain");
						}
					}
					//TODO meta?
				}
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

 uint16_t InstanceAudioJack::lastMIDIKey() {
	 return mLastMIDIKey.exchange(0);
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
	bool portgroup = config::get<bool>(config::key::InstanceAutoConnectPortGroup).value_or(false);
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


	//if we use port group for identifying connections, we don't care if it is physical
	const unsigned long portflags = portgroup ? 0 : JackPortIsPhysical;

	if (autoConnect || indexed || portgroup) {
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

		//connect hardware/port group audio outputs to our inputs
		{
			auto ports = port_names(jack_get_ports(mJackClient, NULL, JACK_DEFAULT_AUDIO_TYPE, portflags|JackPortIsOutput), portgroup);
			for (size_t i = 0; i < mJackAudioPortIn.size(); i++) {
				int index = remap(inlets, i);
				if (index >= 0 && index < ports.size()) {
					jack_connect(mJackClient, ports[static_cast<size_t>(index)].c_str(), jack_port_name(mJackAudioPortIn.at(i)));
				}
			}
		}

		//connect hardware/port group audio inputs to our outputs
		{
			auto ports = port_names(jack_get_ports(mJackClient, NULL, JACK_DEFAULT_AUDIO_TYPE, portflags|JackPortIsInput), portgroup);
			for (size_t i = 0; i < mJackAudioPortOut.size(); i++) {
				int index = remap(outlets, i);
				if (index >= 0 && index < ports.size()) {
					jack_connect(mJackClient, jack_port_name(mJackAudioPortOut.at(i)), ports[static_cast<size_t>(index)].c_str());
				}
			}
		}
	}

	{
		auto ports = port_names(jack_get_ports(mJackClient, NULL, JACK_DEFAULT_MIDI_TYPE, portflags), portgroup);
		for (auto port: ports) {
			connectToMidiIf(jack_port_by_name(mJackClient, port.c_str()));
		}
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

void InstanceAudioJack::connectToMidiIf(jack_port_t * port) {
	if (!mConnect) {
		return;
	}

	auto name = jack_port_name(port);
	bool all = config::get<bool>(config::key::InstanceAutoConnectMIDI).value_or(false);

	//check if we care about port group, then check port group itself
	bool portgroup = config::get<bool>(config::key::InstanceAutoConnectPortGroup).value_or(false);
	if (portgroup) {
		std::lock_guard<std::mutex> guard(mRNBOGraphPortGroupNamesMutex);
		portgroup = mRNBOGraphPortGroupNames.count(name) != 0;
	}

	bool hardware = config::get<bool>(config::key::InstanceAutoConnectMIDIHardware).value_or(false);
	auto flags = jack_port_flags(port);

	if (all || portgroup || (hardware && JackPortIsPhysical & flags)) {
		std::lock_guard<std::mutex> guard(mPortMutex);
		//if we can get the port, it isn't ours and it is a midi port
		if (port && !jack_port_is_mine(mJackClient, port) && std::string(jack_port_type(port)) == std::string(JACK_DEFAULT_MIDI_TYPE)) {
			//ignore through and virtual
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


			//try lock, might not succeed, won't block
			std::unique_lock<std::mutex> guard(mMIDIMapMutex, std::try_to_lock);

			uint16_t lastKey = 0;
			for (auto i = 0; i < count; i++) {
				jack_midi_event_get(&evt, midi_buf, i);

				//time is in frames since the first frame in this callback
				RNBO::MillisecondTime off = (RNBO::MillisecondTime)evt.time * mFrameMillis;
				auto time = nowms + off;

				std::array<uint8_t, 3> bytes = { 0, 0, 0 };
				std::memcpy(bytes.data(), evt.buffer, std::min(bytes.size(), evt.size));
				auto key = midimap::key(bytes[0], bytes[1]);

				if (key != 0) {
					lastKey = key;

					if (guard.owns_lock()) {
						//eval midi map if we have the lock
						auto it = mMIDIMap.find(key);
						if (it != mMIDIMap.end()) {
							double value = midimap::value(bytes[0], bytes[1], bytes[2]);
							for (auto paramId: it->second) {
								mCore->setParameterValueNormalized(paramId, value, time);
							}
							continue;
						}
					}
				}

				mMIDIInList.addEvent(RNBO::MidiEvent(time, 0, evt.buffer, evt.size));

				//look for program change to change preset
				if (mProgramChangeQueue && evt.size == 2 && (evt.buffer[0] & 0xF0) == 0xC0) {
					mProgramChangeQueue->enqueue(ProgramChange { .chan = static_cast<uint8_t>(evt.buffer[0] & 0x0F), .prog = static_cast<uint8_t>(evt.buffer[1]) });
				}
			}

			//report last key
			if (lastKey) {
				mLastMIDIKey.store(lastKey);
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
