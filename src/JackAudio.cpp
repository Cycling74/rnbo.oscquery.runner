#include "JackAudio.h"
#include "Config.h"
#include "ValueCallbackHelper.h"

#include <jack/midiport.h>
#include <readerwriterqueue/readerwriterqueue.h>

#include <boost/optional.hpp>
#include <boost/filesystem.hpp>

#include <fstream>
#include <iostream>
#include <regex>

namespace fs = boost::filesystem;

//we want the sample value to be the same size
static_assert(sizeof(RNBO::SampleValue) == sizeof(jack_default_audio_sample_t), "RNBO SampleValue must be the same size as jack_default_audio_sample_t");

namespace {

	boost::optional<std::string> ns("jack");

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

	const static std::regex alsa_card_regex(R"X(\s*(\d+)\s*\[([^\[]+?)\s*\]:\s*([^;]+?)\s*;\s*([^;]+?)\s*;)X");

	static int processJack(jack_nframes_t nframes, void *arg) {
		reinterpret_cast<InstanceAudioJack *>(arg)->process(nframes);
		return 0;
	}

	static void jackPortRegistration(jack_port_id_t id, int reg, void *arg) {
		reinterpret_cast<InstanceAudioJack *>(arg)->jackPortRegistration(id, reg);
	}

	std::mutex mJackDRCMutex;
	static const fs::path jackdrc_path = config::make_path("~/.jackdrc");
}

ProcessAudioJack::ProcessAudioJack(NodeBuilder builder) : mBuilder(builder), mJackClient(nullptr) {
	//read in config
	{
		mSampleRate = jconfig_get<double>("sample_rate").get_value_or(48000.);
		mPeriodFrames = jconfig_get<int>("period_frames").get_value_or(256);
#ifndef __APPLE__
		mCardName = jconfig_get<std::string>("card_name").get_value_or("");
		mNumPeriods = jconfig_get<int>("num_periods").get_value_or(2);
#endif
	}

	mBuilder([this](opp::node root) {
			mInfo = root.create_child("info");

			auto conf = root.create_child("config");
			conf.set_description("Jack configuration parameters");

#ifndef __APPLE__
			//read info about alsa cards if it exists
			fs::path alsa_cards("/proc/asound/cards");
			if (fs::exists(alsa_cards)) {
				std::ifstream i(alsa_cards.string());

				//get all the lines, use semi instead of new line because regex doesn't support newline
				std::string value;
				std::string line;
				while (std::getline(i, line)) {
					value += (line + ";");
				}
				value += ";";

				auto cards = mInfo.create_child("alsa_cards");
				std::smatch m;
				while (std::regex_search(value, m, alsa_card_regex)) {

					//description
					auto v = m[3].str() + "\n" + m[4].str();
					auto name = std::string("hw:") + m[2].str();
					auto index = std::string("hw:") + m[1].str();
					value = m.suffix().str();

					//Headphones doesn't work
					if (name == "hw:Headphones") {
						continue;
					}

					//set card name to first found non Headphones, it is our best guess
					if (mCardName.empty()) {
						mCardName = name;
						jconfig_set(mCardName, "card_name");
					}

					//hw:NAME
					{
						mCardNames.push_back(name);
						auto c = cards.create_string(name);
						c.set_value(v);
						c.set_access(opp::access_mode::Get);
					}

					//hw:Index
					{
						mCardNames.push_back(index);
						auto c = cards.create_string(index);
						c.set_value(v);
						c.set_access(opp::access_mode::Get);
					}

				}
			}

			{
				auto card = conf.create_string("card");
				card.set_description("ALSA device name");

				if (mCardNames.size() != 0) {
					std::vector<opp::value> accepted;
					for (auto n: mCardNames) {
						accepted.push_back(n);
					}

					//XXX you have to set min and or max before accepted values takes, will file bug report
					card.set_bounding(opp::bounding_mode::Clip);
					card.set_min(accepted.front());
					card.set_max(accepted.back());
					card.set_accepted_values(accepted);
					if (!mCardName.empty()) {
						card.set_value(mCardName);
					}
				}

				ValueCallbackHelper::setCallback(
						card, mValueCallbackHelpers,
						[this](const opp::value& val) {
							if (val.is_string()) {
								mCardName = val.to_string();
								jconfig_set(mCardName, "card_name");
							}
						});
			}

			{
				mNumPeriodsNode = conf.create_int("num_periods");
				mNumPeriodsNode.set_description("Number of periods of playback latency");
				mNumPeriodsNode.set_value(mNumPeriods);
				std::vector<opp::value> accepted = { 1, 2, 3, 4};
				mNumPeriodsNode.set_min(accepted.front());
				mNumPeriodsNode.set_max(accepted.back());
				mNumPeriodsNode.set_accepted_values(accepted);
				mNumPeriodsNode.set_bounding(opp::bounding_mode::Clip);
				ValueCallbackHelper::setCallback(
						mNumPeriodsNode, mValueCallbackHelpers,
						[this](const opp::value& val) {
							//TODO clamp?
							if (val.is_int()) {
								mNumPeriods = val.to_int();
								jconfig_set(mNumPeriods, "num_periods");
							}
						});
			}
#endif
			{
				//accepted is a list of 2**n (32,... 1024)
				std::vector<opp::value> accepted;
				for (int i = 5; i <= 10; i++) {
					accepted.push_back(1 << i);
				}
				mPeriodFramesNode = conf.create_int("period_frames");
				mPeriodFramesNode.set_description("Frames per period");
				mPeriodFramesNode.set_value(mPeriodFrames);
				mPeriodFramesNode.set_min(accepted.front());
				mPeriodFramesNode.set_max(accepted.back());
				mPeriodFramesNode.set_accepted_values(accepted);
				mPeriodFramesNode.set_bounding(opp::bounding_mode::Clip);
				ValueCallbackHelper::setCallback(
						mPeriodFramesNode, mValueCallbackHelpers,
						[this](const opp::value& val) {
							//TODO clamp?
							if (val.is_int()) {
								mPeriodFrames = val.to_int();
								jconfig_set(mPeriodFrames, "period_frames");
							}
						});
			}

			{
				mSampleRateNode = conf.create_float("sample_rate");
				mSampleRateNode.set_description("Sample rate");
				mSampleRateNode.set_value(mSampleRate);
				mSampleRateNode.set_min(44100.0 / 2);
				mSampleRateNode.set_bounding(opp::bounding_mode::Clip);
				ValueCallbackHelper::setCallback(
						mSampleRateNode, mValueCallbackHelpers,
						[this](const opp::value& val) {
							//TODO clamp?
							if (val.is_float()) {
								mSampleRate = val.to_float();
								jconfig_set(mSampleRate, "sample_rate");
							}
						});
			}
	});
	createClient(false);
}

