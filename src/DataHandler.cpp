#include "DataHandler.h"

#include <iostream>
#include <atomic>

#include <sndfile.hh>

#include <boost/optional.hpp>
#include <boost/none.hpp>

//shm
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace fs = boost::filesystem;

using RNBO::DataRefIndex;
using RNBO::ConstRefList;
using RNBO::UpdateRefCallback;
using RNBO::ReleaseRefCallback;

class ShmMemData {
	public:
		ShmMemData(uint8_t * data, size_t size, std::string name) : mData(data), mSize(size), mName(name) {
		}
		~ShmMemData() {
			munmap(mData, mSize);
			shm_unlink(mName.c_str());
		}

		uint8_t * mData;
		size_t mSize;
		std::string mName;
};

class MapData {
	public:
		MapData(RNBO::Index index, std::string id): datarefIndex(index), datarefId(id) { 
			//std::cout << "created MapData " << this << std::endl;
		}
		~MapData() { 
			//std::cout << "destroy MapData " << this << std::endl;
		}

		RNBO::Index datarefIndex;
		std::string datarefId;

		RNBO::DataType datatype;
		char * data = nullptr; //points to one of the front of audio data, bytedata or memory mapped shared memory, null means unmap
		size_t sizeinbytes = 0;

		std::shared_ptr<std::vector<float>> audiodata;
		std::shared_ptr<std::vector<uint8_t>> bytedata;
		std::shared_ptr<ShmMemData> shmdata;

		void copyData(const std::shared_ptr<MapData> src) {
			audiodata = src->audiodata;
			bytedata = src->bytedata;
			shmdata = src->shmdata;

			datatype = src->datatype;
			data = src->data;
			sizeinbytes = src->sizeinbytes;
		}
};

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

struct DataRefInfo {
	bool system = false;
	std::string share_name;
	std::string observe_name;
	size_t len = 0;

	RNBO::DataType datatype;
};

namespace {
	std::mutex GlobalMutex;
#ifdef DATAHANDLER_SHARED_WEAK
	//week doesn't work as we'd expect
	std::unordered_map<std::string, std::weak_ptr<MapData>> SharedMappings;
#else
	std::unordered_map<std::string, std::shared_ptr<MapData>> SharedMappings;
#endif
	std::unordered_map<uintptr_t, std::function<void(const std::string&)>> SharedMappingCallbacks;

	void update_shared(const std::string& key, std::shared_ptr<MapData> mapping) {
		std::unique_lock<std::mutex> lock(GlobalMutex);
		SharedMappings[key] = mapping;

		//broadcast change
		for (auto it: SharedMappingCallbacks) {
			it.second(key);
		}
	}

	void remove_shared(const std::string& key) {
		std::unique_lock<std::mutex> lock(GlobalMutex);
		SharedMappings.erase(key);
		//TODO do we want to broadcast that this is now empty?
	}

	boost::optional<std::shared_ptr<MapData>> get_shared(const std::string& key) {
		std::unique_lock<std::mutex> lock(GlobalMutex);
		auto it = SharedMappings.find(key);
		if (it != SharedMappings.end()) {
#ifdef DATAHANDLER_SHARED_WEAK
			if (auto p = it->second.lock()) {
				return p;
			}
#else
			return it->second;
#endif
		}
		return boost::none;
	}

	void register_callback(const RunnerExternalDataHandler* key, std::function<void(const std::string&)> cb) {
		std::unique_lock<std::mutex> lock(GlobalMutex);
		SharedMappingCallbacks[reinterpret_cast<uintptr_t>(key)] = cb;
	}

	void unregister_callback(const RunnerExternalDataHandler* key) {
		std::unique_lock<std::mutex> lock(GlobalMutex);
		SharedMappingCallbacks.erase(reinterpret_cast<uintptr_t>(key));
	}
}

