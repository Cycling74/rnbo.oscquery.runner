#include <iostream>
#include <fstream>
#include <cstdlib>
#include <utility>
#include <chrono>
#include <algorithm>
#include <libbase64.h>
#include <iomanip>

#include "Controller.h"
#include "Config.h"
#include "Defines.h"
#include "JackAudio.h"
#include "PatcherFactory.h"

#include <boost/process.hpp>
#include <boost/process/child.hpp>

#include <ossia/context.hpp>
#include <ossia/detail/config.hpp>

#include <ossia/protocols/oscquery/oscquery_server_asio.hpp>
#include <ossia/protocols/osc/osc_factory.hpp>

#include <ossia/network/context.hpp>
#include <ossia/network/local/local.hpp>
#include <ossia/network/generic/generic_device.hpp>
#include <ossia/network/generic/generic_parameter.hpp>

#include <sys/types.h>
#include <signal.h>

#ifdef RNBO_USE_DBUS
#include "RunnerUpdateState.h"
#include "RnboUpdateServiceProxy.h"
#endif

using std::cout;
using std::cerr;
using std::endl;
using std::chrono::system_clock;

namespace fs = boost::filesystem;
namespace bp = boost::process;

namespace {
	static const std::string rnbo_version(RNBO_VERSION);
	static const std::string rnbo_system_name(RNBO_SYSTEM_NAME);
	static const std::string rnbo_system_processor(RNBO_SYSTEM_PROCESSOR);
	static std::string build_program("rnbo-compile-so");

	static const std::string rnbo_dylib_suffix(RNBO_DYLIB_SUFFIX);

	static const std::chrono::milliseconds command_wait_timeout(10);
	static const std::chrono::milliseconds compile_command_wait_timeout(5); //poll more quickly so we can get cancels
	static const std::chrono::milliseconds save_debounce_timeout(500);

	static const std::string last_file_name = "last.json";

	static const std::string last_instances_key = "instances";
	static const std::string last_so_key = "so_path";
	static const std::string last_config_key = "config";
	static const std::string index_key = "index";


	fs::path lastFilePath() {
			return config::get<fs::path>(config::key::SaveDir).get() / last_file_name;
	}

	bool base64_decode_inplace(std::string& v) {
		size_t read = 0;
		std::vector<char> decoded(v.size()); //will be smaller than the in fileName
		if (base64_decode(v.c_str(), v.size(), &decoded.front(), &read, 0) != 1) {
			return false;
		}
		v = std::string(decoded.begin(), decoded.end());
		return true;
	}

	struct CompileInfo {
		bp::group mGroup;
		bp::child mProcess;
		std::string mCommandId;
		RNBO::Json mConf;
		fs::path mConfFileName;
		std::string mMaxRNBOVersion;
		boost::optional<unsigned int> mInstanceIndex;
		fs::path mLibPath;
		CompileInfo(
				std::string command, std::vector<std::string> args,
				fs::path libPath,
				std::string cmdId,
				RNBO::Json conf,
				fs::path confFileName,
				std::string maxRNBOVersion,
				boost::optional<unsigned int> instanceIndex
				) :
			mProcess(command, args, mGroup),
			mCommandId(cmdId),
			mConf(conf),
			mConfFileName(confFileName),
			mMaxRNBOVersion(maxRNBOVersion),
			mLibPath(libPath),
			mInstanceIndex(instanceIndex)
		{ }
		CompileInfo(CompileInfo&&) = default;
		CompileInfo& operator=(CompileInfo&& other) = default;

		CompileInfo(CompileInfo&) = delete;
		CompileInfo& operator=(CompileInfo& other) = delete;

		~CompileInfo() {
			if (mProcess.running()) {
				mGroup.terminate();
				//should we just .wait()
				mProcess.detach();
			}
		}

		bool valid() { return mProcess.valid(); }
	};

}


