#include "DataHandler.h"
#include "Config.h"

#include <iostream>
#include <atomic>
#include <thread>

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

const unsigned int DataLoadWorkers = 4;

namespace {
	std::string shm_name(std::string type_name, unsigned int channels, unsigned int samplerate);
}

class ShmMemData {
	public:
		ShmMemData(char * data, size_t size, std::string name) : mData(data), mSize(size), mName(name) {
			//std::cout << "created shm with name " << mName << std::endl;
		}
		~ShmMemData() {
			munmap(mData, mSize);
			shm_unlink(mName.c_str());
			//std::cout << "destroyed shm with name " << mName << std::endl;
		}

		const std::string& name() const { return mName; }

		char * mData;
		size_t mSize;
		std::string mName;
};

class DataTypeInfo {
	public:
		DataTypeInfo(RNBO::DataType::Type dt, uint8_t b, bool s) : datatype(dt), datatypebytes(b), issigned(s) {}

		RNBO::DataType::Type datatype;
		uint8_t datatypebytes;
		bool issigned;
		bool operator==(const DataTypeInfo& other) const {
			return datatype == other.datatype && datatypebytes == other.datatypebytes && issigned == other.issigned;
		}

		std::string type_name() {
			switch (datatype) {
				case RNBO::DataType::Type::Float32AudioBuffer:
					return "f32";
				case RNBO::DataType::Type::Float64AudioBuffer:
					return "f64";
				case RNBO::DataType::Type::SampleAudioBuffer:
					return sizeof(RNBO::SampleValue) == sizeof(float) ? "f32" : "f64";
				case RNBO::DataType::Type::TypedArray:
					{
						std::string prefix(issigned ? "i" : "u");
						return prefix + std::to_string(datatypebytes * 8);
					}
				default:
					return std::string();
			}
		}
};

class MapData {
	public:
		MapData(RNBO::Index index, std::string id, DataTypeInfo info): datarefIndex(index), datarefId(id), typeinfo(info) {
			//std::cout << "created MapData " << this << std::endl;
		}
		~MapData() {
			//std::cout << "destroy MapData " << this << std::endl;
		}

		fs::path filePath;

		RNBO::Index datarefIndex;
		std::string datarefId;
		DataTypeInfo typeinfo;

		RNBO::DataType datatype;
		char * data = nullptr; //points to one of the front of audio data, bytedata or memory mapped shared memory, null means unmap
		size_t sizeinbytes = 0;

		bool docopy = false; //should we copy from an existing dataref if it exists

		bool compatible(const std::shared_ptr<MapData>& other) const {
			return typeinfo == other->typeinfo;
		}

		bool matches(const RNBO::DataType& other) const {
			return datatype.matches(other);
		}

