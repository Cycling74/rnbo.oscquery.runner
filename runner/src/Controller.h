#pragma once

#include <vector>
#include <mutex>
#include <thread>
#include <queue>
#include <atomic>
#include <condition_variable>

#include <ossia-cpp/ossia-cpp98.hpp>
#include "Instance.h"
#include "ProcessAudio.h"

//An object which controls the whole show
class Controller {
	public:
		Controller(std::string server_name = "rnbo");
		~Controller();
		void loadLibrary(const std::string& path);
		void handleCommand(const opp::value& data);
		void handleActive(bool active);
	private:
		void clearInstances(std::lock_guard<std::mutex>&);
		void processCommands();
		opp::oscquery_server mServer;
		opp::node mInstancesNode;
		std::vector<opp::node> mNodes;
		std::vector<std::unique_ptr<Instance>> mInstances;

		std::unique_ptr<ProcessAudio> mProcessAudio;
		opp::node mAudioActive;
		std::mutex mBuildMutex;
		std::mutex mInstanceMutex;

		std::atomic<bool> mProcessCommands;
		std::thread mCommandThread;
		std::mutex mCommandQueueMutex;
		std::queue<std::string> mCommandQueue;
		std::condition_variable mCommandQueueCondition;
};
