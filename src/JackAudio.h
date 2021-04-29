#pragma once

#include <mutex>
#include <memory>
#include <vector>
#include <atomic>

#include <ossia-cpp/ossia-cpp98.hpp>

#include <jack/types.h>
#include <jack/jack.h>
#include <jack/metadata.h>
#include <jack/control.h>

#include "RNBO.h"
#include "InstanceAudio.h"
#include "ProcessAudio.h"
#include "Defines.h"

namespace moodycamel {
template<typename T, size_t MAX_BLOCK_SIZE>
class ReaderWriterQueue;
}

//Global jack settings.
class ProcessAudioJack : public ProcessAudio {
	public:
		ProcessAudioJack(NodeBuilder builder);
		virtual ~ProcessAudioJack();
		virtual bool isActive() override;
		virtual bool setActive(bool active) override;
		virtual void processEvents() override;

		static void jackPropertyChangeCallback(jack_uuid_t subject, const char *key, jack_property_change_t change, void *arg);
	protected:
		void jackPropertyChangeCallback(jack_uuid_t subject, const char *key, jack_property_change_t change);
	private:
		bool createClient(bool startServer);
		bool createServer();
		jack_client_t * mJackClient;
		jackctl_server_t * mJackServer = nullptr;
		jack_uuid_t mJackClientUUID = 0;

		std::atomic<jack_uuid_t> mBPMClientUUID;

		ossia::net::node_base * mInfoNode;
		ossia::net::parameter_base * mTransportBPMParam;
		float mTransportBPMLast = 0.0;
		std::atomic<float> mTransportBPMPropLast;

		ossia::net::parameter_base * mTransportRollingParam;
		bool mTransportRollingLast = false;


		NodeBuilder mBuilder;
		std::mutex mMutex;
		std::vector<std::string> mCardNames;

		double mSampleRate = 44100;
		ossia::net::parameter_base * mSampleRateParam;
		int mPeriodFrames = 256;
		ossia::net::parameter_base * mPeriodFramesParam;

#ifndef __APPLE__
		int mNumPeriods = 2;
		ossia::net::parameter_base * mNumPeriodsParam;
		std::string mCardName;
#endif
};

//Processing and handling for a specific rnbo instance.
class InstanceAudioJack : public InstanceAudio {
	public:
		InstanceAudioJack(std::shared_ptr<RNBO::CoreObject> core, std::string name, NodeBuilder builder);
		virtual ~InstanceAudioJack();
		virtual void start() override;
		virtual void stop() override;
		virtual bool isActive() override;
		virtual void poll() override;
		void process(jack_nframes_t frames);
		//callback that gets called with jack adds or removes client ports
		void jackPortRegistration(jack_port_id_t id, int reg);
	private:
		void connectToHardware();
		void connectToMidiIf(jack_port_t * port);
		std::shared_ptr<RNBO::CoreObject> mCore;

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

		//command queue eventually if we have more than just ports
		std::unique_ptr<moodycamel::ReaderWriterQueue<jack_port_id_t, 32>> mPortQueue;

		//working buffer for port getting port aliases
		char * mJackPortAliases[2];
		std::mutex mPortMutex;

		//transport sync info
		jack_position_t mTransportPosLast;
		jack_transport_state_t mTransportStateLast = jack_transport_state_t::JackTransportStopped;
};
