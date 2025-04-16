#include "Config.h"
#include "JackAudioRecord.h"

#include <boost/filesystem.hpp>

#include <jack/metadata.h>
#include <jack/midiport.h>
#include <jack/uuid.h>

#include <ossia/network/generic/generic_device.hpp>
#include <ossia/network/generic/generic_parameter.hpp>

#include <sndfile.hh>

#include <string>
#include <cassert>
#include <iostream>
#include <chrono>

namespace fs = boost::filesystem;

namespace {
	const jack_nframes_t buffermul = 4; //how many buffers do we store to transfer?

	static int recordProcess(jack_nframes_t nframes, void *arg) {
		reinterpret_cast<JackAudioRecord *>(arg)->process(nframes);
		return 0;
	}
}

JackAudioRecord::JackAudioRecord(NodeBuilder builder) : mBuilder(builder) { }
JackAudioRecord::~JackAudioRecord() {
	close();
	endRecording(true);
}

void JackAudioRecord::process(jack_nframes_t nframes) {
	//TODO read MIDI

	if (mDoRecord.load() == false) {
		return;
	}

	//try to get ports and ringbuffers, if fail, abort
	//does not block
	std::unique_lock<std::mutex> lock(mMutex, std::try_to_lock);
	if(!lock.owns_lock()){
		return;
	}

	const auto bytesneeded = nframes * sizeof(jack_default_audio_sample_t);
	for (size_t i = 0; i < mJackAudioPortIn.size(); i++) {
		auto rb = mRingBuffers[i];
		auto data = reinterpret_cast<char *>(jack_port_get_buffer(mJackAudioPortIn[i], nframes));
		if (jack_ringbuffer_write_space(rb) < bytesneeded) {
#ifdef DEBUG
			std::cerr << "cannot get bytes needed in " << i << std::endl;
#endif
			//XXX report error?
			return;
		}
		auto towrite = bytesneeded;
		while (towrite) {
			auto written = jack_ringbuffer_write(rb, data, towrite);
			towrite -= written;
			data += written;
		}
	}
}

bool JackAudioRecord::open() {
	unsigned int channels = 2; //TODO get from config
	assert(mJackClient == nullptr);

	jack_status_t status;
	mJackClient = jack_client_open("rnbo-record", JackOptions::JackNoStartServer, &status);
	if (status != 0 || mJackClient == nullptr)
		return false;

	mBufferSize = jack_get_buffer_size(mJackClient);
	mSampleRate = jack_get_sample_rate(mJackClient);

	if (!resize(channels)) {
		std::cerr << std::string("failed to create ") << channels << " channels" << std::endl;
		return false;
	}

	mBuilder([this](ossia::net::node_base * root) {
		if (mRecordRoot == nullptr) {
			//root == "jack"
			mRecordRoot = root->create_child("record");

			{
				auto n = mRecordRoot->create_child("active");
				n->set(ossia::net::description_attribute{}, "Toggle to start/stop recording");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
				mActiveParam = n->create_parameter(ossia::val_type::BOOL);
				mActiveParam->push_value(false);
				mActiveParam->add_callback([this](const ossia::value& val) {
					if (val.get_type() == ossia::val_type::BOOL) {
						if (val.get<bool>()) {
							startRecording();
						} else {
							endRecording(false); //let thread complete on its own
						}
					}
				});
			}

			{
				auto n = mRecordRoot->create_child("channels");
				n->set(ossia::net::description_attribute{}, "The number of channels to provide for recording");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);

				auto dom = ossia::init_domain(ossia::val_type::INT);
				dom.set_min(1);
				n->set(ossia::net::domain_attribute{}, dom);
				n->set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);

				mChannelsParam = n->create_parameter(ossia::val_type::INT);
				mChannelsParam->push_value(static_cast<int>(mJackAudioPortIn.size()));
				mChannelsParam->add_callback([this](const ossia::value& val) {
					endRecording(true);
					if (val.get_type() == ossia::val_type::INT) {
						resize(std::max(1, val.get<int>()));
					}
				});
			}
		}
	});

	jack_set_process_callback(mJackClient, recordProcess, this);
	jack_activate(mJackClient);

	return true;
}

void JackAudioRecord::close() {
	if (mRecordRoot) {
		mRecordRoot->clear_children();
	}

	endRecording(true);
	if (mJackClient) {
		jack_deactivate(mJackClient);
		jack_client_close(mJackClient);
		mJackClient = nullptr;
		mJackAudioPortIn.clear(); //free??

		for (auto r: mRingBuffers) {
			jack_ringbuffer_free(r);
		}
		mRingBuffers.clear();
	}
}

