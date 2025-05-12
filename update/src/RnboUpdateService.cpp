#include "RnboUpdateService.h"
#include "Validation.h"
#include <iostream>
#include <stdlib.h>
#include <functional>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string.hpp>

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
	return success;
}

void RnboUpdateService::computeOutdated() {
	setenv("DEBIAN_FRONTEND", "noninteractive", 1);
	uint32_t cnt = 0;
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
}

void RnboUpdateService::findLatestRunner() {
	int newest = 0;

	//simply list all the versions of rnbooscquery and find versions that start with mLibVersion
	setenv("DEBIAN_FRONTEND", "noninteractive", 1);
	execLineFunc("apt-cache madison rnbooscquery", [&newest, this](std::string line) {
			//example line
			//rnbooscquery | 1.4.0-dev.0-2 | https://c74-apt.nyc3.digitaloceanspaces.com/raspbian bookworm/beta armhf Packages
			//the first part matches the library version and -2 represents that application version
			std::vector<std::string> entries;
			boost::algorithm::split(entries, line, boost::is_any_of(" | "));
			if (entries.size() > 1 && entries[0] == "rnbooscquery") {
				std::vector<std::string> versionentries;
				boost::algorithm::split(versionentries, entries[1], boost::is_any_of("-"));
				if (versionentries.size() > 1) {
					std::string appversion = versionentries.back();
					versionentries.pop_back();
					std::string version = boost::algorithm::join(versionentries, "-");
					if (version == mLibVersion) {
						newest = std::max(newest, std::stoi(appversion));
					}
				}
			}
	});

	std::string latest = mLibVersion; //fall back to exact match
	if (newest != 0) {
		latest = mLibVersion + "-" + std::to_string(newest);
	} else {
		std::cerr << "failed to find runner version with version prefix " << mLibVersion << std::endl;
	}

	if (latest != mLatestRunnerVersion) {
		mLatestRunnerVersion = latest;
		emitPropertiesChangedSignal(rnbo_adaptor::INTERFACE_NAME, {"LatestRunnerVersion"});
	}
}

void RnboUpdateService::evaluateCommands() {
	bool searchlatest = false; //TODO should this happen every so often anyway?
	if (mInit) {
		updateState(RunnerUpdateState::Active, "update service init starting");
		updatePackages();
		updateState(RunnerUpdateState::Idle, "update service init complete");
		mInit = false;
	}

	{
		std::unique_lock<std::mutex> guard(mVersionMutex);
		searchlatest = mSearchRunnerVersion;
		mSearchRunnerVersion = false;
		if (mUseLibVersion != mLibVersion) {
			mLibVersion = mUseLibVersion;
			searchlatest = true;
		}
	}

	if (searchlatest) {
		updateState(RunnerUpdateState::Active, "searching for latest runner for librnbo=" + mLibVersion);
		if (!updatePackages()) {
			updateStatus("apt-get update failed, attempting to search anyway");
		}
		findLatestRunner();
		updateState(RunnerUpdateState::Idle, "runner search complete");
	}

	if (mUpdateOutdated) {
		updateState(RunnerUpdateState::Active, "querying outdated package count");
		computeOutdated();
		updateState(RunnerUpdateState::Idle, "querying outdated package count complete");
		mUpdateOutdated = false;
	}
	auto o = mInstallQueue.tryPop();
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
	mInstallQueue.push(version);
	return true;
}

bool RnboUpdateService::UseLibraryVersion(const std::string& version) {
	std::unique_lock<std::mutex> guard(mVersionMutex);
	if (!validation::version(version))
		return false;
	mUseLibVersion = version;
	//always search for the latest runner verison even if we don't change library versions
	mSearchRunnerVersion = true;
	return true;
}

void RnboUpdateService::UpdateOutdated() {
	mUpdateOutdated = true;
}

uint32_t RnboUpdateService::State() { return static_cast<uint32_t>(mState); }
std::string RnboUpdateService::Status() { return mStatus; }
uint32_t RnboUpdateService::OutdatedPackages() { return mOutdatedPackages; }
std::string RnboUpdateService::LatestRunnerVersion() { return mLatestRunnerVersion; }

void RnboUpdateService::updateState(RunnerUpdateState state, const std::string status) {
	mState = state;
	mStatus = status;
	emitPropertiesChangedSignal(rnbo_adaptor::INTERFACE_NAME, {"State", "Status"});
}

void RnboUpdateService::updateStatus(const std::string status) {
	mStatus = status;
	emitPropertiesChangedSignal(rnbo_adaptor::INTERFACE_NAME, {"Status"});
}
