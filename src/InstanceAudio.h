#pragma once

#include <functional>
#include "RNBO.h"

//abstract base class for instance audio
class InstanceAudio {
	public:
		virtual ~InstanceAudio() {}
		virtual void addConfig(RNBO::Json& conf) { }

		virtual void activate() {}
		virtual void connect() {}
		virtual void start() = 0;
		virtual void stop() = 0;
		virtual bool isActive() = 0;

		//called by the instance, in the main thread, to take care of any command processing or what not
		virtual void processEvents() {}

		virtual void registerConfigChangeCallback(std::function<void()> cb) { };
};
