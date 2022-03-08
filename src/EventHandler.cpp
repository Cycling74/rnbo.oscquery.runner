#include "EventHandler.h"

EventHandler::EventHandler(
		ParameterEventCallback paramCallback,
		MessageEventEventCallback msgCallback,

		TransportEventCallback transportCallback,
		TempoEventCallback tempoCallback,
		BeatTimeEventCallback beatTimeCallback,
		TimeSignatureEventCallback timeSigCallback,

		MidiEventEventCallback midiCallback) :
	mParameterCallback(paramCallback),
	mMessageCallback(msgCallback),

	mTransportCallback(transportCallback),
	mTempoCallback(tempoCallback),
	mBeatTimeCallback(beatTimeCallback),
	mTimeSigCallback(timeSigCallback),

	mMidiCallback(midiCallback) {
}

void EventHandler::processEvents() {
	drainEvents();
}

void EventHandler::eventsAvailable() {
	//TODO
}

void EventHandler::handlePresetEvent(const RNBO::PresetEvent& event) {
	//TODO
}

void EventHandler::handleParameterEvent(const RNBO::ParameterEvent& event) {
	if (mParameterCallback) {
		mParameterCallback(event.getIndex(), event.getValue());
	}
}

void EventHandler::handleMessageEvent(const RNBO::MessageEvent& event) {
	if (mMessageCallback) {
		mMessageCallback(event);
	}
}

void EventHandler::handleMidiEvent(const RNBO::MidiEvent& event) {
	if (mMidiCallback) {
		mMidiCallback(event);
	}
}

void EventHandler::handleTransportEvent(const RNBO::TransportEvent& e)
{
	if (mTransportCallback)
		mTransportCallback(e);
}

void EventHandler::handleTempoEvent(const RNBO::TempoEvent& e)
{
	if (mTempoCallback)
		mTempoCallback(e);
}

void EventHandler::handleBeatTimeEvent(const RNBO::BeatTimeEvent& e)
{
	if (mBeatTimeCallback)
		mBeatTimeCallback(e);
}

void EventHandler::handleTimeSignatureEvent(const RNBO::TimeSignatureEvent& e)
{
	if (mTimeSigCallback)
		mTimeSigCallback(e);
}
