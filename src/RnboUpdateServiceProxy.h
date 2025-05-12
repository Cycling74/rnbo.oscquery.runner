#pragma once

#include <map>
#include <vector>
#include <functional>
#include <mutex>

#include <sdbus-c++/sdbus-c++.h>
#include "UpdateServiceProxyGlue.h"
#include "RunnerUpdateState.h"

class RnboUpdateServiceProxy : public sdbus::ProxyInterfaces<com::cycling74::rnbo_proxy, sdbus::Properties_proxy>
{
	public:
		RnboUpdateServiceProxy(std::string destination = std::string(com::cycling74::rnbo_proxy::INTERFACE_NAME), std::string objectPath = "/com/cycling74/rnbo");
		virtual ~RnboUpdateServiceProxy();

		void setStateCallback(std::function<void(RunnerUpdateState)> cb);
		void setStatusCallback(std::function<void(std::string)> cb);
		void setOutdatedPackagesCallback(std::function<void(uint32_t)> cb);
		void setLatestRunnerVersionCallback(std::function<void(std::string)> cb);
	protected:
		std::function<void(RunnerUpdateState)> mStateCallback;
		std::function<void(std::string)> mStatusCallback;
		std::function<void(uint32_t)> mOutdatedPackagesCallback;
		std::function<void(std::string)> mLatestRunnerVersionCallback;
		std::mutex mCallbackMutex;

		virtual void onPropertiesChanged(
				const std::string& interfaceName,
				const std::map<std::string, sdbus::Variant>& changedProperties,
				const std::vector<std::string>& invalidatedProperties);
};
