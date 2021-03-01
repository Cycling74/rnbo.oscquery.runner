#include "RnboUpdateService.h"
#include "Validation.h"
#include <iostream>
#include <stdlib.h>

namespace {
	const std::string RUNNER_PACKAGE_NAME = "rnbooscquery";
}

RnboUpdateService::RnboUpdateService(sdbus::IConnection& connection, std::string objectPath)
: sdbus::AdaptorInterfaces<com::cycling74::rnbo_adaptor, sdbus::Properties_adaptor>(connection, std::move(objectPath))
{
	registerAdaptor();
}

RnboUpdateService::~RnboUpdateService()
{
	unregisterAdaptor();
}


void RnboUpdateService::evaluateCommands() {
	auto o = mRunnerInstallQueue.tryPop();
	if (o) {
		setenv("DEBIAN_FRONTEND", "noninteractive", 1);
		std::string packageVersion = RUNNER_PACKAGE_NAME + "=" + o.get();
		bool success = false;
		updateActive(true, "updating package list");
		if (!exec("apt-get -y update")) {
			updateStatus("apt-get update failed, attempting to install anyway");
		}
		//TODO set property for number of packages that need updating
		updateStatus("installing " + packageVersion);
		success = exec("apt-get install -y --allow-change-held-packages --allow-downgrades " + packageVersion);
		updateStatus("marking " + RUNNER_PACKAGE_NAME + "hold");
		//always mark hold
		exec("apt-mark hold " + RUNNER_PACKAGE_NAME);
		updateActive(false, "install of " + packageVersion + " " + (success ? "success" : "failure"));
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

bool RnboUpdateService::QueueRunnerInstall(const std::string& version) {
	//make sure the version string is valid (so we don't allow injection)
	if (!validation::version(version))
		return false;
	mRunnerInstallQueue.push(version);
	return true;
}

bool RnboUpdateService::Active() { return mActive; }
std::string RnboUpdateService::Status() { return mStatus; }

void RnboUpdateService::updateActive(bool active, const std::string status) {
	mActive = active;
	mStatus = status;
	emitPropertiesChangedSignal(rnbo_adaptor::INTERFACE_NAME, {"Active", "Status"});
}

void RnboUpdateService::updateStatus(const std::string status) {
	mStatus = status;
	emitPropertiesChangedSignal(rnbo_adaptor::INTERFACE_NAME, {"Status"});
}
