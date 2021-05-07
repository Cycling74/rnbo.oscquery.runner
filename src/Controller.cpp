#include <iostream>
#include <fstream>
#include <cstdlib>
#include <utility>
#include <chrono>
#include <libbase64.h>
#include <iomanip>

#include "Controller.h"
#include "Config.h"
#include "Defines.h"
#include "JackAudio.h"
#include "PatcherFactory.h"

#include <boost/algorithm/string/predicate.hpp>

#include <ossia/detail/config.hpp>
#include <ossia/context.hpp>
#include <ossia/network/oscquery/oscquery_server.hpp>
#include <ossia/network/osc/osc.hpp>
#include <ossia/network/local/local.hpp>
#include <ossia/network/generic/generic_device.hpp>
#include <ossia/network/generic/generic_parameter.hpp>

#ifdef RNBO_USE_DBUS
#include "RunnerUpdateState.h"
#include "RnboUpdateServiceProxy.h"
#endif

using std::cout;
using std::cerr;
using std::endl;
using std::chrono::system_clock;

namespace fs = boost::filesystem;

namespace {

	static const std::string rnbo_version(RNBO_VERSION);
	static const std::string rnbo_system_name(RNBO_SYSTEM_NAME);
	static const std::string rnbo_system_processor(RNBO_SYSTEM_PROCESSOR);
	static std::string build_program("rnbo-compile-so");

	static const std::string rnbo_dylib_suffix(RNBO_DYLIB_SUFFIX);

	static const std::chrono::milliseconds command_wait_timeout(10);
	static const std::chrono::milliseconds save_debounce_timeout(500);

	static const std::string last_file_name = "last.json";

	static const std::string last_instances_key = "instances";
	static const std::string last_so_key = "so_path";
	static const std::string last_config_key = "config";


	fs::path lastFilePath() {
			return config::get<fs::path>(config::key::SaveDir).get() / last_file_name;
	}

}


Controller::Controller(std::string server_name) : mProcessCommands(true) {
	mProtocol = new ossia::net::multiplex_protocol();

	auto serv_proto = new ossia::oscquery::oscquery_server_protocol(1234, 5678);

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
	}

	auto j = root->create_child("jack");
	NodeBuilder builder = [j, this](std::function<void(ossia::net::node_base *)> f) {
		std::lock_guard<std::mutex> guard(mBuildMutex);
		f(j);
	};

	mProcessAudio = std::unique_ptr<ProcessAudio>(new ProcessAudioJack(builder));

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

	mInstancesNode = root->create_child("inst");
	mInstancesNode->set(ossia::net::description_attribute{}, "command response");

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

