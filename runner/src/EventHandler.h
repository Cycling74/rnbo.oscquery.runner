#pragma once
#include <RNBO.h>

class EventHandler : public RNBO::EventHandler {
	public:
		typedef std::function<void(const RNBO::ParameterIndex, const RNBO::ParameterValue)> ParameterEventCallback;

		EventHandler(ParameterEventCallback paramCallback);

		virtual void eventsAvailable() override;
		virtual void handlePresetEvent(RNBO::PresetEvent event) override;
		virtual void handleParameterEvent(RNBO::ParameterEvent event) override;
		virtual void handleMessageEvent(RNBO::MessageEvent event) override;

	private:
		ParameterEventCallback mParameterCallback;
};