RunnerExternalDataHandler::RunnerExternalDataHandler(const std::vector<std::string>& datarefIds, const RNBO::Json& datarefDesc) :
	mDataRequest(datarefIds.size()),
	mDataResponse(datarefIds.size()),
	mMapRequest(datarefIds.size()),
	mMapResponse(datarefIds.size()),
	mSharedRefChanged(16) //how many shared refs might we have?
{
	//TODO extract data types

	for (RNBO::Index i = 0; i < datarefIds.size(); i++) {
		auto id = datarefIds[i];
		auto cur = std::make_shared<MapData>(i, id);
		mMappings.emplace_back(cur);
		mProcessMappings.emplace_back(cur);
		mIndexLookup.insert({id, i});
	}

	register_callback(this, std::bind(&RunnerExternalDataHandler::handleSharedMappingChange, this, std::placeholders::_1));
}

RunnerExternalDataHandler::~RunnerExternalDataHandler() {
	unregister_callback(this);

	//clear out queues
	std::unique_lock<std::mutex> lock(mMutex);
	std::shared_ptr<DataCaptureData> data;
	while (mDataResponse.try_dequeue(data)) {
		//drop
	}
	while (mDataRequest.try_dequeue(data)) {
		//drop
	}

	//remove stuff we're sharing
	for (auto it: mSharing) {
		remove_shared(it.second);
	}
}


void RunnerExternalDataHandler::processBeginCallback(DataRefIndex numRefs, ConstRefList refList, UpdateRefCallback updateDataRef, ReleaseRefCallback releaseDataRef)
{
	//skip requests for 1 process cycle to avoid our dataref events being stomped on by internal dataref events
	if (mHasRunProcess) {
		//do updates
		std::shared_ptr<MapData> req;
		while (mMapRequest.try_dequeue(req)) {
			auto i = req->datarefIndex;
			auto ref = refList[i];
			auto cur = mProcessMappings[i];
			//TODO extract RNBO allocated data
			if (ref->getData() != req->data) {
				if (req->data == nullptr) {
					releaseDataRef(i);
				} else {
					updateDataRef(i, static_cast<char *>(req->data), req->sizeinbytes, req->datatype);
				}
				mProcessMappings[i] = req;
				req = cur; //return old data
			} else {
				//anything to do here?
			}

			mMapResponse.try_enqueue(req);
		}
	}
}

void RunnerExternalDataHandler::processEndCallback(DataRefIndex numRefs, ConstRefList refList)
{
	/*
	for (auto i = 0; i < numRefs; i++) {
		auto ref = refList[i];
		auto cur = mProcessMappings[i];
		if (ref->getData() != cur->data) {
			std::cout << "data mismatch" << std::endl;
		}
	}
	*/

	std::shared_ptr<DataCaptureData> req;
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
	mHasRunProcess = true;
}

void RunnerExternalDataHandler::capture(std::string datarefId, DataCaptureCallback callback) {
	std::shared_ptr<DataCaptureData> req = std::make_shared<DataCaptureData>(datarefId);
	if (mDataRequest.try_enqueue(req)) {
		std::unique_lock<std::mutex> lock(mMutex);
		mCallbacks[datarefId] = callback; //what if something already exists at this location?
	} else {
		//error, drop
	}
}

void RunnerExternalDataHandler::processEvents(std::function<void(std::string, std::string)> shmcb)
{
	{
		std::shared_ptr<MapData> resp;
		while (mMapResponse.try_dequeue(resp)) {
			//TODO
		}
	}

	{
		std::shared_ptr<DataCaptureData> resp;
		while (mDataResponse.try_dequeue(resp)) {
			bool cleanup = false;

			switch (resp->state) {
				case DataCaptureState::Error:
					std::cerr << "error getting data for datarefid: " << resp->datarefId << std::endl;
					cleanup = true;
					break;
				case DataCaptureState::GetInfo:
					if (resp->sizeinbytes == 0 || resp->srcData == nullptr) {
						cleanup = true;
						std::cerr << "capture requested buffer " << resp->datarefId << " is empty, ignoring" << std::endl;
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
						if (resp->bytesread > 0) {
							//do callback
							std::unique_lock<std::mutex> lock(mMutex);
							auto it = mCallbacks.find(resp->datarefId);
							if (it != mCallbacks.end()) {
								it->second(resp->datarefId, resp->datatype, std::move(resp->data));
								mCallbacks.erase(it);
							}
						}
					}
					//otherwise keep reading
					break;
			}

			if (!cleanup && !mDataRequest.try_enqueue(resp)) {
				std::cerr << "failed to write back request" << std::endl;
				//drop
			}
		}
	}
	{
		std::string key;
		while (mSharedRefChanged.try_dequeue(key)) {
			std::shared_ptr<MapData> req; //move req out of lock to not deadlock when storeAndRequest
			{
				std::unique_lock<std::mutex> lock(mMutex);

				//update any found observers
				auto observers = mObservers.find(key);
				if (observers != mObservers.end()) {
					auto shared = get_shared(key);
					for (auto datarefId: observers->second) {
						auto it = mIndexLookup.find(datarefId);
						if (it == mIndexLookup.end()) {
							std::cerr << "failed to find dataref with id " << datarefId << std::endl;
							continue;
						}

						req = std::make_shared<MapData>(it->second, datarefId);
						if (shared) {
							//TODO verify that datatype matches
							req->copyData(shared.get());
						}
					}
				}
			}
			if (req) {
				storeAndRequest(req);
			}
		}
	}
}

