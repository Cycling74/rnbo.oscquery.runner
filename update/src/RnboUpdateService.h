#include "IRnboUpdateService.h"
#include "Queue.h"

#include <core/dbus/skeleton.h>
#include <core/dbus/bus.h>
#include <core/dbus/object.h>
#include <core/dbus/property.h>
#include <core/dbus/signal.h>
#include <core/dbus/interfaces/properties.h>

#include <memory>
#include <map>

class RnboUpdateService: public core::dbus::Skeleton<IRnboUpdateService>
{
	public:
		typedef std::shared_ptr<RnboUpdateService> Ptr;
    RnboUpdateService(const core::dbus::Bus::Ptr& bus);
		~RnboUpdateService() = default;

		void evaluate_commands();
		bool queue_runner_install(const std::string& version);

		static bool version_valid(const std::string& version);

	protected:
		void handle_queue_install_runner(const core::dbus::Message::Ptr& msg);
		void signal_property_changes(const std::map<std::string, core::dbus::types::Variant> props);

	private:
		void update_active(bool active, const std::string status);
		void update_status(const std::string status);

		bool exec(const std::string cmd);
		core::dbus::Object::Ptr mObject;
		std::shared_ptr<core::dbus::Property<IRnboUpdateService::Properties::Active>> mPropActive;
		std::shared_ptr<core::dbus::Property<IRnboUpdateService::Properties::Status>> mPropStatus;

		Queue<std::string> mRunnerInstallQueue;
};
