#pragma once

#include <mutex>
#include <memory>
#include <vector>
#include <ossia-cpp/ossia-cpp98.hpp>
#include <jack/types.h>
#include <jack/jack.h>

#include "RNBO.h"
#include "InstanceAudio.h"
#include "ProcessAudio.h"
#include "Defines.h"

namespace moodycamel {
template<typename T, size_t MAX_BLOCK_SIZE>
class ReaderWriterQueue;
}

class ValueCallbackHelper;

//Global jack settings.
class ProcessAudioJack : public ProcessAudio {
	public:
		ProcessAudioJack(NodeBuilder builder);
		virtual ~ProcessAudioJack();
		virtual bool isActive() override;
		virtual bool setActive(bool active) override;
	private:
		bool createClient(bool startServer);
		void writeJackDRC();
		jack_client_t * mJackClient;
		std::vector<opp::node> mNodes;
		opp::node mInfo;
		NodeBuilder mBuilder;
		std::mutex mMutex;
		std::vector<std::string> mCardNames;

		double mSampleRate = 44100;
		opp::node mSampleRateNode;
		int mPeriodFrames = 256;
		opp::node mPeriodFramesNode;

#ifdef __APPLE__
		//apple with homebrew, TODO make this configurable
		std::string mCmdPrefix = "/usr/local/bin/jackd";
		std::string mCmdSuffix = "-Xcoremidi -dcoreaudio";
#else
		std::string mCmdPrefix = "/usr/bin/jackd";
		std::string mCmdSuffix = "-dalsa -Xseq";
		int mNumPeriods = 2;
		opp::node mNumPeriodsNode;
		std::string mCardName;
#endif

		std::vector<std::shared_ptr<ValueCallbackHelper>> mValueCallbackHelpers;
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

		//command queue eventually if we have more than just ports
		std::unique_ptr<moodycamel::ReaderWriterQueue<jack_port_id_t, 32>> mPortQueue;

		//working buffer for port getting port aliases
		char * mJackPortAliases[2];
		std::mutex mPortMutex;

		//transport sync info
		jack_position_t mTransportPosLast;
		jack_transport_state_t mTransportStateLast = jack_transport_state_t::JackTransportStopped;
};
