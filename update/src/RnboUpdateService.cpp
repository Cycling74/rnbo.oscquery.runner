#include "RnboUpdateService.h"
#include <iostream>
#include <regex>
#include <stdlib.h>

namespace {
	const std::string RUNNER_PACKAGE_NAME = "rnbooscquery";
	//https://www.debian.org/doc/debian-policy/ch-controlfields.html#version
	const std::regex VERSION_VALID_REGEX(R"X(^(?:\d+:)?\d[[:alnum:]\.\+\-~]*?(?:-[[:alnum:]\+\.~]+)?$)X");
}

RnboUpdateService::RnboUpdateService(const core::dbus::Bus::Ptr& bus) :
			core::dbus::Skeleton<IRnboUpdateService>(bus),
			mObject(access_service()->add_object_for_path(IRnboUpdateService::object_path()))
{
	mObject->install_method_handler<IRnboUpdateService::Methods::QueueRunnerInstall>(std::bind(&RnboUpdateService::handle_queue_install_runner, this, std::placeholders::_1));
	mPropActive = mObject->get_property<IRnboUpdateService::Properties::Active>();
	mPropActive->set(false);

	mPropStatus = mObject->get_property<IRnboUpdateService::Properties::Status>();
	mPropStatus->set("waiting");

	signal_property_changes({
		{IRnboUpdateService::Properties::Active::name(), core::dbus::types::TypedVariant<bool>(mPropActive->get())},
		{IRnboUpdateService::Properties::Status::name(), core::dbus::types::TypedVariant<std::string>(mPropStatus->get())}
	});
	//mObject->emit_signal<IRnboUpdateService::Signals::Foo, IRnboUpdateService::Signals::Foo::ArgumentType>(false);
}


void RnboUpdateService::evaluate_commands() {
	auto o = mRunnerInstallQueue.tryPop();
	if (o) {
		setenv("DEBIAN_FRONTEND", "noninteractive", 1);
		std::string packageVersion = RUNNER_PACKAGE_NAME + "=" + o.get();
		bool success = false;
		update_active(true, "updating package list");
		if (!exec("apt-get -y update")) {
			update_status("apt-get update failed, attempting to install anyway");
		}
		//TODO set property for number of packages that need updating
		update_status("installing " + packageVersion);
		success = exec("apt-get install -y --allow-change-held-packages --allow-downgrades " + packageVersion);
		update_status("marking " + RUNNER_PACKAGE_NAME + "hold");
		//always mark hold
		exec("apt-mark hold " + RUNNER_PACKAGE_NAME);
		update_active(false, "install of " + packageVersion + " " + (success ? "success" : "failure"));
	}
}

bool RnboUpdateService::exec(const std::string cmd) {
	//wait for lock
	std::string fullCmd = "while fuser /var/{lib/{dpkg,apt/lists},cache/apt/archives}/lock >/dev/null 2>&1; do sleep 1; done &&" + cmd;
	auto status = std::system(fullCmd.c_str());
	if (status != 0) {
		std::cerr << "failed " << cmd << " with status " << status << std::endl;
	}
	return status == 0;
}

bool RnboUpdateService::queue_runner_install(const std::string& version) {
	//make sure the version string is valid (so we don't allow injection)
	if (!std::regex_match(version, VERSION_VALID_REGEX))
		return false;
	mRunnerInstallQueue.push(version);
	return true;
}

void RnboUpdateService::handle_queue_install_runner(const core::dbus::Message::Ptr& msg) {
	std::string arg;
	msg->reader() >> arg;
	auto out = queue_runner_install(arg);
	auto reply = core::dbus::Message::make_method_return(msg);
	reply->writer() << out;
	access_bus()->send(reply);
}

void RnboUpdateService::signal_property_changes(const std::map<std::string, core::dbus::types::Variant> props) {
	core::dbus::interfaces::Properties::Signals::PropertiesChanged::ArgumentType args(core::dbus::traits::Service<IRnboUpdateService>::interface_name(), props, {});
	mObject->emit_signal<core::dbus::interfaces::Properties::Signals::PropertiesChanged>(args);
}

void RnboUpdateService::update_active(bool active, const std::string status) {
	mPropActive->set(active);
	mPropStatus->set(status);
	signal_property_changes({
		{IRnboUpdateService::Properties::Active::name(), core::dbus::types::TypedVariant<bool>(mPropActive->get())},
		{IRnboUpdateService::Properties::Status::name(), core::dbus::types::TypedVariant<std::string>(mPropStatus->get())}
	});
}

void RnboUpdateService::update_status(const std::string status) {
	mPropStatus->set(status);
	signal_property_changes({
		{IRnboUpdateService::Properties::Status::name(), core::dbus::types::TypedVariant<std::string>(mPropStatus->get())}
	});
}
