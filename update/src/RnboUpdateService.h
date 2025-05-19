#pragma once

#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include "UpdateServiceServerGlue.h"
#include "Queue.h"
#include "RunnerUpdateState.h"
#include <cstdint>
#include <mutex>

class RnboUpdateService : public sdbus::AdaptorInterfaces<com::cycling74::rnbo_adaptor, sdbus::Properties_adaptor>
{
	public:
		RnboUpdateService(sdbus::IConnection& connection, std::string objectPath = "/com/cycling74/rnbo");
		virtual ~RnboUpdateService();

		void evaluateCommands();
	private:
		bool mInit = true;
		bool mUpdateOutdated = true;
		Queue<std::string> mInstallQueue;

		//return true on success
		bool updatePackages();
		void computeOutdated();
		void findLatestRunner();
		std::string findLatest(std::string package);

		//methods
		virtual bool QueueRunnerInstall(const std::string& version) override;
    virtual bool QueueInstall(const std::string& packagename, const std::string& version) override;
		virtual bool UseLibraryVersion(const std::string& version, const std::vector<std::string>& dependencies) override;
		virtual void UpdateOutdated() override;
		virtual void Upgrade() override;

		//properties
		virtual uint32_t State() override;
		virtual std::string Status() override;
		virtual uint32_t OutdatedPackages() override;
		virtual std::string LatestRunnerVersion() override;
    virtual std::string NewUpdateServiceVersion() override;
    virtual std::vector<sdbus::Struct<std::string, std::string>> DependencyUpdates() override;

		void updateState(RunnerUpdateState state, const std::string status);
		void updateStatus(const std::string status);

		RunnerUpdateState mState = RunnerUpdateState::Idle;
		std::string mStatus = "waiting";
		std::string mLatestRunnerVersion;
		std::string mNewUpdateServiceVersion;
		uint32_t mOutdatedPackages = 0;
		std::vector<sdbus::Struct<std::string, std::string>> mDependencyUpdates;

		std::mutex mVersionMutex;
		std::string mUseLibVersion;
		std::string mLibVersion;
		std::vector<std::string> mDependencies;
		bool mSearchRunnerVersion = false;
		bool mUpgrade = false;
};
