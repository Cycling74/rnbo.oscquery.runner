#pragma once

#include <mutex>
#include <ossia-cpp/ossia-cpp98.hpp>
#include <jack/types.h>
#include <jack/jack.h>

#include "RNBO.h"
#include "InstanceAudio.h"
#include "ProcessAudio.h"
#include "Defines.h"

//Global jack settings.
class ProcessAudioJack : public ProcessAudio {
	public:
		ProcessAudioJack(NodeBuilder builder);
		virtual ~ProcessAudioJack();
		virtual bool isActive() override;
		virtual bool setActive(bool active) override;
	private:
		bool createClient(bool startServer);
		jack_client_t * mJackClient;
		std::vector<opp::node> mNodes;
		opp::node mInfo;
		opp::node mJackDCommand;
		NodeBuilder mBuilder;
		std::mutex mMutex;
};

//Processing and handling for a specific rnbo instance.
class InstanceAudioJack : public InstanceAudio {
	public:
		InstanceAudioJack(std::shared_ptr<RNBO::CoreObject> core, std::string name, NodeBuilder builder);
		virtual ~InstanceAudioJack();
		virtual void start() override;
		virtual void stop() override;
		virtual bool isActive() override;
		void process(jack_nframes_t frames);
	private:
		void connectToHardware();
		std::shared_ptr<RNBO::CoreObject> mCore;
		std::vector<opp::node> mNodes;

		jack_client_t * mJackClient;
		std::vector<jack_port_t *> mJackAudioPortOut;
		std::vector<jack_port_t *> mJackAudioPortIn;

		jack_port_t * mJackMidiIn;
		jack_port_t * mJackMidiOut;

		//number of milliseconds per frame
		RNBO::MillisecondTime mFrameMillis;
		//number of frames per millisecond
		RNBO::MillisecondTime mMilliFrame;

		std::vector<jack_default_audio_sample_t *> mSampleBufferPtrIn;
		std::vector<jack_default_audio_sample_t *> mSampleBufferPtrOut;

		RNBO::MidiEventList mMIDIOutList;
		RNBO::MidiEventList mMIDIInList;
		std::mutex mMutex;
		bool mRunning = false;

		//transport sync info
		jack_position_t mTransportPosLast;
		jack_transport_state_t mTransportStateLast = jack_transport_state_t::JackTransportStopped;
};