Controller::Controller(std::string server_name) : mDB(), mProcessCommands(true) {
	mProtocol = new ossia::net::multiplex_protocol();
	mOssiaContext = ossia::net::create_network_context();
	auto serv_proto = new ossia::oscquery_asio::oscquery_server_protocol(mOssiaContext, 1234, 5678);

	mServer = std::unique_ptr<ossia::net::generic_device>(new ossia::net::generic_device(std::unique_ptr<ossia::net::protocol_base>(mProtocol), server_name));
	mServer->set_echo(true);

	mProtocol->expose_to(std::unique_ptr<ossia::net::protocol_base>(serv_proto));

	auto root = mServer->create_child("rnbo");

	//expose some information
	auto info = root->create_child("info");
	info->set(ossia::net::description_attribute{}, "information about RNBO and the running system");

	for (auto it: {
			std::make_pair("version", rnbo_version),
			std::make_pair("system_name", rnbo_system_name),
			std::make_pair("system_processor", rnbo_system_processor),
			}) {
		auto n = info->create_child(it.first);
		auto p = n->create_parameter(ossia::val_type::STRING);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
		p->push_value(it.second);
	}

	//get PRETTY_NAME from os-release if it exists
	try {
		fs::path osRelease("/etc/os-release");
		if (fs::exists(osRelease)) {
			std::ifstream i(osRelease.string());
			std::string line;
			while (std::getline(i, line)) {
				std::size_t found = line.find("PRETTY_NAME");
				if (found != std::string::npos) {
					auto eq = line.find("=");
					if (eq != std::string::npos) {
						std::string name = line.substr(eq + 1);

						//trim quotes
						if (name.size() && name[0] == '"' && name[name.size() - 1] == '"') {
							name = name.substr(1, name.size() - 2);
						}

						auto n = info->create_child("system_os_name");
						auto p = n->create_parameter(ossia::val_type::STRING);
						n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);

						p->push_value(name);
					} else {
						std::cerr << "/etc/os-release not in expected KEY=VALUE format" << std::endl;
					}
					break;
				}
			}
		}
	} catch (const std::exception& e) {
		std::cerr << "error reading /etc/os-release: " << e.what() << std::endl;
	}

	{
		auto n = info->create_child("system_id");
		auto p = n->create_parameter(ossia::val_type::STRING);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
		n->set(ossia::net::description_attribute{}, "a unique, one time generated id for this system");
		p->push_value(config::get_system_id());
	}

	{
		//ossia doesn't seem to support 64bit integers, so we use a string as 31 bits
		//might not be enough to indicate disk space
		auto n = info->create_child("disk_bytes_available");
		mDiskSpaceParam = n->create_parameter(ossia::val_type::STRING);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
		updateDiskSpace();
	}

	//add support for commands
	{
		std::vector<ossia::value> supported = { "file_write_extended", "compile-with_config_file" };

		auto n = info->create_child("supported_cmds");
		auto p = n->create_parameter(ossia::val_type::LIST);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
		n->set(ossia::net::description_attribute{}, "details about post 1.0 commands added");
		p->push_value(supported);
	}

	{
		auto n = root->create_child("cmd");
		auto p = n->create_parameter(ossia::val_type::STRING);

		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
		n->set(ossia::net::description_attribute{}, "command handler");

		p->add_callback([this, p](const ossia::value& v) {
				//libossia reports the value of SET only parameters, so silently set
				//the value to empty when we get an update
				if (v.get_type() == ossia::val_type::STRING) {
					auto s = v.get<std::string>();
					if (s.size() > 0) {
						mCommandQueue.push(s);
						p->set_value_quiet(std::string());
					}
				}
		});
	}
	{
		auto n = root->create_child("resp");
		mResponseParam = n->create_parameter(ossia::val_type::STRING);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
		n->set(ossia::net::description_attribute{}, "command response");
	}

	{
		auto rep = root->create_child("listeners");

		{
			auto n = rep->create_child("entries");
			mListenersListParam = n->create_parameter(ossia::val_type::LIST);
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
			n->set(ossia::net::description_attribute{}, "list of OSC listeners");
		}

		auto cmdBuilder = [](std::string method, const std::string& payload) -> std::string {
			try {
				auto p = payload.find(":");
				if (p != std::string::npos) {
					RNBO::Json cmd = {
						{"method", method},
						{"id", "internal"},
						{"params",
							{
								{"ip", payload.substr(0, p)},
								{"port", std::stoi(payload.substr(p + 1, std::string::npos))},
							}
						}
					};
					return cmd.dump();
				}
			} catch (...) {
			}
			std::cerr << "failed to make " + method + " command for " + payload << std::endl;
			return {};
		};

		{
			auto n = rep->create_child("add");
			auto p = n->create_parameter(ossia::val_type::STRING);
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
			n->set(ossia::net::description_attribute{}, "add OSC UDP listener: \"ip:port\"");

			p->add_callback([this, cmdBuilder](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::STRING) {
					auto cmd = cmdBuilder("listener_add", v.get<std::string>());
					if (cmd.size()) {
						mCommandQueue.push(cmd);
					}
				}
			});
		}

		{
			auto n = rep->create_child("del");
			auto p = n->create_parameter(ossia::val_type::STRING);
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
			n->set(ossia::net::description_attribute{}, "delete OSC UDP listener: \"ip:port\"");

			p->add_callback([this, cmdBuilder](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::STRING) {
					auto cmd = cmdBuilder("listener_del", v.get<std::string>());
					if (cmd.size()) {
						mCommandQueue.push(cmd);
					}
				}
			});
		}
		{
			auto n = rep->create_child("clear");
			auto p = n->create_parameter(ossia::val_type::IMPULSE);
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
			n->set(ossia::net::description_attribute{}, "clear all OSC UDP listeners");

			p->add_callback([this, cmdBuilder](const ossia::value& v) {
				mCommandQueue.push(cmdBuilder("listener_clear", "0:0"));
			});
		}
	}

	auto j = root->create_child("jack");
	NodeBuilder builder = [j, this](std::function<void(ossia::net::node_base *)> f) {
		std::lock_guard<std::mutex> guard(mBuildMutex);
		f(j);
	};

	mProcessAudio = std::make_shared<ProcessAudioJack>(builder);

	{
		auto n = j->create_child("active");
		mAudioActive = n->create_parameter(ossia::val_type::BOOL);
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::BI);
		mAudioActive->push_value(mProcessAudio->isActive());

		mAudioActive->add_callback([this](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::BOOL) {
					handleActive(v.get<bool>());
				}
		});
	}

	mPatchersNode = root->create_child("patchers");
	mPatchersNode->set(ossia::net::description_attribute{}, "patcher descriptions");

	updatePatchersInfo();

	mInstancesNode = root->create_child("inst");
	mInstancesNode->set(ossia::net::description_attribute{}, "code export instances");
	{
		auto ctl = mInstancesNode->create_child("control");

		auto cmdBuilder = [](std::string method, int index, const std::string& name) -> std::string {
			RNBO::Json cmd = {
				{"method", method},
				{"id", "internal"},
				{"params",
					{
						{"name", name},
						{"index", index},
					}
				}
			};
			return cmd.dump();
		};

		{
			auto n = ctl->create_child("unload");
			auto p = n->create_parameter(ossia::val_type::INT);
			n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
			n->set(ossia::net::description_attribute{}, "Unload a running instance by index");

			p->add_callback([this, cmdBuilder](const ossia::value& v) {
					if (v.get_type() == ossia::val_type::INT) {
						auto index = v.get<int>();
						if (index >= 0) {
							mCommandQueue.push(cmdBuilder("instance_unload", index, ""));
						}
					}
			});

		}

		{
			mInstanceLoadNode = ctl->create_child("load");
			auto p = mInstanceLoadNode->create_parameter(ossia::val_type::LIST);
			mInstanceLoadNode->set(ossia::net::access_mode_attribute{}, ossia::access_mode::SET);
			mInstanceLoadNode->set(ossia::net::description_attribute{}, "Load a pre-built patcher by name into the given index args: index name");

			p->add_callback([this, cmdBuilder](const ossia::value& v) {
				if (v.get_type() == ossia::val_type::LIST) {
					auto l = v.get<std::vector<ossia::value>>();
					if (l.size() == 2 && l[1].get_type() == ossia::val_type::STRING && l[0].get_type() == ossia::val_type::INT) {
						auto index = l[0].get<int>();
						auto name = l[1].get<std::string>();

						if (index >= 0) {
							mCommandQueue.push(cmdBuilder("instance_load", index, name));
						}

					}
				}
			});

		}
	}


	bool supports_install = false;
	auto update = info->create_child("update");
	update->set(ossia::net::description_attribute{}, "Self upgrade/downgrade");

