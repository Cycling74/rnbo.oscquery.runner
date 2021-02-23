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
#include "ValueCallbackHelper.h"

#include <boost/algorithm/string/predicate.hpp>

#ifdef RNBO_USE_DBUS

#include <core/dbus/bus.h>
#include <core/dbus/service.h>
#include <core/dbus/signal.h>
#include <core/dbus/object.h>

#include <core/dbus/asio/executor.h>
#include <core/dbus/types/stl/tuple.h>
#include <core/dbus/types/stl/vector.h>
#include <core/dbus/types/struct.h>

#include "IRnboUpdateService.h"

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

Controller::Controller(std::string server_name) : mServer(server_name), mProcessCommands(true) {
	//tell the ossia server to echo updates sent from remote clients (so other clients seem them)
	mServer.set_echo(true);
	auto r = mServer.get_root_node().create_child("rnbo");

	//expose some information
	auto info = r.create_child("info");
	info.set_description("information about RNBO and the running system");
	for (auto it: {
			std::make_pair("version", rnbo_version),
			std::make_pair("system_name", rnbo_system_name),
			std::make_pair("system_processor", rnbo_system_processor),
			}) {
		auto n = info.create_string(it.first);
		n.set_access(opp::access_mode::Get);
		n.set_value(it.second);
	}

	{
		//ossia doesn't seem to support 64bit integers, so we use a string as 31 bits
		//might not be enough to indicate disk space
		mDiskSpaceNode = info.create_string("disk_bytes_available");
		updateDiskSpace();
	}

	{
		auto c = r.create_string("cmd");
		c.set_description("command handler");
		c.set_access(opp::access_mode::Set);
		ValueCallbackHelper::setCallback(
				c, mValueCallbackHelpers,
				[this](const opp::value& val) {
					if (val.is_string()) {
						mCommandQueue.push(val.to_string());
					}
			});

	}
	{
		mResponseNode = r.create_string("resp");
		mResponseNode.set_description("command response");
		mResponseNode.set_access(opp::access_mode::Get);
	}

	auto j = r.create_child("jack");
	NodeBuilder builder = [j, this](std::function<void(opp::node)> f) {
		std::lock_guard<std::mutex> guard(mBuildMutex);
		f(j);
	};

	mProcessAudio = std::unique_ptr<ProcessAudio>(new ProcessAudioJack(builder));
	mAudioActive = j.create_bool("active");
	mAudioActive.set_access(opp::access_mode::Bi);
	mAudioActive.set_value(mProcessAudio->isActive());
	ValueCallbackHelper::setCallback(
			mAudioActive, mValueCallbackHelpers,
			[this](const opp::value& val) {
				if (val.is_bool()) {
					handleActive(val.to_bool());
				}
		});

	mInstancesNode = r.create_child("inst");
	mInstancesNode.set_description("RNBO codegen instances");

//TODO enable dbus based upgrades
#ifdef RNBO_USE_DBUS
	//setup dbus
	mDBusBus = std::make_shared<core::dbus::Bus>(core::dbus::WellKnownBus::system);
	auto ex = core::dbus::asio::make_executor(mDBusBus);
	mDBusBus->install_executor(ex);
	mDBusThread = std::thread(std::bind(&core::dbus::Bus::run, mDBusBus));
	mDBusService = core::dbus::Service::use_service(mDBusBus, core::dbus::traits::Service<IRnboUpdateService>::interface_name());
	if (mDBusService) {
		mDBusObject = mDBusService->object_for_path(IRnboUpdateService::Methods::InstallRunner::object_path());
	}
	if (!mDBusService || !mDBusObject) {
		cerr << "failed to get rnbo dbus update object" << endl;
	} else {
		//TODO figure out how to get signals working
#if 0
		auto sig = mDBusObject->get_signal<IRnboUpdateService::Signals::InstallStatus>();
		if (sig) {
			sig->connect([](const IRnboUpdateService::Signals::InstallStatus::ArgumentType& args) {
					std::cout << "install_status " << std::get<0>(args) << " " << std::get<1>(args) << endl;
					});
		} else {
			cerr << "failed to get dbus install_status signal" << endl;
		}
#endif
	}
#endif

	//let the outside know if this instance supports up/downgrade
	{
		mSupportsInstall = info.create_bool("supports_install");
		mSupportsInstall.set_description("Does this runner support remote upgrade/downgrade");
		mSupportsInstall.set_access(opp::access_mode::Get);
		mSupportsInstall.set_value(false);
#ifdef RNBO_USE_DBUS
		//TODO use property to make sure that the service exists
		//mSupportsInstall.set_value(static_cast<bool>(mDBusObject));
#endif
	}

	mCommandThread = std::thread(&Controller::processCommands, this);
}

