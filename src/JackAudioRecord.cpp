#include "Config.h"
#include "JackAudioRecord.h"
#include "Util.h"

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
#include <iterator>

namespace fs = boost::filesystem;

namespace {

	const std::uintmax_t free_bytes_threshold = 104857600; //100MB

	const std::string channels_config_key = "channels";
	const std::string timeout_config_key = "timeout_seconds";
	const char * record_port_group = "rnbo-graph-record-sink";

	const std::string default_filename_templ = "%y%m%dT%H%M%S-captured";
	const double seconds_report_period_s = 0.1; //only report every 100ms

	const jack_nframes_t buffermul = 8; //how many buffers do we store to transfer?

	static int recordProcess(jack_nframes_t nframes, void *arg) {
		reinterpret_cast<JackAudioRecord *>(arg)->process(nframes);
		return 0;
	}

	const boost::optional<std::string> ns("jack_record");
	template <typename T>
	boost::optional<T> jconfig_get(const std::string& key) {
		return config::get<T>(key, ns);
	}

	template <typename T>
	void jconfig_set(const T& v, const std::string& key) {
		return config::set<T>(v, key, ns);
	}

	template boost::optional<int> jconfig_get(const std::string& key);
	template boost::optional<double> jconfig_get(const std::string& key);
	template boost::optional<bool> jconfig_get(const std::string& key);
	template boost::optional<std::string> jconfig_get(const std::string& key);
}

