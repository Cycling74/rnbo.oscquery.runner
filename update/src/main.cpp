#include "RnboUpdateService.h"
#include <memory>
#include <iostream>

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
	std::thread mDBusThread;
	std::shared_ptr<core::dbus::Bus> mDBusBus;

	mDBusBus = std::make_shared<core::dbus::Bus>(core::dbus::WellKnownBus::system);
	auto ex = core::dbus::asio::make_executor(mDBusBus);
	mDBusBus->install_executor(ex);
	auto service = core::dbus::announce_service_on_bus<IRnboUpdateService, RnboUpdateService>(mDBusBus);
	mDBusBus->run();
	return 0;
}
