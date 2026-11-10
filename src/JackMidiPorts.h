#pragma once

#include "MidiConnections.h"
#include "MidiAutoConnect.h"
#include <cctype>
#include <cerrno>
#include <jack/jack.h>
#include <jack/metadata.h>
#include <jack/midiport.h>
#include <jack/uuid.h>

// Shared by the runner and the live JACK regression harness. These are graph
// queries, never called from the audio process callback.
inline MidiConnections::Port midiJackPort(jack_port_t* port, const MidiConnections* history = nullptr,
		const std::string& cachedUsbIdentity = {}) {
	const auto flags = jack_port_flags(port);
	const auto* name = jack_port_name(port);
	MidiConnections::Port result{jack_uuid_to_index(jack_port_uuid(port)),
		{name ? name : "", {}}, (flags & JackPortIsOutput) != 0, {}};
	result.physical = (flags & JackPortIsPhysical) != 0;
	std::vector<char> a(jack_port_name_size()), b(jack_port_name_size());
	char* aliases[] = {a.data(), b.data()};
	const int count = jack_port_get_aliases(port, aliases);
	for (int i = 0; i < count; ++i) result.aliases.emplace_back(aliases[i]);
	std::string metadata;
	char *value = nullptr, *type = nullptr;
	if (!jack_get_property(jack_port_uuid(port), midi_identity::metadata_key, &value, &type)) {
		if (value) metadata = value;
		jack_free(value);
		jack_free(type);
	}
	if (!result.aliases.empty()) result.endpoint.alias = midi_identity::alias(result.endpoint.name, result.aliases);
	// Freeze USB identity for this port incarnation: card numbers can be reused
	// before a stale JACK port unregisters. Also avoid polling sysfs repeatedly.
	if (history) {
		auto previous = history->ports().find(result.id);
		if (previous != history->ports().end() && previous->second.endpoint.name == result.endpoint.name &&
			previous->second.endpoint.identity.starts_with("usb:")) {
			result.endpoint.identity = previous->second.endpoint.identity;
			return result;
		}
	}
	if (!cachedUsbIdentity.empty()) {
		result.endpoint.identity = cachedUsbIdentity;
		return result;
	}
	result.endpoint.identity = midi_identity::get(result.endpoint.name, result.aliases,
		(flags & JackPortIsPhysical) != 0, result.source, metadata);
	return result;
}

struct MidiJackSnapshot {
	std::vector<MidiConnections::Port> ports;
	std::vector<MidiConnections::Pair> edges;
};

inline MidiJackSnapshot midiJackSnapshot(jack_client_t* client, const MidiConnections* history = nullptr) {
	MidiJackSnapshot result;
	const char** names = jack_get_ports(client, nullptr, JACK_DEFAULT_MIDI_TYPE, 0);
	if (!names) return result;
	for (auto ptr = names; *ptr; ++ptr) {
		auto port = jack_port_by_name(client, *ptr);
		if (!port) continue;
		char *group = nullptr, *type = nullptr;
		bool hidden = false;
		if (!jack_get_property(jack_port_uuid(port), JACK_METADATA_PORT_GROUP, &group, &type)) {
			hidden = group && std::string(group) == "rnbo-graph-hidden";
			jack_free(group); jack_free(type);
		}
		if (hidden) continue;
		auto description = midiJackPort(port, history);
		if (description.source) {
			const char** peers = jack_port_get_connections(port);
			if (peers) {
				for (auto peer = peers; *peer; ++peer) {
					auto sink = jack_port_by_name(client, *peer);
					if (sink) result.edges.emplace_back(description.id, jack_uuid_to_index(jack_port_uuid(sink)));
				}
				jack_free(peers);
			}
		}
		result.ports.push_back(std::move(description));
	}
	jack_free(names);
	return result;
}

// Persist the same physical-MIDI alias address as the release runner. Actual
// JACK names are hints, not addresses an older runner must understand.
template<class Connection> void midiJackStoreEndpoint(Connection& info, const MidiConnections::Port& port, bool source) {
	const auto address = port.physical ? midi_identity::alias(port.endpoint.name, port.aliases) : port.endpoint.name;
	const auto colon = address.find(':');
	auto& name = source ? info.source_name : info.sink_name;
	auto& portName = source ? info.source_port_name : info.sink_port_name;
	name = address.substr(0, colon);
	portName = colon == std::string::npos ? "" : address.substr(colon + 1);
	(source ? info.source_midi_identity : info.sink_midi_identity) = port.endpoint.identity;
	(source ? info.source_midi_port : info.sink_midi_port) =
		port.physical || !port.endpoint.identity.empty() ? port.endpoint.name : "";
}

// Shared with tests so indexed/renamed RNBO instances ignore erroneous device
// identities and stale hints written by earlier versions of the branch.
template<class Connection> MidiConnections::Edge midiJackSavedEdge(const Connection& info) {
	auto endpoint = [](const std::string& name, const std::string& port, const std::string& identity,
			const std::string& hint, int instance, bool source) {
		const auto address = name + (port.empty() ? "" : ":" + port);
		if (instance >= 0 || midi_identity::instanceAlias(identity)) return MidiConnections::Endpoint{address, {}};
		return MidiConnections::canonical({hint.empty() ? address : hint, identity, hint.empty() ? "" : address}, source);
	};
	return {endpoint(info.source_name, info.source_port_name, info.source_midi_identity, info.source_midi_port, info.source_instance_index, true),
		endpoint(info.sink_name, info.sink_port_name, info.sink_midi_identity, info.sink_midi_port, info.sink_instance_index, false)};
}

