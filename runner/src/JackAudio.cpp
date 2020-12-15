#include "JackAudio.h"
#include "Config.h"
#include <jack/midiport.h>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

//we want the sample value to be the same size
static_assert(sizeof(RNBO::SampleValue) == sizeof(jack_default_audio_sample_t), "RNBO SampleValue must be the same size as jack_default_audio_sample_t");

namespace {
	static int process_jack(jack_nframes_t nframes, void *arg) {
		reinterpret_cast<InstanceAudioJack *>(arg)->process(nframes);
		return 0;
	}

	std::mutex mJackDRCMutex;
	static const fs::path jackdrc_path = config::make_path("~/.jackdrc");

	std::string getJackDRC() {
		std::lock_guard<std::mutex> guard(mJackDRCMutex);
		if (fs::exists(jackdrc_path)) {
			std::ifstream i(jackdrc_path.u8string());
			std::string v;
			if (std::getline(i, v)) {
				return v;
			}
		}
#ifdef __APPLE__
		return "/usr/local/bin/jackd -Xcoremidi -dcoreaudio -r44100 -p64";
#else
		return "/usr/bin/jackd -dalsa -dhw:1 -r44100 -p64 -n2";
#endif
	}

	void writeJackDRC(std::string value) {
		if (value.size()) {
			std::lock_guard<std::mutex> guard(mJackDRCMutex);
			std::ofstream o(jackdrc_path.u8string());
			o << value;
			o.close(); //flush
		}
	}
}


ProcessAudioJack::ProcessAudioJack(NodeBuilder builder) : mBuilder(builder), mJackClient(nullptr) {
	mBuilder([this](opp::node root) {
			mInfo = root.create_child("info");

			mJackDCommand = mInfo.create_string("server_command");
			mJackDCommand.set_description("the command used to start jackd");
			mJackDCommand.set_value(getJackDRC());

			//read info about alsa cards if it exists
			fs::path alsa_cards("/proc/asound/cards");
			if (fs::exists(alsa_cards)) {
				std::ifstream i(alsa_cards.u8string());
				std::string value;
				std::string line;
				while (std::getline(i, line)) {
					value += (line + "\n");
				}
				auto cards = mInfo.create_string("alsa_cards");
				cards.set_value(value);
				cards.set_access(opp::access_mode::Get);
				mNodes.push_back(cards);
			}
	});
	createClient(false);
}

ProcessAudioJack::~ProcessAudioJack() {
	setActive(false);
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
			writeJackDRC(mJackDCommand.get_value().to_string());
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

					{
						double sr = jack_get_sample_rate(mJackClient);
						auto p = mInfo.create_float("sample_rate");
						p.set_description("The sample rate that jack is using");
						p.set_access(opp::access_mode::Get);
						p.set_value(sr);
						mNodes.push_back(p);
					}
					{
						jack_nframes_t bs = jack_get_buffer_size(mJackClient);
						auto p = mInfo.create_int("buffer_size");
						p.set_description("The buffer size that jack is using");
						p.set_access(opp::access_mode::Get);
						p.set_value(static_cast<int>(bs));
						mNodes.push_back(p);
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
	jack_set_process_callback(mJackClient, process_jack, this);

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
	close();
}

void InstanceAudioJack::close() {
	stop(); //stop locks
	{
		std::lock_guard<std::mutex> guard(mMutex);
		if (mJackClient) {
			jack_client_close(mJackClient);
			mJackClient = nullptr;
		}
		//TODO unregister ports?
	}
}

void InstanceAudioJack::start() {
	std::lock_guard<std::mutex> guard(mMutex);
	//protect against double activate or deactivate
	if (!mRunning) {
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
		//MIDI, usually the last port is the one we want
		if ((ports = jack_get_ports(mJackClient, NULL, JACK_DEFAULT_MIDI_TYPE, JackPortIsPhysical|JackPortIsOutput)) != NULL) {
			auto ptr = ports;
			//get the last port
			while (*ptr != nullptr && *(ptr + 1) != nullptr)
				ptr++;
			if (*ptr)
				jack_connect(mJackClient, *ptr, jack_port_name(mJackMidiIn));
			jack_free(ports);
		}

		if ((ports = jack_get_ports(mJackClient, NULL, JACK_DEFAULT_MIDI_TYPE, JackPortIsPhysical|JackPortIsInput)) != NULL) {
			auto ptr = ports;
			//get the last port
			while (*ptr != nullptr && *(ptr + 1) != nullptr)
				ptr++;
			if (*ptr)
				jack_connect(mJackClient, jack_port_name(mJackMidiOut), *ptr);
			jack_free(ports);
		}
	}
}

void InstanceAudioJack::process(jack_nframes_t nframes) {
	//get the current time
	auto nowms = mCore->getCurrentTime();
	//TODO sync to jack's time?

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
		auto midi_buf = jack_port_get_buffer(mJackMidiOut, nframes);
		for (const auto& e : mMIDIOutList) {
			jack_nframes_t frame = std::max(0.0, e.getTime() - nowms) * mMilliFrame;
			jack_midi_event_write(midi_buf, frame, e.getData(), e.getLength());
		}
		mMIDIOutList.clear();
	}
}
