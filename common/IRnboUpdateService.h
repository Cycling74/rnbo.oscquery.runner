#include <core/dbus/service.h>
#include <core/dbus/macros.h>
#include <core/dbus/types/object_path.h>
#include <core/dbus/types/struct.h>

//rnbo update service dbus configuration info
class IRnboUpdateService {
	public:
		virtual ~IRnboUpdateService() = default;

		virtual bool queue_runner_install(const std::string& version) = 0;

		static core::dbus::types::ObjectPath object_path() {
			static core::dbus::types::ObjectPath p("/com/cycling74/rnbo");
			return p;
		}

		struct Methods {
			DBUS_CPP_METHOD_DEF(QueueRunnerInstall, IRnboUpdateService)
		};

		struct Signals
		{
			//macro doesn't like us providing this type directly
			typedef core::dbus::types::Struct<std::tuple<bool, std::string>> InstallStatusArg;
			DBUS_CPP_SIGNAL_DEF(InstallStatus, IRnboUpdateService, InstallStatusArg)
		};

		struct Properties
		{
			DBUS_CPP_READABLE_PROPERTY_DEF(Active, IRnboUpdateService, bool);
			DBUS_CPP_READABLE_PROPERTY_DEF(Status, IRnboUpdateService, std::string);
		};
};

//for some reason it seems that you have to alter the namespace provided by the library in order to create your service
namespace core
{
	namespace dbus
	{
		namespace traits
		{
			template<>
				struct Service<IRnboUpdateService>
				{
					inline static const std::string& interface_name()
					{
						static const std::string s("com.cycling74.rnbo");
						return s;
					}
				};
		}
	}
}