		bool createshm(std::string type_name, unsigned int channels = 0, unsigned int samplerate = 0) {
			std::string name = shm_name(type_name, channels, samplerate);
			int oflag = O_CREAT | O_RDWR; //create, read/write
			mode_t mode = S_IRUSR | S_IWUSR; //use read/write
			int fd = shm_open(name.c_str(), oflag, mode);
			if (fd < 0) {
				std::cerr << "cannot open shared memory with name " << name << std::endl;
				return false;
			} else if (ftruncate(fd, sizeinbytes) == -1) {
				std::cerr << "cannot resize shared memory with name " << name << std::endl;
				shm_unlink(name.c_str());
				return false;
			}

			char * memmapped = reinterpret_cast<char *>(mmap(NULL, sizeinbytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
			if (memmapped == MAP_FAILED) {
				std::cerr << "cannot mmap shared memory with name " << name << std::endl;
				shm_unlink(name.c_str());
				return false;
			}

			//copy to shared memory
			if (data != nullptr) {
				std::memcpy(memmapped, data, sizeinbytes);
			}
			data = memmapped;
			audiodata32.reset();
			audiodata64.reset();
			bytedata.reset();
			shmdata = std::make_shared<ShmMemData>(memmapped, sizeinbytes, name);

			return true;
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


class DataRefInfo {
	public:
		DataRefInfo(RNBO::Index index, std::string id) :
			datarefIndex(index), datarefId(id) {
		}

		void update(const RNBO::ExternalDataRef * ref) {
			data = ref->getData();
			sizeinbytes = ref->getSizeInBytes();
			datatype = ref->getType();
		}

		RNBO::Index datarefIndex;
		std::string datarefId;

		char * data = nullptr;
		size_t sizeinbytes = 0;
		RNBO::DataType datatype;
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


namespace {
	std::weak_ptr<DataLoadJobQueue> GlobalJobQueue;
	std::mutex CreateMutex;

	class Job {
		public:
			Job(
			std::shared_ptr<RunnerExternalDataHandler> h,
			std::string id,
			fs::path fp) :  handler(h), datarefId(id), filePath(fp)
		{}
			std::shared_ptr<RunnerExternalDataHandler> handler;
			std::string datarefId;
			fs::path filePath;

			bool valid() const {
				return datarefId.size() > 0;
			}
			void dowork() {
				handler->load(datarefId, filePath);
			}
	};

}
class DataLoadJobQueue {
	public:
		DataLoadJobQueue() {
			for (auto i = 0; i < DataLoadWorkers; i++) {
				mWorkers.emplace_back(std::thread(&DataLoadJobQueue::work, this));
			}
		}

		~DataLoadJobQueue() {
			mDoWork = false;
			for (auto& w: mWorkers) {
				w.join();
			}
		}

		static std::shared_ptr<DataLoadJobQueue> Get() {
			std::unique_lock<std::mutex> lock(CreateMutex);
			if (auto ptr = GlobalJobQueue.lock()) {
				return ptr;
			}
			auto ptr = std::make_shared<DataLoadJobQueue>();
			GlobalJobQueue = ptr;
			return ptr;
		};

		void queue(std::shared_ptr<RunnerExternalDataHandler> handler, const std::string& datarefId, const fs::path& filePath) {
			mJobs.push(Job(handler, datarefId, filePath));
		}

		void work() {
			while (mDoWork) {
				if (auto j = mJobs.tryPop()) {
					j->dowork();
				} else {
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
			}
		}

	private:
		bool mDoWork = true;
		Queue<Job> mJobs;
		std::vector<std::thread> mWorkers;
};


namespace {
#ifdef __APPLE__
	std::atomic_uint64_t mUID = 0;
#endif

	std::mutex GlobalMutex;
	//week doesn't work as we'd expect
	std::unordered_map<std::string, std::weak_ptr<MapData>> SharedMappings;
	std::unordered_map<uintptr_t, std::function<void(const std::string&, std::shared_ptr<MapData>)>> SharedMappingCallbacks;

	void update_shared(const std::string& key, std::shared_ptr<MapData> mapping) {
		std::unique_lock<std::mutex> lock(GlobalMutex);
		SharedMappings[key] = mapping;


		//broadcast change
		for (auto it: SharedMappingCallbacks) {
			it.second(key, mapping);
		}
	}

	void remove_shared(const std::string& key) {
		std::unique_lock<std::mutex> lock(GlobalMutex);
		SharedMappings.erase(key);

		//broadcast change
		for (auto it: SharedMappingCallbacks) {
			it.second(key, {});
		}
	}

	boost::optional<std::shared_ptr<MapData>> get_shared(const std::string& key) {
		std::unique_lock<std::mutex> lock(GlobalMutex);
		auto it = SharedMappings.find(key);
		if (it != SharedMappings.end()) {
			if (auto p = it->second.lock()) {
				return p;
			}
		}
		return boost::none;
	}

	void register_callback(const RunnerExternalDataHandler* key, std::function<void(const std::string&, std::shared_ptr<MapData>)> cb) {
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
		std::string name = "rnbo-" + type_name ;
		if (channels > 0) {
			name = name + "-" + std::to_string(channels) + "-" + std::to_string(samplerate);
		}
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
	mInfoRequest(datarefIds.size()),
	mInfoResponse(datarefIds.size())
{
	mDataLoader = DataLoadJobQueue::Get();

	for (RNBO::Index i = 0; i < datarefIds.size(); i++) {
		auto id = datarefIds[i];
		mIndexLookup.insert({id, i});

		//extract data types
		RNBO::DataType::Type datatype = RNBO::DataType::Type::Untyped;
		uint8_t datatypeBytes = 0;
		bool issigned = true;
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
							issigned = false;
						} else {
							std::cerr << "unhandled buffer type " << rtype_s << std::endl;
						}
						break;
					}
				}
			}
		}

		DataTypeInfo typeinfo(datatype, datatypeBytes, issigned);
		mDataTypeDataBytes.push_back(typeinfo);

		auto cur = make_map(id, i);
		mMappings.emplace_back(cur);
		mProcessMappings.emplace_back(cur);
	}

	register_callback(this, std::bind(&RunnerExternalDataHandler::handleSharedMappingChange, this, std::placeholders::_1, std::placeholders::_2));
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

RNBO::Json RunnerExternalDataHandler::fileMappingJson()
{
	std::unique_lock<std::mutex> lock(mMutex);
	RNBO::Json datarefs = RNBO::Json::object();
	for (auto& kv: mDataRefFileNameMap)
		datarefs[kv.first] = kv.second;
	return datarefs;
}

void RunnerExternalDataHandler::processBeginCallback(DataRefIndex numRefs, ConstRefList refList, UpdateRefCallback updateDataRef, ReleaseRefCallback releaseDataRef)
{
	//skip requests for 1 process cycle to avoid our dataref events being stomped on by internal dataref events
	if (mHasRunProcess) {

		//do updates
		{
			std::shared_ptr<MapData> req;
			while (mMapRequest.try_dequeue(req)) {
				auto i = req->datarefIndex;
				auto ref = refList[i];
				auto cur = mProcessMappings[i];
				if (ref->getData() != req->data) {
					//extract RNBO allocated data
					if (req->docopy && ref->getData() != nullptr) {
						if (req->sizeinbytes == ref->getSizeInBytes()) {
							req->docopy = false;
							std::memcpy(req->data, ref->getData(), ref->getSizeInBytes());
						} else {
							//XXX error
						}
					}

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

		//get info
		{
			std::shared_ptr<DataRefInfo> req;
			while (mInfoRequest.try_dequeue(req)) {
				auto i = req->datarefIndex;
				req->update(refList[i]);
				mInfoResponse.try_enqueue(req);
			}
		}
	}
}

void RunnerExternalDataHandler::processEndCallback(DataRefIndex numRefs, ConstRefList refList)
{
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

void RunnerExternalDataHandler::requestLoad(const std::string& datarefId, const std::string& fileName)
{
	auto dataFileDir = config::get<fs::path>(config::key::DataFileDir);
	if (dataFileDir) {
		fs::path filePath;
		if (fileName.size()) {
			filePath = dataFileDir.get() / fs::path(fileName);
			if (!fs::exists(filePath)) {
				//see about adding ".wav" so users can send 1234 and get 1234.wav
				filePath = dataFileDir.get() / fs::path(fileName + ".wav");
				if (!fs::exists(filePath)) {
					return;
				}
			}
		}
		mDataLoader->queue(shared_from_this(), datarefId, filePath);
	} else {
		std::cerr << config::key::DataFileDir << " not in config" << std::endl;
	}
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

void RunnerExternalDataHandler::processEvents(std::unordered_map<std::string, ossia::net::parameter_base *>& dataRefNodes)
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
		while (auto c = mSharedRefChanged.tryPop()) {
			auto [key, shared] = c.get();
			{
				std::unique_lock<std::mutex> lock(mMutex);

				//update any found observers
				auto observers = mObservers.find(key);
				if (observers != mObservers.end()) {
					for (auto datarefId: observers->second) {
						auto req = make_map(datarefId);
						if (shared) {
							//TODO verify that datatype matches
							req->copyData(shared);
						}
            mDataLoad.push(req);
					}
				}
			}
		}
	}
	{
		//if we have an info response, see if we need to alloc new data and copy
		std::shared_ptr<DataRefInfo> resp;
    while (mInfoResponse.try_dequeue(resp)) {
      RNBO::Index index = resp->datarefIndex;
      DataTypeInfo typeinfo = mDataTypeDataBytes[index];

      std::unique_lock<std::mutex> lock(mMutex);
      if (resp->sizeinbytes > 0) {
        bool system = mSystem.count(index) > 0;
        std::string sharedname;
        {
          auto it = mSharing.find(index);
          if (it != mSharing.end()) {
            sharedname = it->second;
          }
        }
        bool doalloc = (system || sharedname.size() > 0);

        auto mapping = mMappings[index];
        if (auto p = mapping.lock()) {
          doalloc = doalloc && system && !p->shmdata;
        } else {
          doalloc = resp->data != nullptr && doalloc;
        }

        std::string type_name = typeinfo.type_name();
        if (type_name != "f32" && type_name != "f64" && type_name != "u8") {
          std::cerr << "type not yet supported " << type_name << std::endl;
          continue;
        }

        std::shared_ptr<MapData> req;
        if (doalloc) {
          req = make_map(resp->datarefId, index);
          if (system) {
            unsigned int channels = 0;
            unsigned int samplerate = 0;
            switch (resp->datatype.type) {
              case RNBO::DataType::Type::Float32AudioBuffer:
              case RNBO::DataType::Type::Float64AudioBuffer:
              case RNBO::DataType::Type::SampleAudioBuffer:
                channels = static_cast<unsigned int>(resp->datatype.audioBufferInfo.channels);
                samplerate = static_cast<unsigned int>(resp->datatype.audioBufferInfo.samplerate);
                break;
              default:
                break;
            }

            req->sizeinbytes = resp->sizeinbytes;
            if (!req->createshm(type_name, channels, samplerate)) {
              std::cerr << "failed to create shm" << std::endl;
              continue;
            }
          } else {
            if (type_name == "f32") {
              req->audiodata32 = std::make_shared<std::vector<float>>(req->sizeinbytes / sizeof(float), 0.0);
              req->data = reinterpret_cast<char *>(&req->audiodata32->front());
            } else if (type_name == "f64") {
              req->audiodata64 = std::make_shared<std::vector<double>>(req->sizeinbytes / sizeof(double), 0.0);
              req->data = reinterpret_cast<char *>(&req->audiodata64->front());
            } else if (type_name == "u8") {
              req->bytedata = std::make_shared<std::vector<uint8_t>>(req->sizeinbytes, 0);
              req->data = reinterpret_cast<char *>(&req->bytedata->front());
            } else {
              continue; //shouldn't happen
            }
          }
          req->docopy = true;

          //XXX
          if (sharedname.size()) {
            update_shared(sharedname, req);
          }
        }
        if (req) {
          mDataLoad.push(req);
        }
      }
    }
	}
	{
		while (auto c = mDataLoad.tryPop()) {
      std::unique_lock<std::mutex> lock(mMutex);
      auto mapping = c.get();
      const auto index = mapping->datarefIndex;
      const std::string filename = mapping->filePath.filename().string();

      auto sharing = mSharing.find(index);
      if (sharing != mSharing.end()) {
        update_shared(sharing->second, mapping);
      }

      mMappings[mapping->datarefIndex] = mapping; //keep a weak copy
      mMapRequest.enqueue(mapping);

      auto nodeit = dataRefNodes.find(mapping->datarefId);

      if (nodeit != dataRefNodes.end()) {
        auto value = nodeit->second->value();
        if (value.get<std::string>() != filename) {
          nodeit->second->push_value_quiet(filename);
        }
        ossia::net::node_base& node = nodeit->second->get_node();
        {
          const std::string key = "shm";
          auto shm = node.find_child(key);
          if (mapping->shmdata) {
            ossia::net::parameter_base * p = nullptr;
            if (shm == nullptr) {
              shm = node.create_child(key);
              p = shm->create_parameter(ossia::val_type::STRING);
              shm->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
              shm->set(ossia::net::description_attribute{}, "Shared Memory Name");
            } else {
              p = shm->get_parameter();
            }
            p->push_value(mapping->shmdata->name());
          } else if (shm) {
            node.remove_child(key);
          }
        }
      }

      if (filename.size() == 0) {
        mDataRefFileNameMap.erase(mapping->datarefId);
      } else {
        mDataRefFileNameMap[mapping->datarefId] = filename;
      }
		}
	}
}

//called from multiple threads at once
void RunnerExternalDataHandler::load(const std::string& datarefId, const fs::path& filePath)
{
	bool system = false;
	RNBO::Index index = get_index(datarefId);
	DataTypeInfo typeinfo = mDataTypeDataBytes[index];

	//reload existing mapping if we can
	auto reset = [this, index](){
		std::unique_lock<std::mutex> lock(mMutex);
		if (auto p = mMappings[index].lock()) {
			mDataLoad.push(p);
		}
	};

	{
		std::string observekey_cur;
		{
			std::unique_lock<std::mutex> lock(mMutex);
			auto it = mObserving.find(index);
			if (it != mObserving.end()) {
				observekey_cur = it->second;
			}
			system = mSystem.count(index) > 0;
		}
		if (observekey_cur.size()) {
			if (auto p = get_shared(observekey_cur)) {
        auto req = make_map(datarefId, index);
        req->copyData(p.get());
				mDataLoad.push(req);
			}
			return;
		}
	}

	//make sure this is an audio buffer
	std::string type_name = typeinfo.type_name();
	if (type_name != "f32" && type_name != "f64") {
		return;
	}

	std::shared_ptr<MapData> req = make_map(datarefId, index);

	if (filePath.empty()) {
		mDataLoad.push(req);
		return;
	}

	try {
		if (!fs::exists(filePath)) {
			std::cerr << "no file at " << filePath << std::endl;
			reset();
			return;
		}
		SndfileHandle sndfile(filePath.string());
		if (!sndfile) {
			std::cerr << "couldn't open as sound file " << filePath << std::endl;
			reset();
			return;
		}

		//sanity check
		if (sndfile.channels() < 1 || sndfile.samplerate() < 1.0 || sndfile.frames() < 1) {
			std::cerr << "sound file needs to have samplerate, frames and channels greater than zero " << filePath.string() <<
				" samplerate: " << sndfile.samplerate() <<
				" channels: " << sndfile.channels() <<
				" frames: " << sndfile.frames() <<
				std::endl;
			reset();
			return;
		}

		if (type_name == "f32") {
			auto [data, framesRead] = read_sndfile<float>(sndfile);
			if (!data) {
				reset();
				return;
			}
			RNBO::Float32AudioBuffer bufferType(sndfile.channels(), static_cast<double>(sndfile.samplerate()));

			req->data = reinterpret_cast<char *>(&data->front());
			req->sizeinbytes = sizeof(float) * framesRead * sndfile.channels();
			req->datatype = bufferType;
			req->audiodata32 = std::move(data);
		} else {
			auto [data, framesRead] = read_sndfile<double>(sndfile);
			if (!data) {
				reset();
				return;
			}
			RNBO::Float64AudioBuffer bufferType(sndfile.channels(), static_cast<double>(sndfile.samplerate()));

			req->data = reinterpret_cast<char *>(&data->front());
			req->sizeinbytes = sizeof(double) * framesRead * sndfile.channels();
			req->datatype = bufferType;
			req->audiodata64 = std::move(data);
		}

		req->filePath = filePath;

		if (system && !req->createshm(type_name, sndfile.channels(), sndfile.samplerate())) {
			std::cerr << "failed to create shm" << std::endl;
		}

		mDataLoad.push(req);
	} catch (std::exception& e) {
		std::cerr << "exception reading data ref file: " << e.what() << std::endl;
		reset();
	}
}

void RunnerExternalDataHandler::handleMeta(const std::string& datarefId, const RNBO::Json& meta)
{
	RNBO::Index index = get_index(datarefId);
	bool doshm = false;
	bool reload = false;
	bool getinfo = false; //get info from process side to see if we need to alloc our own memory and copy RNBO alloc data

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
				//should broadcast update
				if (auto p = mapping.lock()) {
					//XXX when p has empty data..
					update_shared(sharekey_next, p);
				} else {
					remove_shared(sharekey_next);
				}
			} else {
				mSharing.erase(index);
			}
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
				auto o = get_shared(observekey_next);
				if (o) {
					req = make_map(datarefId, index);
					req->copyData(o.get());
				}
			}
		}

		if (doshm) {
			mSystem.insert(index);
		} else {
			if (mSystem.count(index) > 0) {
				//unmap shared memory
				req = make_map(datarefId, index);
			}
			mSystem.erase(index);
		}

		//should we get info, only if we don't have files loaded
		if (doshm || (sharekey_next.size() && sharekey_next != sharekey_cur)) {
			if (auto p = mMappings[index].lock()) {
				getinfo = p->data == nullptr;
			} else {
				getinfo = true;
			}
		}
	}

	if (getinfo) {
		auto inforeq = std::make_shared<DataRefInfo>(index, datarefId);
		mInfoRequest.enqueue(inforeq);
	}

	if (req) {
		mDataLoad.push(req);
	}
}

void RunnerExternalDataHandler::handleSharedMappingChange(const std::string& key, std::shared_ptr<MapData> ptr)
{
	//global lock held, wait for processEvents
	mSharedRefChanged.push({key, ptr});
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
