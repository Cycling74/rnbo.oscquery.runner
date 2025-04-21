//An instance of a RNBO patcher.

#pragma once

#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <set>

#include <boost/optional.hpp>
#include <boost/filesystem.hpp>

#include <ossia/network/value/value.hpp>

#include "RNBO.h"
#include "EventHandler.h"
#include "InstanceAudio.h"
#include "Defines.h"
#include "Queue.h"
#include "DB.h"
#include "MIDIMap.h"

class PatcherFactory;
namespace moodycamel {
template<typename T, size_t MAX_BLOCK_SIZE>
class ReaderWriterQueue;
}

class RunnerExternalDataHandler;
class ProcessAudio;

using OSCCallback = std::function<void(const std::string& addr, const ossia::value& v)>;
using OSCRegisterCallback = std::function<void(bool doregister, const std::string& oscaddr, const std::string& localaddr)>;

class Instance {
	public:
		Instance(
				std::shared_ptr<DB> db, 
				std::shared_ptr<PatcherFactory> factory, 
				std::string name, 
				NodeBuilder builder, 
				RNBO::Json conf, 
				std::shared_ptr<ProcessAudio> processAudio, 
				unsigned int index,
				OSCCallback oscCallback,
				OSCRegisterCallback oscRegisterCallback
				);
		~Instance();

		unsigned int index() const { return mIndex; }
		const std::string& name() const { return mName; }

		void activate();
		void connect();
		void start(float fadems = 10.0);
		void stop(float fadems = 10.0);

		AudioState audioState();

		//process any events in the current thread
		void processEvents();

		void savePreset(std::string name, std::string set_name = std::string());
		//returns true if the preset was found and load is being attempted
		bool loadPreset(std::string name, std::string set_name = std::string());
		void loadPreset(unsigned int index);

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
		void registerPresetLoadedCallback(std::function<void(const std::string& presetName, const std::string& setName)> cb);

		bool presetsDirty() {
			auto dirty = mPresetsDirty;
			mPresetsDirty = false;
			return dirty;
		}

		//must only be called from event thread
		void presetsUpdateMarkClean() {
			mPresetsDirty = false;
			updatePresetEntries();
		}
		void markConfigChanged(bool v) {
			mConfigChanged = v;
		}
	private:
		bool mPresetsDirty = false;

