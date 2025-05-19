#include "RnboUpdateService.h"
#include "Validation.h"
#include <iostream>
#include <stdlib.h>
#include <functional>
#include <algorithm>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string.hpp>

namespace {
	const std::string RUNNER_PACKAGE_NAME = "rnbooscquery";
	const std::string UPDATE_SERVICE_PACKAGE_NAME = "rnbo-update-service";

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
			std::cout << buffer.data();
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
	execLineFunc("apt-get -q -y -s upgrade", [&cnt](std::string line) {
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
	bool exact = false; //if there is an exact match

	//simply list all the versions of rnbooscquery and find versions that start with mLibVersion
	setenv("DEBIAN_FRONTEND", "noninteractive", 1);
	execLineFunc("apt-cache madison rnbooscquery", [&newest, &exact, this](std::string line) {
			//example line
			//rnbooscquery | 1.4.0-dev.0-2 | https://c74-apt.nyc3.digitaloceanspaces.com/raspbian bookworm/beta armhf Packages
			//the first part matches the library version and -2 represents that application version
			std::vector<std::string> entries;
			boost::algorithm::split(entries, line, boost::is_any_of("|"));
			if (entries.size() > 1) {
				std::string prefix = entries[0];
				std::string version = entries[1];
				boost::trim(prefix);
				boost::trim(version);

				if (prefix != RUNNER_PACKAGE_NAME) {
					return;
				}

				if (version == mLibVersion) {
					exact = true;
					return;
				}

				std::vector<std::string> versionentries;
				boost::algorithm::split(versionentries, version, boost::is_any_of("-"));
				if (versionentries.size() > 1) {
					try {
						std::string appversion = versionentries.back();
						versionentries.pop_back();
						std::string v = boost::algorithm::join(versionentries, "-");
						if (v == mLibVersion) {
							newest = std::max(newest, std::stoi(appversion));
						}
					} catch (...) {}
				}
			}
	});

	std::string latest = mLibVersion; //fall back to exact match
	if (newest != 0) {
		latest = mLibVersion + "-" + std::to_string(newest);
		std::cout << "found rnbooscquery=" << latest << std::endl;
	} else if (!exact) {
		std::cerr << "failed to find runner version with version prefix " << mLibVersion << std::endl;
	}

	if (latest != mLatestRunnerVersion) {
		mLatestRunnerVersion = latest;
		emitPropertiesChangedSignal(rnbo_adaptor::INTERFACE_NAME, {"LatestRunnerVersion"});
	}
}

//TODO make sure that the package doesn't conflict with our library version
std::string RnboUpdateService::findLatest(std::string package) {
	setenv("DEBIAN_FRONTEND", "noninteractive", 1);
	std::string candidate;
	std::string installed;
	execLineFunc("apt-cache policy " + package, [&candidate, &installed](std::string line) {
			boost::trim(line);
			const std::string chead = "Candidate: ";
			const std::string ihead = "Installed: ";
			if (line.rfind(chead, 0) == 0) {
				candidate = line.substr(chead.size());
			} else if (line.rfind(ihead, 0) == 0) {
				installed = line.substr(ihead.size());
			}
	});
	if (candidate == installed)
		return std::string();
	return candidate;
}

void RnboUpdateService::evaluateCommands() {
	bool searchlatestrunner = false; //TODO should this happen every so often anyway?
	bool searchlatestdeps = false; //TODO should this happen every so often anyway?

	if (mInit) {
		updateState(RunnerUpdateState::Active, "update service init starting");
		updatePackages();
		updateState(RunnerUpdateState::Idle, "update service init complete");
		mInit = false;
		searchlatestdeps = true;
	}

	if (mUpgrade) {
		mUpgrade = false;
		updateState(RunnerUpdateState::Active, "update service upgrading");
		bool success = false;
		std::string msg = "apt-get update failed";
		if (updatePackages()) {
			setenv("DEBIAN_FRONTEND", "noninteractive", 1);
			success = exec("apt-get -y upgrade").first;
			msg = std::string("apt-get upgrade ") + std::string(success ? "success" : "failed");
		}
		updateState(success ? RunnerUpdateState::Idle : RunnerUpdateState::Failed, msg);
		mUpdateOutdated = true;
	}

	{
		std::unique_lock<std::mutex> guard(mVersionMutex);
		searchlatestrunner = mSearchRunnerVersion;
		mSearchRunnerVersion = false;
		if (mUseLibVersion != mLibVersion) {
			mLibVersion = mUseLibVersion;
			searchlatestrunner = true;
		}
	}

	if (searchlatestrunner) {
		updateState(RunnerUpdateState::Active, "searching for latest runner for librnbo=" + mLibVersion);
		if (!updatePackages()) {
			updateStatus("apt-get update failed, attempting to search anyway");
		}
		findLatestRunner();
		updateState(RunnerUpdateState::Idle, "runner search complete");

		searchlatestdeps = true;
	}

	if (mUpdateOutdated) {
		updateState(RunnerUpdateState::Active, "querying outdated package count");
		computeOutdated();
		updateState(RunnerUpdateState::Idle, "querying outdated package count complete");
		mUpdateOutdated = false;
	}
	auto o = mInstallQueue.tryPop();
	if (o) {
		std::string packageVersion = o.get();
		if (!updatePackages()) {
			updateStatus("apt-get update failed, attempting to install anyway");
		}
		updateStatus("installing " + packageVersion);
		auto r = exec("apt-get install -y --install-recommends --install-suggests --allow-change-held-packages --allow-downgrades " + packageVersion);
		bool success = r.first;

		if (packageVersion.rfind(RUNNER_PACKAGE_NAME, 0) == 0) {
			updateStatus("marking " + RUNNER_PACKAGE_NAME + "hold");
			//always mark hold
			exec("apt-mark hold " + RUNNER_PACKAGE_NAME);
		}

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
		if (success) {
			searchlatestdeps = true;
		}
	}

	if (searchlatestdeps) {
		{
			std::unique_lock<std::mutex> guard(mVersionMutex);
			std::vector<sdbus::Struct<std::string, std::string>> updates;
			for (auto name: mDependencies) {
				auto latest = findLatest(name);
				if (latest.size()) {
					updates.emplace_back(name, latest);
				}
			}
			if (updates.size() != mDependencyUpdates.size() || !std::equal(updates.begin(), updates.end(), mDependencyUpdates.begin())) {
				mDependencyUpdates = std::move(updates);
				emitPropertiesChangedSignal(rnbo_adaptor::INTERFACE_NAME, {"DependencyUpdates"});
			}
		}

		{
			auto latest = findLatest(UPDATE_SERVICE_PACKAGE_NAME);
			if (mNewUpdateServiceVersion != latest) {
				mNewUpdateServiceVersion = latest;
				emitPropertiesChangedSignal(rnbo_adaptor::INTERFACE_NAME, {"NewUpdateServiceVersion"});
			}
		}
	}
}

bool RnboUpdateService::QueueRunnerInstall(const std::string& version) {
	return QueueInstall(RUNNER_PACKAGE_NAME, version);
}

bool RnboUpdateService::QueueInstall(const std::string& packageName, const std::string& version) {
	//make sure the version string is valid (so we don't allow injection)
	if (!validation::version(version))
		return false;
	mInstallQueue.push(packageName + "=" + version);
	return true;
}

bool RnboUpdateService::UseLibraryVersion(const std::string& version, const std::vector<std::string>& dependencies) {
	std::unique_lock<std::mutex> guard(mVersionMutex);
	if (!validation::version(version))
		return false;

	mUseLibVersion = version;
	mDependencies = dependencies;

	//always search for the latest runner verison even if we don't change library versions
	mSearchRunnerVersion = true;
	return true;
}

void RnboUpdateService::UpdateOutdated() {
	mUpdateOutdated = true;
}

void RnboUpdateService::Upgrade() {
	mUpgrade = true;
}

uint32_t RnboUpdateService::State() { return static_cast<uint32_t>(mState); }
std::string RnboUpdateService::Status() { return mStatus; }
uint32_t RnboUpdateService::OutdatedPackages() { return mOutdatedPackages; }
std::string RnboUpdateService::LatestRunnerVersion() { return mLatestRunnerVersion; }
std::string RnboUpdateService::NewUpdateServiceVersion() { return mNewUpdateServiceVersion; }
std::vector<sdbus::Struct<std::string, std::string>> RnboUpdateService::DependencyUpdates() { return mDependencyUpdates; }

void RnboUpdateService::updateState(RunnerUpdateState state, const std::string status) {
	mState = state;
	mStatus = status;
	emitPropertiesChangedSignal(rnbo_adaptor::INTERFACE_NAME, {"State", "Status"});
}

void RnboUpdateService::updateStatus(const std::string status) {
	mStatus = status;
	emitPropertiesChangedSignal(rnbo_adaptor::INTERFACE_NAME, {"Status"});
}
