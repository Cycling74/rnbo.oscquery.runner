#pragma once

#include <RNBO.h>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <readerwriterqueue/readerwriterqueue.h>
#include <concurrentqueue/concurrentqueue.h>

using DataCaptureCallback = std::function<void(std::string datarefId, RNBO::DataType datatype, std::vector<char>&& data)>;

const size_t DataCaptureQueueLength = 32;

enum class DataCaptureState {
	GetInfo,
	Read,
	Error
};

struct DataCaptureData {
	DataCaptureData(std::string id) : datarefId(id) {}

	std::string datarefId;
	DataCaptureState state = DataCaptureState::GetInfo;

	char * srcData = nullptr; //to use as a reference to make sure that the data hasn't changed
	size_t sizeinbytes = 0;
	RNBO::DataType datatype;

	std::vector<char> data;
	size_t bytesread = 0;
};

//setup to serialize data out of RNBO to a file
class RunnerExternalDataHandler : public RNBO::ExternalDataHandler {
	public:
		RunnerExternalDataHandler();
		~RunnerExternalDataHandler();

		virtual void processBeginCallback(RNBO::DataRefIndex numRefs, RNBO::ConstRefList refList, RNBO::UpdateRefCallback updateDataRef, RNBO::ReleaseRefCallback releaseDataRef) override;
		virtual void processEndCallback(RNBO::DataRefIndex numRefs, RNBO::ConstRefList refList) override; 

		void capture(std::string datarefId, DataCaptureCallback callback);
		//how may bytes to read per Read request
		void chunkSize(size_t bytes) { mChunkBytes = bytes; }

		void processEvents();
	private:
		std::mutex mMutex; //only non blocking locking in process thread
		std::unordered_map<std::string, DataCaptureCallback> mCallbacks;
		size_t mChunkBytes = 256;

		moodycamel::ConcurrentQueue<DataCaptureData *> mDataRequest; // into process
		moodycamel::ReaderWriterQueue<DataCaptureData *, DataCaptureQueueLength> mDataResponse; // from process
};
