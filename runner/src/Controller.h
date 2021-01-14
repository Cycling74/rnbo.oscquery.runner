#pragma once

#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <filesystem>
#include <optional>

#include <ossia-cpp/ossia-cpp98.hpp>

#include "Instance.h"
#include "ProcessAudio.h"
#include "Queue.h"

//An object which controls the whole show
class Controller {
	public:
		Controller(std::string server_name = "rnbo");
		~Controller();

		//return true on success
		bool loadLibrary(const std::string& path, std::string cmdId = std::string(), RNBO::Json conf = nullptr);
		bool loadLast();

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
		void saveLast();
		//queue a saveLast, this is thread safe, saveLast will happen in the process() thread
		void queueSave(bool s = true);

		opp::oscquery_server mServer;
		opp::node mInstancesNode;
		opp::node mResponseNode;
		std::vector<opp::node> mNodes;

		//instance and path to SO
		std::vector<std::pair<std::unique_ptr<Instance>, std::filesystem::path>> mInstances;

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

		std::mutex mSaveMutex;
		bool mSave = false;
		//a timeout for when to save, debouncing
		std::optional<std::chrono::time_point<std::chrono::system_clock>> mSaveNext;
};
