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
//UDP OSC sender aimed at jack_transport_link's receive port. Pimpl'd so oscpack's socket headers
//don't leak in here.
class JTLCommandSender;

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

		virtual void handleLinkTransportOSC(const std::string& addr, const ossia::value& val) override;

		virtual SetLinkAudioInfo linkAudioSetup() override;
		virtual void setLinkAudioSetup(const SetLinkAudioInfo& setup) override;
		virtual bool takeLinkAudioSetupChanged() override;

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

		//Link Audio (jack_transport_link) bridge
		void buildLinkAudioNodes(ossia::net::node_base * root);
		void syncLinkAudioFromState();
		//per-slot children are named by the slot key jack_transport_link publishes; the runner
		//never computes a hash, it only echoes the keys it read
		void reconcileLinkAudioSourceSlots(const std::vector<std::string>& keys);
		void reconcileLinkAudioSinkSlots(const std::vector<std::string>& keys);
		//track jack_transport_link's OSC endpoint (from its osc-port metadata key) and keep our
		//listener registration alive; main thread only
		void updateJTLEndpoint(std::chrono::time_point<std::chrono::steady_clock> now);
		//queue one encoded OSC command for jack_transport_link, from any thread
		void queueJTLCommand(std::string packet);
		//send the queued commands; returns true if anything went out. main thread only
		bool flushJTLCommands();
		//tell jack_transport_link to stop pushing state at us, and forget the endpoint
		void unregisterJTLListener();
		bool readTransportProperty(jack_uuid_t subject, const std::string& key, std::string& out);

		bool createClient(bool startServer);
		bool createServer();

		void connectToMidiIf(jack_port_t * port);

		jack_client_t * mJackClient = nullptr;
		jackctl_server_t * mJackServer = nullptr;
		jack_uuid_t mJackClientUUID = 0;

		std::atomic<jack_uuid_t> mTransportClientUUID;
		bool mLinkSyncNeedsUpdate = false;
		//master Link on/off, requested from an ossia callback and written to metadata in
		//processEvents -- same deferral as mLinkSyncNeedsUpdate right above
		std::atomic<bool> mLinkEnabledWrite = true;
		std::atomic<bool> mLinkEnabledNeedsWrite = false;

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
		ossia::net::parameter_base * mTransportLinkSyncParam = nullptr;
		ossia::net::parameter_base * mTransportLinkPeersParam = nullptr;

		ossia::net::parameter_base * mTransportRollingParam = nullptr;
		std::atomic<bool> mTransportRollingLast = false;
		std::atomic<bool> mTransportRollingUpdate = false; //from the process callback

		//Link Audio bridge (proxies jack_transport_link's linkaudio/* metadata)
		struct LinkAudioSourceSlot {
			std::string key;
			//the configured identity, from linkaudio/sources
			std::string peer;
			std::string channel;
			ossia::net::parameter_base * peerParam = nullptr;
			ossia::net::parameter_base * channelParam = nullptr;
			//read-only receive telemetry, proxied from linkaudio/source-status
			ossia::net::parameter_base * buffered_ms = nullptr;
			ossia::net::parameter_base * dropouts = nullptr;
			ossia::net::parameter_base * arrival_offset_ms = nullptr;
			ossia::net::parameter_base * jitter_ms = nullptr;
			ossia::net::parameter_base * connected = nullptr;
			ossia::net::parameter_base * receiving = nullptr;
		};
		struct LinkAudioSinkSlot {
			std::string key;
			std::string nameValue;
			ossia::net::parameter_base * name = nullptr;
		};
		ossia::net::node_base * mLinkNode = nullptr;
		ossia::net::parameter_base * mLinkEnabledParam = nullptr;
		ossia::net::node_base * mLinkAudioNode = nullptr;
		//the `list` containers under sources/sinks that hold the per-slot nodes. Slots live in
		//their own container so a slot key can never collide with a sibling command name --
		//without it, a client reading the tree would have to tell "add" apart from a hex key.
		ossia::net::node_base * mLinkAudioSourceListNode = nullptr;
		ossia::net::node_base * mLinkAudioSinkListNode = nullptr;
		ossia::net::parameter_base * mLinkAudioAvailableParam = nullptr;
		ossia::net::parameter_base * mLinkAudioChannelsParam = nullptr;
		ossia::net::parameter_base * mLinkAudioPeerNameParam = nullptr;
		ossia::net::parameter_base * mLinkAudioLatencyMsParam = nullptr;
		ossia::net::parameter_base * mLinkAudioSyncToIncomingParam = nullptr;
		ossia::net::parameter_base * mLinkAudioSourcesOrderParam = nullptr;
		ossia::net::parameter_base * mLinkAudioSinksOrderParam = nullptr;
		//cached slot lists, in display order. A strict mirror of jack_transport_link's state: the
		//commands are identity-based, so no callback writes here and nothing can drift.
		std::vector<LinkAudioSourceSlot> mLinkAudioSourceSlots;
		std::vector<LinkAudioSinkSlot> mLinkAudioSinkSlots;
		std::atomic<bool> mLinkAudioNeedsSync = false;

		//Last state jack_transport_link pushed us, per topic. Each blob is a self-healing snapshot
		//of its own topic, which is why they stay whole JSON strings: the two lists are the
		//authority for both the slot set and the display order, and the telemetry is joined to them
		//by key, so splitting them into per-slot values would lose that atomicity.
		struct LinkAudioState {
			bool available = false;
			std::string channelsJson = "[]";
			std::string sinksJson = "[]";
			std::string sourcesJson = "[]";
			std::string sourceStatusJson = "[]";
			std::string peerName;
			float latencyMs = 100.0f;
			bool syncToIncoming = false;
		};
		LinkAudioState mLinkAudioState;
		//written by handleLinkTransportOSC on the network poll, snapshot-read by processEvents
		std::mutex mLinkAudioStateMutex;

		//jack_transport_link's OSC receive port, read from its osc-port metadata key. 0 = we don't
		//know where it is, which is also how Link Audio reports unavailable.
		int mJTLPort = 0;
		std::unique_ptr<JTLCommandSender> mJTLSender;
		std::vector<std::string> mJTLPendingCommands;
		std::mutex mJTLCommandMutex;
		//set by the property-change callback; the read itself happens on the main thread
		std::atomic<bool> mJTLPortNeedsRead = false;
		//when to re-send our listener registration, and when to next poll for the port key
		std::chrono::time_point<std::chrono::steady_clock> mJTLRegisterNext;
		std::chrono::time_point<std::chrono::steady_clock> mJTLDiscoverNext;
		//Connections involving jack-transport-link ports, held until we've seen them actually made.
		//
		//Not just the ones that failed: jack_transport_link cycles jack_deactivate/jack_activate
		//around a structural slot change and restores only the connections it snapshotted first, so
		//a connect that *succeeded* inside that window is silently dropped. Reissuing until the
		//deadline covers both cases -- jack_connect on an existing connection is a cheap EEXIST.
		struct PendingLinkConnection {
			SetConnectionInfo info;
			std::chrono::time_point<std::chrono::steady_clock> until;
		};
		std::vector<PendingLinkConnection> mLinkAudioPendingConnections;
		std::mutex mPendingConnectionsMutex;
		void retryLinkAudioPendingConnections();
		//add or refresh one pending edge, keyed by identity so repeated re-applies extend a deadline
		//instead of stacking up duplicate connect attempts (expects mPendingConnectionsMutex held)
		void pendLinkAudioConnection(const SetConnectionInfo& info, std::chrono::time_point<std::chrono::steady_clock> until);
		//forget everything pending, so a previous set's unreachable edges don't leak into the next
		void clearLinkAudioPendingConnections();

		//The slots the loaded set expects, held as desired state rather than sent outright:
		//flushJTLCommands drops commands when we have no endpoint, and at boot the initial set
		//loads well before jack_transport_link is discovered. Applied when we have an endpoint,
		//re-applied if that endpoint changes, cleared once jtl echoes a matching list back.
		//It is kept after jtl confirms it, not dropped, so that a jack_transport_link restart
		//re-applies the loaded set rather than inheriting whatever jtl restored from its own config.
		//It is dropped when the user edits the arrangement, because from then on the live slots --
		//not the set's -- are what should survive a restart.
		boost::optional<SetLinkAudioInfo> mLinkAudioDesired;
		bool mLinkAudioDesiredSent = false;
		bool mLinkAudioDesiredConverged = false;
		//Link Audio has been unavailable since we last applied. Whatever we told jack_transport_link
		//died with it, so the arrangement has to be sent again once it's back -- and this is the
		//signal for that, rather than inferring a restart from the reported list, which reads exactly
		//like a user edit.
		bool mLinkAudioSawUnavailable = false;
		//how long slot changes are still attributed to an in-flight load rather than to the user
		std::chrono::time_point<std::chrono::steady_clock> mLinkAudioDesiredSuppressUntil;
		std::mutex mLinkAudioDesiredMutex;
		void applyLinkAudioDesired();
		//the user has issued a slot command of their own, so the loaded set's arrangement is no
		//longer what should be reinstated. Called from the imperative ossia callbacks, which are the
		//only way a slot changes at our request.
		void linkAudioUserTookOver();

		//the loaded set's edges that touch jack_transport_link, kept so they can be reasserted after
		//a jtl restart re-creates the slot ports (guarded by mPendingConnectionsMutex)
		std::vector<SetConnectionInfo> mLinkAudioSetConnections;

		//the slot identities we last saw jack_transport_link report, so a change can be
		//distinguished from a re-publish of the same list
		std::vector<std::string> mLinkAudioSetupLastSends;
		std::vector<std::pair<std::string, std::string>> mLinkAudioSetupLastReceives;
		std::atomic<bool> mLinkAudioSetupChanged = false;

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
		jack_port_t * mJackMidiIn = nullptr;

		jack_port_t * mResetMidiOut = nullptr;
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
