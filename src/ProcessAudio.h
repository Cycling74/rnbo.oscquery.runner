#pragma once
#include "RNBO.h"
#include "DB.h"

namespace ossia {
	class value;
}

struct ConnectionChange {
  std::vector<std::string> port;
	bool issource;
	std::vector<std::vector<std::string>> connections;

	ConnectionChange(std::vector<std::string> p, bool s, std::vector<std::vector<std::string>> v) : port(p), issource(s), connections(v) { }
};

//A controller that handles audio for the entire appliction.
class ProcessAudio {
	public:
		virtual ~ProcessAudio() {}
		virtual bool isActive() = 0;
		virtual bool setActive(bool active, bool withServer = true) = 0;

		//process any events in the current thread
		virtual void processEvents(std::function<void(ConnectionChange)> connectionChangeCallback = nullptr) = 0;

		//try to connect with a previous config, return true if successful
		virtual bool connect(const std::vector<SetConnectionInfo>& connections, bool withControlConnections) { return false; }

		//get the current connection config
		virtual std::vector<SetConnectionInfo> connections() { return {}; }

		virtual void disconnect(const std::vector<SetConnectionInfo>& connections) { }

		virtual void updatePorts() {}
		virtual void sendReset() {}

		//handle one Link Audio state push from jack_transport_link, addressed under
		///jacklink/state/. Default no-op: only the JACK implementation has a transport client to
		//bridge. Called from the network poll, so an implementation must not touch the node tree.
		virtual void handleLinkTransportOSC(const std::string& addr, const ossia::value& val) { }

		//the Link Audio slots currently configured, in display order, for saving into a set
		virtual SetLinkAudioInfo linkAudioSetup() { return {}; }

		//the Link Audio slots a set being loaded expects. Declarative: slots not named here go
		//away. Not necessarily applied by the time this returns -- jack_transport_link may not be
		//reachable yet, so an implementation may hold this as desired state.
		virtual void setLinkAudioSetup(const SetLinkAudioInfo& setup) { }


		//transport handlers
		virtual void handleTransportState(bool running) = 0;
		virtual void handleTransportTempo(double bpm) = 0;
		virtual void handleTransportBeatTime(double btime) = 0;
		virtual void handleTransportTimeSig(double numerator, double denominator) = 0;
};
