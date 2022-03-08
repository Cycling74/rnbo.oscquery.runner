#pragma once

#include <mutex>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>

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
		void process(jack_nframes_t frames);

		virtual void handleTransportState(bool running) override;
		virtual void handleTransportTempo(double bpm) override;
		virtual void handleTransportBeatTime(double btime) override;
		virtual void handleTransportTimeSig(double numerator, double denominator) override;

		static void jackPropertyChangeCallback(jack_uuid_t subject, const char *key, jack_property_change_t change, void *arg);
	protected:
		void jackPropertyChangeCallback(jack_uuid_t subject, const char *key, jack_property_change_t change);
	private:
		void updateCards();
		void updateCardNodes();

		bool createClient(bool startServer);
		bool createServer();
		jack_client_t * mJackClient = nullptr;
		jackctl_server_t * mJackServer = nullptr;
		jack_uuid_t mJackClientUUID = 0;

		std::atomic<jack_uuid_t> mBPMClientUUID;

		ossia::net::node_base * mInfoNode = nullptr;
		ossia::net::node_base * mTransportNode = nullptr;
		ossia::net::parameter_base * mTransportBPMParam = nullptr;
		float mTransportBPMLast = 0.0;
		std::atomic<float> mTransportBPMPropLast;

		ossia::net::parameter_base * mTransportRollingParam = nullptr;
		std::atomic<bool> mTransportRollingLast = false;
		std::atomic<bool> mTransportRollingUpdate = false; //from the process callback

		NodeBuilder mBuilder;
		std::mutex mMutex;

		double mSampleRate = 44100;
		ossia::net::parameter_base * mSampleRateParam = nullptr;
		int mPeriodFrames = 256;
		ossia::net::parameter_base * mPeriodFramesParam = nullptr;

		//only used on systems with alsa
		int mNumPeriods = 2;
		ossia::net::parameter_base * mNumPeriodsParam;
		std::string mCardName;

		std::thread mCardThread;
		std::mutex mCardMutex;
		std::atomic_bool mPollCards = true;
		std::atomic_bool mCardsUpdated = false;

		ossia::net::node_base * mCardNode = nullptr;
		ossia::net::node_base * mCardListNode = nullptr;
		//name -> Description
		std::map<std::string, std::string> mCardNamesAndDescriptions;
};

//Processing and handling for a specific rnbo instance.
class InstanceAudioJack : public InstanceAudio {
	public:
		InstanceAudioJack(
				std::shared_ptr<RNBO::CoreObject> core,
				std::string name,
				NodeBuilder builder,
				std::function<void(ProgramChange)> progChangeCallback
				);
		virtual ~InstanceAudioJack();
		virtual void start() override;
		virtual void stop() override;
		virtual bool isActive() override;
		virtual void processEvents() override;
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

		std::unique_ptr<moodycamel::ReaderWriterQueue<jack_port_id_t, 32>> mPortQueue;
		std::unique_ptr<moodycamel::ReaderWriterQueue<ProgramChange, 32>> mProgramChangeQueue;

		//working buffer for port getting port aliases
		char * mJackPortAliases[2];
		std::mutex mPortMutex;

		//transport sync info
		jack_position_t mTransportPosLast;
		jack_transport_state_t mTransportStateLast = jack_transport_state_t::JackTransportStopped;

		std::function<void(ProgramChange)> mProgramChangeCallback;
};
