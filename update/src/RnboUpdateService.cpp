#include "RnboUpdateService.h"
#include "Validation.h"
#include <iostream>
#include <stdlib.h>
#include <functional>
#include <boost/algorithm/string/join.hpp>

namespace {
	const std::string RUNNER_PACKAGE_NAME = "rnbooscquery";

	//https://stackoverflow.com/questions/478898/how-do-i-execute-a-command-and-get-the-output-of-the-command-within-c-using-po
	bool execLineFunc(const std::string cmd, std::function<void(std::string)> lineFunc) {
		std::array<char, 128> buffer;
		std::vector<std::string> result;
		std::string fullCmd = "while fuser /var/{lib/{dpkg,apt/lists},cache/apt/archives}/lock >/dev/null 2>&1; do sleep 1; done && " + cmd + " 2>&1";
		FILE* pipe = popen(fullCmd.c_str(), "r");
		if (!pipe) {
			throw std::runtime_error("popen() failed!");
		}
		while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
			std::cout << buffer.data() << std::endl;
			lineFunc(buffer.data());
		}
		return WEXITSTATUS(pclose(pipe)) == 0;
	}

	std::pair<bool, std::vector<std::string>> exec(const std::string cmd) {
		std::vector<std::string> result;
		auto ret = execLineFunc(cmd, [&result](std::string line) { result.push_back(line); });
		return std::make_pair(ret, result);
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

bool RnboUpdateService::updatePackages() {
	setenv("DEBIAN_FRONTEND", "noninteractive", 1);
	updateState(RunnerUpdateState::Active, "updating package list");
	bool success = exec("apt-get -y update").first;
	uint32_t cnt = 0;

	updateStatus("querying outdated package count");
	execLineFunc("apt-get -q -y -s dist-upgrade", [&cnt](std::string line) {
			//lines that start with Inst are installations
			if (line.rfind("Inst", 0) == 0) {
			cnt++;
			}
			});
	if (cnt != mOutdatedPackages) {
		mOutdatedPackages = cnt;
		emitPropertiesChangedSignal(rnbo_adaptor::INTERFACE_NAME, {"OutdatedPackages"});
	}
	return success;
}

void RnboUpdateService::evaluateCommands() {
	if (mInit) {
		updateState(RunnerUpdateState::Active, "update service init starting");
		updatePackages();
		updateState(RunnerUpdateState::Idle, "update service init complete");
		mInit = false;
	}
	auto o = mRunnerInstallQueue.tryPop();
	if (o) {
		std::string packageVersion = RUNNER_PACKAGE_NAME + "=" + o.get();
		if (!updatePackages()) {
			updateStatus("apt-get update failed, attempting to install anyway");
		}
		updateStatus("installing " + packageVersion);
		auto r = exec("apt-get install -y --install-recommends --install-suggests --allow-change-held-packages --allow-downgrades " + packageVersion);
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
				msg += std::string(" failed with error") + (err.size() > 1 ? "s: " : ": ") + boost::algorithm::join(err, ", ");
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
uint32_t RnboUpdateService::OutdatedPackages() { return mOutdatedPackages; }

void RnboUpdateService::updateState(RunnerUpdateState state, const std::string status) {
	mState = state;
	mStatus = status;
	emitPropertiesChangedSignal(rnbo_adaptor::INTERFACE_NAME, {"State", "Status"});
}

void RnboUpdateService::updateStatus(const std::string status) {
	mStatus = status;
	emitPropertiesChangedSignal(rnbo_adaptor::INTERFACE_NAME, {"Status"});
}
