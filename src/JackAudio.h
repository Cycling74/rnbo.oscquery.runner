#pragma once

#include <mutex>
#include <memory>
#include <vector>
#include <atomic>
#include <thread>
#include <set>
#include <ossia-cpp/ossia-cpp98.hpp>
#include <unordered_map>

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

enum class JackPortChange {
	Register,
	Unregister,
	Rename,
	Connection
};

class JackAudioRecord;

//Global jack settings.
class ProcessAudioJack : public ProcessAudio {
	public:
		ProcessAudioJack(NodeBuilder builder, std::function<void(ProgramChange)> progChangeCallback = nullptr);
		virtual ~ProcessAudioJack();

		virtual bool isActive() override;
		virtual bool setActive(bool active, bool withServer = true) override;
		virtual void processEvents(std::function<void(ConnectionChange)> connectionChangeCallback = nullptr) override;
		void process(jack_nframes_t frames);

		virtual bool connect(const std::vector<SetConnectionInfo>& connections, bool withControlConnections) override;
		virtual std::vector<SetConnectionInfo> connections() override;

		// disconnect non rnbo
		virtual void disconnect(const std::vector<SetConnectionInfo>& connections) override;

		virtual void handleTransportState(bool running) override;
		virtual void handleTransportTempo(double bpm) override;
		virtual void handleTransportBeatTime(double btime) override;
		virtual void handleTransportTimeSig(double numerator, double denominator) override;

		virtual void updatePorts() override;
		virtual void sendReset() override;

		void portRenamed(jack_port_id_t port, const char *old_name, const char *new_name);
		void jackPortRegistration(jack_port_id_t id, int reg);
		void portConnected(jack_port_id_t a, jack_port_id_t b, bool connected);
		void xrun();

		static void jackPropertyChangeCallback(jack_uuid_t subject, const char *key, jack_property_change_t change, void *arg);
	protected:
		void jackPropertyChangeCallback(jack_uuid_t subject, const char *key, jack_property_change_t change);
	private:
		bool updateCards();
		void updateCardNodes();

		void updatePortProperties(jack_port_t* port);

		bool createClient(bool startServer);
		bool createServer();

		void connectToMidiIf(jack_port_t * port);

		jack_client_t * mJackClient = nullptr;
		jackctl_server_t * mJackServer = nullptr;
		jack_uuid_t mJackClientUUID = 0;

		std::atomic<jack_uuid_t> mBPMClientUUID;

		ossia::net::node_base * mInfoNode = nullptr;
		ossia::net::node_base * mPortInfoNode = nullptr;
		ossia::net::parameter_base * mAudioActiveParam = nullptr;

		ossia::net::node_base * mPortAudioSourceConnectionsNode = nullptr;
		ossia::net::node_base * mPortMIDISourceConnectionsNode = nullptr;

		ossia::net::node_base * mPortAliases = nullptr;
		ossia::net::node_base * mPortProps = nullptr;
		ossia::net::parameter_base * mPortAudioSinksParam = nullptr;
		ossia::net::parameter_base * mPortAudioSourcesParam = nullptr;
		ossia::net::parameter_base * mPortMidiSinksParam = nullptr;
		ossia::net::parameter_base * mPortMidiSourcesParam = nullptr;

		ossia::net::parameter_base * mIsRealTimeParam = nullptr;
		ossia::net::parameter_base * mIsOwnedParam = nullptr;
		std::atomic<int> mXRunCount = 0;
		int mXRunCountLast = 0;

		ossia::net::parameter_base * mCPULoadParam = nullptr;
		ossia::net::parameter_base * mXRunCountParam = nullptr;
		std::chrono::time_point<std::chrono::steady_clock> mStatsPollNext;

		bool mHasCreatedClient = false;
		bool mHasCreatedServer = false;

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

		std::string mExtraArgs = "";
		ossia::net::parameter_base * mExtraArgsParam = nullptr;

		//only used on systems with alsa
		int mNumPeriods = 2;
		ossia::net::parameter_base * mNumPeriodsParam;
		std::string mCardName;
		std::string mMIDISystem = "seq";

		std::chrono::time_point<std::chrono::steady_clock> mCardsPollNext;

		ossia::net::node_base * mCardNode = nullptr;
		ossia::net::node_base * mCardListNode = nullptr;
		//name -> Description
		std::map<std::string, std::string> mCardNamesAndDescriptions;

