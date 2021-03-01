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

void RnboUpdateServiceProxy::onPropertiesChanged(
		const std::string& interfaceName,
		const std::map<std::string, sdbus::Variant>& changedProperties,
		const std::vector<std::string>& invalidatedProperties) {
	cout << "got prop change " << endl;
	for (auto& kv: changedProperties) {
		cout << "key " << kv.first << endl;
	}
	//TODO
}
