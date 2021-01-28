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

	static const std::chrono::milliseconds command_wait_timeout(10);
	static const std::chrono::milliseconds save_debounce_timeout(500);

	static const std::string last_file_name = "last.json";

	static const std::string last_instances_key = "instances";
	static const std::string last_so_key = "so_path";
	static const std::string last_config_key = "config";


	fs::path lastFilePath() {
			return config::get<fs::path>(config::key::SaveDir) / last_file_name;
	}
}

Controller::Controller(std::string server_name) : mServer(server_name), mProcessCommands(true) {
	auto r = mServer.get_root_node().create_child("rnbo");
	mNodes.push_back(r);

	//expose some information
	auto info = r.create_child("info");
	info.set_description("information about RNBO and the running system");
	mNodes.push_back(info);
	for (auto it: {
			std::make_pair("version", rnbo_version),
			std::make_pair("system_name", rnbo_system_name),
			std::make_pair("system_processor", rnbo_system_processor),
			}) {
		auto n = info.create_string(it.first); n.set_access(opp::access_mode::Get);
		n.set_value(it.second);
		mNodes.push_back(n);
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
		c.set_value_callback([](void * context, const opp::value& val) {
			Controller * c = reinterpret_cast<Controller *>(context);
			c->handleCommand(val);
		}, this);
		mNodes.push_back(c);
	}
	{
		mResponseNode = r.create_string("resp");
		mResponseNode.set_description("command response");
		mResponseNode.set_access(opp::access_mode::Get);
	}

	auto j = r.create_child("jack");
	mNodes.push_back(j);
	NodeBuilder builder = [j, this](std::function<void(opp::node)> f) {
		std::lock_guard<std::mutex> guard(mBuildMutex);
		f(j);
	};

	mProcessAudio = std::unique_ptr<ProcessAudio>(new ProcessAudioJack(builder));
	mAudioActive = j.create_bool("active");
	mAudioActive.set_access(opp::access_mode::Bi);
	mAudioActive.set_value(mProcessAudio->isActive());
	mAudioActive.set_value_callback([](void* context, const opp::value& val) {
		Controller * c = reinterpret_cast<Controller *>(context);
		c->handleActive(val.is_bool() && val.to_bool());
	}, this);

	mInstancesNode = r.create_child("inst");
	mInstancesNode.set_description("RNBO codegen instances");

	mCommandThread = std::thread(&Controller::processCommands, this);
}

Controller::~Controller() {
	mProcessCommands.store(false);
	mCommandThread.join();
}

