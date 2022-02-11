#pragma once
#include <RNBO.h>

class EventHandler : public RNBO::EventHandler {
	public:
		typedef std::function<void(const RNBO::ParameterIndex, const RNBO::ParameterValue)> ParameterEventCallback;
		typedef std::function<void(RNBO::MessageEvent)> MessageEventEventCallback;
		typedef std::function<void(RNBO::MidiEvent)> MidiEventEventCallback;

		EventHandler(ParameterEventCallback paramCallback, MessageEventEventCallback msgCallback, MidiEventEventCallback midiCallback = nullptr);

		virtual void eventsAvailable() override;
		virtual void handlePresetEvent(const RNBO::PresetEvent& event) override;
		virtual void handleParameterEvent(const RNBO::ParameterEvent& event) override;
		virtual void handleMessageEvent(const RNBO::MessageEvent& event) override;
		virtual void handleMidiEvent(const RNBO::MidiEvent& event) override;

		//evaluate any queued events, in the current thread
		void processEvents();
	private:
		ParameterEventCallback mParameterCallback;
		MessageEventEventCallback mMessageCallback;
		MidiEventEventCallback mMidiCallback;
};