bool RunnerExternalDataHandler::load(const std::string& datarefId, const fs::path& filePath)
{
	RNBO::Index index;

	//TODO handle share, observe and shmem
	//TODO make sure this is an audio buffer
	{
		auto it = mIndexLookup.find(datarefId);
		if (it == mIndexLookup.end()) {
			std::cerr << "failed to find dataref with id " << datarefId << std::endl;
			return false;
		}
		index = it->second;
	}

	{
		std::unique_lock<std::mutex> lock(mMutex);
		if (mObserving.find(index) != mObserving.end()) {
			//don't load over observing, though what if we have a rnbo alloc thing in there??
			return false;
		}
	}

	std::shared_ptr<MapData> req = std::make_shared<MapData>(index, datarefId);

	if (filePath.empty()) {
		{
			std::unique_lock<std::mutex> lock(mMutex);
			auto sharing = mSharing.find(index);
			if (sharing != mSharing.end()) {
				update_shared(sharing->second, req);
			}
		}
		storeAndRequest(req);
		return true;
	}
	try {
		if (!fs::exists(filePath)) {
			std::cerr << "no file at " << filePath << std::endl;
			//TODO clear node value?
			return false;
		}
		SndfileHandle sndfile(filePath.string());
		if (!sndfile) {
			std::cerr << "couldn't open as sound file " << filePath << std::endl;
			//TODO clear node value?
			return false;
		}

		//sanity check
		if (sndfile.channels() < 1 || sndfile.samplerate() < 1.0 || sndfile.frames() < 1) {
			std::cerr << "sound file needs to have samplerate, frames and channels greater than zero " << filePath.string() <<
				" samplerate: " << sndfile.samplerate() <<
				" channels: " << sndfile.channels() <<
				" frames: " << sndfile.frames() <<
				std::endl;
			return false;
		}

		std::shared_ptr<std::vector<float>> data;
		sf_count_t framesRead = 0;

		//actually read in audio and set the data
		//Some formats have an unknown frame size, so we have to read a bit at a time
		if (sndfile.frames() < SF_COUNT_MAX) {
			data = std::make_shared<std::vector<float>>(static_cast<size_t>(sndfile.channels()) * static_cast<size_t>(sndfile.frames()));
			framesRead = sndfile.readf(&data->front(), sndfile.frames());
		} else {
			const sf_count_t framesToRead = static_cast<sf_count_t>(sndfile.samplerate());
			//blockSize, offset, offsetIncr are in samples, not frames
			const auto blockSize = static_cast<size_t>(sndfile.channels()) * static_cast<size_t>(framesToRead);
			size_t offset = 0;
			size_t offsetIncr = framesToRead * sndfile.channels();
			sf_count_t read = 0;
			//reserve 5 seconds of space
			data = std::make_shared<std::vector<float>>(blockSize * 5);
			do {
				data->resize(offset + blockSize);
				read = sndfile.readf(&data->front() + offset, framesToRead);
				framesRead += read;
				offset += offsetIncr;
			} while (read == framesToRead);
		}

		if (framesRead == 0) {
			std::cerr << "read zero frames from " << filePath.string() << std::endl;
			return false;
		}

		RNBO::Float32AudioBuffer bufferType(sndfile.channels(), static_cast<double>(sndfile.samplerate()));

		req->data = reinterpret_cast<char *>(&data->front());
		req->sizeinbytes = sizeof(float) * framesRead * sndfile.channels();
		req->datatype = bufferType;
		req->audiodata = std::move(data);

		{
			std::unique_lock<std::mutex> lock(mMutex);
			auto sharing = mSharing.find(index);
			if (sharing != mSharing.end()) {
				update_shared(sharing->second, req);
			}
		}
		storeAndRequest(req);

		return true;
	} catch (std::exception& e) {
		std::cerr << "exception reading data ref file: " << e.what() << std::endl;
	}
	return false;
}

