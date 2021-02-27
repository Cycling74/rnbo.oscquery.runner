#pragma once

#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <optional>

#include <ossia-cpp/ossia-cpp98.hpp>
#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

#include "Instance.h"
#include "ProcessAudio.h"
#include "Queue.h"

//forward declarations
class ValueCallbackHelper;

#ifdef RNBO_USE_DBUS

#include "IRnboUpdateService.h"

namespace core {
	namespace dbus {
		class Bus;
		class Object;
		class Service;
		template<typename T>
			class Property;
	}
}
#endif

class UpdateServiceProxy;

//An object which controls the whole show
class Controller {
	public:
		Controller(std::string server_name = "rnbo");
		~Controller();

		//return true on success
		bool loadLibrary(const std::string& path, std::string cmdId = std::string(), RNBO::Json conf = nullptr, bool saveConfig = true);
		bool loadLast();

		//returns true until we should quit
		bool process();
	private:
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

		opp::oscquery_server mServer;
		opp::node mInstancesNode;
		opp::node mResponseNode;

		//instance and path to SO
		std::vector<std::pair<std::unique_ptr<Instance>, boost::filesystem::path>> mInstances;

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
		boost::optional<std::chrono::time_point<std::chrono::system_clock>> mSaveNext;

		std::vector<std::shared_ptr<ValueCallbackHelper>> mValueCallbackHelpers;

		std::shared_ptr<UpdateServiceProxy> mUpdateServiceProxy;

#ifdef RNBO_USE_DBUS
		std::thread mDBusThread;
		std::shared_ptr<core::dbus::Bus> mDBusBus;
		std::shared_ptr<core::dbus::Service> mDBusService;
		std::shared_ptr<core::dbus::Object> mDBusObject;

		std::shared_ptr<core::dbus::Property<IRnboUpdateService::Properties::Active>> mPropUpdateActive;
		std::shared_ptr<core::dbus::Property<IRnboUpdateService::Properties::Status>> mPropUpdateStatus;
#endif

		//TODO remove when we figure out how to get property signals
		std::chrono::duration<int> mPropertyPollPeriod = std::chrono::seconds(1);
		std::chrono::time_point<std::chrono::system_clock> mPropertyPollNext;
		bool mUpdateActiveLast = false;
		opp::node mNodeUpdateActive;
		std::string mUpdateStatusLast;
		opp::node mNodeUpdateStatus;
};
