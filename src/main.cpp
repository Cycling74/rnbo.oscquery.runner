#include <chrono>
#include <thread>
#include <csignal>
#include <OptionParser.h>
#include <boost/filesystem.hpp>

#include "Controller.h"
#include "Config.h"

//for gethostname
#include <unistd.h>
#include <limits.h>

using optparse::OptionParser;
using std::cout;
using std::cerr;
using std::endl;
using std::chrono::steady_clock;

namespace fs = boost::filesystem;

#include <iostream>

namespace {
	std::atomic<bool> mRun(true);;
}

void signal_handler(int signal) {
	mRun.store(false);
}

int main(int argc, const char * argv[]) {
	OptionParser parser = OptionParser().description("rnbo so runner");

	parser.add_option("-f", "--file")
		.dest("filename")
		.help("load dynamic library FILE")
		.metavar("FILE");
	parser.add_option("-c", "--config")
		.dest("config")
		.help("set the path to the configuration file FILE")
		.metavar("FILE");
	parser.add_option("-w", "--write-config")
		.action("store_true")
		.dest("write_config")
		.set_default("0")
		.help("write the default configuration to the config file");
	parser.add_option("-W", "--wait-for-audio")
		.action("store_true")
		.dest("wait_for_audio")
		.set_default("0")
		.help("wait for audio (jack server) to be active before trying to start");
	parser.add_option("-q", "--quiet")
		.action("store_false")
		.dest("verbose")
		.set_default("1")
		.help("don't print status messages to stdout");
	parser.add_option("-v", "--version")
		.action("store_true")
		.dest("version")
		.set_default("0")
		.help("print out version info");

	optparse::Values options = parser.parse_args(argc, argv);
	std::vector<std::string> args = parser.args();

	bool verbose = options.get("verbose");
	if (verbose) {
		cout << options["filename"] << endl;
	}

	//optionally set the config file path
	if (options["config"].size()) {
		config::set_file_path(options["config"]);
	}

	std::signal(SIGINT, signal_handler);

	//initialize the config
	config::init();

	//write config file and exit
	if (options.get("write_config")) {
		config::write_file();
		cout << "wrote config to path: " << config::file_path().string() << std::endl;
		return 0;
	}

	if (options.get("version")) {
		std::string rnbo_version(RNBO_VERSION);
		cout << "runner rnbo version: " << rnbo_version << std::endl;
		return 0;
	}


	//get the host name (or override)
	auto host = config::get<std::string>(config::key::HostNameOverride);
	std::string hostName;
	if (host) {
		hostName = host.value();
	} else {
		char hostname[_POSIX_HOST_NAME_MAX];
		gethostname(hostname, _POSIX_HOST_NAME_MAX);
		hostName = std::string(hostname);
	}

	//make sure these directories exists, so we can write to them
	for (auto key: {config::key::DataFileDir, config::key::SaveDir, config::key::SourceCacheDir, config::key::CompileCacheDir, config::key::BackupDir}) {
		fs::create_directories(config::get<fs::path>(key).get());
	}
	{
		Controller c("rnbo:" + hostName);

		if (options.get("wait_for_audio")) {
			//loop and wait for audio
			while (!c.tryActivateAudio(false)) {
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			}
		}

#ifndef RNBO_OSCQUERY_BUILTIN_PATCHER
		if (options["filename"].size()) {
			c.loadLibrary(options["filename"]);
		} else if (config::get<bool>(config::key::InstanceAutoStartLast).value_or(true)){
			std::string set = config::get<std::string>(config::key::SetLastName).value_or(LAST_SET_NAME);
			c.loadInitialSet(set);
		}
#else
			c.loadBuiltIn();
#endif

		auto config_timeout = std::chrono::seconds(1);
		std::chrono::time_point<std::chrono::steady_clock> config_poll_next = steady_clock::now() + config_timeout;
		while (c.processEvents() && mRun.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
			if (config_poll_next <= steady_clock::now()) {
				config_poll_next = steady_clock::now() + config_timeout;
				config::write_if_dirty();
			}
		}
	}
	return 0;
}
