#pragma once

#include "Defines.h"

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <vector>
#include <atomic>

class JackAudioRecord {
	public:
		JackAudioRecord(NodeBuilder builder);
		~JackAudioRecord();
		void process(jack_nframes_t nframes);
		bool open();
		void close();
		void write();
	private:
		jack_client_t * mJackClient = nullptr;

		std::vector<jack_port_t *> mJackAudioPortIn;
		std::vector<jack_ringbuffer_t *> mRingBuffers;
		std::vector<jack_default_audio_sample_t> mInterlaceBuffer;

		std::atomic<bool> mDoRecord = false;
		std::atomic<bool> mRun = true;

		int mSampleRate = 0;
		NodeBuilder mBuilder;
};