#if RNBO_USE_DBUS
	mUpdateServiceProxy = std::make_shared<RnboUpdateServiceProxy>();
	if (mUpdateServiceProxy) {
		try {
			//setup dbus
			RunnerUpdateState state;
			runner_update::from(mUpdateServiceProxy->State(), state);
			std::string status = mUpdateServiceProxy->Status();

			{
				auto n = update->create_child("state");
				auto p = n->create_parameter(ossia::val_type::STRING);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				n->set(ossia::net::description_attribute{}, "Update state");
				p->push_value(runner_update::into(state));

				std::vector<ossia::value> accepted;
				for (auto v: runner_update::all()) {
					accepted.push_back(v);
				}
				auto dom = ossia::init_domain(ossia::val_type::STRING);
				ossia::set_values(dom, accepted);
				n->set(ossia::net::domain_attribute{}, dom);
				n->set(ossia::net::bounding_mode_attribute{}, ossia::bounding_mode::CLIP);

				mUpdateServiceProxy->setStateCallback([p](RunnerUpdateState state) mutable { p->push_value(runner_update::into(state)); });
			}

			{
				auto n = update->create_child("status");
				auto p = n->create_parameter(ossia::val_type::STRING);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				n->set(ossia::net::description_attribute{}, "Latest update status");
				p->push_value(status);
				mUpdateServiceProxy->setStatusCallback([p](std::string status) mutable { p->push_value(status); });
			}

			{
				auto n = update->create_child("outdated");
				auto p = n->create_parameter(ossia::val_type::INT);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				n->set(ossia::net::description_attribute{}, "Number of outdated packages detected on the system");
				p->push_value(-1);
				mUpdateServiceProxy->setOutdatedPackagesCallback([p](uint32_t cnt) mutable { p->push_value(static_cast<int>(cnt)); });
			}
			supports_install = true;
		} catch (const std::exception& e) {
			cerr << "exception caught " << e.what() << endl;
			//reset shared ptrs as we use them later to decide if we should try to update or poll
			supports_install = false;
		} catch (...) {
			cerr << "unknown exception caught " << endl;
			supports_install = false;
		}
	}
#endif

	//let the outside know if this instance supports up/downgrade
	{
		auto n = update->create_child("supported");
		auto p = n->create_parameter(ossia::val_type::BOOL);
		n->set(ossia::net::description_attribute{}, "Does this runner support remote upgrade/downgrade");
		n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
		p->push_value(supports_install);
	}

	mCommandThread = std::thread(&Controller::processCommands, this);
}

Controller::~Controller() {
	{
		std::lock_guard<std::mutex> guard(mBuildMutex);
		clearInstances(guard);
	}
	mProtocol = nullptr;
	mProcessCommands.store(false);
	mCommandThread.join();
	mProcessAudio.reset();
	mServer.reset();
}

bool Controller::loadLibrary(const std::string& path, std::string cmdId, RNBO::Json conf, bool saveConfig, unsigned int instanceIndex) {
	//clear out our last instance preset, loadLast should already have it if there is one
	mInstanceLastPreset.reset();

	auto fname = fs::path(path).filename().string();
	try {
		//make sure that the version numbers match in the name of the library
		if (!boost::algorithm::ends_with(fname, rnbo_dylib_suffix)) {
			std::string errs("the requested library: " + fname + " doesn't match version suffix " + rnbo_dylib_suffix);
			cerr << errs << endl;
			//we should never really have an ID here because we would have just built it, but just in case.
			if (cmdId.size()) {
				reportCommandError(cmdId, static_cast<unsigned int>(CompileLoadError::VersionMismatch), errs);
			}
			return false;
		}

		//activate if we need to
		if (!tryActivateAudio()) {
			cerr << "audio is not active, cannot create instance(s)" << endl;
			if (cmdId.size()) {
				reportCommandError(cmdId, static_cast<unsigned int>(CompileLoadError::AudioNotActive), "cannot activate audio");
			}
			reportActive();
			return false;
		}
		//make sure that no other instances can be created while this is active
		std::lock_guard<std::mutex> iguard(mInstanceMutex);
		auto factory = PatcherFactory::CreateFactory(path);
		ossia::net::node_base * instNode = nullptr;
		std::string instIndex = std::to_string(instanceIndex);

		std::string name = "rnbo" + instIndex;
		if (conf.contains("name") && conf["name"].is_string()) {
			name = conf["name"].get<std::string>();
		}

		{
			std::lock_guard<std::mutex> guard(mBuildMutex);
			unloadInstance(guard, instanceIndex);
			instNode = mInstancesNode->create_child(instIndex);
		}
		auto builder = [instNode, this](std::function<void(ossia::net::node_base*)> f) {
			std::lock_guard<std::mutex> guard(mBuildMutex);
			f(instNode);
		};

		auto instance = new Instance(factory, name, builder, conf, mProcessAudio, instanceIndex);
		{
			std::lock_guard<std::mutex> guard(mBuildMutex);
			//queue a save whenenever the configuration changes
			instance->registerConfigChangeCallback([this] {
					queueSave();
			});
			instance->start();
			mInstances.emplace_back(std::make_pair(instance, path));
		}
		if (cmdId.size()) {
			reportCommandResult(cmdId, {
				{"code", static_cast<unsigned int>(CompileLoadStatus::Loaded)},
				{"message", "loaded"},
				{"progress", 100}
			});
		}
		if (saveConfig)
			queueSave();
		reportActive();
		return true;
	} catch (const std::exception& e) {
		std::cerr << "failed to load library: " << fname << " exception: " << e.what() << std::endl;
	} catch (...) {
		std::cerr << "failed to load library: " << fname << std::endl;
	}
	return false;
}

