#include "DBusService.h"
#include <memory>

#include <core/dbus/bus.h>
#include <core/dbus/service.h>
#include <core/dbus/signal.h>
#include <core/dbus/object.h>

#include <core/dbus/asio/executor.h>
#include <core/dbus/types/stl/tuple.h>
#include <core/dbus/types/stl/vector.h>
#include <core/dbus/types/struct.h>

int main(int argc, const char * argv[]) {
	std::thread mDBusThread;
	std::shared_ptr<core::dbus::Bus> mDBusBus;
	std::shared_ptr<core::dbus::Service> mDBusService;
	std::shared_ptr<core::dbus::Object> mDBusObject;

	mDBusBus = std::make_shared<core::dbus::Bus>(core::dbus::WellKnownBus::system);
	auto ex = core::dbus::asio::make_executor(mDBusBus);
	mDBusBus->install_executor(ex);
	mDBusService = core::dbus::Service::use_service(mDBusBus, core::dbus::traits::Service<RnboUpdateService>::interface_name());
	if (mDBusService) {
		mDBusObject = mDBusService->object_for_path(core::dbus::types::ObjectPath("/com/cycling74/rnbo"));
	}
	if (!mDBusService || !mDBusObject) {
		cerr << "failed to get rnbo dbus update object" << endl;
	} else {
		//TODO figure out how to get signals working
		auto sig = mDBusObject->get_signal<RnboUpdateService::Signals::InstallStatus>();
		if (sig) {
			sig->connect([](const RnboUpdateService::Signals::InstallStatus::ArgumentType& args) {
					std::cout << "install_status " << std::get<0>(args) << " " << std::get<1>(args) << endl;
					});
		} else {
			cerr << "failed to get dbus install_status signal" << endl;
		}
	}
	mDBusBus->run();
	return 0;
}
