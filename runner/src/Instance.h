//An instance of a RNBO patcher.

#pragma once

#include <vector>
#include <memory>
#include <ossia-cpp/ossia-cpp98.hpp>
#include "RNBO.h"
#include "EventHandler.h"
#include "InstanceAudio.h"
#include "Defines.h"

class PatcherFactory;

class Instance {
	public:
		Instance(std::shared_ptr<PatcherFactory> factory, std::string name, NodeBuilder builder);
		~Instance();

		void start();
		void stop();
	private:
		struct ValueCallbackHelper;
		std::vector<opp::node> mNodes;
		std::unique_ptr<InstanceAudio> mAudio;
		std::unique_ptr<EventHandler> mEventHandler;
		std::shared_ptr<PatcherFactory> mPatcherFactory;
		std::shared_ptr<RNBO::CoreObject> mCore;
		std::vector<std::shared_ptr<ValueCallbackHelper>> mValueCallbackHelpers;
		std::map<RNBO::ParameterIndex, opp::node> mIndexToNode;
};
