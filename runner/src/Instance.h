//An instance of a RNBO patcher.

#pragma once

#include <vector>
#include <memory>
#include <thread>

#include <ossia-cpp/ossia-cpp98.hpp>

#include "RNBO.h"
#include "EventHandler.h"
#include "InstanceAudio.h"
#include "Defines.h"
#include "Queue.h"

class PatcherFactory;

class Instance {
	public:
		Instance(std::shared_ptr<PatcherFactory> factory, std::string name, NodeBuilder builder, RNBO::Json conf);
		~Instance();

		void start();
		void stop();
		//process any events in the current thread
		void processEvents();

	private:
		class ValueCallbackHelper;
		struct DataRefCommand {
			std::string fileName;
			std::string id;
			DataRefCommand(std::string inFileName, RNBO::ExternalDataId inId) : fileName(inFileName), id(inId) {}
		};
		void processDataRefCommands();
		static void valueCallbackTrampoline(void* context, const opp::value& val);

		std::vector<opp::node> mNodes;
		std::unique_ptr<InstanceAudio> mAudio;
		std::unique_ptr<EventHandler> mEventHandler;
		std::shared_ptr<PatcherFactory> mPatcherFactory;
		std::shared_ptr<RNBO::CoreObject> mCore;

		std::vector<std::shared_ptr<ValueCallbackHelper>> mValueCallbackHelpers;

		std::map<RNBO::ParameterIndex, opp::node> mIndexToNode;

		opp::node mActiveNode;

		//queue for loading or unloading data refs
		Queue<DataRefCommand> mDataRefCommandQueue;
		//only accessed in the data ref thread
		std::unordered_map<std::string, std::shared_ptr<std::vector<float>>> mDataRefs;
		std::unordered_map<std::string, opp::node> mDataRefNodes;
		std::thread mDataRefThread;
		std::atomic<bool> mDataRefProcessCommands;
};
