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
using std::chrono::system_clock;

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
	parser.add_option("-q", "--quiet")
		.action("store_false")
		.dest("verbose")
		.set_default("1")
		.help("don't print status messages to stdout");

	optparse::Values options = parser.parse_args(argc, argv);
	std::vector<std::string> args = parser.args();

	if (options.get("verbose"))
		cout << options["filename"] << endl;

	std::signal(SIGINT, signal_handler);

	//initialize the config
	//TODO, optionally set the config file path
	config::init();

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
	for (auto key: {config::key::DataFileDir, config::key::SaveDir, config::key::SourceCacheDir, config::key::CompileCacheDir}) {
		fs::create_directories(config::get<fs::path>(key).get());
	}
	{
		Controller c("rnbo:" + hostName);
		if (options["filename"].size()) {
			c.loadLibrary(options["filename"]);
		} else if (config::get<bool>(config::key::InstanceAutoStartLast)){
			c.loadLast();
		}

		auto config_timeout = std::chrono::seconds(1);
		std::chrono::time_point<std::chrono::system_clock> config_poll_next = system_clock::now() + config_timeout;
		while (c.process() && mRun.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			if (config_poll_next <= system_clock::now()) {
				config_poll_next = system_clock::now() + config_timeout;
				config::write_if_dirty();
			}
		}
	}
	return 0;
}
