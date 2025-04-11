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
		//load set marked as initial, or.. if that doesn't exist, load lastSetName
		void loadInitialSet();
		void loadSet(std::string name);
#ifdef RNBO_OSCQUERY_BUILTIN_PATCHER
		bool loadBuiltIn();
#endif

		//returns true until we should quit
		bool processEvents();

		bool tryActivateAudio(bool startServer = true);

	private:

		void doLoadSet(std::string name);

		void reportActive();
		void clearInstances(std::lock_guard<std::mutex>&, float fadeTime);
		void unloadInstance(std::lock_guard<std::mutex>&, unsigned int index);

		void registerCommands();
		void processCommands();
		void reportCommandResult(std::string id, RNBO::Json res);
		void reportCommandError(std::string id, unsigned int code, std::string message);
		void reportCommandStatus(std::string id, RNBO::Json obj, bool printerr = false);

		void handleActive(bool active);
		void updateDiskStats();
		void updateDatafileStats();
		void updateListenersList();
		void restoreListeners();
		void listenersAddProtocol(const std::string& ip, uint16_t port);

		std::unordered_map<std::string, std::function<void(const std::string& method, const std::string& id, const RNBO::Json& params)>> mCommandHandlers;

		SetInfo setInfo();

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
		void updateSetInitialName(std::string name);
		void updateSetViews(const std::string& setname);
		//need to lock build mutex around these
		void addSetView(std::string setname, int index);
		void removeSetView(int index);
		void reportSetViewOrder(const std::string& setname);

		//since preset save is async, we can optionall add "toadd" even if it isn't in the DB
		void updateSetPresetNames(std::string toadd = std::string());
		void saveSetPreset(const std::string& setName, std::string presetName);
		void loadSetPreset(const std::string& setName, std::string presetName);
		void handleInstancePresetLoad(unsigned int index, const std::string& setName, const std::string& presetName);

		unsigned int nextInstanceIndex();

    void installPackage(const boost::filesystem::path& location);

		//guard by mInstanceMutex
		std::string mPendingSetPresetName;
		std::set<unsigned int> mInstancesPendingPresetLoad;

		void handleProgramChange(ProgramChange);
		void ensureSet(std::string& name);
		std::string getCurrentSetName();
		std::string getCurrentSetPresetName();

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
		ossia::net::node_base * mSetPresetLoadNode = nullptr;
		ossia::net::parameter_base * mSetPresetLoadedParam = nullptr;

		ossia::net::parameter_base * mSetCurrentNameParam = nullptr;
		ossia::net::parameter_base * mSetInitialNameParam = nullptr;

		ossia::net::parameter_base * mSetMetaParam = nullptr;

		ossia::net::node_base * mSetViewsListNode = nullptr;
		ossia::net::parameter_base * mSetViewsOrderParam = nullptr;

		std::mutex mSetNamesMutex;
		bool mSetNamesUpdated = false;
		std::vector<ossia::value> mSetNames;

		std::mutex mSetPresetNamesMutex;
		bool mSetPresetNamesUpdated = false;
		std::vector<ossia::value> mSetPresetNames;

		std::mutex mSetLoadPendingMutex;
		boost::optional<std::string> mSetLoadPending;

		ossia::net::parameter_base * mSetDirtyParam = nullptr;

		//instance, path to SO, path to config
		std::vector<std::tuple<std::shared_ptr<Instance>, boost::filesystem::path, boost::filesystem::path>> mInstances;

		std::vector<std::shared_ptr<Instance>> mStoppingInstances;

		ossia::net::parameter_base * mDiskSpaceParam = nullptr;
		ossia::net::parameter_base * mDataFileDirMTimeParam = nullptr;
		std::uintmax_t mDiskSpaceLast = 0;
		std::string mDataFileDirMTimeLast;

		ossia::net::parameter_base * mMigrationAvailable = nullptr;

		std::chrono::duration<int> mDiskSpacePollPeriod = std::chrono::seconds(10);
		std::chrono::time_point<std::chrono::steady_clock> mDiskSpacePollNext;

		std::chrono::duration<int> mDatafilePollPeriod = std::chrono::seconds(1);
		std::chrono::time_point<std::chrono::steady_clock> mDatafilePollNext;

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

		int mPatcherProgramChangeChannel = 17; //0 == omni, 17 == none
		int mSetProgramChangeChannel = 17; //0 == omni, 17 == none
		int mSetPresetProgramChangeChannel = 17; //0 == omni, 17 == none

		boost::filesystem::path mSourceCache;
		boost::filesystem::path mCompileCache;

		bool mFirstSetLoad = true;

    std::string mSystemPrettyName;

#ifdef RNBO_USE_DBUS
		std::shared_ptr<RnboUpdateServiceProxy> mUpdateServiceProxy;
#endif
};
