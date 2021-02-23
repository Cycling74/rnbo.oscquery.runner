#include "DBusService.h"

#include <core/dbus/skeleton.h>
#include <core/dbus/bus.h>
#include <core/dbus/object.h>
#include <core/dbus/property.h>

#include <memory>

class RnboUpdateService: public core::dbus::Skeleton<IRnboUpdateService>
{
	public:
		typedef std::shared_ptr<RnboUpdateService> Ptr;
    RnboUpdateService(const core::dbus::Bus::Ptr& bus);
		~RnboUpdateService() = default;
		void install_runner(std::string version, bool upgradeOther);
	protected:
		void handle_install_runner(const core::dbus::Message::Ptr& msg);
	private:
		core::dbus::Object::Ptr mObject;
		std::shared_ptr<core::dbus::Property<IRnboUpdateService::Properties::Active>> mPropActive;
};
