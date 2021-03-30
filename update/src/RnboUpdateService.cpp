#include "RnboUpdateService.h"
#include "Validation.h"
#include <iostream>
#include <stdlib.h>
#include <boost/algorithm/string/join.hpp>

namespace {
	const std::string RUNNER_PACKAGE_NAME = "rnbooscquery";

	//https://stackoverflow.com/questions/478898/how-do-i-execute-a-command-and-get-the-output-of-the-command-within-c-using-po
	std::pair<bool, std::vector<std::string>> exec(const std::string cmd) {
		std::array<char, 128> buffer;
		std::vector<std::string> result;
		std::string fullCmd = "while fuser /var/{lib/{dpkg,apt/lists},cache/apt/archives}/lock >/dev/null 2>&1; do sleep 1; done && " + cmd + " 2>&1";
		FILE* pipe = popen(fullCmd.c_str(), "r");
		if (!pipe) {
			throw std::runtime_error("popen() failed!");
		}
		while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
			std::cout << buffer.data() << std::endl;
			result.push_back(buffer.data());
		}
		return std::make_pair(WEXITSTATUS(pclose(pipe)) == 0, result);
	}

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
		updateState(RunnerUpdateState::Active, "updating package list");
		auto r = exec("apt-get -y update");
		if (r.first) {
			updateStatus("apt-get update failed, attempting to install anyway");
		}
		//TODO set property for number of packages that need updating
		updateStatus("installing " + packageVersion);
		r = exec("apt-get install -y --allow-change-held-packages --allow-downgrades " + packageVersion);
		bool success = r.first;
		updateStatus("marking " + RUNNER_PACKAGE_NAME + "hold");
		//always mark hold
		exec("apt-mark hold " + RUNNER_PACKAGE_NAME);

		std::string msg = "install of " + packageVersion;
		if (success) {
			msg += " success";
		} else {
			//find any E messages and append
			std::vector<std::string> err;
			for (auto l: r.second) {
				if (l.rfind("E: ", 0) == 0)
					err.push_back(l.substr(3));
			}
			if (err.size()) {
				msg += " failed with errors: " + boost::algorithm::join(err, ", ");
			} else {
				msg += " failed for unknown reasons";
			}
		}
		updateState(success ? RunnerUpdateState::Idle : RunnerUpdateState::Failed, msg);
	}
}

bool RnboUpdateService::QueueRunnerInstall(const std::string& version) {
	//make sure the version string is valid (so we don't allow injection)
	if (!validation::version(version))
		return false;
	mRunnerInstallQueue.push(version);
	return true;
}

uint32_t RnboUpdateService::State() { return static_cast<uint32_t>(mState); }
std::string RnboUpdateService::Status() { return mStatus; }

void RnboUpdateService::updateState(RunnerUpdateState state, const std::string status) {
	mState = state;
	mStatus = status;
	emitPropertiesChangedSignal(rnbo_adaptor::INTERFACE_NAME, {"State", "Status"});
}

void RnboUpdateService::updateStatus(const std::string status) {
	mStatus = status;
	emitPropertiesChangedSignal(rnbo_adaptor::INTERFACE_NAME, {"Status"});
}
