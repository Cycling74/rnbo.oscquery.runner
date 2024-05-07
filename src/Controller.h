#pragma once

#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <optional>
#include <set>
#include <memory>
#include <functional>

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>

#include "Instance.h"
#include "ProcessAudio.h"
#include "Queue.h"
#include "DB.h"

//forward declarations
namespace ossia {
	namespace net {
		class multiplex_protocol;
		class network_context;
	}
}

#ifdef RNBO_USE_DBUS
class RnboUpdateServiceProxy;
#endif

//An object which controls the whole show
class Controller {
	public:
		Controller(std::string server_name = "rnbo");
		~Controller();

		//return null on failure
		std::shared_ptr<Instance> loadLibrary(const std::string& path, std::string cmdId = std::string(), RNBO::Json conf = nullptr, bool saveConfig = true, unsigned int instanceIndex = 0, const boost::filesystem::path& config_path = boost::filesystem::path());
		void loadSet(boost::filesystem::path filename = boost::filesystem::path());
#ifdef RNBO_OSCQUERY_BUILTIN_PATCHER
		bool loadBuiltIn();
#endif

		//returns true until we should quit
		bool processEvents();
	private:
		void doLoadSet(boost::filesystem::path filename);

		bool tryActivateAudio();
		void reportActive();
		void clearInstances(std::lock_guard<std::mutex>&, float fadeTime);
		void unloadInstance(std::lock_guard<std::mutex>&, unsigned int index);

		void registerCommands();
		void processCommands();
		void reportCommandResult(std::string id, RNBO::Json res);
		void reportCommandError(std::string id, unsigned int code, std::string message);
		void reportCommandStatus(std::string id, RNBO::Json obj);

		void handleActive(bool active);
		void updateDiskSpace();
		void updateListenersList();
		void restoreListeners();
		void listenersAddProtocol(const std::string& ip, uint16_t port);

		std::unordered_map<std::string, std::function<void(const std::string& method, const std::string& id, const RNBO::Json& params)>> mCommandHandlers;

		//save set, return the path of the save
		boost::optional<boost::filesystem::path> saveSet(std::string name, std::string meta, bool abort_empty);

		void patcherStore(
				const std::string& name,
				const boost::filesystem::path& libFile,
				const boost::filesystem::path& configFilePath,
				const boost::filesystem::path& rnboPatchPath,
				const std::string& maxRNBOVersion,
				const RNBO::Json& config,
				bool migratePresets
		);

		//queue a saveSet, this is thread safe, saveLast will happen in the process() thread
		void queueSave();

		void updatePatchersInfo(std::string addedOrUpdated = std::string());
		void destroyPatcher(const std::string& name);

		//only to be called during setup or in the command thread
		void updateSetNames();

		unsigned int nextInstanceIndex();

		void handleProgramChange(ProgramChange);

		std::shared_ptr<DB> mDB;
		std::unique_ptr<ossia::net::generic_device> mServer;
		std::shared_ptr<ossia::net::network_context> mOssiaContext;
		ossia::net::multiplex_protocol * mProtocol;
		std::mutex mOssiaContextMutex;

		ossia::net::node_base * mInstancesNode;
		ossia::net::parameter_base * mResponseParam = nullptr;
		ossia::net::node_base * mInstanceLoadNode;
		ossia::net::node_base * mPatchersNode;

		ossia::net::node_base * mSetLoadNode = nullptr;
		ossia::net::parameter_base * mSetLoadParam = nullptr;

		ossia::net::parameter_base * mSetMetaParam = nullptr;

		std::mutex mSetNamesMutex;
		bool mSetNamesUpdated = false;
		std::vector<ossia::value> mSetNames;

		std::mutex mSetLoadPendingMutex;
		boost::filesystem::path mSetLoadPendingPath;

		//instance, path to SO, path to config
		std::vector<std::tuple<std::shared_ptr<Instance>, boost::filesystem::path, boost::filesystem::path>> mInstances;

		std::vector<std::shared_ptr<Instance>> mStoppingInstances;

		ossia::net::parameter_base * mDiskSpaceParam = nullptr;
		std::uintmax_t mDiskSpaceLast = 0;
		std::chrono::duration<int> mDiskSpacePollPeriod = std::chrono::seconds(10);
		std::chrono::time_point<std::chrono::steady_clock> mDiskSpacePollNext;

		std::shared_ptr<ProcessAudio> mProcessAudio;
		ossia::net::parameter_base * mAudioActive;
		std::mutex mBuildMutex;
		std::mutex mInstanceMutex;

		//for saving/restoring while toggling audio settings
		std::unordered_map<unsigned int, RNBO::UniquePresetPtr> mInstanceLastPreset;

		Queue<std::string> mCommandQueue;

		std::mutex mSaveMutex;
		bool mSave = false;
		//a timeout for when to save, debouncing
		boost::optional<std::chrono::time_point<std::chrono::steady_clock>> mSaveNext;

		boost::optional<std::chrono::time_point<std::chrono::steady_clock>> mProcessNext;

		ossia::net::parameter_base * mListenersListParam = nullptr;

		float mInstFadeInMs = 20.0f;
		float mInstFadeOutMs = 20.0f;

		int mPatcherProgramChangeChannel = 17; //omni, 17 == none

		boost::filesystem::path mSourceCache;
		boost::filesystem::path mCompileCache;

#ifdef RNBO_USE_DBUS
		std::shared_ptr<RnboUpdateServiceProxy> mUpdateServiceProxy;
#endif
};
