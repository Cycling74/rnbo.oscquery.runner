#pragma once

#include <sdbus-c++/sdbus-c++.h>
#include "UpdateServiceServerGlue.h"
#include "Queue.h"
#include "RunnerUpdateState.h"

class RnboUpdateService : public sdbus::AdaptorInterfaces<com::cycling74::rnbo_adaptor, sdbus::Properties_adaptor>
{
	public:
		RnboUpdateService(sdbus::IConnection& connection, std::string objectPath = "/com/cycling74/rnbo");
		virtual ~RnboUpdateService();

		void evaluateCommands();
	private:
		Queue<std::string> mRunnerInstallQueue;

		//methods
		virtual bool QueueRunnerInstall(const std::string& version);

		//properties
		virtual uint32_t State();
		virtual std::string Status();

		void updateState(RunnerUpdateState state, const std::string status);
		void updateStatus(const std::string status);

		RunnerUpdateState mState = RunnerUpdateState::Idle;
		std::string mStatus = "waiting";
};
