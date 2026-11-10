#pragma once

#include "RNBO.h"
#include <string>

struct SetConnectionInfo {
	// Names retain the release runner's alias form for physical MIDI ports.
	// Optional JACK addresses only break ties between identical devices.
	std::string source_midi_identity, sink_midi_identity;
	std::string source_midi_port, sink_midi_port;
	std::string source_name;
	int source_instance_index = -1;
	std::string source_port_name;
	std::string sink_name;
	int sink_instance_index = -1;
	std::string sink_port_name;

	SetConnectionInfo(std::string src_name, std::string src_port_name,
			std::string snk_name, std::string snk_port_name)
		: source_name(src_name), source_port_name(src_port_name),
		  sink_name(snk_name), sink_port_name(snk_port_name) {}
	SetConnectionInfo() = default;

	RNBO::Json toJson() const {
		RNBO::Json json = {{"source_name", source_name}, {"source_instance_index", source_instance_index},
			{"source_port_name", source_port_name}, {"sink_name", sink_name},
			{"sink_instance_index", sink_instance_index}, {"sink_port_name", sink_port_name}};
		if (!source_midi_identity.empty()) json["source_midi_identity"] = source_midi_identity;
		if (!sink_midi_identity.empty()) json["sink_midi_identity"] = sink_midi_identity;
		if (!source_midi_port.empty()) json["source_midi_port"] = source_midi_port;
		if (!sink_midi_port.empty()) json["sink_midi_port"] = sink_midi_port;
		return json;
	}
	static SetConnectionInfo fromJson(const RNBO::Json& json) {
		SetConnectionInfo info(json["source_name"].get<std::string>(), json["source_port_name"].get<std::string>(),
			json["sink_name"].get<std::string>(), json["sink_port_name"].get<std::string>());
		info.source_instance_index = json["source_instance_index"].get<int>();
		info.sink_instance_index = json["sink_instance_index"].get<int>();
		info.source_midi_identity = json.value("source_midi_identity", std::string());
		info.sink_midi_identity = json.value("sink_midi_identity", std::string());
		info.source_midi_port = json.value("source_midi_port", std::string());
		info.sink_midi_port = json.value("sink_midi_port", std::string());
		// Accept exports from the earlier branch: actual names in the address
		// fields, and a separate alias. Do not emit those redundant alias fields.
		auto upgrade = [](std::string& name, std::string& port, std::string& hint,
				const std::string& identity, int instance, const std::string& alias) {
			if (alias.empty() || identity.empty() || !hint.empty() || instance >= 0 || identity.find(":rnbomidi") != std::string::npos) return;
			hint = name + (port.empty() ? "" : ":" + port);
			auto colon = alias.find(':');
			name = alias.substr(0, colon);
			port = colon == std::string::npos ? "" : alias.substr(colon + 1);
		};
		upgrade(info.source_name, info.source_port_name, info.source_midi_port, info.source_midi_identity,
			info.source_instance_index, json.value("source_midi_alias", std::string()));
		upgrade(info.sink_name, info.sink_port_name, info.sink_midi_port, info.sink_midi_identity,
			info.sink_instance_index, json.value("sink_midi_alias", std::string()));
		return info;
	}
};
