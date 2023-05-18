#pragma once

//abstract base class for instance audio
class InstanceAudio {
	public:
		virtual ~InstanceAudio() {}
		virtual std::string name() { return std::string(); }

		virtual void activate() {}
		virtual void connect() {}
		virtual void start() = 0;
		virtual void stop() = 0;
		virtual bool isActive() = 0;

		//called by the instance, in the main thread, to take care of any command processing or what not
		virtual void processEvents() {}
};