ProcessAudioJack::~ProcessAudioJack() {
	setActive(false);
}

void ProcessAudioJack::writeJackDRC() {
	std::lock_guard<std::mutex> guard(mJackDRCMutex);
	std::ofstream o(jackdrc_path.string());
	o << mCmdPrefix << " " << mCmdSuffix;
#ifndef __APPLE__
	//default to hw:0 if there is no name set
	auto card = mCardName.empty() ? "hw:0" : mCardName;
	o << " --device \"" << card << "\"";
	o << " --nperiods " << mNumPeriods;
#endif
	o << " --period " << mPeriodFrames;
	o << " --rate " << mSampleRate;
	o.close(); //flush
}

bool ProcessAudioJack::isActive() {
	std::lock_guard<std::mutex> guard(mMutex);
	return mJackClient != nullptr;
}

bool ProcessAudioJack::setActive(bool active) {
	if (active) {
		return createClient(true);
	} else {
		std::lock_guard<std::mutex> guard(mMutex);
		if (mJackClient) {
			jack_client_close(mJackClient);
			mJackClient = nullptr;
		}
		return false;
	}
}

bool ProcessAudioJack::createClient(bool startServer) {
	std::lock_guard<std::mutex> guard(mMutex);
	if (mJackClient == nullptr) {
		jack_options_t options = JackOptions::JackNoStartServer;

		//if we start the server, we want to write the command too
		if (startServer) {
			options = JackOptions::JackNullOption;
			writeJackDRC();
		}

		jack_status_t status;
		mJackClient = jack_client_open("rnbo-info", options, &status);
		if (status == 0 && mJackClient) {
			mBuilder([this](opp::node root) {
					{
						auto p = mInfo.create_bool("is_realtime");
						p.set_description("indicates if jack is running in realtime mode or not");
						p.set_access(opp::access_mode::Get);
						p.set_value(jack_is_realtime(mJackClient) != 0);
						mNodes.push_back(p);
					}

					double sr = jack_get_sample_rate(mJackClient);
					jack_nframes_t bs = jack_get_buffer_size(mJackClient);
					if (sr != mSampleRate) {
						mSampleRateNode.set_value(static_cast<float>(sr));
					}
					if (bs != mPeriodFrames) {
						mPeriodFramesNode.set_value(static_cast<int>(bs));
					}
			});
			//TODO build up i/o dynamically
		}
	}
	return mJackClient != nullptr;
}

