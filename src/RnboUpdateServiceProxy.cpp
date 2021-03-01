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

void RnboUpdateServiceProxy::setActiveCallback(std::function<void(bool)> cb) {
	std::lock_guard<std::mutex> guard(mCallbackMutex);
	mActiveCallback = cb;
}
void RnboUpdateServiceProxy::setStatusCallback(std::function<void(std::string)> cb) {
	std::lock_guard<std::mutex> guard(mCallbackMutex);
	mStatusCallback = cb;
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
		} else if (key == "Active") {
			if (mActiveCallback && var.containsValueOfType<bool>()) {
				mActiveCallback(var.get<bool>());
			}
		}
	}
}