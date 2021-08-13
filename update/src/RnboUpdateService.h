#pragma once

#include <sdbus-c++/sdbus-c++.h>
#include <string>
#include "UpdateServiceServerGlue.h"
#include "Queue.h"
#include "RunnerUpdateState.h"
#include <cstdint>

class RnboUpdateService : public sdbus::AdaptorInterfaces<com::cycling74::rnbo_adaptor, sdbus::Properties_adaptor>
{
	public:
		RnboUpdateService(sdbus::IConnection& connection, std::string objectPath = "/com/cycling74/rnbo");
		virtual ~RnboUpdateService();

		void evaluateCommands();
	private:
		bool mInit = true;
		bool mUpdateOutdated = true;
		Queue<std::string> mRunnerInstallQueue;

		//return true on success
		bool updatePackages();
		void computeOutdated();

		//methods
		virtual bool QueueRunnerInstall(const std::string& version);
		virtual void UpdateOutdated();

		//properties
		virtual uint32_t State();
		virtual std::string Status();
		virtual uint32_t OutdatedPackages();

		void updateState(RunnerUpdateState state, const std::string status);
		void updateStatus(const std::string status);

		RunnerUpdateState mState = RunnerUpdateState::Idle;
		std::string mStatus = "waiting";
		uint32_t mOutdatedPackages = 0;
};