struct MidiJackEndpoint {
	std::string name;
	bool present = false;
	bool midi = false;
	bool missingDevice = false;
	const MidiConnections::Port* port = nullptr;
};

inline MidiJackEndpoint midiJackResolveEndpoint(jack_client_t* client, const MidiConnections& graph,
		MidiConnections::Endpoint& endpoint, bool source, const MidiConnections::Selection& taken = {}) {
	endpoint = MidiConnections::canonical(endpoint, source);
	const auto address = endpoint.alias.empty() ? endpoint.name : endpoint.alias;
	auto live = jack_port_by_name(client, address.c_str());
	if (endpoint.identity.empty() && live && std::string(jack_port_type(live)) != JACK_DEFAULT_MIDI_TYPE)
		return {endpoint.name, true, false, false};
	if (const auto* port = graph.resolve(endpoint, source, taken)) {
		endpoint = port->endpoint;
		return {endpoint.name, true, true, false, port};
	}
	if (!endpoint.identity.empty()) return {{}, false, true, true};
	if (!live && midi_identity::legacyAlias(address)) {
		endpoint.identity = "legacy:" + midi_identity::normalized(address);
		return {{}, false, true, true};
	}
	return {endpoint.name, live != nullptr, live != nullptr, false};
}

inline void midiJackLoadEdge(jack_client_t* client, MidiConnections& graph, MidiConnections::Edge edge,
		MidiConnections::Selection* selection = nullptr) {
	MidiConnections::Selection local;
	auto& taken = selection ? *selection : local;
	// Resolution overwrites the endpoints with live ports; keep what was asked for.
	const auto requested = edge;
	const auto source = midiJackResolveEndpoint(client, graph, edge.source, true, taken);
	const auto sink = midiJackResolveEndpoint(client, graph, edge.sink, false, taken);
	int result = -1;
	if (!source.name.empty() && !sink.name.empty()) result = jack_connect(client, source.name.c_str(), sink.name.c_str());
	if (result == 0 || result == EEXIST) {
		if (source.port && sink.port) {
			taken.insert(source.port->id); taken.insert(sink.port->id);
			graph.connected({source.port->id, sink.port->id}, false, &requested);
		}
	} else if ((source.missingDevice || sink.missingDevice) &&
		(source.missingDevice || (source.present && source.midi)) &&
		(sink.missingDevice || (sink.present && sink.midi))) {
		graph.defer(edge, requested);
		if (source.port) taken.insert(source.port->id);
		if (sink.port) taken.insert(sink.port->id);
	}
}

inline void midiJackRetryConnections(jack_client_t* client, MidiConnections& graph) {
	auto taken = graph.occupied();
	// Resolving one cable can move its device's other waiting cables, so read
	// the live list each time instead of a copy.
	for (size_t i = 0; i < graph.pending().size();) {
		const auto edge = graph.pending()[i];
		// Indexed lookup returns immediately for absent endpoints; no JACK scans
		// or metadata queries are needed per pending cable.
		const auto [source, sink] = graph.resolveRecovery(edge, taken);
		const auto result = source && sink ? jack_connect(client, source->endpoint.name.c_str(), sink->endpoint.name.c_str()) : -1;
		if (result != 0 && result != EEXIST) { ++i; continue; }
		graph.forget(edge); // the next cable moves into this slot
		graph.resolved(edge, {source->endpoint, sink->endpoint}, MidiConnections::Pair{source->id, sink->id});
		graph.connected({source->id, sink->id}, false, &edge);
		taken.insert(source->id); taken.insert(sink->id);
	}
}

struct MidiJackAutoOptions {
	bool enabled = false; // mConnect for instances; control-auto-connect for control
	bool all = false, hardware = false, portgroup = false, control = false;
};

inline void midiJackAutoConnect(jack_client_t* client, jack_port_t* input, jack_port_t* output,
		jack_port_t* port, MidiAutoConnect& policy, const MidiJackAutoOptions& options,
		const MidiConnections* history = nullptr) {
	if (!options.enabled || !port || jack_port_is_mine(client, port) ||
		std::string(jack_port_type(port)) != JACK_DEFAULT_MIDI_TYPE) return;
	if (!(options.all || options.portgroup || (options.hardware && (jack_port_flags(port) & JackPortIsPhysical)))) return;
	const auto* name = jack_port_name(port);
	const auto description = midiJackPort(port, history,
		policy.cachedUsbIdentity(jack_uuid_to_index(jack_port_uuid(port)), name));
	auto through = [](std::string name) {
		std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
		return name.find("through") != std::string::npos || name.find("virtual") != std::string::npos;
	};
	if (through(name) || (!options.control && description.endpoint.name.find(CONTROL_CLIENT_NAME) != std::string::npos)) return;
	for (const auto& alias : description.aliases)
		if (through(alias) || (options.control && midi_identity::instanceAlias(alias))) return;
	policy.attempt(description, [&] {
		const auto result = description.source ? jack_connect(client, name, jack_port_name(input))
			: jack_connect(client, jack_port_name(output), name);
		return result == 0 || result == EEXIST;
	});
}
