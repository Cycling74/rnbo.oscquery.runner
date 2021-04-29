#pragma once

#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <optional>


#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

#include "Instance.h"
#include "ProcessAudio.h"
#include "Queue.h"

//forward declarations

#ifdef RNBO_USE_DBUS
class RnboUpdateServiceProxy;
#endif

//An object which controls the whole show
class Controller {
	public:
		Controller(std::string server_name = "rnbo");
		~Controller();

		//return true on success
		bool loadLibrary(const std::string& path, std::string cmdId = std::string(), RNBO::Json conf = nullptr, bool saveConfig = true);
		bool loadLast();
#ifdef RNBO_OSCQUERY_BUILTIN_PATCHER
		bool loadBuiltIn();
#endif

		//returns true until we should quit
		bool process();
	private:
		bool tryActivateAudio();
		void clearInstances(std::lock_guard<std::mutex>&);
		void processCommands();
		void reportCommandResult(std::string id, RNBO::Json res);
		void reportCommandError(std::string id, unsigned int code, std::string message);
		void reportCommandStatus(std::string id, RNBO::Json obj);

		void handleActive(bool active);
		void updateDiskSpace();
		void saveLast();
		//queue a saveLast, this is thread safe, saveLast will happen in the process() thread
		void queueSave();

		std::unique_ptr<ossia::net::generic_device> mServer;

		//TODO
		ossia::net::node_base * mInstancesNode;
		ossia::net::parameter_base * mResponseParam = nullptr;

		//instance and path to SO
		std::vector<std::pair<std::unique_ptr<Instance>, boost::filesystem::path>> mInstances;

		ossia::net::parameter_base * mDiskSpaceParam = nullptr;
		std::uintmax_t mDiskSpaceLast = 0;
		std::chrono::duration<int> mDiskSpacePollPeriod = std::chrono::seconds(10);
		std::chrono::time_point<std::chrono::system_clock> mDiskSpacePollNext;

		std::unique_ptr<ProcessAudio> mProcessAudio;
		ossia::net::parameter_base * mAudioActive;
		std::mutex mBuildMutex;
		std::mutex mInstanceMutex;

		std::atomic<bool> mProcessCommands;
		std::thread mCommandThread;

		Queue<std::string> mCommandQueue;

		std::mutex mSaveMutex;
		bool mSave = false;
		//a timeout for when to save, debouncing
		boost::optional<std::chrono::time_point<std::chrono::system_clock>> mSaveNext;

#ifdef RNBO_USE_DBUS
		std::shared_ptr<RnboUpdateServiceProxy> mUpdateServiceProxy;
#endif
};
