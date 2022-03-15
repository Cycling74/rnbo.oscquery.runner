#pragma once
#include <RNBO.h>

class EventHandler : public RNBO::EventHandler {
	public:
		typedef std::function<void(const RNBO::ParameterIndex, const RNBO::ParameterValue)> ParameterEventCallback;
		typedef std::function<void(RNBO::MessageEvent)> MessageEventEventCallback;
		typedef std::function<void(RNBO::MidiEvent)> MidiEventEventCallback;
		typedef std::function<void(RNBO::TransportEvent)> TransportEventCallback;
		typedef std::function<void(RNBO::TempoEvent)> TempoEventCallback;
		typedef std::function<void(RNBO::BeatTimeEvent)> BeatTimeEventCallback;
		typedef std::function<void(RNBO::TimeSignatureEvent)> TimeSignatureEventCallback;

		EventHandler(
				ParameterEventCallback paramCallback,
				MessageEventEventCallback msgCallback,

				TransportEventCallback transportCallback,
				TempoEventCallback tempoCallback,
				BeatTimeEventCallback beatTimeCallback,
				TimeSignatureEventCallback timeSigCallback,

				MidiEventEventCallback midiCallback = nullptr
				);

		virtual void eventsAvailable() override;
		virtual void handlePresetEvent(const RNBO::PresetEvent& event) override;
		virtual void handleParameterEvent(const RNBO::ParameterEvent& event) override;
		virtual void handleMessageEvent(const RNBO::MessageEvent& event) override;
		virtual void handleMidiEvent(const RNBO::MidiEvent& event) override;

		virtual void handleTransportEvent(const RNBO::TransportEvent& e) override;
		virtual void handleTempoEvent(const RNBO::TempoEvent& e) override;
		virtual void handleBeatTimeEvent(const RNBO::BeatTimeEvent& e) override;
		virtual void handleTimeSignatureEvent(const RNBO::TimeSignatureEvent& e) override;

		//evaluate any queued events, in the current thread
		void processEvents();
	private:
		ParameterEventCallback mParameterCallback;
		MessageEventEventCallback mMessageCallback;

		TransportEventCallback mTransportCallback;
		TempoEventCallback mTempoCallback;
		BeatTimeEventCallback mBeatTimeCallback;
		TimeSignatureEventCallback mTimeSigCallback;

		MidiEventEventCallback mMidiCallback;
};