bool Controller::loadLast() {
	try {
		RNBO::UniquePresetPtr preset;

		{
			std::lock_guard<std::mutex> guard(mBuildMutex);
			clearInstances(guard);
			//get the last preset (saved before clearing);
			std::swap(preset, mInstanceLastPreset);
		}


		//try to start the last
		auto lastFile = lastFilePath();
		if (!fs::exists(lastFile))
			return false;
		RNBO::Json c;
		{
			std::ifstream i(lastFile.string());
			i >> c;
			i.close();
		}

		if (!c[last_instances_key].is_array()) {
			cerr << "malformed last data" << endl;
			return false;
		}

		//load instances
		for (auto i: c[last_instances_key]) {
			std::string so = i[last_so_key];
			unsigned int index = 0;
			if (i.contains(index_key) && i[index_key].is_number()) {
				index = i[index_key].get<int>();
			}
			//load library but don't save config
			if (!loadLibrary(so, std::string(), i[last_config_key], false, index)) {
				cerr << "failed to load so " << so << endl;
				return false;
			}
		}

		//load last preset if we have it
		if (preset) {
			std::lock_guard<std::mutex> guard(mBuildMutex);
			if (mInstances.size()) {
				mInstances.front().first->loadPreset(std::move(preset));
			}
		}
	} catch (const std::exception& e) {
		cerr << "exception " << e.what() << " trying to load last setup" << endl;
	} catch (...) {
		cerr << "unknown exception trying to load last setup" << endl;
	}
	return false;
}

#ifdef RNBO_OSCQUERY_BUILTIN_PATCHER
bool Controller::loadBuiltIn() {
	mSave = false;
	try {
		if (!tryActivateAudio()) {
			reportActive();
			cerr << "audio is not active, cannot create builtin instance" << endl;
			return false;
		}

		{
			//make sure that no other instances can be created while this is active
			std::lock_guard<std::mutex> iguard(mInstanceMutex);
			auto factory = PatcherFactory::CreateBuiltInFactory();
			ossia::net::node_base * instNode;
			std::string instIndex;
			{
				std::lock_guard<std::mutex> guard(mBuildMutex);
				clearInstances(guard);
				instIndex = std::to_string(mInstances.size());
				instNode = mInstancesNode->create_child(instIndex);
			}
			auto builder = [instNode, this](std::function<void(ossia::net::node_base *)> f) {
				std::lock_guard<std::mutex> guard(mBuildMutex);
				f(instNode);
			};
			auto instance = new Instance(factory, "rnbo" + instIndex, builder, {}, mProcessAudio);
			{
				std::lock_guard<std::mutex> guard(mBuildMutex);
				instance->start();
				mInstances.emplace_back(std::make_pair(instance, fs::path()));
			}
		}
		reportActive();
		return true;
	} catch (const std::exception& e) {
		cerr << "exception " << e.what() << " trying to load built in" << endl;
	} catch (...) {
		cerr << "unknown exception trying to load built in" << endl;
	}
	return false;
}
#endif

void Controller::saveLast() {
	RNBO::Json instances = RNBO::Json::array();
	RNBO::Json last = RNBO::Json::object();
	{
		std::lock_guard<std::mutex> iguard(mInstanceMutex);
		for (auto& i: mInstances) {
			RNBO::Json data = RNBO::Json::object();
			data[last_so_key] = i.second.string();
			data[last_config_key] = i.first->currentConfig();
			data[index_key] = i.first->index();
			instances.push_back(data);
		}
	}
	last[last_instances_key] = instances;
	auto lastFile = lastFilePath();
	std::ofstream o(lastFile.string());
	o << std::setw(4) << last << std::endl;
}

void Controller::queueSave() {
	std::lock_guard<std::mutex> guard(mSaveMutex);
	mSave = true;
}

