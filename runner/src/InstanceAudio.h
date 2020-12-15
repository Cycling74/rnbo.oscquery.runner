#pragma once

//abstract base class for instance audio
class InstanceAudio {
	public:
		virtual ~InstanceAudio() {}
		virtual void start() = 0;
		virtual void stop() = 0;
		virtual void close() = 0;
		virtual bool isActive() = 0;
};
