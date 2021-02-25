#include "RnboUpdateService.h"

#include <memory>
#include <iostream>
#include <chrono>
#include <thread>

#include <core/dbus/bus.h>
#include <core/dbus/service.h>
#include <core/dbus/announcer.h>

#include <core/dbus/asio/executor.h>
#include <core/dbus/types/stl/tuple.h>
#include <core/dbus/types/stl/vector.h>
#include <core/dbus/types/struct.h>

using std::cout;
using std::cerr;
using std::endl;

int main(int argc, const char * argv[]) {
	auto bus = std::make_shared<core::dbus::Bus>(core::dbus::WellKnownBus::system);
	auto ex = core::dbus::asio::make_executor(bus);
	bus->install_executor(ex);
	if (!ex) {
		throw std::runtime_error("failed to create executor");
	}
	auto t = std::thread(std::bind(&core::dbus::Bus::run, bus));

	auto service = core::dbus::announce_service_on_bus<IRnboUpdateService, RnboUpdateService>(bus);
	if (!t.joinable()) {
		throw std::runtime_error("failed to bind run thread");
	}

	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		service->evaluate_commands();
	}

	return 0;
}