		bool loadJsonPreset(const std::string& content, std::string name, std::string setPresetName = std::string());
		//stored parameter meta
		std::function<void()> mConfigChangeCallback = nullptr;
		std::function<void(const std::string&, const std::string&)> mPresetLoadedCallback = nullptr;

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
				Save,
				Rename
			};
			CommandType type;
			std::string preset;
			std::string newname;
			PresetCommand(CommandType t, std::string p, std::string n = std::string()) : type(t), preset(p), newname(n) {}
		};

		struct MetaUpdateCommand {
			enum class Subject {
				Param,
				Inport,
				Outport,
				DataRef
			};

			ossia::net::node_base * node = nullptr; //"meta" node (and children)
			ossia::net::parameter_base * param = nullptr; //"meta" string value param

			Subject subject = Subject::Param;
			std::string messageTag;
			RNBO::ParameterIndex paramIndex = 0;
			std::string meta;
			MetaUpdateCommand(
				ossia::net::node_base * n,
				ossia::net::parameter_base * p,
				RNBO::ParameterIndex index, std::string m) :
				node(n), param(p),
				subject(Subject::Param), paramIndex(index), meta(m) { }
			MetaUpdateCommand(
				ossia::net::node_base * n,
				ossia::net::parameter_base * p,
				Subject s,
				std::string tag, std::string m) :
				node(n), param(p),
				subject(s), messageTag(tag), meta(m) { }
		};

		struct ParamOSCUpdateData {
			std::shared_ptr<std::mutex> mutex; //mutex to avoid infinite recursion
			std::shared_ptr<std::mutex> oscmutex;
			ossia::net::parameter_base * param = nullptr;
			ossia::net::parameter_base * normparam = nullptr;
			std::string oscaddr;

			//map between string enum value and numeric values, only used for enum params
			std::unordered_map<std::string, RNBO::ParameterValue> nameToVal;
			std::unordered_map<int, std::string> valToName;

			//should params map to/from normalized version?
			bool usenormalized = false;

			ParamOSCUpdateData();
			void push_osc(ossia::value val, float normval, OSCCallback cb);
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
		//serialize dataref to file
		void saveDataref(std::string id, boost::filesystem::path dir, std::string filenameTempl);

		void handleInportMessage(RNBO::MessageTag tag, const ossia::value& value);
		void handleOutportMessage(RNBO::MessageEvent e);
		void handleMidiCallback(RNBO::MidiEvent e);

		void handleParamUpdate(RNBO::ParameterIndex index, RNBO::ParameterValue value);
		void handlePresetEvent(const RNBO::PresetEvent& e);

		void handleMetadataUpdate(MetaUpdateCommand update);

		void handleEnumParamOscUpdate(RNBO::ParameterIndex index, const ossia::value& val);
		void handleFloatParamOscUpdate(RNBO::ParameterIndex index, const ossia::value& val);
		void handleNormalizedFloatParamOscUpdate(RNBO::ParameterIndex index, const ossia::value& val);

		std::unique_ptr<InstanceAudio> mAudio;
		std::unique_ptr<EventHandler> mEventHandler;
		std::shared_ptr<PatcherFactory> mPatcherFactory;
		std::shared_ptr<RNBO::CoreObject> mCore;
		RNBO::ParameterEventInterfaceUniquePtr mParamInterface;


		//parameter index -> update data
		std::map<RNBO::ParameterIndex, ParamOSCUpdateData> mIndexToParam;

		ossia::net::parameter_base* mActiveParam;
		ossia::net::parameter_base* mMIDIOutParam;

		//queue for loading or unloading data refs
		Queue<DataRefCommand> mDataRefCommandQueue;
		//queue for moving dataref deallocs out of audio thread
		std::unique_ptr<moodycamel::ReaderWriterQueue<std::shared_ptr<std::vector<float>>, 32>> mDataRefCleanupQueue;
		//preset name, preset ptr, set name (maybe empty)
		std::unique_ptr<moodycamel::ReaderWriterQueue<std::tuple<std::string, RNBO::ConstPresetPtr, std::string>, 32>> mPresetSaveQueue;

		//only accessed in the data ref thread
		std::unordered_map<std::string, std::shared_ptr<std::vector<float>>> mDataRefs;
		std::thread mDataRefThread;
		std::atomic<bool> mDataRefProcessCommands;
		//keep the parameters so we can clear out when files don't exist
		std::unordered_map<std::string, ossia::net::parameter_base *> mDataRefNodes;

		Queue<MetaUpdateCommand> mMetaUpdateQueue;

		std::mutex mMIDIMapMutex;
		std::unordered_map<uint16_t, std::set<RNBO::ParameterIndex>> mParamMIDIMap; //ParamMIDIMap::key() -> [parameter index, param index]
		std::unordered_map<RNBO::ParameterIndex, uint16_t> mParamMIDIMapLookup; //reverse Lookup of above, no need for mutex as this is only accessed in meta map thread
		std::unordered_map<uint16_t, std::set<RNBO::MessageTag>> mInportMIDIMap; //ParamMIDIMap::key() -> [inport tag, inport tag]
		std::unordered_map<RNBO::MessageTag, uint16_t> mInportMIDIMapLookup; //reverse Lookup of above, no need for mutex as this is only accessed in meta map thread

		ossia::net::parameter_base * mMIDILastParam; //for mapping
		bool mMIDILastReport = false; //if we publish to the above param, it is pretty noisy otherwise

		//name -> value (if any)
		std::unordered_map<std::string, std::string> mInportMetaDefault;
		std::unordered_map<std::string, std::string> mOutportMetaDefault;
		std::unordered_map<RNBO::ParameterIndex, std::string> mParamMetaDefault;
		std::unordered_map<std::string, std::string> mDataRefMetaDefault;

		//mappings that aren't default, to be stored with configuration
		std::mutex mMetaMapMutex;
		std::unordered_map<std::string, std::string> mInportMetaMapped;
		std::unordered_map<std::string, std::string> mOutportMetaMapped;
		std::unordered_map<RNBO::ParameterIndex, std::string> mParamMetaMapped;
		std::unordered_map<RNBO::ParameterIndex, ossia::net::parameter_base *> mParamMetaParams;
		std::unordered_map<std::string, std::string> mDataRefMetaMapped;

		ossia::net::node_base * mOSCRoot = nullptr;
		OSCCallback mOSCCallback; //for outgoing osc messages
		OSCRegisterCallback mOSCRegisterCallback; //for incoming osc messages

		//functions to run when we clear out OSC mapping
		//cleanupKey -> function
		std::unordered_map<std::string, std::function<void()>> mMetaCleanup;

		//map of dataref name to file name
		std::mutex mDataRefFileNameMutex;
		std::unordered_map<std::string, std::string> mDataRefFileNameMap;

		//presets
		ossia::net::parameter_base * mPresetEntries;
		std::mutex mPresetMutex;

		std::string mPresetInitial; //the user indicated initial preset
		std::string mPresetNameLatest; //the most recently loaded preset name
		std::string mSetPresetNameLatest; //the most recently loaded set preset name

		ossia::net::parameter_base* mPresetInitialParam = nullptr;
		ossia::net::parameter_base* mPresetLoadedParam = nullptr;
		ossia::net::parameter_base* mPresetProgramChangeChannelParam;
		int mPresetProgramChangeChannel = 0; //omni, 17 == none

		Queue<PresetCommand> mPresetCommandQueue;

		std::unordered_map<std::string, ossia::net::parameter_base *> mOutportParam;
		std::unordered_map<std::string, std::string> mOutportOSCMap; //outport tag -> osc addr that should get sent

		RNBO::Json mConfig;
		unsigned int mIndex = 0;

		std::string mName;
		std::shared_ptr<DB> mDB;

		//do we use a preset name instead of preset content when saving set presets
		bool mSetPresetPatcherNamed = false;

		ossia::net::parameter_base * mNameAliasParam = nullptr;

		std::unique_ptr<RunnerExternalDataHandler> mDataHandler;
};