void JackAudioRecord::startRecording() {
	endRecording(true);

	//TODO compute timeout
	mWrite.store(true);
	mWriteThread = std::thread(&JackAudioRecord::write, this);
}
void JackAudioRecord::endRecording(bool wait) {
	mWrite.store(false);
	if (wait && mWriteThread.joinable()) {
		mWriteThread.join();
	}
}

bool JackAudioRecord::resize(int channels) {
	std::unique_lock<std::mutex> lock(mMutex);

	while (mJackAudioPortIn.size() > channels) {
		auto r = mRingBuffers.back();
		jack_ringbuffer_free(r);
		auto p = mJackAudioPortIn.back();
		jack_port_unregister(mJackClient, p);

		mJackAudioPortIn.pop_back();
		mRingBuffers.pop_back();
	}

	unsigned int i = mJackAudioPortIn.size();
	auto bufferbytes = mBufferSize * buffermul * sizeof(jack_default_audio_sample_t);
	for (; i < channels; i++) {
		auto rb = jack_ringbuffer_create(bufferbytes);
		//shouldn't happen
		if (rb == nullptr) {
			jack_client_close(mJackClient);
			mJackClient = nullptr;
			return false;
		}
		jack_ringbuffer_mlock(rb);

		auto port = jack_port_register(mJackClient,
				("in" + std::to_string(i + 1)).c_str(),
				JACK_DEFAULT_AUDIO_TYPE,
				JackPortFlags::JackPortIsInput | JackPortFlags::JackPortIsTerminal,
				0
		);

		jack_uuid_t uuid = jack_port_uuid(port);
		if (!jack_uuid_empty(uuid)) {
			std::string pretty = "Record In " + std::to_string(i + 1);
			jack_set_property(mJackClient, uuid, JACK_METADATA_PRETTY_NAME, pretty.c_str(), "text/plain");
			jack_set_property(mJackClient, uuid, JACK_METADATA_PORT_GROUP, "rnbo-graph-user-sink", "text/plain");
		}

		mJackAudioPortIn.push_back(port);
		mRingBuffers.push_back(rb);
	}
	return true;
}

void JackAudioRecord::write() {
	//clear out buffers (not real time)
	{
		std::unique_lock<std::mutex> lock(mMutex);
		for (auto r: mRingBuffers) {
			jack_ringbuffer_reset(r);
		}
	}

	mDoRecord.store(true); //indicate that we should record

	fs::path tmpfile = config::get<fs::path>(config::key::TempDir).get() / "rnborunner-recording.wav";

	//TODO - make configurable
	fs::path dstdir = config::get<fs::path>(config::key::DataFileDir).get();

	//TODO - make configurable
	std::string timeTag = std::to_string(std::chrono::seconds(std::time(NULL)).count());
	fs::path dstfile = dstdir / fs::path("recording-" + timeTag + ".wav");

	fs::create_directories(tmpfile.parent_path());
	boost::system::error_code ec;
	fs::remove(tmpfile, ec);

	{
		const size_t channels = mRingBuffers.size();
		SndfileHandle sndfile(tmpfile.string(), SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_PCM_16, channels, mSampleRate);
		if (!sndfile) {
			std::cerr << "error opening temp sndfile: " << tmpfile.string() << std::endl;
			//XXX write to param
			return;
		}

		//TODO what should the huristic be for sleep time? currently doing 1/8 of a buffer
		auto sleepms = std::chrono::milliseconds(std::max(1, static_cast<int>(ceil(static_cast<double>(mBufferSize) / static_cast<double>(mSampleRate) / 8.0 * 1000.0))));

		while (mWrite.load() && sndfile) {
			//figure out how many bytes we should read
			size_t bytes = jack_ringbuffer_read_space(mRingBuffers[0]);
			for (size_t i = 1; i < mRingBuffers.size(); i++) {
				bytes = std::min(bytes, jack_ringbuffer_read_space(mRingBuffers[i]));
			}

			const size_t frames = (bytes - (bytes % sizeof(jack_default_audio_sample_t))) / sizeof(jack_default_audio_sample_t);
			const size_t samples = frames * channels;

			if (frames == 0)
				continue;

			mInterlaceBuffer.resize(samples);

			size_t index = 0;
			for (size_t i = 0; i < frames; i++) {
				for (auto rb: mRingBuffers) {
					jack_ringbuffer_read(rb, reinterpret_cast<char *>(mInterlaceBuffer.data() + index), sizeof(jack_default_audio_sample_t));
					index++;
				}
			}
			auto written = sndfile.writef(mInterlaceBuffer.data(), frames);

			std::this_thread::sleep_for(sleepms);
		}
	}
	//TODO move tmp file
	mDoRecord.store(false);

	fs::rename(tmpfile, dstfile, ec);
	if (ec) {
		std::cerr << "failed to move file " << tmpfile.string() << " to " << dstfile.string() << std::endl;
	}
}