void RunnerExternalDataHandler::unload(const std::string& datarefId)
{	
	auto it = mIndexLookup.find(datarefId);
	if (it == mIndexLookup.end()) {
		std::cerr << "failed to find dataref with id " << datarefId << std::endl;
		return;
	}

	std::shared_ptr<MapData> req = std::make_shared<MapData>(it->second, datarefId);
	storeAndRequest(req); //empty
}

bool RunnerExternalDataHandler::handleMeta(const std::string& datarefId, const RNBO::Json& meta)
{
	RNBO::Index index;

	{
		auto it = mIndexLookup.find(datarefId);
		if (it == mIndexLookup.end()) {
			std::cerr << "failed to find dataref with id " << datarefId << std::endl;
			return false;
		}
		index = it->second;
	}

	std::string sharekey_next;
	std::string observekey_next;

	if (meta.is_object()) {
		if (meta.contains("share") && meta["share"].is_string()) {
			sharekey_next = meta["share"].get<std::string>();
		} else if (meta.contains("observe") && meta["observe"].is_string()) { //cannot observe AND share
			observekey_next = meta["observe"].get<std::string>();
		}

		if (meta.contains("system")) {
			//TODO
		}
	}

	std::shared_ptr<MapData> req;
	{
		std::unique_lock<std::mutex> lock(mMutex);

		std::string sharekey_cur;
		std::string observekey_cur;

		auto sharing = mSharing.find(index);
		if (sharing != mSharing.end()) {
			sharekey_cur = sharing->second;
		}

		auto observing = mObserving.find(index);
		if (observing != mObserving.end()) {
			observekey_cur = observing->second;
		}


		if (sharekey_next != sharekey_cur) {
			auto mapping = mMappings[index];

			//cleanup
			if (sharekey_cur.size()) {
				remove_shared(sharekey_cur);
			}

			if (sharekey_next.size()) {
				mSharing[index] = sharekey_next;
				if (auto p = mapping.lock()) {
					//should broadcast update
					update_shared(sharekey_next, p);
				} else {
					remove_shared(sharekey_next);
				}
			} else {
				mSharing.erase(index);
			}
			//is there mObservers cleanup to do??
		}

		if (observekey_next != observekey_cur) {
			if (observekey_next.size()) {
				mObserving[index] = observekey_next;
			} else {
				mObserving.erase(index);
			}

			//update observer sets
			if (observekey_cur.size()) {
				auto it = mObservers.find(observekey_cur);
				if (it != mObservers.end()) {
					it->second.erase(datarefId);
				}
			}
			if (observekey_next.size()) {
				auto it = mObservers.find(observekey_next);
				if (it != mObservers.end()) {
					it->second.insert(datarefId);
				} else {
					mObservers[observekey_next] = { datarefId };
				}

				//try to get shared data
				req = std::make_shared<MapData>(index, datarefId);
				auto o = get_shared(observekey_next);
				if (o) {
					req->copyData(o.get());
				}
			}
		}
	}
	if (req) {
		storeAndRequest(req);
		return true;
	} else {
		return false;
	}
}

void RunnerExternalDataHandler::handleSharedMappingChange(const std::string& key)
{
	//global lock held, wait for processEvents
	mSharedRefChanged.enqueue(key);
}

void RunnerExternalDataHandler::storeAndRequest(std::shared_ptr<MapData> mapping) {
	std::unique_lock<std::mutex> lock(mMutex);
	mMappings[mapping->datarefIndex] = mapping; //keep a weak copy
	mMapRequest.enqueue(mapping);
}