bool Controller::loadLibrary(const std::string& path, std::string cmdId, RNBO::Json conf) {
	//TODO make sure that the version numbers match in the name of the library

	mAudioActive.set_value(mProcessAudio->setActive(true));
	if (!mProcessAudio->isActive()) {
		cerr << "audio is not active, cannot created instance(s)" << endl;
		if (cmdId.size()) {
			reportCommandError(cmdId, static_cast<unsigned int>(CompileLoadError::AudioNotActive), "audio not active");
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
				queueSave(true);
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
	queueSave(true);
	return true;
}

bool Controller::loadLast() {
	try {
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
			if (!loadLibrary(so, std::string(), i[last_config_key])) {
				cerr << "failed to load so " << so << endl;
				return false;
			}
		}
		//load last only happens in the main thread, we don't need to save last again
		queueSave(false);
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

void Controller::queueSave(bool s) {
	std::lock_guard<std::mutex> guard(mSaveMutex);
	mSave = s;
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
	if (mProcessAudio->setActive(active) != active) {
		cerr << "couldn't set active" << endl;
		//XXX deffer to setting not active
	}
}

void Controller::handleCommand(const opp::value& data) {
	mCommandQueue.push(data.to_string());
}

void Controller::clearInstances(std::lock_guard<std::mutex>&) {
	mInstancesNode.remove_children();
	mInstances.clear();
}

void Controller::processCommands() {
	fs::path sourceCache = config::get<fs::path>(config::key::SourceCacheDir);
	fs::path compileCache = config::get<fs::path>(config::key::CompileCacheDir);
	//setup user defined location of the build program, if they've set it
	auto configBuildExe = config::get<fs::path>(config::key::SOBuildExe);
	if (!configBuildExe.empty() && fs::exists(configBuildExe))
		build_program = configBuildExe.string();

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
	auto fileCmdDir = [this](std::string& id, std::string filetype) -> fs::path {
		if (filetype == "datafile")
			return config::get<fs::path>(config::key::DataFileDir);
		if (filetype == "programfile")
			return config::get<fs::path>(config::key::ProgramFileDir);
		reportCommandError(id, static_cast<unsigned int>(FileCommandError::InvalidRequestObject), "unknown filetype " + filetype);
		return {};
	};

	//wait for commands, then process them
	while (mProcessCommands.load()) {
		try {
			auto cmd = mCommandQueue.popTimeout(command_wait_timeout);
			if (!cmd)
				continue;
			std::string cmdStr = cmd.get();
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

				fs::path libPath = fs::absolute(compileCache / fs::path(std::string(RNBO_DYLIB_PREFIX) + libName + "." + std::string(RNBO_DYLIB_SUFFIX)));
				//program path_to_generated.cpp libraryName pathToConfigFile
				std::string buildCmd = build_program;
				for (auto a: { generated.string(), libName, config::get<fs::path>(config::key::RnboCPPDir).string(), config::get<fs::path>(config::key::CompileCacheDir).string() }) {
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
				fs::path dir = fileCmdDir(id, params["filetype"]);
				if (dir.empty())
					continue;

				std::string fileName = params["filename"];
				fs::path filePath = dir / fs::path(fileName);
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

				fs::path dir = fileCmdDir(id, params["filetype"]);
				if (dir.empty())
					continue;

				std::string fileName = params["filename"];
				fs::path filePath = dir / fs::path(fileName);
				std::fstream fs;
				fs.open(filePath.string(), std::fstream::out | std::fstream::trunc | std::fstream::binary);
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
				//TODO this is sketchy, figure out a better way
				if (!cmdObj.contains("params") || !params.contains("uri")) {
					reportCommandError(id, static_cast<unsigned int>(InstallProgramError::InvalidRequestObject), "request object invalid");
					continue;
				}
				std::string uri = params["uri"];
				if (uri.find("file://") == 0) {
					//only allow files in the programfile_dir
					auto p = config::get<fs::path>(config::key::ProgramFileDir) / fs::path(uri).leaf();
					if (!fs::exists(p)) {
						reportCommandError(id, static_cast<unsigned int>(InstallProgramError::InvalidRequestObject), "file not found at path: " + p.string());
						continue;
					}
					uri = "file://" + p.string();
				}
				//TODO assert no injection in uri
				reportCommandResult(id, {
						{"code", static_cast<unsigned int>(InstallProgramStatus::Received)},
						{"message", "received"},
						{"progress", 10}
						});
				auto bstr = [](std::string k) -> std::string { return config::get<bool>(k) ? "true" : "false" ; };
				std::string installCmd = config::get<fs::path>(config::key::SelfUpdateExe).string()
					+ " --install-prefix " + config::get<std::string>(config::key::InstallPrefix)
					+ " --use-sudo " + bstr(config::key::InstallUseSudo)
					+ " --ld-config " + bstr(config::key::InstallDoLDConfig)
					+ " --restart-service " + bstr(config::key::InstallDoRestart)
					+ " " + uri
				;
				auto status = std::system(installCmd.c_str());
				if (status != 0) {
					reportCommandError(id, static_cast<unsigned int>(InstallProgramError::Unknown), "install failed");
				} else {
					reportCommandResult(id, {
							{"code", static_cast<unsigned int>(InstallProgramStatus::Completed)},
							{"message", "installed"},
							{"progress", 100}
							});
				}
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
		fs::space_info compileCacheSpace = fs::space(fs::absolute(config::get<fs::path>(config::key::CompileCacheDir)));
		auto available = compileCacheSpace.available;
		if (mDiskSpaceLast != available) {
			mDiskSpaceLast = available;
			mDiskSpaceNode.set_value(std::to_string(mDiskSpaceLast));
		}
		mDiskSpacePollNext = system_clock::now() + mDiskSpacePollPeriod;
}
