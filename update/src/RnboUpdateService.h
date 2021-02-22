#include "DBusService.h"

#include <core/dbus/skeleton.h>
#include <core/dbus/bus.h>
#include <core/dbus/object.h>

class RnboUpdateService: public core::dbus::Skeleton<IRnboUpdateService>
{
	public:
		typedef std::shared_ptr<RnboUpdateService> Ptr;

    RnboUpdateService(const core::dbus::Bus::Ptr& bus) :
			core::dbus::Skeleton<IRnboUpdateService>(bus),
			mObject(access_service()->add_object_for_path(IRnboUpdateService::InstallRunner::object_path())) { }
	private:
		core::dbus::Object::Ptr mObject;
};
