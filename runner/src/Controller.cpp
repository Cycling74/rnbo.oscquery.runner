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

using std::cout;
using std::cerr;
using std::endl;

namespace fs = std::filesystem;

namespace {
	static const std::string rnbo_version(RNBO_VERSION);
	static const std::string rnbo_system_name(RNBO_SYSTEM_NAME);
	static const std::string rnbo_system_processor(RNBO_SYSTEM_PROCESSOR);
	static const std::string build_program("rnbo-compile-so");
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

void Controller::loadLibrary(const std::string& path) {
	mAudioActive.set_value(mProcessAudio->setActive(true));
	if (!mProcessAudio->isActive()) {
		cerr << "audio is not active, cannot created instance(s)" << endl;
		return;
	}
	//make sure that no other instances can be created while this is active
	std::lock_guard<std::mutex> iguard(mInstanceMutex);
	auto factory = PatcherFactory::CreateFactory(path);
	opp::node instNode;
	{
		std::lock_guard<std::mutex> guard(mBuildMutex);
		clearInstances(guard);
		instNode = mInstancesNode.create_child("0");
	}
	auto builder = [instNode, this](std::function<void(opp::node)> f) {
		std::lock_guard<std::mutex> guard(mBuildMutex);
		f(instNode);
	};
	auto instance = new Instance(factory, "rnbo0", builder);
	{
		std::lock_guard<std::mutex> guard(mBuildMutex);
		instance->start();
		mInstances.emplace_back(instance);
	}
}

void Controller::handleActive(bool active) {
	//TODO move to another thread?
	//clear out instances if we're deactivating
	if (!active) {
		std::lock_guard<std::mutex> guard(mBuildMutex);
		clearInstances(guard);
	}
	mAudioActive.set_value(mProcessAudio->setActive(active));
}

void Controller::handleCommand(const opp::value& data) {
	std::lock_guard<std::mutex> guard(mCommandQueueMutex);
	mCommandQueueCondition.notify_one();
	mCommandQueue.push(data.to_string());
}

void Controller::clearInstances(std::lock_guard<std::mutex>&) {
	mInstancesNode.remove_children();
	mInstances.clear();
}

void Controller::processCommands() {
	fs::path sourceCache = config::get<fs::path>(config::key::SourceCacheDir);
	fs::path compileCache = config::get<fs::path>(config::key::CompileCacheDir);
	fs::create_directories(sourceCache);

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
		if (!cmdObj.contains("method")) {
			cerr << "invalid cmd json" << cmdStr << endl;
			continue;
		}
		std::string method = cmdObj["method"];
		if (method == "compile") {
			if (!cmdObj.contains("params")) {
				cerr << "cannot find params" << endl;
				continue;
			}
			RNBO::Json params = cmdObj["params"];
			if (!params.contains("code")) {
				cerr << "cannot find code" << endl;
				continue;
			}

			fs::path generated = fs::absolute(sourceCache / fileName);
			std::fstream fs;
			fs.open(generated.u8string(), std::fstream::out | std::fstream::trunc);
			if (!fs.is_open()) {
				cerr << "failed to open file " << generated << endl;
				continue;
			}
			std::string code = params["code"];
			fs << code;
			fs.close();
			cout << "got new codegen" << endl;

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
				cerr << "build failed with status " << status << endl;
			} else if (fs::exists(libPath)) {
				loadLibrary(libPath.u8string());
			} else {
				cerr << "couldn't find compiled library at " << libPath << endl;
			}
		} else {
			cerr << "unknown method " << method << endl;
			continue;
		}
	}
}
