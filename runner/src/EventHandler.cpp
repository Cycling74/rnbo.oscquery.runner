#include "EventHandler.h"

EventHandler::EventHandler(ParameterEventCallback paramCallback) : mParameterCallback(paramCallback) {
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
	//TODO
}
