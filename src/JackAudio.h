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
#include <boost/optional.hpp>

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
		ProcessAudioJack(NodeBuilder builder, std::function<void(ProgramChange)> progChangeCallback = nullptr);
		virtual ~ProcessAudioJack();

		virtual bool isActive() override;
		virtual bool setActive(bool active) override;
		virtual void processEvents() override;
		void process(jack_nframes_t frames);

		virtual bool connect(const RNBO::Json& config) override;
		virtual RNBO::Json connections() override;

		virtual void handleTransportState(bool running) override;
		virtual void handleTransportTempo(double bpm) override;
		virtual void handleTransportBeatTime(double btime) override;
		virtual void handleTransportTimeSig(double numerator, double denominator) override;

		virtual void updatePorts() override;
		void portRenamed(jack_port_id_t port, const char *old_name, const char *new_name);
		void jackPortRegistration(jack_port_id_t id, int reg);
		void portConnected(jack_port_id_t a, jack_port_id_t b, bool connected);

		static void jackPropertyChangeCallback(jack_uuid_t subject, const char *key, jack_property_change_t change, void *arg);
	protected:
		void jackPropertyChangeCallback(jack_uuid_t subject, const char *key, jack_property_change_t change);
	private:
		void updateCards();
		void updateCardNodes();

		bool createClient(bool startServer);
		bool createServer();

		void connectToMidiIf(jack_port_t * port);

		jack_client_t * mJackClient = nullptr;
		jackctl_server_t * mJackServer = nullptr;
		jack_uuid_t mJackClientUUID = 0;

		std::atomic<jack_uuid_t> mBPMClientUUID;

		ossia::net::node_base * mInfoNode = nullptr;
		ossia::net::node_base * mPortInfoNode = nullptr;

		ossia::net::node_base * mPortAliases = nullptr;
		ossia::net::parameter_base * mPortAudioSinksParam = nullptr;
		ossia::net::parameter_base * mPortAudioSourcesParam = nullptr;
		ossia::net::parameter_base * mPortMidiSinksParam = nullptr;
		ossia::net::parameter_base * mPortMidiSourcesParam = nullptr;

		ossia::net::parameter_base * mIsRealTimeParam = nullptr;
		ossia::net::parameter_base * mIsOwnedParam = nullptr;
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

		std::function<void(ProgramChange)> mProgramChangeCallback;
		std::unique_ptr<moodycamel::ReaderWriterQueue<jack_port_id_t, 32>> mPortQueue;
		std::unique_ptr<moodycamel::ReaderWriterQueue<ProgramChange, 32>> mProgramChangeQueue;

		ossia::net::parameter_base * mMidiInParam = nullptr;
		jack_port_t * mJackMidiIn;

		//working buffer for port getting port aliases
		char * mJackPortAliases[2];

		std::mutex mMidiInNamesMutex;
		std::vector<std::string> mMidiInPortNames;
		bool mMidiPortNamesUpdated = false;

		//should we poll midi input connections?
		boost::optional<std::chrono::time_point<std::chrono::steady_clock>> mMidiInPoll;
		std::mutex mMidiInPollMutex;
};

//Processing and handling for a specific rnbo instance.
class InstanceAudioJack : public InstanceAudio {
	public:
		InstanceAudioJack(
				std::shared_ptr<RNBO::CoreObject> core,
				RNBO::Json conf,
				std::string name,
				NodeBuilder builder,
				std::function<void(ProgramChange)> progChangeCallback
				);
		virtual ~InstanceAudioJack();

		virtual void addConfig(RNBO::Json& conf) override;

		virtual void activate() override;
		virtual void connect() override;
		virtual void start(float fadems=0.0f) override;
		virtual void stop(float fadems=0.0f) override;

		virtual void processEvents() override;

		void process(jack_nframes_t frames);
		//callback that gets called with jack adds or removes client ports
		void jackPortRegistration(jack_port_id_t id, int reg);

		void portConnected(jack_port_id_t a, jack_port_id_t b, bool connected);

		virtual void registerConfigChangeCallback(std::function<void()> cb) override { mConfigChangeCallback = cb; }
	private:
		std::atomic<float> mFade = 1.0;
		std::atomic<float> mFadeIncr = 0.1;

		void connectToMidiIf(jack_port_t * port);
		std::shared_ptr<RNBO::CoreObject> mCore;
		RNBO::Json mInstanceConf;

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
		bool mActivated = false;
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

		std::unordered_map<jack_port_t *, ossia::net::parameter_base *> mPortParamMap;
		std::function<void()> mConfigChangeCallback = nullptr;
};
