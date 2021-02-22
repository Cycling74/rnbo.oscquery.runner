#include <core/dbus/service.h>
#include <core/dbus/types/object_path.h>

//rnbo update service dbus configuration info
class IRnboUpdateService {
	public:
		virtual ~IRnboUpdateService() = default;

		struct InstallRunner
		{
			inline static std::string name() { return "install_runner"; };
			typedef IRnboUpdateService Interface;
			inline static const std::chrono::milliseconds default_timeout() { return std::chrono::seconds{1}; }

			static core::dbus::types::ObjectPath object_path() {
				static core::dbus::types::ObjectPath p("/com/cycling74/rnbo");
				return p;
			}
		};
		struct Signals
		{
			struct InstallStatus
			{
				inline static std::string name() { return "install_status"; };
				typedef IRnboUpdateService Interface;
				typedef std::tuple<bool, std::string> ArgumentType;
			};
		};
		struct Properties
		{
			struct Active
			{
				inline static std::string name() { return "active"; };
				typedef IRnboUpdateService Interface;
				typedef bool ValueType;
				static const bool readable = true;
				static const bool writable = false;
			};

			struct Status
			{
				inline static std::string name() { return "status"; };
				typedef IRnboUpdateService Interface;
				typedef std::string ValueType;
				static const bool readable = true;
				static const bool writable = false;
			};
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