void Controller::updatePatchersInfo() {

	mPatchersNode->clear_children();
	mDB.patchers([this](const std::string& name, int audio_inputs, int audio_outputs, int midi_inputs, int midi_outputs, const std::string& created_at) {
			auto r = mPatchersNode->create_child(name);

			{
				auto n = r->create_child("io");
				auto p = n->create_parameter(ossia::val_type::LIST);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				n->set(ossia::net::description_attribute{}, "input and output counts: audio ins, audio outs, midi ins, midi outs");

				std::vector<ossia::value> l;
				l.push_back(audio_inputs);
				l.push_back(audio_outputs);
				l.push_back(midi_inputs);
				l.push_back(midi_outputs);

				p->push_value(l);
			}

			{
				auto n = r->create_child("created_at");
				auto p = n->create_parameter(ossia::val_type::STRING);
				n->set(ossia::net::access_mode_attribute{}, ossia::access_mode::GET);
				p->push_value(created_at);
			}
	});
}

bool Controller::processEvents() {
	try {
		{
			std::lock_guard<std::mutex> guard(mOssiaContextMutex);
			ossia::net::poll_network_context(*mOssiaContext);
		}

		auto now = system_clock::now();
		{
			std::lock_guard<std::mutex> guard(mBuildMutex);
			if (mProcessAudio)
				mProcessAudio->processEvents();
			for (auto& i: mInstances)
				i.first->processEvents();
		}
		if (mDiskSpacePollNext <= now) {
			//XXX shouldn't need this mutex but removing listeners is causing this to throw an exception so
			//using a hammer to make sure that doesn't happen
			std::lock_guard<std::mutex> guard(mOssiaContextMutex);
			updateDiskSpace();
		}

		bool save = false;
		{
			//see if we got the save flag set, debounce
			std::lock_guard<std::mutex> guard(mSaveMutex);
			if (mSave) {
				mSave = false;
				mSaveNext = system_clock::now() + save_debounce_timeout;
			} else if (mSaveNext && mSaveNext.get() < now) {
				save = true;
				mSaveNext.reset();
			}
		}
		if (save) {
			saveLast();
		}
	} catch (const std::exception& e) {
		std::cerr << "exception in Controller::process thread " << e.what() << std::endl;
	}

	//TODO allow for quitting?
	return true;
}

void Controller::handleActive(bool active) {
	//TODO move to another thread?
	//clear out instances if we're deactivating
	if (!active) {
		std::lock_guard<std::mutex> guard(mBuildMutex);
		//save preset to reload on activation
		//might cause a glitch?
		if (mInstances.size()) {
			mInstanceLastPreset = mInstances.front().first->getPresetSync();
		} else {
			mInstanceLastPreset.reset();
		}
		clearInstances(guard);
	}


	bool wasActive = mProcessAudio->isActive();
	if (mProcessAudio->setActive(active) != active) {
		cerr << "couldn't set active" << endl;
		//XXX defer to setting not active
	} else if (!wasActive && mProcessAudio->isActive()) {
		//if the process audio became active and there are no instances loaded, try to load_last
		std::lock_guard<std::mutex> guard(mBuildMutex);
		if (!mInstances.size()) {
			//load last if we're activating from inactive
			mCommandQueue.push("load_last");
		}
	}
}

bool Controller::tryActivateAudio() {
	if (!mProcessAudio->isActive())
		mProcessAudio->setActive(true);
	return mProcessAudio->isActive();
}

void Controller::reportActive() {
	mAudioActive->set_value(mProcessAudio->isActive());
}

void Controller::clearInstances(std::lock_guard<std::mutex>&) {
	for (auto it = mInstances.begin(); it < mInstances.end(); it++) {
		auto index = std::to_string(it->first->index());
		if (!mInstancesNode->remove_child(index)) {
			std::cerr << "failed to remove instance node with index " << index << std::endl;
		}
	}
	mInstances.clear();
}

void Controller::unloadInstance(std::lock_guard<std::mutex>&, unsigned int index) {
	for (auto it = mInstances.begin(); it < mInstances.end(); it++) {
		if (it->first->index() == index) {
			mInstances.erase(it);
			if (!mInstancesNode->remove_child(std::to_string(index))) {
				std::cerr << "failed to remove instance node with index " << index << std::endl;
			}
			return;
		}
	}
}

