#include "DataHandler.h"
#include <iostream>

using RNBO::DataRefIndex;
using RNBO::ConstRefList;
using RNBO::UpdateRefCallback;
using RNBO::ReleaseRefCallback;

RunnerExternalDataHandler::RunnerExternalDataHandler() :
	mDataRequest(DataCaptureQueueLength),
	mDataResponse(DataCaptureQueueLength)
{
}

RunnerExternalDataHandler::~RunnerExternalDataHandler() {
	//clear out queues
	std::unique_lock<std::mutex> lock(mMutex);
	DataCaptureData * data = nullptr;
	while (mDataResponse.try_dequeue(data)) {
		delete data;
	}
	while (mDataRequest.try_dequeue(data)) {
		delete data;
	}
}


void RunnerExternalDataHandler::processBeginCallback(DataRefIndex /*numRefs*/, ConstRefList /*refList*/, UpdateRefCallback /*updateDataRef*/, ReleaseRefCallback /*releaseDataRef*/)
{
	//DO NOTHING
}

void RunnerExternalDataHandler::processEndCallback(DataRefIndex numRefs, ConstRefList refList)
{
	DataCaptureData * req;
	while (mDataRequest.try_dequeue(req)) {
		bool found = false;
		for (DataRefIndex i = 0; i < numRefs; i++) {
			auto ref = refList[i];
			if (std::strcmp(ref->getMemoryId(), req->datarefId.c_str()) == 0) {
				found = true;
				switch (req->state) {
					case DataCaptureState::Read:
						if (req->srcData == ref->getData() && req->data.size() == req->sizeinbytes && req->datatype == ref->getType()) {
							size_t offset = req->bytesread;
							size_t read = std::min(mChunkBytes, req->data.size() - offset);
							std::memcpy(req->data.data() + offset, ref->getData() + offset, read);
							req->bytesread += read;
							break;
						} else {
							//data changed
							req->state = DataCaptureState::GetInfo;
						}
						//INTENTIONAL drop into next case
					case DataCaptureState::GetInfo:
						req->sizeinbytes = ref->getSizeInBytes();
						req->datatype = ref->getType();
						req->srcData = ref->getData();
						break;
					case DataCaptureState::Error:
						break;
				}
			}
		}

		if (!found) {
			req->state = DataCaptureState::Error;
		}

		//what to do if fails?
		mDataResponse.try_enqueue(req);
	}
}

void RunnerExternalDataHandler::capture(std::string datarefId, DataCaptureCallback callback) {
	DataCaptureData * req = new DataCaptureData(datarefId);
	if (mDataRequest.try_enqueue(req)) {
		std::unique_lock<std::mutex> lock(mMutex);
		mCallbacks[datarefId] = callback; //what if something already exists at this location?
	} else {
		delete req;
	}
}

void RunnerExternalDataHandler::processEvents()
{
	DataCaptureData * resp = nullptr;
	while (mDataResponse.try_dequeue(resp)) {
		bool cleanup = false;

		switch (resp->state) {
			case DataCaptureState::Error:
				std::cerr << "error getting data for datarefid: " << resp->datarefId << std::endl;
				cleanup = true;
				break;
			case DataCaptureState::GetInfo:
				if (resp->sizeinbytes == 0) {
					cleanup = true; //should this happen?
				} else {
					//alloc data needed to copy
					resp->data.resize(resp->sizeinbytes);
					resp->state = DataCaptureState::Read;
					resp->bytesread = 0;
				}
				break;
			case DataCaptureState::Read:
				if (resp->bytesread >= resp->data.size()) {
					cleanup = true;
					//do callback
					std::unique_lock<std::mutex> lock(mMutex);
					auto it = mCallbacks.find(resp->datarefId);
					if (it != mCallbacks.end()) {
						it->second(resp->datarefId, resp->datatype, std::move(resp->data));
						mCallbacks.erase(it);
					}
				}
				//otherwise keep reading
				break;
		}

		if (cleanup) {
			delete resp;
		} else if (!mDataRequest.try_enqueue(resp)) {
			std::cerr << "failed to write back request" << std::endl;
			delete resp;
		}
	}
}
