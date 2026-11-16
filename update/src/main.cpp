#include "RnboUpdateService.h"

#include <memory>
#include <iostream>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <string>

#include "RnboUpdateService.h"

using std::cout;
using std::cerr;
using std::endl;

int main(int argc, const char * argv[]) {
	//report the version and exit, without touching dbus
	for (int i = 1; i < argc; i++) {
		const std::string arg(argv[i]);
		if (arg == "-v" || arg == "--version") {
			cout << UPDATE_SERVICE_VERSION << endl;
			return 0;
		}
	}

	const char* serviceName = "com.cycling74.rnbo";
	auto connection = sdbus::createSystemBusConnection(serviceName);
	if (!connection)
		throw std::runtime_error("could not create system dbus connection");

	{
		RnboUpdateService service(*connection);
		while (true) {
			connection->processPendingRequest();
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			service.evaluateCommands();
		}
	}

	return 0;
}
