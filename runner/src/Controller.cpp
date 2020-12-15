#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <utility>
#include <chrono>

#include "Controller.h"
#include "Config.h"
#include "Defines.h"
#include "JackAudio.h"
#include "PatcherFactory.h"

#define WORKAROUND_OSSIA_REMOVE_BUG

using std::cout;
using std::cerr;
using std::endl;

namespace fs = std::filesystem;

namespace {
	static const std::string rnbo_version(RNBO_VERSION);
	static const std::string rnbo_system_name(RNBO_SYSTEM_NAME);
	static const std::string rnbo_system_processor(RNBO_SYSTEM_PROCESSOR);
	static std::string build_program("rnbo-compile-so");
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
		auto n = info.create_string(it.first);
		n.set_access(opp::access_mode::Get);
		n.set_value(it.second);
		mNodes.push_back(n);
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
	mCommandQueueCondition.notify_one();
	mCommandThread.join();
}

void Controller::loadLibrary(const std::string& path, std::string cmdId) {
	mAudioActive.set_value(mProcessAudio->setActive(true));
	if (!mProcessAudio->isActive()) {
		cerr << "audio is not active, cannot created instance(s)" << endl;
		if (cmdId.size()) {
			reportCommandError(cmdId, static_cast<unsigned int>(CompileLoadError::AudioNotActive), "audio not active");
		}
		return;
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
	auto instance = new Instance(factory, "rnbo" + instIndex, builder);
	{
		std::lock_guard<std::mutex> guard(mBuildMutex);
		instance->start();
		mInstances.emplace_back(instance);
	}
	if (cmdId.size()) {
		reportCommandResult(cmdId, {
			{"code", static_cast<unsigned int>(CompileLoadStatus::Loaded)},
			{"message", "loaded"},
			{"progress", 100}
		});
	}
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
	std::lock_guard<std::mutex> guard(mCommandQueueMutex);
	mCommandQueue.push(data.to_string());
	mCommandQueueCondition.notify_one();
}

void Controller::clearInstances(std::lock_guard<std::mutex>&) {
	//NOTE there is a bug in ossia, remove_children and remove_child screws up the tree elsewhere, so for now we're simply stopping the other instances
#ifdef WORKAROUND_OSSIA_REMOVE_BUG
	for (auto& i: mInstances) {
		i->close();
	}
#else
	mInstancesNode.remove_children();
	mInstances.clear();
#endif
}

void Controller::processCommands() {
	fs::path sourceCache = config::get<fs::path>(config::key::SourceCacheDir);
	fs::path compileCache = config::get<fs::path>(config::key::CompileCacheDir);
	fs::create_directories(sourceCache);

	//setup user defined location of the build program, if they've set it
	std::string configBuildExe = config::get<std::string>(config::key::SOBuildExe);
	if (configBuildExe.size())
		build_program = config::make_path(configBuildExe);

	//TODO get from payload
	std::string fileName = "rnbogenerated.cpp";

	//wait for commands, then process them
	while (mProcessCommands.load()) {
		std::string cmdStr;
		//lock pop a command if there is one
		{
			std::unique_lock<std::mutex> guard(mCommandQueueMutex);
			if (mCommandQueue.empty()) {
				mCommandQueueCondition.wait(guard);
				continue;
			}
			cmdStr = mCommandQueue.front();
			mCommandQueue.pop();
		}

		auto cmdObj = RNBO::Json::parse(cmdStr);;
		if (!cmdObj.contains("method") || !cmdObj.contains("id")) {
			cerr << "invalid cmd json" << cmdStr << endl;
			continue;
		}
		std::string id = cmdObj["id"];
		std::string method = cmdObj["method"];
		if (method == "compile") {
			RNBO::Json params = cmdObj["params"];
			if (!cmdObj.contains("params") || !params.contains("code")) {
				reportCommandError(id, static_cast<unsigned int>(CompileLoadError::InvalidRequestObject), "request object invalid");
				continue;
			}
			std::string code = params["code"];
			fs::path generated = fs::absolute(sourceCache / fileName);
			std::fstream fs;
			fs.open(generated.u8string(), std::fstream::out | std::fstream::trunc);
			if (!fs.is_open()) {
				reportCommandError(id, static_cast<unsigned int>(CompileLoadError::SourceWriteFailed), "failed to open file for write: " + generated.u8string());
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
			std::string libName = "RNBORunnerSO" + std::to_string(std::chrono::seconds(std::time(NULL)).count());

			fs::path libPath = fs::absolute(compileCache / fs::path(std::string(RNBO_DYLIB_PREFIX) + libName + "." + std::string(RNBO_DYLIB_SUFFIX)));
			//program path_to_generated.cpp libraryName pathToConfigFile
			std::string buildCmd = build_program + " \"" + generated.u8string() + "\" \"" + libName + "\" \"" + fs::absolute(config::file_path()).u8string() + "\"";
			auto status = std::system(buildCmd.c_str());
			if (status != 0) {
				reportCommandError(id, static_cast<unsigned int>(CompileLoadError::CompileFailed), "compile failed with status: " + std::to_string(status));
			} else if (fs::exists(libPath)) {
				reportCommandResult(id, {
					{"code", static_cast<unsigned int>(CompileLoadStatus::Compiled)},
					{"message", "compiled"},
					{"progress", 90}
				});
				loadLibrary(libPath.u8string(), id);
			} else {
				reportCommandError(id, static_cast<unsigned int>(CompileLoadError::LibraryNotFound), "couldn't find compiled library at " + libPath.u8string());
			}
		} else {
			cerr << "unknown method " << method << endl;
			continue;
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