InstanceAudioJack::InstanceAudioJack(std::shared_ptr<RNBO::CoreObject> core, std::string name, NodeBuilder builder) : mCore(core), mRunning(false) {
	//get jack client, fail early if we can't
	mJackClient = jack_client_open(name.c_str(), JackOptions::JackNoStartServer, nullptr);
	if (!mJackClient)
		throw new std::runtime_error("couldn't create jack client");
	jack_set_process_callback(mJackClient, processJack, this);

	//setup command queue
	mPortQueue = RNBO::make_unique<moodycamel::ReaderWriterQueue<jack_port_id_t, 32>>(32);

	//init aliases
	mJackPortAliases[0] = new char[jack_port_name_size()];
	mJackPortAliases[1] = new char[jack_port_name_size()];

	//zero out transport position
	std::memset(&mTransportPosLast, 0, sizeof(jack_position_t));

	builder([this](opp::node root) {
		//setup jack
		auto jack = root.create_child("jack");
		mNodes.push_back(jack);

		//create i/o
		{
			std::vector<opp::value> names;
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

			auto audio_ins = jack.create_list("audio_ins");
			audio_ins.set_access(opp::access_mode::Get);
			audio_ins.set_value(names);
			mNodes.push_back(audio_ins);
		}
		{
			std::vector<opp::value> names;
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
			auto audio_outs = jack.create_list("audio_outs");
			audio_outs.set_access(opp::access_mode::Get);
			audio_outs.set_value(names);
			mNodes.push_back(audio_outs);
		}

		{
			mJackMidiIn = jack_port_register(mJackClient,
					"midiin1",
					JACK_DEFAULT_MIDI_TYPE,
					JackPortFlags::JackPortIsInput,
					0
			);
			auto midi_ins = jack.create_list("midi_ins");
			midi_ins.set_access(opp::access_mode::Get);
			midi_ins.set_value(opp::value({opp::value(std::string(jack_port_name(mJackMidiIn)))}));
			mNodes.push_back(midi_ins);
		}
		{
			mJackMidiOut = jack_port_register(mJackClient,
					"midiout1",
					JACK_DEFAULT_MIDI_TYPE,
					JackPortFlags::JackPortIsOutput,
					0
			);
			auto midi_outs = jack.create_list("midi_outs");
			midi_outs.set_access(opp::access_mode::Get);
			midi_outs.set_value(opp::value({opp::value(std::string(jack_port_name(mJackMidiOut)))}));
			mNodes.push_back(midi_outs);
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

void InstanceAudioJack::poll() {
	jack_port_id_t id;
	//process port registrations
	while (mPortQueue->try_dequeue(id)) {
		connectToMidiIf(jack_port_by_id(mJackClient, id));
	}
}

void InstanceAudioJack::connectToHardware() {
	const char ** ports;

	if (config::get<bool>(config::key::InstanceAutoConnectAudio)) {
		//connect hardware audio outputs to our inputs
		if ((ports = jack_get_ports(mJackClient, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortIsPhysical|JackPortIsOutput)) != NULL) {
			auto ptr = ports;
			for (auto it = mJackAudioPortIn.begin(); it != mJackAudioPortIn.end() && *ptr != nullptr; it++, ptr++) {
				jack_connect(mJackClient, *ptr, jack_port_name(*it));
			}
			jack_free(ports);
		}

		//connect hardware audio inputs to our outputs
		if ((ports = jack_get_ports(mJackClient, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortIsPhysical|JackPortIsInput)) != NULL) {
			auto ptr = ports;
			for (auto it = mJackAudioPortOut.begin(); it != mJackAudioPortOut.end() && *ptr != nullptr; it++, ptr++) {
				jack_connect(mJackClient, jack_port_name(*it), *ptr);
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
	{
		jack_position_t jackPos;
		auto state = jack_transport_query(mJackClient, &jackPos);

		if (state != mTransportStateLast) {
			RNBO::TransportEvent event(nowms, state == jack_transport_state_t::JackTransportRolling ? RNBO::TransportState::RUNNING : RNBO::TransportState::STOPPED);
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
		}
	}

	//RNBO process
	mCore->process(
			static_cast<RNBO::SampleValue **>(mSampleBufferPtrIn.size() == 0 ? nullptr : &mSampleBufferPtrIn.front()), mSampleBufferPtrIn.size(),
			static_cast<RNBO::SampleValue **>(mSampleBufferPtrOut.size() == 0 ? nullptr : &mSampleBufferPtrOut.front()), mSampleBufferPtrOut.size(),
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