Controller::~Controller() {
	mProcessCommands.store(false);
	mCommandThread.join();
#ifdef RNBO_USE_DBUS
	mDBusBus->stop();
	if (mDBusThread.joinable()) {
		mDBusThread.join();
	}
#endif
}

bool Controller::loadLibrary(const std::string& path, std::string cmdId, RNBO::Json conf, bool saveConfig) {
	auto fname = fs::path(path).filename().string();
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
	if (!mProcessAudio->isActive())
		mAudioActive.set_value(mProcessAudio->setActive(true));
	if (!mProcessAudio->isActive()) {
		cerr << "audio is not active, cannot create instance(s)" << endl;
		if (cmdId.size()) {
			reportCommandError(cmdId, static_cast<unsigned int>(CompileLoadError::AudioNotActive), "cannot activate audio");
		}
		return false;
	}
	//make sure that no other instances can be created while this is active
	std::lock_guard<std::mutex> iguard(mInstanceMutex);
	auto factory = PatcherFactory::CreateFactory(path);
	opp::node instNode;
	std::string instIndex;
	{
		std::lock_guard<std::mutex> guard(mBuildMutex);
		clearInstances(guard);
		instIndex = std::to_string(mInstances.size());
		instNode = mInstancesNode.create_child(instIndex);
	}
	auto builder = [instNode, this](std::function<void(opp::node)> f) {
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
	} catch (std::exception& e) {
		cerr << "exception " << e.what() << " trying to load last setup" << endl;
	}
	return false;
}

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
	{
		std::lock_guard<std::mutex> guard(mBuildMutex);
		for (auto& i: mInstances)
			i.first->processEvents();
	}
	if (mDiskSpacePollNext <= system_clock::now())
		updateDiskSpace();

	bool save = false;
	{
		//see if we got the save flag set, debounce
		std::lock_guard<std::mutex> guard(mSaveMutex);
		if (mSave) {
			mSave = false;
			mSaveNext = system_clock::now() + save_debounce_timeout;
		} else if (mSaveNext && mSaveNext.get() < system_clock::now()) {
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
		//XXX deffer to setting not active
	} else if (!wasActive) {
		//load last if we're activating from inactive
		mCommandQueue.push("load_last");
	}
}

void Controller::clearInstances(std::lock_guard<std::mutex>&) {
	mInstancesNode.remove_children();
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
			} else if (method == "install") {
#ifndef RNBO_USE_DBUS
				reportCommandError(id, static_cast<unsigned int>(InstallProgramError::NotEnabled), "self update not enabled for this runner instance");
				continue;
#else
				if (!mDBusObject) {
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
				bool upgradeOther = params.contains("upgrade_other") && params["upgrade_other"].is_boolean() && params["upgrade_other"].get<bool>();
				try {
					mDBusObject->invoke_method_synchronously<RnboUpdateService::InstallRunner, void, std::string, bool>(version, upgradeOther);
				} catch (const std::runtime_error& e) {
					cerr << "failed to request upgrade: " << e.what() << endl;
					reportCommandError(id, static_cast<unsigned int>(InstallProgramError::Unknown), e.what());
				}
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(InstallProgramStatus::Completed)},
					{"message", "installation initiated"},
					{"progress", 100}
				});
#endif
			} else {
				cerr << "unknown method " << method << endl;
				continue;
			}
		} catch (std::exception& e) {
			cerr << "exception processing command" << e.what() << endl;
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
	mResponseNode.set_value(status);
}

void Controller::updateDiskSpace() {
		//could also look at sample dir?
		fs::space_info compileCacheSpace = fs::space(fs::absolute(config::get<fs::path>(config::key::CompileCacheDir).get()));
		auto available = compileCacheSpace.available;
		if (mDiskSpaceLast != available) {
			mDiskSpaceLast = available;
			mDiskSpaceNode.set_value(std::to_string(mDiskSpaceLast));
		}
		mDiskSpacePollNext = system_clock::now() + mDiskSpacePollPeriod;
}
