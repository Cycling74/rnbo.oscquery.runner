#include "RnboUpdateServiceProxy.h"
#include <iostream>

using std::cout;
using std::endl;

RnboUpdateServiceProxy::RnboUpdateServiceProxy(std::string destination, std::string objectPath)
	: sdbus::ProxyInterfaces<com::cycling74::rnbo_proxy, sdbus::Properties_proxy>(sdbus::createSystemBusConnection(), std::move(destination), std::move(objectPath))
{
	registerProxy();
}

RnboUpdateServiceProxy::~RnboUpdateServiceProxy() { unregisterProxy(); }

void RnboUpdateServiceProxy::setStateCallback(std::function<void(RunnerUpdateState)> cb) {
	std::lock_guard<std::mutex> guard(mCallbackMutex);
	mStateCallback = cb;
	if (cb) {
		try {
			RunnerUpdateState state;
			uint32_t val = getProxy().getProperty("State").onInterface(com::cycling74::rnbo_proxy::INTERFACE_NAME);
			if (runner_update::from(val, state)) {
				cb(state);
			}
		} catch (...) {
		}
	}
}
void RnboUpdateServiceProxy::setStatusCallback(std::function<void(std::string)> cb) {
	std::lock_guard<std::mutex> guard(mCallbackMutex);
	mStatusCallback = cb;
	if (cb) {
		try {
			std::string val = getProxy().getProperty("Status").onInterface(com::cycling74::rnbo_proxy::INTERFACE_NAME);
			cb(val);
		} catch (...) {
		}
	}
}

void RnboUpdateServiceProxy::setOutdatedPackagesCallback(std::function<void(uint32_t)> cb) {
	std::lock_guard<std::mutex> guard(mCallbackMutex);
	mOutdatedPackagesCallback = cb;
	if (cb) {
		try {
			uint32_t val = getProxy().getProperty("OutdatedPackages").onInterface(com::cycling74::rnbo_proxy::INTERFACE_NAME);
			cb(val);
		} catch (...) {
		}
	}
}

void RnboUpdateServiceProxy::onPropertiesChanged(
		const std::string& interfaceName,
		const std::map<std::string, sdbus::Variant>& changedProperties,
		const std::vector<std::string>& invalidatedProperties) {
	std::lock_guard<std::mutex> guard(mCallbackMutex);
	for (auto& kv: changedProperties) {
		auto& key = kv.first;
		auto& var = kv.second;
		if (key == "Status") {
			if (mStatusCallback && var.containsValueOfType<std::string>()) {
				mStatusCallback(var.get<std::string>());
			}
		} else if (key == "State") {
			RunnerUpdateState state;
			if (mStateCallback && var.containsValueOfType<uint32_t>() && runner_update::from(var.get<uint32_t>(), state)) {
				mStateCallback(state);
			}
		} else if (key == "OutdatedPackages") {
			if (mOutdatedPackagesCallback && var.containsValueOfType<uint32_t>()) {
				mOutdatedPackagesCallback(var.get<uint32_t>());
			}
		}
	}
}