void Controller::processCommands() {

	fs::path sourceCache = config::get<fs::path>(config::key::SourceCacheDir).get();
	fs::path compileCache = config::get<fs::path>(config::key::CompileCacheDir).get();
	//setup user defined location of the build program, if they've set it
	auto configBuildExe = config::get<fs::path>(config::key::SOBuildExe);
	if (configBuildExe && fs::exists(configBuildExe.get()))
		build_program = configBuildExe.get().string();

	//helper to validate and report as there are 2 different commands
	auto validateFileCmd = [this](std::string& id, RNBO::Json& cmdObj, RNBO::Json& params, bool withData) -> bool {
		//TODO assert that filename doesn't contain slashes so you can't specify files outside of the desired dir?
		if (!cmdObj.contains("params") || !params.contains("filename") || !params.contains("filetype") || (withData && !params.contains("data"))) {
			reportCommandError(id, static_cast<unsigned int>(FileCommandError::InvalidRequestObject), "request object invalid");
			return false;
		}
		reportCommandResult(id, {
			{"code", static_cast<unsigned int>(FileCommandStatus::Received)},
			{"message", "received"},
			{"progress", 1}
		});
		return true;
	};

	//helper to validate and report as there are 2 different commands
	auto validateListenerCmd = [this](std::string& id, RNBO::Json& cmdObj, RNBO::Json& params, std::pair<std::string, uint16_t>& key) -> bool {
		if (!cmdObj.contains("params") || !params.contains("ip") || !params.contains("port")) {
			reportCommandError(id, static_cast<unsigned int>(FileCommandError::InvalidRequestObject), "request object invalid");
			return false;
		}
		reportCommandResult(id, {
			{"code", static_cast<unsigned int>(ListenerCommandStatus::Received)},
			{"message", "received"},
			{"progress", 1}
		});
		auto ip = params["ip"].get<std::string>();
		auto port = static_cast<uint16_t>(params["port"].get<int>());
		key = std::make_pair(ip, port);
		return true;
	};

	auto fileCmdDir = [this, sourceCache](std::string& id, std::string filetype) -> boost::optional<fs::path> {
		boost::optional<fs::path> r;
		if (filetype == "datafile") {
			r = config::get<fs::path>(config::key::DataFileDir);
		} else if (filetype == "sourcefile") {
			r = sourceCache;
		} else {
			reportCommandError(id, static_cast<unsigned int>(FileCommandError::InvalidRequestObject), "unknown filetype " + filetype);
			return {};
		}
		if (!r) {
			reportCommandError(id, static_cast<unsigned int>(FileCommandError::Unknown), "no entry in config for filetype: " + filetype);
		}
		return r;
	};

	//clear all but oscquery
	auto listeners_clear = [this]() {
		for (auto& p: mProtocol->get_protocols()) {
			auto o = dynamic_cast<ossia::oscquery_asio::oscquery_server_protocol*>(p.get());
			if (!o) {
				mProtocol->stop_expose_to(*p);
			}
		}
	};

	auto listeners_add = [this](const std::string& ip, uint16_t port) {
		using conf = ossia::net::osc_protocol_configuration;
		auto protocol = ossia::net::make_osc_protocol(
				mOssiaContext,
				{
					.mode = conf::HOST,
					.version = conf::OSC1_1,
					.framing = conf::SLIP, //gcc doesn't like the default members, so we specify this even though it is a default
					.transport = ossia::net::udp_configuration {{
						.local = std::nullopt,
						.remote = ossia::net::send_socket_configuration {{ip, port}}
					}}
				}
			);
		mProtocol->expose_to(std::move(protocol));
	};

	boost::optional<CompileInfo> compileProcess;

	//wait for commands, then process them
	while (mProcessCommands.load()) {
		try {
			//adjust timeout
			auto wait = command_wait_timeout;
			const bool compiling = compileProcess && compileProcess->valid();
			if (compiling) {
				wait = compile_command_wait_timeout;
				//see if the process has completed
				if (!compileProcess->mProcess.running()) {
					//need to wait to get the correct exit code
					compileProcess->mProcess.wait();

					auto status = compileProcess->mProcess.exit_code();
					auto id = compileProcess->mCommandId;
					auto libPath = compileProcess->mLibPath;
					auto conf = compileProcess->mConf;
					auto confFileName = compileProcess->mConfFileName;
					auto instanceIndex = compileProcess->mInstanceIndex;
					auto maxRNBOVersion = compileProcess->mMaxRNBOVersion;
					compileProcess.reset();
					if (status != 0) {
						reportCommandError(id, static_cast<unsigned int>(CompileLoadError::CompileFailed), "compile failed with status: " + std::to_string(status));
					} else if (fs::exists(libPath)) {
						if (conf.contains("name") && conf["name"].is_string()) {
							std::string name = conf["name"].get<std::string>();

							int audio_inputs = 0;
							int audio_outputs = 0;
							int midi_inputs = 0;
							int midi_outputs = 0;

							if (conf.contains("numInputChannels")) {
								audio_inputs = conf["numInputChannels"].get<int>();
							}
							if (conf.contains("numOutputChannels")) {
								audio_outputs = conf["numOutputChannels"].get<int>();
							}
							if (conf.contains("numMidiInputPorts")) {
								midi_inputs = conf["numMidiInputPorts"].get<int>();
							}
							if (conf.contains("numMidiOutputPorts")) {
								midi_outputs = conf["numMidiOutputPorts"].get<int>();
							}

							mDB.patcherStore(name, libPath.filename(), confFileName, maxRNBOVersion, audio_inputs, audio_outputs, midi_inputs, midi_outputs);

							{
								std::lock_guard<std::mutex> guard(mBuildMutex);
								updatePatchersInfo();
							}
						}

						if (instanceIndex != boost::none) {
							reportCommandResult(id, {
								{"code", static_cast<unsigned int>(CompileLoadStatus::Compiled)},
								{"message", "compiled"},
								{"progress", 90}
							});
							loadLibrary(libPath.string(), id, conf, true, instanceIndex.get());
						} else {
							reportCommandResult(id, {
								{"code", static_cast<unsigned int>(CompileLoadStatus::Compiled)},
								{"message", "compiled"},
								{"progress", 100}
							});
						}
					} else {
						reportCommandError(id, static_cast<unsigned int>(CompileLoadError::LibraryNotFound), "couldn't find compiled library at " + libPath.string());
					}
				}
			}

			auto cmd = mCommandQueue.popTimeout(wait);
			if (!cmd)
				continue;
			std::string cmdStr = cmd.get();


			//internal commands
			if (cmdStr == "load_last") {
				//terminate existing compile
				compileProcess.reset();
				loadLast();
				continue;
			}

			auto cmdObj = RNBO::Json::parse(cmdStr);
			if (!cmdObj.contains("method") || !cmdObj.contains("id")) {
				cerr << "invalid cmd json" << cmdStr << endl;
				continue;
			}
			std::string id = cmdObj["id"];
			std::string method = cmdObj["method"];
			RNBO::Json params = cmdObj["params"];
			if (method == "compile_cancel") {
				//should terminate
				compileProcess.reset();
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(CompileLoadStatus::Cancelled)},
					{"message", "cancelled"},
					{"progress", 100}
				});
				continue;
			} else if (method == "compile") {
				//terminate existing
				compileProcess.reset();

				std::string timeTag = std::to_string(std::chrono::seconds(std::time(NULL)).count());
#if RNBO_USE_DBUS
				//update the outpdated package list
				if (mUpdateServiceProxy && params.contains("update_outdated") && params["update_outdated"].get<bool>()) {
					try {
						mUpdateServiceProxy->UpdateOutdated();
					} catch (...) { }
				}
#endif

				//support either a pre-written file or embedded "code"
				if (!cmdObj.contains("params") || !(params.contains("filename") || params.contains("code"))) {
					reportCommandError(id, static_cast<unsigned int>(CompileLoadError::InvalidRequestObject), "request object invalid");
					continue;
				}
				//get filename or generate one
				std::string fileName = params.contains("filename") ? params["filename"].get<std::string>() : ("rnbogen." + timeTag + ".cpp");
				fs::path sourceFile = fs::absolute(sourceCache / fileName);

				//write code if we have it
				if (params.contains("code")) {
					std::string code = params["code"];
					std::fstream f;
					f.open(sourceFile.string(), std::fstream::out | std::fstream::trunc);
					if (!f.is_open()) {
						reportCommandError(id, static_cast<unsigned int>(CompileLoadError::SourceWriteFailed), "failed to open file for write: " + sourceFile.string());
						continue;
					}
					f << code;
					f.close();
				}

				//make sure the source file exists
				if (!fs::exists(sourceFile)) {
					reportCommandError(id, static_cast<unsigned int>(CompileLoadError::SourceFileDoesNotExist), "cannot file source file: " + sourceFile.string());
					continue;
				}
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(CompileLoadStatus::Received)},
					{"message", "received"},
					{"progress", 10}
				});

				//create library name, based on time so we don't have to unload existing
				std::string libName = "RNBORunnerSO" + timeTag;

				fs::path libPath = fs::absolute(compileCache / fs::path(std::string(RNBO_DYLIB_PREFIX) + libName + "." + rnbo_dylib_suffix));
				//program path_to_generated.cpp libraryName pathToConfigFile
				std::vector<std::string> args = {
					sourceFile.string(), libName, config::get<fs::path>(config::key::RnboCPPDir).get().string(), config::get<fs::path>(config::key::CompileCacheDir).get().string()
				};
				auto cmake = config::get<fs::path>(config::key::CMakePath);
				if (cmake) {
					args.push_back(cmake.get().string());
				}

				//start compile
				{
					//config might be in a file
					RNBO::Json config;
					boost::optional<unsigned int> instanceIndex = 0;
					std::string confFileName;
					std::string maxRNBOVersion = "unknown";
					if (params.contains("config_file")) {
						confFileName = params["config_file"].get<std::string>();
						fs::path config_file = fs::absolute(sourceCache / confFileName);
						std::ifstream i(config_file.string());
						i >> config;
						i.close();
					} else if (params.contains("config")) {
						config = params["config"];
					}

					if (params.contains("rnbo_version")) {
						maxRNBOVersion = params["rnbo_version"].get<std::string>();
					}

					if (params.contains("load")) {
						if (params["load"].is_null()) {
							instanceIndex = boost::none;
						} else {
							instanceIndex = boost::make_optional(static_cast<unsigned int>(params["load"].get<int>()));
							std::lock_guard<std::mutex> guard(mBuildMutex);
							unloadInstance(guard, instanceIndex.get());
						}
					}
					compileProcess = CompileInfo(build_program, args, libPath, id, config, confFileName, maxRNBOVersion, instanceIndex);
				}

			} else if (method == "instance_load") {
				int index = params["index"].get<int>();
				std::string name = params["name"].get<std::string>();

				fs::path libPath;
				fs::path confPath;
				RNBO::Json config;
				if (mDB.patcherGetLatest(name, libPath, confPath)) {
					libPath = fs::absolute(compileCache / libPath);
					confPath = fs::absolute(sourceCache / confPath);

					if (fs::exists(libPath)) {
						if (fs::exists(confPath)) {
							std::ifstream i(confPath.string());
							i >> config;
							i.close();
						}
						loadLibrary(libPath.string(), std::string(), config, true, static_cast<unsigned int>(index));
					}
				}
			} else if (method == "instance_unload") {
				unsigned int index = static_cast<unsigned int>(params["index"].get<int>());
				std::lock_guard<std::mutex> guard(mBuildMutex);
				unloadInstance(guard, index);
				queueSave();
			} else if (method == "file_delete") {
				if (!validateFileCmd(id, cmdObj, params, false))
					continue;
				auto dir = fileCmdDir(id, params["filetype"]);
				if (!dir)
					continue;

				std::string fileName = params["filename"];
				fs::path filePath = dir.get() / fs::path(fileName);
				boost::system::error_code ec;
				if (fs::remove(filePath, ec)) {
					reportCommandResult(id, {
						{"code", static_cast<unsigned int>(FileCommandStatus::Completed)},
						{"message", "deleted"},
						{"progress", 100}
					});
				} else {
					reportCommandError(id, static_cast<unsigned int>(FileCommandError::DeleteFailed), "delete failed with message " + ec.message());
				}
			} else if (method == "file_write_extended" || method == "file_write") {
				if (!validateFileCmd(id, cmdObj, params, true))
					continue;

				auto dir = fileCmdDir(id, params["filetype"]);
				if (!dir)
					continue;

				//file_write_extended base64 encodes the file name so we can have non ascii in there
				std::string fileName = params["filename"];
				if (method == "file_write_extended" && !base64_decode_inplace(fileName)) {
					reportCommandError(id, static_cast<unsigned int>(FileCommandError::DecodeFailed), "failed to decode filename");
					continue;
				}
				fs::path filePath = dir.get() / fs::path(fileName);
				std::fstream fs;
				//allow for "append" to add to the end of an existing file
				bool append = params["append"].is_boolean() && params["append"].get<bool>();
				fs.open(filePath.string(), std::fstream::out | std::fstream::binary | (append ? std::fstream::app : std::fstream::trunc));
				if (!fs.is_open()) {
					reportCommandError(id, static_cast<unsigned int>(FileCommandError::WriteFailed), "failed to open file for write: " + filePath.string());
					continue;
				}

				std::string data = params["data"];
				std::vector<char> out(data.size()); //out will be smaller than the in data
				size_t read = 0;
				if (base64_decode(data.c_str(), data.size(), &out.front(), &read, 0) != 1) {
					reportCommandError(id, static_cast<unsigned int>(FileCommandError::DecodeFailed), "failed to decode data");
					continue;
				}
				fs.write(&out.front(), sizeof(char) * read);
				fs.close();

				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(FileCommandStatus::Completed)},
					{"message", "written"},
					{"progress", 100}
				});
			} else if (method == "listener_add") {
				std::pair<std::string, uint16_t> key;
				if (!validateListenerCmd(id, cmdObj, params, key))
					continue;

				if (mListeners.find(key) == mListeners.end()) {
					std::lock_guard<std::mutex> guard(mOssiaContextMutex);
					listeners_add(key.first, key.second);
					mListeners.insert(key);
				}

				updateListenersList();
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(ListenerCommandStatus::Completed)},
					{"message", "created"},
					{"progress", 100}
				});
			} else if (method == "listener_del") {
				std::pair<std::string, uint16_t> key;
				if (!validateListenerCmd(id, cmdObj, params, key))
					continue;

				//TODO visit listeners and only remove the one we care to remove.
				//at this point, finding the type is hard so we just remove them all and then add back the ones we care about
				if (mListeners.erase(key) != 0) {
					std::lock_guard<std::mutex> guard(mOssiaContextMutex);
					listeners_clear();
					for (auto& kv: mListeners) {
						listeners_add(kv.first, kv.second);
					}
					updateListenersList();
				}
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(ListenerCommandStatus::Completed)},
					{"message", "deleted"},
					{"progress", 100}
				});
			} else if (method == "listener_clear") {
				std::pair<std::string, uint16_t> key;
				if (!validateListenerCmd(id, cmdObj, params, key))
					continue;

				std::lock_guard<std::mutex> guard(mOssiaContextMutex);
				mListeners.clear();
				listeners_clear();
				updateListenersList();
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(ListenerCommandStatus::Completed)},
					{"message", "cleared"},
					{"progress", 100}
				});
			} else if (method == "install") {
#ifndef RNBO_USE_DBUS
				reportCommandError(id, static_cast<unsigned int>(InstallProgramError::NotEnabled), "self update not enabled for this runner instance");
				continue;
#else
				if (!mUpdateServiceProxy) {
					reportCommandError(id, static_cast<unsigned int>(InstallProgramError::NotEnabled), "dbus object does not exist");
					continue;
				}
				if (!cmdObj.contains("params") || !params.contains("version")) {
					reportCommandError(id, static_cast<unsigned int>(InstallProgramError::InvalidRequestObject), "request object invalid");
					continue;
				}
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(InstallProgramStatus::Received)},
					{"message", "signaling update service"},
					{"progress", 10}
				});
				std::string version = params["version"];
				try {
					if (!mUpdateServiceProxy->QueueRunnerInstall(version)) {
						reportCommandError(id, static_cast<unsigned int>(InstallProgramError::Unknown), "service reported error, check version string");
						continue;
					}
					reportCommandResult(id, {
							{"code", static_cast<unsigned int>(InstallProgramStatus::Completed)},
							{"message", "installation initiated"},
							{"progress", 100}
					});
				} catch (...) {
					reportCommandError(id, static_cast<unsigned int>(InstallProgramError::Unknown), "dbus reported error");
					continue;
				}
