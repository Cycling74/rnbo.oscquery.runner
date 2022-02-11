#include "EventHandler.h"

EventHandler::EventHandler(ParameterEventCallback paramCallback, MessageEventEventCallback msgCallback, MidiEventEventCallback midiCallback) : mParameterCallback(paramCallback), mMessageCallback(msgCallback), mMidiCallback(midiCallback) {
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