		std::function<void(ProgramChange)> mProgramChangeCallback;
		std::unique_ptr<moodycamel::ReaderWriterQueue<std::pair<jack_port_id_t, JackPortChange>, 32>> mPortQueue;
		std::unique_ptr<moodycamel::ReaderWriterQueue<ProgramChange, 32>> mProgramChangeQueue;

		ossia::net::parameter_base * mMidiInParam = nullptr;
		jack_port_t * mJackMidiIn;

		jack_port_t * mResetMidiOut;
		std::atomic<bool> mSendReset = false;
		ossia::net::parameter_base * mSendResetParam = nullptr;

		//working buffer for port getting port aliases
		char * mJackPortAliases[2];

		//from libossia
		bool mMidiPortConnectionsChanged = false;
		std::set<std::string> mSourceAudioPortConnectionUpdates;
		std::set<std::string> mSourceMIDIPortConnectionUpdates;

		//should we poll ports, connections?
		boost::optional<std::chrono::time_point<std::chrono::steady_clock>> mPortPoll;
		boost::optional<std::chrono::time_point<std::chrono::steady_clock>> mPortConnectionPoll;
		boost::optional<std::chrono::time_point<std::chrono::steady_clock>> mPortPropertyPoll;

		//which ports got updates (names)?
		std::set<std::string> mPortConnectionUpdates;


		std::unordered_map<jack_uuid_t, std::string> mPortUUIDToName;
		std::mutex mPortUUIDToNameMutex;
		std::set<std::string> mPortPropertyUpdates;

		std::unique_ptr<JackAudioRecord> mRecordNode;
};

//Processing and handling for a specific rnbo instance.
class InstanceAudioJack : public InstanceAudio {
	public:
		InstanceAudioJack(
				std::shared_ptr<RNBO::CoreObject> core,
				RNBO::Json conf,
				unsigned int index,
				std::string name,
				NodeBuilder builder,
				std::function<void(ProgramChange)> progChangeCallback,
				std::mutex& midiMapMutex,
				std::unordered_map<uint16_t, std::set<RNBO::ParameterIndex>>& paramMidiMap,
				std::unordered_map<uint16_t, std::set<RNBO::MessageTag>>& inportMidiMap
				);
		virtual ~InstanceAudioJack();

		virtual void addConfig(RNBO::Json& conf) override;

		virtual void activate() override;
		virtual void connect() override;
		virtual void start(float fadems=0.0f) override;
		virtual void stop(float fadems=0.0f) override;

		virtual size_t bufferSize() override { return mBufferSize; }

		virtual uint16_t lastMIDIKey() override;

		virtual void processEvents() override;

		void process(jack_nframes_t frames);
		//callback that gets called with jack adds or removes client ports
		void jackPortRegistration(jack_port_id_t id, int reg);

		void portConnected(jack_port_id_t a, jack_port_id_t b, bool connected);

		virtual void registerConfigChangeCallback(std::function<void()> cb) override { mConfigChangeCallback = cb; }
	private:
		size_t mBufferSize = 0;
		bool mConnect = false; // should we do any automatic connections?
		std::atomic<float> mFade = 1.0;
		std::atomic<float> mFadeIncr = 0.1;
		std::atomic<uint16_t> mLastMIDIKey = 0;

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
		std::unique_ptr<moodycamel::ReaderWriterQueue<jack_port_id_t, 32>> mPortConnectedQueue;
		std::unique_ptr<moodycamel::ReaderWriterQueue<ProgramChange, 32>> mProgramChangeQueue;

		//working buffer for port getting port aliases
		char * mJackPortAliases[2];
		std::mutex mPortMutex;

		//transport sync info
		jack_position_t mTransportPosLast;
		jack_transport_state_t mTransportStateLast = jack_transport_state_t::JackTransportStopped;

		std::function<void(ProgramChange)> mProgramChangeCallback;

		std::mutex& mMIDIMapMutex;
		std::unordered_map<uint16_t, std::set<RNBO::ParameterIndex>>& mParamMIDIMap;
		std::unordered_map<uint16_t, std::set<RNBO::MessageTag>>& mInportMIDIMap;

		std::unordered_map<jack_port_t *, ossia::net::parameter_base *> mPortParamMap;
		std::function<void()> mConfigChangeCallback = nullptr;
};
