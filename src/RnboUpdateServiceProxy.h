#pragma once

#include <map>
#include <vector>
#include <sdbus-c++/sdbus-c++.h>
#include "UpdateServiceProxyGlue.h"

class RnboUpdateServiceProxy : public sdbus::ProxyInterfaces<com::cycling74::rnbo_proxy, sdbus::Properties_proxy>
{
	public:
		RnboUpdateServiceProxy(std::string destination = std::string(com::cycling74::rnbo_proxy::INTERFACE_NAME), std::string objectPath = "/com/cycling74/rnbo");
		virtual ~RnboUpdateServiceProxy();
	protected:
		virtual void onPropertiesChanged(
				const std::string& interfaceName,
				const std::map<std::string, sdbus::Variant>& changedProperties,
				const std::vector<std::string>& invalidatedProperties);
};
