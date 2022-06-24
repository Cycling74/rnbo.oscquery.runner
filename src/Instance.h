//An instance of a RNBO patcher.

#pragma once

#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

#include <boost/optional.hpp>

#include <ossia/network/value/value.hpp>

#include "RNBO.h"
#include "EventHandler.h"
#include "InstanceAudio.h"
#include "Defines.h"
#include "Queue.h"

class PatcherFactory;
namespace moodycamel {
template<typename T, size_t MAX_BLOCK_SIZE>
class ReaderWriterQueue;
}

class ProcessAudio;

class Instance {
	public:
		Instance(std::shared_ptr<PatcherFactory> factory, std::string name, NodeBuilder builder, RNBO::Json conf, std::shared_ptr<ProcessAudio> processAudio);
		~Instance();

		void start();
		void stop();
		//process any events in the current thread
		void processEvents();

		void loadPreset(std::string name);

		//does not save "latest".. used for loading between audio sessions
		void loadPreset(RNBO::UniquePresetPtr preset);
		RNBO::UniquePresetPtr getPresetSync();

		// get the current configuration for this instance:
		//  last loaded preset
		//  sample mapping
		RNBO::Json currentConfig();
		//register a function to be called when configuration values change
		//this will be called in the same thread as `processEvents`
		void registerConfigChangeCallback(std::function<void()> cb);
	private:
		//stored parameter meta
		RNBO::Json mParamConfig;
		std::function<void()> mConfigChangeCallback = nullptr;
		std::mutex mConfigChangedMutex;
		bool mConfigChanged = false;

		struct DataRefCommand {
			std::string fileName;
			std::string id;
			DataRefCommand(std::string inFileName, RNBO::ExternalDataId inId) : fileName(inFileName), id(inId) {}
		};
		struct PresetCommand {
			enum class CommandType {
				Delete,
				Initial,
				Load,
				Save
			};
			CommandType type;
			std::string preset;
			PresetCommand(CommandType t, std::string p) : type(t), preset(p) {}
		};

		void processDataRefCommands();
		void updatePresetEntries();
		void handleProgramChange(ProgramChange);

		//called from various threads
		void queueConfigChangeSignal();
		//only called at startup or in the processDataRefCommands thread
		bool loadDataRef(const std::string& id, const std::string& fileName);
		//attempts to load the dataref, clears out the oscquery value if it fails
		bool loadDataRefCleanup(const std::string& id, const std::string& fileName);
		void handleInportMessage(RNBO::MessageTag tag, const ossia::value& value);
		void handleOutportMessage(RNBO::MessageEvent e);
		void handleMidiCallback(RNBO::MidiEvent e);

		void handleParamUpdate(RNBO::ParameterIndex index, RNBO::ParameterValue value);

		std::unique_ptr<InstanceAudio> mAudio;
		std::unique_ptr<EventHandler> mEventHandler;
		std::shared_ptr<PatcherFactory> mPatcherFactory;
		std::shared_ptr<RNBO::CoreObject> mCore;

		//parameter index -> (node and optional int -> string for enum lookups)
		std::map<RNBO::ParameterIndex, std::pair<ossia::net::parameter_base*, boost::optional<std::unordered_map<int, std::string>>>> mIndexToParam;

		ossia::net::parameter_base* mActiveParam;
		ossia::net::parameter_base* mMIDIOutParam;

		//queue for loading or unloading data refs
		Queue<DataRefCommand> mDataRefCommandQueue;
		//queue for moving dataref deallocs out of audio thread
		std::unique_ptr<moodycamel::ReaderWriterQueue<std::shared_ptr<std::vector<float>>, 32>> mDataRefCleanupQueue;

		//only accessed in the data ref thread
		std::unordered_map<std::string, std::shared_ptr<std::vector<float>>> mDataRefs;
		std::thread mDataRefThread;
		std::atomic<bool> mDataRefProcessCommands;
		//keep the parameters so we can clear out when files don't exist
		std::unordered_map<std::string, ossia::net::parameter_base *> mDataRefNodes;


		//map of dataref name to file name
		std::mutex mDataRefFileNameMutex;
		std::unordered_map<std::string, std::string> mDataRefFileNameMap;

		//presets
		ossia::net::parameter_base * mPresetEntries;
		std::map<std::string, RNBO::ConstPresetPtr> mPresets;
		std::mutex mPresetMutex;
		std::string mPresetLatest; //the most recently loaded preset
		std::string mPresetInitial; //the user indicated initial preset
		ossia::net::parameter_base* mPresetInitialParam;
		ossia::net::parameter_base* mPresetProgramChangeChannelParam;
		int mPresetProgramChangeChannel = 0; //omni, 17 == none

		Queue<PresetCommand> mPresetCommandQueue;

		//callback data from RNBO, a name and a ptr
		std::unique_ptr<moodycamel::ReaderWriterQueue<std::pair<std::string, RNBO::ConstPresetPtr>, 2>> mPresetSavedQueue;

		//simply the names of imports and outports, for building up OSCQuery and seralization
		std::vector<std::string> mInportTags;
		std::unordered_map<std::string, ossia::net::parameter_base *> mOutportParams;
};
