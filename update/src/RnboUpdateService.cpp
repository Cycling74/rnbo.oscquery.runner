#include "RnboUpdateService.h"
#include <iostream>

RnboUpdateService::RnboUpdateService(const core::dbus::Bus::Ptr& bus) :
			core::dbus::Skeleton<IRnboUpdateService>(bus),
			mObject(access_service()->add_object_for_path(IRnboUpdateService::object_path()))
{
	mObject->install_method_handler<IRnboUpdateService::InstallRunner>( std::bind(&RnboUpdateService::handle_install_runner, this, std::placeholders::_1));
	mPropActive = mObject->get_property<IRnboUpdateService::Properties::Active>();
	mPropActive->set(true);

	mPropStatus = mObject->get_property<IRnboUpdateService::Properties::Status>();
	mPropStatus->set("blah blah");

	auto changed_signal = mObject->get_signal<core::dbus::interfaces::Properties::Signals::PropertiesChanged>();
	core::dbus::interfaces::Properties::Signals::PropertiesChanged::ArgumentType
		args(
				core::dbus::traits::Service<IRnboUpdateService>::interface_name(),
				{
				{IRnboUpdateService::Properties::Active::name(), core::dbus::types::TypedVariant<bool>(false)},
				{IRnboUpdateService::Properties::Status::name(), core::dbus::types::TypedVariant<std::string>("blah blah")}
				},
				{});
	mObject->emit_signal<core::dbus::interfaces::Properties::Signals::PropertiesChanged, core::dbus::interfaces::Properties::Signals::PropertiesChanged::ArgumentType>(args);

	//mObject->emit_signal<IRnboUpdateService::Signals::Foo, IRnboUpdateService::Signals::Foo::ArgumentType>(false);
}

void RnboUpdateService::install_runner(std::string version, bool upgradeOther) {
	std::cout << "install runner " << version << (upgradeOther ? "upgrade" : "") << std::endl;
	//TODO
}

void RnboUpdateService::handle_install_runner(const core::dbus::Message::Ptr& msg) {
	std::tuple<std::string, bool> args;
	msg->reader() >> args;
	install_runner(std::get<0>(args), std::get<1>(args));
	auto reply = core::dbus::Message::make_method_return(msg);
	//reply->writer() << out;
	access_bus()->send(reply);
}
