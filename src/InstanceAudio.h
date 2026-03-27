#pragma once

#include <functional>
#include <atomic>
#include "RNBO.h"

enum class AudioState {
	Idle,
	Starting,
	Running,
	Stopping,
	Stopped
};

//abstract base class for instance audio
class InstanceAudio {
	public:
		virtual ~InstanceAudio() {}
		virtual void addConfig(RNBO::Json& conf) { }

		virtual void activate() {}
		virtual void connect() {}
		virtual void start(float fadems = 0.0f) = 0;
		virtual void stop(float fadems = 0.0f) = 0;
		virtual AudioState state() const {
			return mAudioState.load();
		}

		//get the key and zero it out
		virtual uint16_t lastMIDIKey() = 0;

		//called by the instance, in the main thread, to take care of any command processing or what not
		virtual void processEvents() {}

		virtual void registerConfigChangeCallback(std::function<void()> cb) { };

		virtual size_t bufferSize() = 0;

		int8_t midiInputChannel() {
			return mMIDIInputChannel;
		}

		void setMidiInputChannel(int c) {
			mMIDIInputChannel = static_cast<int8_t>(std::clamp(c, -1, 15));
		}
	protected:
		std::atomic<AudioState> mAudioState = AudioState::Idle;
		int8_t mMIDIInputChannel = -1; //0 based, negative means omni
};
