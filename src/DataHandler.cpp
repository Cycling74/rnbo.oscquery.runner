#include "DataHandler.h"

#include <iostream>
#include <atomic>

#include <sndfile.hh>

//shm
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/lexical_cast.hpp>

namespace fs = boost::filesystem;

using RNBO::DataRefIndex;
using RNBO::ConstRefList;
using RNBO::UpdateRefCallback;
using RNBO::ReleaseRefCallback;

class ShmMemData {
	public:
		ShmMemData(char * data, size_t size, std::string name) : mData(data), mSize(size), mName(name) {
		}
		~ShmMemData() {
			munmap(mData, mSize);
			shm_unlink(mName.c_str());
		}

		char * mData;
		size_t mSize;
		std::string mName;
};

class MapData {
	public:
		MapData(RNBO::Index index, std::string id, DataTypeInfo info): datarefIndex(index), datarefId(id), typeinfo(info) { 
			//std::cout << "created MapData " << this << std::endl;
		}
		~MapData() { 
			//std::cout << "destroy MapData " << this << std::endl;
		}

		RNBO::Index datarefIndex;
		std::string datarefId;
		DataTypeInfo typeinfo;

		RNBO::DataType datatype;
		char * data = nullptr; //points to one of the front of audio data, bytedata or memory mapped shared memory, null means unmap
		size_t sizeinbytes = 0;

		bool compatible(const std::shared_ptr<MapData>& other) const {
			return typeinfo == other->typeinfo;
		}

		bool matches(const RNBO::DataType& other) const {
			return datatype.matches(other);
		}

		std::shared_ptr<std::vector<float>> audiodata32;
		std::shared_ptr<std::vector<double>> audiodata64;
		std::shared_ptr<std::vector<uint8_t>> bytedata;
		std::shared_ptr<ShmMemData> shmdata;

