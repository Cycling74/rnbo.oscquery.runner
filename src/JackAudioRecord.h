#pragma once

#include "Defines.h"

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <vector>
#include <atomic>
#include <thread>
#include <semaphore>

class JackAudioRecord {
	public:
		JackAudioRecord(NodeBuilder builder);
		~JackAudioRecord();
		void process(jack_nframes_t nframes);
		bool open();
		void close();
	private:
		void startRecording();
		void endRecording(bool wait);
		bool resize(int channels, bool toggleactive = true);

		void write();

		jack_client_t * mJackClient = nullptr;

		std::vector<jack_port_t *> mJackAudioPortIn;
		std::vector<jack_ringbuffer_t *> mRingBuffers;

		std::atomic<bool> mDoRecord = false;
		std::atomic<bool> mRun = true;
		std::atomic<bool> mWrite = true;

		std::string mFileNameTmpl;

		int mSampleRate = 0;
		jack_nframes_t mBufferSize = 0;
		NodeBuilder mBuilder;

		ossia::net::node_base * mRecordRoot = nullptr;

		ossia::net::parameter_base * mActiveParam = nullptr;
		ossia::net::parameter_base * mChannelsParam = nullptr;
		ossia::net::parameter_base * mTimeoutParam = nullptr;
		ossia::net::parameter_base * mSecondsCapturedParam = nullptr;
		ossia::net::parameter_base * mBufferFullCountParam = nullptr;

		std::atomic<int> mBufferFullCount;

		std::thread mWriteThread;

		std::binary_semaphore mDataAvailable;
};
