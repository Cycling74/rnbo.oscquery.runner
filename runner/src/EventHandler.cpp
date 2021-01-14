#include "EventHandler.h"

EventHandler::EventHandler(ParameterEventCallback paramCallback, MessageEventEventCallback msgCallback) : mParameterCallback(paramCallback), mMessageCallback(msgCallback) {
}

void EventHandler::processEvents() {
	drainEvents();
}

void EventHandler::eventsAvailable() {
	//TODO
}

void EventHandler::handlePresetEvent(RNBO::PresetEvent event) {
	//TODO
}

void EventHandler::handleParameterEvent(RNBO::ParameterEvent event) {
	if (mParameterCallback) {
		mParameterCallback(event.getIndex(), event.getValue());
	}
}

void EventHandler::handleMessageEvent(RNBO::MessageEvent event) {
	if (mMessageCallback) {
		mMessageCallback(event);
	}
}
