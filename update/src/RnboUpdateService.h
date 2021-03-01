#pragma once

#include <sdbus-c++/sdbus-c++.h>
#include "UpdateServiceServerGlue.h"
#include "Queue.h"

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
		virtual bool Active();
		virtual std::string Status();

		void updateActive(bool active, const std::string status);
		void updateStatus(const std::string status);
		bool exec(const std::string cmd);

		bool mActive = false;
		std::string mStatus = "waiting";
};
