#pragma once

//A controller that handles audio for the entire appliction.
class ProcessAudio {
	public:
		virtual ~ProcessAudio() {}
		virtual bool isActive() = 0;
		virtual bool setActive(bool active) = 0;
		//process any events in the current thread
		virtual void processEvents() = 0;

		//transport handlers
		virtual void handleTransportState(bool running) = 0;
		virtual void handleTransportTempo(double bpm) = 0;
		virtual void handleTransportBeatTime(double btime) = 0;
		virtual void handleTransportTimeSig(double numerator, double denominator) = 0;
};