#endif
			} else {
				cerr << "unknown method " << method << endl;
				continue;
			}
		} catch (const std::exception& e) {
			cerr << "exception processing command " << e.what() << endl;
		} catch (...) {
			cerr << "unknown exception processing command " << endl;
		}
	}
}

void Controller::reportCommandResult(std::string id, RNBO::Json res) {
	reportCommandStatus(id, { {"result", res} });
}


void Controller::reportCommandError(std::string id, unsigned int code, std::string message) {
	reportCommandStatus(id, {
			{ "error",
			{
				{ "code", code },
				{ "message", message }
			}
			}
	});
}

void Controller::reportCommandStatus(std::string id, RNBO::Json obj) {
	obj["jsonrpc"] = "2.0";
	obj["id"] = id;
	std::string status = obj.dump();
	if (id == "internal") {
		std::cout << status << std::endl;
	} else {
		mResponseParam->push_value(status);
	}
}

void Controller::updateDiskSpace() {
		//could also look at sample dir?
		fs::space_info compileCacheSpace = fs::space(fs::absolute(config::get<fs::path>(config::key::CompileCacheDir).get()));
		auto available = compileCacheSpace.available;
		if (mDiskSpaceLast != available) {
			mDiskSpaceLast = available;
			if (mDiskSpaceParam)
				mDiskSpaceParam->push_value(std::to_string(mDiskSpaceLast));
		}
		mDiskSpacePollNext = system_clock::now() + mDiskSpacePollPeriod;
}

void Controller::updateListenersList() {
	std::vector<ossia::value> l;
	for (auto& kv: mListeners) {
		l.push_back(kv.first + ":" + std::to_string(kv.second));
	}
	mListenersListParam->push_value(l);
}

