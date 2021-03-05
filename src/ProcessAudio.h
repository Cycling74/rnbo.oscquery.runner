#pragma once

//A controller that handles audio for the entire appliction.
class ProcessAudio {
	public:
		virtual ~ProcessAudio() {}
		virtual bool isActive() = 0;
		virtual bool setActive(bool active) = 0;
		//process any events in the current thread
		virtual void processEvents() = 0;
};
