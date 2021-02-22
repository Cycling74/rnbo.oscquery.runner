#include <core/dbus/service.h>

//rnbo update service dbus configuration info
struct RnboUpdateService {
	struct InstallRunner
	{
		inline static std::string name() { return "install_runner"; };
		typedef RnboUpdateService Interface;
		inline static const std::chrono::milliseconds default_timeout() { return std::chrono::seconds{1}; }
	};
	struct Signals
	{
		struct InstallStatus
		{
			inline static std::string name() { return "install_status"; };
			typedef RnboUpdateService Interface;
			typedef std::tuple<bool, std::string> ArgumentType;
		};
	};
	struct Properties
	{
		struct Active
		{
			inline static std::string name() { return "active"; };
			typedef RnboUpdateService Interface;
			typedef bool ValueType;
			static const bool readable = true;
			static const bool writable = false;
		};

		struct Status
		{
			inline static std::string name() { return "status"; };
			typedef RnboUpdateService Interface;
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
				struct Service<RnboUpdateService>
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