		void copyData(const std::shared_ptr<MapData> src) {
			audiodata32 = src->audiodata32;
			audiodata64 = src->audiodata64;
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
#ifdef __APPLE__
	std::atomic_uint64_t mUID = 0;
#endif

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

	template <typename T>
	std::pair<std::shared_ptr<std::vector<T>>, sf_count_t> read_sndfile(SndfileHandle& sndfile) {
		std::shared_ptr<std::vector<T>> data;

		sf_count_t framesRead = 0;

		//actually read in audio and set the data
		//Some formats have an unknown frame size, so we have to read a bit at a time
		if (sndfile.frames() < SF_COUNT_MAX) {
			data = std::make_shared<std::vector<T>>(static_cast<size_t>(sndfile.channels()) * static_cast<size_t>(sndfile.frames()));
			framesRead = sndfile.readf(&data->front(), sndfile.frames());
		} else {
			const sf_count_t framesToRead = static_cast<sf_count_t>(sndfile.samplerate());
			//blockSize, offset, offsetIncr are in samples, not frames
			const auto blockSize = static_cast<size_t>(sndfile.channels()) * static_cast<size_t>(framesToRead);
			size_t offset = 0;
			size_t offsetIncr = framesToRead * sndfile.channels();
			sf_count_t read = 0;
			//reserve 5 seconds of space
			data = std::make_shared<std::vector<T>>(blockSize * 5);
			do {
				data->resize(offset + blockSize);
				read = sndfile.readf(&data->front() + offset, framesToRead);
				framesRead += read;
				offset += offsetIncr;
			} while (read == framesToRead);
		}

		if (framesRead == 0) {
			std::cerr << "read zero frames read from audio file" << std::endl;
			data.reset();
		}
		return std::make_pair(data, framesRead);
	}

	std::string shm_name(std::string type_name, unsigned int channels, unsigned int samplerate) {
		std::string name = "rnbo-" + type_name + "-" + std::to_string(channels) + "-" + std::to_string(samplerate);
		//apple has very short name length limits
		{
#ifdef __APPLE__
			auto id = mUID.fetch_add(1);
			name = name + "-" + std::to_string(id);
#else
			boost::uuids::uuid uid = boost::uuids::random_generator()();
			name = name + "-" + boost::lexical_cast<std::string>(uid);
#endif
		}
		return name;
	}
}

RunnerExternalDataHandler::RunnerExternalDataHandler(const std::vector<std::string>& datarefIds, const RNBO::Json& datarefDesc) :
	mDataRequest(datarefIds.size()),
	mDataResponse(datarefIds.size()),
	mMapRequest(datarefIds.size()),
	mMapResponse(datarefIds.size()),
	mSharedRefChanged(16) //how many shared refs might we have?
{

	for (RNBO::Index i = 0; i < datarefIds.size(); i++) {
		auto id = datarefIds[i];
		mIndexLookup.insert({id, i});

		//extract data types
		RNBO::DataType::Type datatype = RNBO::DataType::Type::Untyped;
		uint8_t datatypeBytes = 0;
		if (datarefDesc.is_array()) {
			for (const auto& ref: datarefDesc) {
				if (ref.is_object() && ref.contains("id") && ref.contains("type")) {
					auto rid = ref["id"];
					auto rtype = ref["type"];
					if (rid.is_string() && rid.get<std::string>() == id && rtype.is_string()) {
						std::string rtype_s = rtype.get<std::string>();
						if (rtype_s == "Float32Buffer") {
							datatype = RNBO::DataType::Type::Float32AudioBuffer;
							datatypeBytes = 4;
						} else if (rtype_s == "Float64Buffer") {
							datatype = RNBO::DataType::Type::Float64AudioBuffer;
							datatypeBytes = 8;
						} else if (rtype_s == "SampleBuffer") {
							datatype = RNBO::DataType::Type::SampleAudioBuffer;
							datatypeBytes = sizeof(RNBO::SampleValue);
						} else if (rtype_s == "UInt8Buffer") {
							datatype = RNBO::DataType::Type::TypedArray;
							datatypeBytes = 1;
						} else {
							std::cerr << "unhandled buffer type " << rtype_s << std::endl;
						}
						break;
					}
				}
			}
		}

		DataTypeInfo typeinfo = std::make_pair(datatype, datatypeBytes);
		mDataTypeDataBytes.push_back(typeinfo);

		auto cur = make_map(id, i);
		mMappings.emplace_back(cur);
		mProcessMappings.emplace_back(cur);
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
						req = make_map(datarefId);
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
	bool system = false;
	RNBO::Index index = get_index(datarefId);;
	DataTypeInfo typeinfo = mDataTypeDataBytes[index];

	{
		std::unique_lock<std::mutex> lock(mMutex);
		if (mObserving.find(index) != mObserving.end()) {
			//don't load over observing, though what if we have a rnbo alloc thing in there??
			return false;
		}
		system = mSystem.count(index) > 0;
	}

	//TODO make sure this is an audio buffer
	bool isf32 = false;
	switch (typeinfo.first) {
		case RNBO::DataType::Type::Float32AudioBuffer:
			isf32 = true;
			break;
		case RNBO::DataType::Type::Float64AudioBuffer: 
			isf32 = false;
			break;
		case RNBO::DataType::Type::SampleAudioBuffer:
			isf32 = sizeof(RNBO::SampleValue) == sizeof(float);
			break;
		default:
			return false;
	}

	std::shared_ptr<MapData> req = make_map(datarefId, index);

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

		if (isf32) {
			auto [data, framesRead] = read_sndfile<float>(sndfile);
			if (!data) {
				return false;
			}
			RNBO::Float32AudioBuffer bufferType(sndfile.channels(), static_cast<double>(sndfile.samplerate()));

			req->data = reinterpret_cast<char *>(&data->front());
			req->sizeinbytes = sizeof(float) * framesRead * sndfile.channels();
			req->datatype = bufferType;
			req->audiodata32 = std::move(data);
		} else {
			auto [data, framesRead] = read_sndfile<double>(sndfile);
			if (!data) {
				return false;
			}
			RNBO::Float64AudioBuffer bufferType(sndfile.channels(), static_cast<double>(sndfile.samplerate()));

			req->data = reinterpret_cast<char *>(&data->front());
			req->sizeinbytes = sizeof(double) * framesRead * sndfile.channels();
			req->datatype = bufferType;
			req->audiodata64 = std::move(data);
		}

		if (system) {
			std::string name = shm_name(isf32 ? "f32" : "f64", sndfile.channels(), sndfile.samplerate());
			int oflag = O_CREAT | O_RDWR; //create, read/write
			mode_t mode = S_IRUSR | S_IWUSR; //use read/write
			int fd = shm_open(name.c_str(), oflag, mode);
			if (fd < 0) {
				std::cerr << "cannot open shared memory with name " << name << std::endl;
				return false;
			} else if (ftruncate(fd, req->sizeinbytes) == -1) {
				std::cerr << "cannot resize shared memory with name " << name << std::endl;
				shm_unlink(name.c_str());
				return false;
			}

			char * shmdata = reinterpret_cast<char *>(mmap(NULL, req->sizeinbytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
			if (shmdata == MAP_FAILED) {
				std::cerr << "cannot mmap shared memory with name " << name << std::endl;
				shm_unlink(name.c_str());
				return false;
			}

			//copy to shared memory
			std::memcpy(shmdata, req->data, req->sizeinbytes);
			req->data = shmdata;
			req->audiodata32.reset();
			req->audiodata64.reset();
			req->shmdata = std::make_shared<ShmMemData>(shmdata, req->sizeinbytes, name);

			std::cout << "opened shm " << name << std::endl;
		}

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
	std::shared_ptr<MapData> req = make_map(datarefId);
	storeAndRequest(req); //empty
}

std::string RunnerExternalDataHandler::handleMeta(const std::string& datarefId, const RNBO::Json& meta)
{
	RNBO::Index index = get_index(datarefId);
	std::string resp;
	bool doshm = false;

	std::string sharekey_next;
	std::string observekey_next;

	if (meta.is_object()) {
		if (meta.contains("share") && meta["share"].is_string()) {
			sharekey_next = meta["share"].get<std::string>();
		} else if (meta.contains("observe") && meta["observe"].is_string()) { //cannot observe AND share
			observekey_next = meta["observe"].get<std::string>();
		}

		if (meta.contains("system") && meta["system"].is_boolean() && meta["system"].get<bool>()) {
			doshm = true;
			observekey_next = std::string(); //cannot observe and do shared memory
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
				req = make_map(datarefId, index);
				resp = "clear";
				auto o = get_shared(observekey_next);
				if (o) {
					req->copyData(o.get());
				}
			}
		}

		if (doshm) {
			mSystem.insert(index);
			resp = "reset";
		} else {
			if (mSystem.count(index) > 0) {
				//unmap shared memory
				req = make_map(datarefId, index);
				resp = "reset";
			}
			mSystem.erase(index);
		}
	}
	if (req) {
		storeAndRequest(req);
		return resp;
	} else {
		return resp;
	}
}

void RunnerExternalDataHandler::handleSharedMappingChange(const std::string& key)
{
	//global lock held, wait for processEvents
	mSharedRefChanged.enqueue(key);
}

void RunnerExternalDataHandler::storeAndRequest(std::shared_ptr<MapData> mapping) 
{
	std::unique_lock<std::mutex> lock(mMutex);
	mMappings[mapping->datarefIndex] = mapping; //keep a weak copy
	mMapRequest.enqueue(mapping);
}

RNBO::Index RunnerExternalDataHandler::get_index(const std::string& datarefId) const
{
	auto it = mIndexLookup.find(datarefId);
	if (it == mIndexLookup.end()) {
		std::string msg("failed to find dataref with id " + datarefId);
		throw std::runtime_error(msg);
	}
	return it->second;
}

std::shared_ptr<MapData> RunnerExternalDataHandler::make_map(const std::string& datarefId, boost::optional<RNBO::Index> index) const 
{
	if (!index) {
		index = get_index(datarefId);
	}
	auto typeinfo = mDataTypeDataBytes[*index];
	auto m = std::make_shared<MapData>(*index, datarefId, typeinfo);
	return m;
}
