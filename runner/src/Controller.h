#pragma once

#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

#include <ossia-cpp/ossia-cpp98.hpp>

#include "Instance.h"
#include "ProcessAudio.h"
#include "Queue.h"

//An object which controls the whole show
class Controller {
	public:
		Controller(std::string server_name = "rnbo");
		~Controller();
		void loadLibrary(const std::string& path, std::string cmdId = std::string(), RNBO::Json conf = nullptr);
		void handleCommand(const opp::value& data);
		void handleActive(bool active);
		//returns true until we should quit
		bool process();
	private:
		void clearInstances(std::lock_guard<std::mutex>&);
		void processCommands();
		void reportCommandResult(std::string id, RNBO::Json res);
		void reportCommandError(std::string id, unsigned int code, std::string message);
		void reportCommandStatus(std::string id, RNBO::Json obj);

		void updateDiskSpace();

		opp::oscquery_server mServer;
		opp::node mInstancesNode;
		opp::node mResponseNode;
		std::vector<opp::node> mNodes;
		std::vector<std::unique_ptr<Instance>> mInstances;

		opp::node mDiskSpaceNode;
		std::uintmax_t mDiskSpaceLast = 0;
		std::chrono::duration<int> mDiskSpacePollPeriod = std::chrono::seconds(10);
		std::chrono::time_point<std::chrono::system_clock> mDiskSpacePollNext;

		std::unique_ptr<ProcessAudio> mProcessAudio;
		opp::node mAudioActive;
		std::mutex mBuildMutex;
		std::mutex mInstanceMutex;

		std::atomic<bool> mProcessCommands;
		std::thread mCommandThread;

		Queue<std::string> mCommandQueue;
};
