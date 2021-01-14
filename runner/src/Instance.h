//An instance of a RNBO patcher.

#pragma once

#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

#include <ossia-cpp/ossia-cpp98.hpp>

#include "RNBO.h"
#include "EventHandler.h"
#include "InstanceAudio.h"
#include "Defines.h"
#include "Queue.h"

class PatcherFactory;
class ValueCallbackHelper;
namespace moodycamel {
template<typename T, size_t MAX_BLOCK_SIZE>
class ReaderWriterQueue;
}

class Instance {
	public:
		Instance(std::shared_ptr<PatcherFactory> factory, std::string name, NodeBuilder builder, RNBO::Json conf);
		~Instance();

		void start();
		void stop();
		//process any events in the current thread
		void processEvents();

		void loadPreset(std::string name);

		// get the current configuration for this instance:
		//  last loaded preset
		//  sample mapping
		RNBO::Json currentConfig();
		//register a function to be called when configuration values change
		//this will be called in the same thread as `processEvents`
		void registerConfigChangeCallback(std::function<void()> cb);
	private:
		std::function<void()> mConfigChangeCallback = nullptr;
		std::mutex mConfigChangedMutex;
		bool mConfigChanged = false;

		struct DataRefCommand {
			std::string fileName;
			std::string id;
			DataRefCommand(std::string inFileName, RNBO::ExternalDataId inId) : fileName(inFileName), id(inId) {}
		};
		void processDataRefCommands();
		void updatePresetEntries();
		//called from various threads
		void queueConfigChangeSignal();
		//only called at startup or in the processDataRefCommands thread
		bool loadDataRef(const std::string& id, const std::string& fileName);

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

		//map of dataref name to file name
		std::mutex mDataRefFileNameMutex;
		std::unordered_map<std::string, std::string> mDataRefFileNameMap;

		//presets
		opp::node mPresetEntires;
		std::unordered_map<std::string, RNBO::ConstPresetPtr> mPresets;
		std::mutex mPresetMutex;
		std::string mPresetLatest; //the most recently loaded preset
		std::string mPresetInitial; //the user indicated initial preset

		//callback data from RNBO, a name and a ptr
		std::unique_ptr<moodycamel::ReaderWriterQueue<std::pair<std::string, RNBO::ConstPresetPtr>, 2>> mPresetSavedQueue;
};
