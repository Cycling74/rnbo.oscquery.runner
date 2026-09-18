#include "SetDirtyState.h"
#include <iostream>
#include <stdexcept>
#include <vector>

using Json = RNBO::Json;
static void require(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}
struct Edge {
	std::string source_name, source_port_name, sink_name, sink_port_name;
};

int main() {
	const auto meta = SetDirtyState::metadata(R"({"0":{"x":10,"y":20}})");
	const auto device = SetDirtyState::device(Json::parse(R"({
        "namealias":"Synth", "midi_input_channel":0, "preset_midi_channel":"omni",
        "insetpreset":true,"setpreset":"values",
        "metaoverride":{
            "params":[{"index":1,"meta":{"midi":{"cc":10},"min":0}}, {"index":0,"meta":{"max":1}}],
            "inports":{"trigger":{"osc":"/trigger"}},
            "outports":{"level":{"osc":"/level"}},
            "datarefs":{"sample":{"channels":2}}
        },"preset_last":"initial", "datarefs":{"sample":"a.wav"}
    })"));
	const std::vector<Edge> edges = {{"synth", "out1", "system", "in1"}, {"synth", "out2", "system", "in2"}};
	const auto graph = SetDirtyState::connections(edges);
	const SetDirtyState::Components baseline = {{"meta", meta}, {"device/0", device}, {"connections", graph}};
	SetDirtyState state;
	state.reset(baseline);
	require(!state.dirty(), "load must start clean");

	state.update("meta", SetDirtyState::metadata("{ \"0\": {\"y\":20, \"x\":10} }"));
	state.update("device/0", device);
	state.update("connections", graph);
	require(!state.dirty(), "late duplicate restoration notifications must stay clean");

	auto moved = meta;
	moved["0"]["x"] = 11;
	state.update("meta", moved);
	require(state.dirty(), "moving a device must dirty the set");
	state.update("meta", meta);
	require(!state.dirty(), "restoring coordinates must clear dirty");

	state.update("device/1", device);
	require(state.dirty(), "adding a device must dirty the set");
	state.remove("device/1");
	require(!state.dirty(), "removing a newly added device must restore clean");
	state.remove("device/0");
	require(state.dirty(), "removing a saved device must dirty the set");
	state.update("device/0", device);
	require(!state.dirty(), "restoring a saved device must restore clean");
	state.remove("device/99");
	require(!state.dirty(), "unloading a nonexistent device must be a no-op");

	for (const auto &field :
	     {"namealias", "midi_input_channel", "preset_midi_channel", "insetpreset", "setpreset"}) {
		auto changed = device;
		changed[field] = "different";
		state.update("device/0", changed);
		require(state.dirty(), "device settings must dirty the set");
		state.update("device/0", device);
		require(!state.dirty(), "restoring device settings must clear dirty");
	}
	for (const auto &subject : {"params", "inports", "outports", "datarefs"}) {
		auto changed = device;
		changed["metaoverride"][subject] = Json::object();
		state.update("device/0", changed);
		require(state.dirty(), "each metadata category must dirty the set");
		state.update("device/0", device);
		require(!state.dirty(), "restoring metadata must clear dirty");
	}
	auto runtime = device;
	runtime["preset_last"] = "other";
	runtime["datarefs"] = {{"sample", "b.wav"}};
	state.update("device/0", SetDirtyState::device(runtime));
	require(!state.dirty(), "preset state and buffer contents must not dirty structure");
	auto reordered = device;
	reordered["metaoverride"]["params"] =
	    Json::parse(R"([{"index":0,"meta":{"max":1}}, {"index":1,"meta":{"min":0,"midi":{"cc":10}}}])");
	state.update("device/0", SetDirtyState::device(reordered));
	require(!state.dirty(), "parameter metadata order must not count as an edit");

	state.update("connections", SetDirtyState::connections(std::vector<Edge>{edges[1], edges[0], edges[1]}));
	require(!state.dirty(), "connection order and duplicates must not count as edits");
	state.update("connections", SetDirtyState::connections(std::vector<Edge>{edges[0]}));
	require(state.dirty(), "disconnecting must dirty the set");
	state.update("meta", moved);
	state.update("connections", graph);
	require(state.dirty(), "restoring one component must not clear another component's edit");
	state.update("meta", meta);
	require(!state.dirty(), "restoring all components must clear dirty");

	// Successful explicit save replaces the baseline. Autosave never invokes reset.
	auto saved = baseline;
	saved["meta"] = moved;
	state.update("meta", moved);
	state.reset(saved);
	state.update("meta", moved);
	require(!state.dirty(), "save followed by a delayed notification must remain clean");
	state.update("meta", meta);
	require(state.dirty(), "old baseline must not survive a save");

	state.reset(baseline);
	state.adopt("link", Json::parse(R"({"sends":[],"receives":[]})"));
	require(!state.dirty(), "first discovery of unspecified Link slots must stay clean");
	state.update("link", Json::parse(R"({"sends":["new"],"receives":[]})"));
	require(state.dirty(), "adding a Link device must dirty the set");
	state.update("link", Json::parse(R"({"sends":[],"receives":[]})"));
	require(!state.dirty(), "reverting Link slots must clear dirty");

	require(SetDirtyState::metadata("") == SetDirtyState::metadata("{}"), "empty metadata must normalize");
	require(SetDirtyState::metadata("opaque") == Json("opaque"), "opaque metadata must remain comparable");
	std::cout << "Set dirty state regression tests passed\n";
}