bool Controller::loadLibrary(const std::string& path, std::string cmdId, RNBO::Json conf, bool saveConfig) {
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
			return false;
		}
		//make sure that no other instances can be created while this is active
		std::lock_guard<std::mutex> iguard(mInstanceMutex);
		auto factory = PatcherFactory::CreateFactory(path);
		ossia::net::node_base * instNode = nullptr;
		std::string instIndex;
		{
			std::lock_guard<std::mutex> guard(mBuildMutex);
			clearInstances(guard);
			instIndex = std::to_string(mInstances.size());
			instNode = mInstancesNode->create_child(instIndex);
		}
		auto builder = [instNode, this](std::function<void(ossia::net::node_base*)> f) {
			std::lock_guard<std::mutex> guard(mBuildMutex);
			f(instNode);
		};
		auto instance = new Instance(factory, "rnbo" + instIndex, builder, conf);
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
		{
			std::lock_guard<std::mutex> guard(mBuildMutex);
			clearInstances(guard);
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
			//load library but don't save config
			if (!loadLibrary(so, std::string(), i[last_config_key], false)) {
				cerr << "failed to load so " << so << endl;
				return false;
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
			auto instance = new Instance(factory, "rnbo" + instIndex, builder, {});
			{
				std::lock_guard<std::mutex> guard(mBuildMutex);
				instance->start();
				mInstances.emplace_back(std::make_pair(instance, fs::path()));
			}
		}
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

bool Controller::process() {
	auto now = system_clock::now();
	{
		std::lock_guard<std::mutex> guard(mBuildMutex);
		if (mProcessAudio)
			mProcessAudio->processEvents();
		for (auto& i: mInstances)
			i.first->processEvents();
	}
	if (mDiskSpacePollNext <= now)
		updateDiskSpace();

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

	//TODO allow for quitting?
	return true;
}

void Controller::handleActive(bool active) {
	//TODO move to another thread?
	//clear out instances if we're deactivating
	if (!active) {
		std::lock_guard<std::mutex> guard(mBuildMutex);
		clearInstances(guard);
	}
	bool wasActive = mProcessAudio->isActive();
	if (mProcessAudio->setActive(active) != active) {
		cerr << "couldn't set active" << endl;
		//XXX defer to setting not active
	} else if (!wasActive) {
		//load last if we're activating from inactive
		mCommandQueue.push("load_last");
	}
}

bool Controller::tryActivateAudio() {
	if (!mProcessAudio->isActive())
		mAudioActive->push_value(mProcessAudio->setActive(true));
	return mProcessAudio->isActive();
}

void Controller::clearInstances(std::lock_guard<std::mutex>&) {
	mInstancesNode->clear_children();
	mInstances.clear();
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
	auto validateListenerCmd = [this](std::string& id, RNBO::Json& cmdObj, RNBO::Json& params, std::string& ip, uint16_t& port, std::string& key) -> bool {
		if (!cmdObj.contains("params") || !params.contains("ip") || !params.contains("port")) {
			reportCommandError(id, static_cast<unsigned int>(FileCommandError::InvalidRequestObject), "request object invalid");
			return false;
		}
		reportCommandResult(id, {
			{"code", static_cast<unsigned int>(ListenerCommandStatus::Received)},
			{"message", "received"},
			{"progress", 1}
		});
		ip = params["ip"].get<std::string>();
		port = static_cast<uint16_t>(params["port"].get<int>());
		key = ip + ":" + std::to_string(port);
		return true;
	};

	auto fileCmdDir = [this](std::string& id, std::string filetype) -> boost::optional<fs::path> {
		boost::optional<fs::path> r;
		if (filetype == "datafile") {
			r = config::get<fs::path>(config::key::DataFileDir);
		} else {
			reportCommandError(id, static_cast<unsigned int>(FileCommandError::InvalidRequestObject), "unknown filetype " + filetype);
			return {};
		}
		if (!r) {
			reportCommandError(id, static_cast<unsigned int>(FileCommandError::Unknown), "no entry in config for filetype: " + filetype);
		}
		return r;
	};

	//wait for commands, then process them
	while (mProcessCommands.load()) {
		try {
			auto cmd = mCommandQueue.popTimeout(command_wait_timeout);
			if (!cmd)
				continue;
			std::string cmdStr = cmd.get();

			//internal commands
			if (cmdStr == "load_last") {
				loadLast();
				continue;
			}

			auto cmdObj = RNBO::Json::parse(cmdStr);;
			if (!cmdObj.contains("method") || !cmdObj.contains("id")) {
				cerr << "invalid cmd json" << cmdStr << endl;
				continue;
			}
			std::string id = cmdObj["id"];
			std::string method = cmdObj["method"];
			RNBO::Json params = cmdObj["params"];
			if (method == "compile") {
				std::string timeTag = std::to_string(std::chrono::seconds(std::time(NULL)).count());

				//TODO get from payload
				std::string fileName = "rnbogenerated." + timeTag + ".cpp";

				if (!cmdObj.contains("params") || !params.contains("code")) {
					reportCommandError(id, static_cast<unsigned int>(CompileLoadError::InvalidRequestObject), "request object invalid");
					continue;
				}
				std::string code = params["code"];
				fs::path generated = fs::absolute(sourceCache / fileName);
				std::fstream fs;
				fs.open(generated.string(), std::fstream::out | std::fstream::trunc);
				if (!fs.is_open()) {
					reportCommandError(id, static_cast<unsigned int>(CompileLoadError::SourceWriteFailed), "failed to open file for write: " + generated.string());
					continue;
				}
				fs << code;
				fs.close();
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(CompileLoadStatus::Received)},
					{"message", "received"},
					{"progress", 10}
				});

				//clear out instances in prep
				{
					std::lock_guard<std::mutex> guard(mBuildMutex);
					clearInstances(guard);
				}

				//create library name, based on time so we don't have to unload existing
				std::string libName = "RNBORunnerSO" + timeTag;

				fs::path libPath = fs::absolute(compileCache / fs::path(std::string(RNBO_DYLIB_PREFIX) + libName + "." + rnbo_dylib_suffix));
				//program path_to_generated.cpp libraryName pathToConfigFile
				std::string buildCmd = build_program;
				for (auto a: { generated.string(), libName, config::get<fs::path>(config::key::RnboCPPDir).get().string(), config::get<fs::path>(config::key::CompileCacheDir).get().string() }) {
					buildCmd += (" \"" + a + "\"");
				}
				auto status = std::system(buildCmd.c_str());
				if (status != 0) {
					reportCommandError(id, static_cast<unsigned int>(CompileLoadError::CompileFailed), "compile failed with status: " + std::to_string(status));
				} else if (fs::exists(libPath)) {
					reportCommandResult(id, {
						{"code", static_cast<unsigned int>(CompileLoadStatus::Compiled)},
						{"message", "compiled"},
						{"progress", 90}
					});
					loadLibrary(libPath.string(), id, params["config"]);
				} else {
					reportCommandError(id, static_cast<unsigned int>(CompileLoadError::LibraryNotFound), "couldn't find compiled library at " + libPath.string());
				}
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
			} else if (method == "file_write") {
				if (!validateFileCmd(id, cmdObj, params, true))
					continue;

				auto dir = fileCmdDir(id, params["filetype"]);
				if (!dir)
					continue;

				std::string fileName = params["filename"];
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
				std::string ip, key;
				uint16_t port;
				if (!validateListenerCmd(id, cmdObj, params, ip, port, key))
					continue;

				if (mListeners.find(key) == mListeners.end()) {
					auto protcol = new ossia::net::osc_protocol(ip, port);
					mProtocol->expose_to(std::unique_ptr<ossia::net::protocol_base>(protcol));
					mListeners.insert(key);
				}

				updateListenersList();
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(ListenerCommandStatus::Completed)},
					{"message", "created"},
					{"progress", 100}
				});
			} else if (method == "listener_del") {
				std::string ip, key;
				uint16_t port;
				if (!validateListenerCmd(id, cmdObj, params, ip, port, key))
					continue;

				if (mListeners.erase(key) != 0) {
					for (auto& p: mProtocol->get_protocols()) {
						auto o = dynamic_cast<ossia::net::osc_protocol*>(p.get());
						if (o) {
							if (o->get_ip() == ip && o->get_remote_port() == port) {
								mProtocol->stop_expose_to(*p);
								break;
							}
						}
					}
				}

				updateListenersList();
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(ListenerCommandStatus::Completed)},
					{"message", "deleted"},
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
	for (auto e: mListeners) {
		l.push_back(e);
	}
	mListenersListParam->push_value(l);
}