JackAudioRecord::JackAudioRecord(NodeBuilder builder) : mBuilder(builder), mFileNameTmpl(default_filename_templ) { }
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
			mBufferFullCount.fetch_add(1);
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
	//already open
	if (mJackClient != nullptr) {
		return true;
	}

	int channels = std::max(1, jconfig_get<int>(channels_config_key).value_or(2));

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

			float timeout = jconfig_get<double>(timeout_config_key).value_or(0.0f); //disabled

			{
				auto n = mRecordRoot->create_child("active");
				mActiveParam = n->create_parameter(ossia::val_type::BOOL);

				n->set(ossia::net::description_attribute{}, "Toggle to start/stop recording");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
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
				auto n = mRecordRoot->create_child("capture");
				auto p = n->create_parameter(ossia::val_type::STRING);

				n->set(ossia::net::description_attribute{}, "Start capturing with the given string as the file name template, empty string gives the default: " + default_filename_templ);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
				p->add_callback([this](const ossia::value& val) {
					if (val.get_type() == ossia::val_type::STRING) {
						//stop recording, set template (one shot) and start recording
						endRecording(true);
						auto tmpl = val.get<std::string>();
						if (tmpl.size() == 0) {
							tmpl = default_filename_templ;
						}
						mFileNameTmpl = tmpl;
						mActiveParam->push_value_quiet(true);
						startRecording();
					}
				});
			}

			{
				auto n = mRecordRoot->create_child("channels");
				mChannelsParam = n->create_parameter(ossia::val_type::INT);

				n->set(ossia::net::description_attribute{}, "The number of channels to provide for recording");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
				mChannelsParam->push_value(static_cast<int>(mJackAudioPortIn.size()));
				mChannelsParam->add_callback([this](const ossia::value& val) {
					endRecording(true);
					if (val.get_type() == ossia::val_type::INT) {
						int channels = std::max(1, val.get<int>());
						resize(channels);
						jconfig_set(channels, channels_config_key);
					}
				});

				auto dom = ossia::init_domain(ossia::val_type::INT);
				dom.set_min(1);
				n->set(ossia::net::domain_attribute{}, dom);
				n->set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);
			}
			{
				auto n = mRecordRoot->create_child("timeout");
				mTimeoutParam = n->create_parameter(ossia::val_type::FLOAT);

				n->set(ossia::net::description_attribute{}, "A timeout in seconds to use for stopping the recording. Set to 0 to disable");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
				mTimeoutParam->push_value(timeout);
				mTimeoutParam->add_callback([this](const ossia::value& val) {
					double seconds = 0.0;
					if (val.get_type() == ossia::val_type::INT) {
						seconds = static_cast<double>(val.get<int>());
					} else if (val.get_type() == ossia::val_type::FLOAT) {
						seconds = static_cast<double>(val.get<float>());
					} else {
						return;
					}
					jconfig_set(seconds, timeout_config_key);
				});

				auto dom = ossia::init_domain(ossia::val_type::FLOAT);
				dom.set_min(0.0);
				n->set(ossia::net::domain_attribute{}, dom);
				n->set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);
			}
			{
				auto n = mRecordRoot->create_child("captured");
				mSecondsCapturedParam = n->create_parameter(ossia::val_type::FLOAT);

				n->set(ossia::net::description_attribute{}, "The number of seconds of recording time captured.");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				mSecondsCapturedParam->push_value(0.0);
			}
			{
				auto n = mRecordRoot->create_child("full_count");
				mBufferFullCountParam = n->create_parameter(ossia::val_type::INT);

				n->set(ossia::net::description_attribute{}, "A count of times that the disk serialization buffer was full while trying to write from the audio thread");
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				mBufferFullCountParam->push_value(0);
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

	//update full count
	mBufferFullCount.store(0);
	if (mBufferFullCountParam->value().get<int>() != 0) {
		mBufferFullCountParam->push_value(0);
	}

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
			jack_set_property(mJackClient, uuid, JACK_METADATA_PORT_GROUP, record_port_group, "text/plain");
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

	//TODO - make configurable
	std::string ext = "wav";
	int format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
	fs::path dstdir = config::get<fs::path>(config::key::DataFileDir).get();
	fs::path tmpdir = config::get<fs::path>(config::key::TempDir).get();

	std::string tmpl = mFileNameTmpl;
	mFileNameTmpl = default_filename_templ; //reset

	fs::path dstfile;
	{
		std::string filename = runner::render_time_template(tmpl);
		dstfile = dstdir / fs::path(filename + "." + ext);
	}

	fs::path tmpfile = tmpdir / fs::path(std::string("rnborunner-recording.") + ext);
	fs::create_directories(tmpfile.parent_path());
	fs::create_directories(dstfile.parent_path());

	boost::system::error_code ec;
	fs::remove(tmpfile, ec);

	//compute timeout
	size_t timeoutframes = 0;
	if (mTimeoutParam->get_value_type() == ossia::val_type::FLOAT) {
		float timeoutseconds = mTimeoutParam->value().get<float>();
		if (timeoutseconds > 0.0f) {
			timeoutframes = static_cast<size_t>(static_cast<double>(mSampleRate) * static_cast<double>(timeoutseconds));
		}
	}

	double secondslast = 0.0;
	mSecondsCapturedParam->push_value(0.0);

	{
		const size_t channels = mRingBuffers.size();
		SndfileHandle sndfile(tmpfile.string(), SFM_WRITE, format, channels, mSampleRate);
		if (!sndfile) {
			std::cerr << "error opening temp sndfile: " << tmpfile.string() << std::endl;
			mActiveParam->push_value_quiet(false);
			return;
		}

		//TODO what should the huristic be for sleep time? currently doing 1/8 of a buffer
		//auto sleeptime = std::chrono::microseconds(std::max(1, static_cast<int>(ceil(static_cast<double>(mBufferSize) / static_cast<double>(mSampleRate) / 8.0 * 1000000.0))));
		double sampleratef = static_cast<double>(mSampleRate);

		mDoRecord.store(true); //indicate that we should record
		size_t frameswritten = 0;
		double secondswritten = 0.0;
		int fullreported = 0;
		while (mWrite.load() && sndfile && (timeoutframes == 0 || frameswritten < timeoutframes)) {
			//figure out how many bytes we should read
			size_t bytes = jack_ringbuffer_read_space(mRingBuffers[0]);
			for (size_t i = 1; i < mRingBuffers.size(); i++) {
				bytes = std::min(bytes, jack_ringbuffer_read_space(mRingBuffers[i]));
			}

			size_t frames = (bytes - (bytes % sizeof(jack_default_audio_sample_t))) / sizeof(jack_default_audio_sample_t);
			if (frames == 0) {
				//TODO do we want to sleep? we definitely don't want to fill up the buffer..
				//std::this_thread::sleep_for(sleeptime);
				std::this_thread::yield();
			} else {
				const size_t samples = frames * channels;
				mInterlaceBuffer.resize(samples);

				size_t index = 0;
				for (size_t i = 0; i < frames; i++) {
					for (auto rb: mRingBuffers) {
						jack_ringbuffer_read(rb, reinterpret_cast<char *>(mInterlaceBuffer.data() + index), sizeof(jack_default_audio_sample_t));
						index++;
					}
				}

				//don't read more frames than requested
				if (timeoutframes > 0) {
					frames = std::min(frames, timeoutframes - frameswritten);
				}

				sndfile.writef(mInterlaceBuffer.data(), frames);
				frameswritten += frames;

				secondswritten = static_cast<double>(frameswritten) / sampleratef;
				if (std::abs(secondslast - secondswritten) >= seconds_report_period_s) {
					mSecondsCapturedParam->push_value(static_cast<float>(secondswritten));
					secondslast = secondswritten;
				}

				//TODO how often do we actually want to do this??
				//exit if we go over our free space threshold
				fs::space_info dstspace = fs::space(dstdir);
				fs::space_info tmpspace = fs::space(tmpdir);
				if (dstspace.available < free_bytes_threshold || tmpspace.available < free_bytes_threshold) {
					std::cerr << "available storage space less than byte threshold " << free_bytes_threshold << " ending recording" << std::endl;
					break;
				}
			}

			//update full count
			int full = mBufferFullCount.load();
			if (full != fullreported) {
				fullreported = full;
				mBufferFullCountParam->push_value(full);
			}
		}
		mSecondsCapturedParam->push_value(static_cast<float>(secondswritten));
	}
	mDoRecord.store(false);
	if (mActiveParam->value().get<bool>()) {
		mActiveParam->push_value_quiet(false);
	}

	fs::rename(tmpfile, dstfile, ec);
	if (ec) {
		std::cerr << "failed to move file " << tmpfile.string() << " to " << dstfile.string() << " with error " << ec.message() << std::endl;
	}
}
