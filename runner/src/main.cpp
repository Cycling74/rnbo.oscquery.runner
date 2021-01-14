#include <chrono>
#include <thread>
#include <filesystem>
#include <OptionParser.h>

#include "Controller.h"
#include "Config.h"

//for gethostname
#include <unistd.h>
#include <limits.h>

using optparse::OptionParser;
using std::cout;
using std::endl;

namespace fs = std::filesystem;

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

	//initialize the config
	//TODO, optionally set the config file path
	config::init();

	//get the host name (or override)
	auto host = config::get<std::string>(config::key::HostNameOverride);
	if (!host.size()) {
		char hostname[_POSIX_HOST_NAME_MAX];
		gethostname(hostname, _POSIX_HOST_NAME_MAX);
		host = std::string(hostname);
	}

	//make sure our data directory exists, so we can write to it
	fs::create_directories(config::get<fs::path>(config::key::DataFileDir));

	//TODO figure out why the sidebar doesn't like colons in names.. would like this to be "rnbo:hostname"
	Controller c("rnbo-" + host);
	if (options["filename"].size())
		c.loadLibrary(options["filename"]);
	while (c.process()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return 0;
}
