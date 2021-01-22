#pragma once
#include <RNBO.h>

class EventHandler : public RNBO::EventHandler {
	public:
		typedef std::function<void(const RNBO::ParameterIndex, const RNBO::ParameterValue)> ParameterEventCallback;
		typedef std::function<void(RNBO::MessageEvent)> MessageEventEventCallback;

		EventHandler(ParameterEventCallback paramCallback, MessageEventEventCallback msgCallback);

		virtual void eventsAvailable() override;
		virtual void handlePresetEvent(RNBO::PresetEvent event) override;
		virtual void handleParameterEvent(RNBO::ParameterEvent event) override;
		virtual void handleMessageEvent(RNBO::MessageEvent event) override;

		//evaluate any queued events, in the current thread
		void processEvents();
	private:
		ParameterEventCallback mParameterCallback;
		MessageEventEventCallback mMessageCallback;
};
