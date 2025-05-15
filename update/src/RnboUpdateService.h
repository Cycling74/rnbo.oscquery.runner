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

    bool QueueInstall(std::string package, const std::string& version);

		//methods
		virtual bool QueueRunnerInstall(const std::string& version) override;
    virtual bool QueueRunnerPanelInstall(const std::string& version) override;
    virtual bool QueueJackTransportLinkInstall(const std::string& version) override;
		virtual bool UseLibraryVersion(const std::string& version) override;
		virtual void UpdateOutdated() override;

		//properties
		virtual uint32_t State() override;
		virtual std::string Status() override;
		virtual uint32_t OutdatedPackages() override;
		virtual std::string LatestRunnerVersion() override;
    virtual std::string LatestRunnerPanelVersion() override;
    virtual std::string LatestJackTransportLinkVersion() override;
    virtual std::string NewUpdateServiceVersion() override;

		void updateState(RunnerUpdateState state, const std::string status);
		void updateStatus(const std::string status);

		RunnerUpdateState mState = RunnerUpdateState::Idle;
		std::string mStatus = "waiting";
		std::string mLatestRunnerVersion;
		std::string mLatestRunnerPanelVersion;
		std::string mLatestJackTransportLinkVersion;
		std::string mNewUpdateServiceVersion;
		uint32_t mOutdatedPackages = 0;

		std::mutex mVersionMutex;
		std::string mUseLibVersion;
		std::string mLibVersion;
		bool mSearchRunnerVersion = false;
};
