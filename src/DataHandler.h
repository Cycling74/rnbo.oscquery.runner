#pragma once

#include <RNBO.h>

#include <mutex>
#include <functional>
#include <set>
#include <unordered_map>

#include <readerwriterqueue/readerwriterqueue.h>
#include <concurrentqueue/concurrentqueue.h>

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include <boost/none.hpp>

using DataCaptureCallback = std::function<void(std::string datarefId, RNBO::DataType datatype, std::vector<char>&& data)>;

struct DataCaptureData;
class MapData;
class DataRefInfo;
class DataTypeInfo;

//setup to serialize data out of RNBO to a file
class RunnerExternalDataHandler : public RNBO::ExternalDataHandler {
	public:
		RunnerExternalDataHandler(const std::vector<std::string>& datarefIds, const RNBO::Json& datarefDesc);
		~RunnerExternalDataHandler();

		virtual void processBeginCallback(RNBO::DataRefIndex numRefs, RNBO::ConstRefList refList, RNBO::UpdateRefCallback updateDataRef, RNBO::ReleaseRefCallback releaseDataRef) override;
		virtual void processEndCallback(RNBO::DataRefIndex numRefs, RNBO::ConstRefList refList) override;

		bool load(const std::string& datarefId, const boost::filesystem::path& filePath);
		void unload(const std::string& datarefId);

		//returns "clear" if the dataref is observing
		//returns "reset" if the dataref was system and no longer is
		//returns shared memory filename if the dataref loaded sharedmemory
		//returns empty if neither of these
		std::string handleMeta(const std::string& datarefId, const RNBO::Json& meta);

		void capture(std::string datarefId, DataCaptureCallback callback);
		//how may bytes to read per Read request
		void chunkSize(size_t bytes) { mChunkBytes = bytes; }

		void processEvents(std::function<void(std::string, std::string)> shmcb = nullptr);

		void handleSharedMappingChange(const std::string& key);
	private:
		bool mHasRunProcess = false; //bug workaround
		void storeAndRequest(std::shared_ptr<MapData> mapping);
		RNBO::Index get_index(const std::string& datarefId) const;
		std::shared_ptr<MapData> make_map(const std::string& datarefId, boost::optional<RNBO::Index> index = boost::none) const;

		std::mutex mMutex;
		std::unordered_map<std::string, DataCaptureCallback> mCallbacks;
		size_t mChunkBytes = 256;

		std::vector<std::shared_ptr<MapData>> mProcessMappings;
		std::vector<std::weak_ptr<MapData>> mMappings;
		std::vector<DataTypeInfo> mDataTypeDataBytes;

		std::unordered_map<std::string, RNBO::Index> mIndexLookup;

		moodycamel::ConcurrentQueue<std::shared_ptr<DataCaptureData>> mDataRequest; // into process
		moodycamel::ReaderWriterQueue<std::shared_ptr<DataCaptureData>> mDataResponse; // from process
																																														
		moodycamel::ConcurrentQueue<std::shared_ptr<MapData>> mMapRequest; // into process
		moodycamel::ReaderWriterQueue<std::shared_ptr<MapData>> mMapResponse; // from process
																																					
		//get info from the process side
		moodycamel::ConcurrentQueue<std::shared_ptr<DataRefInfo>> mInfoRequest; // into process
		moodycamel::ReaderWriterQueue<std::shared_ptr<DataRefInfo>> mInfoResponse; // from process
																																				
		moodycamel::ConcurrentQueue<std::string> mSharedRefChanged; //from various threads into processEvents

		std::unordered_map<std::string, std::set<std::string>> mObservers; //shared key -> datarefId
		std::unordered_map<std::string, std::string> mShared; //datarefId -> shared key
																													
		//index -> key
		std::unordered_map<RNBO::Index, std::string> mObserving;
		std::unordered_map<RNBO::Index, std::string> mSharing;
		std::set<RNBO::Index> mSystem;
};
