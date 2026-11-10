#include "MidiConnections.h"
#include "MidiAutoConnect.h"
#include "SetDirtyState.h"
#include "SetConnectionInfo.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char* message) {
	if (!value) throw std::runtime_error(message);
}

using Port = MidiConnections::Port;
using Edge = MidiConnections::Edge;
static const Port keyboard{1, {"system:midi_capture_1", "usb:keys:source"}, true};
static const Port synth{2, {"synth-0:midiin1", ""}, false};
static const Edge cable{keyboard.endpoint, synth.endpoint};

int main() {
	using namespace std::chrono_literals;
	const auto t = MidiConnections::Clock::now();
	const auto window = MidiConnections::disconnectWindow;
	{
		MidiConnections waiting;
		waiting.observe({keyboard, synth}, {{1, 2}}, t);
		waiting.disconnected({1, 2}, t);
		waiting.observe({keyboard, synth}, {}, t + window - 1ms);
		require(waiting.pending().empty(), "never reconnect an undecided cable while both ports remain present");
		waiting.removed(1, t + window - 1ms);
		waiting.observe({synth}, {}, t + 5s); // delayed event-thread processing
		require(waiting.pending().size() == 1, "removal inside window survives late event processing");
	}
	for (auto delay : {window, window + 1ms, window + 3s}) {
		MidiConnections intentional;
		intentional.observe({keyboard, synth}, {{1, 2}}, t);
		intentional.disconnected({1, 2}, t);
		// Both events can be drained together long afterwards. Compare callback
		// times, not how close the two method calls happen to be here.
		intentional.removed(1, t + delay);
		intentional.observe({synth}, {}, t + 10s);
		require(intentional.pending().empty(), "removal at/after deadline must not recover an intentional disconnect");
	}
	{
		MidiConnections intentional;
		intentional.observe({keyboard, synth}, {{1, 2}}, t);
		intentional.disconnected({1, 2}, t);
		intentional.disconnected({1, 2}, t + window - 1ms);
		intentional.observe({keyboard, synth}, {}, t + window);
		intentional.removed(1, t + window + 1ms);
		require(intentional.pending().empty(), "duplicate notifications must not extend hysteresis");
	}
	{
		MidiConnections savedDuringWindow;
		savedDuringWindow.observe({keyboard, synth}, {{1, 2}}, t);
		savedDuringWindow.disconnected({1, 2}, t);
		savedDuringWindow.saved({});
		savedDuringWindow.removed(1, t + 1ms);
		require(savedDuringWindow.pending().empty(), "explicit save cancels undecided cables immediately");
	}
	{
		MidiConnections reconnected;
		reconnected.observe({keyboard, synth}, {{1, 2}}, t);
		reconnected.disconnected({1, 2}, t);
		reconnected.connected({1, 2});
		reconnected.observe({keyboard, synth}, {{1, 2}}, t + 1s);
		reconnected.disconnected({1, 2}, t + 2s);
		reconnected.removed(1, t + 2s + 1ms);
		require(reconnected.pending().size() == 1, "reconnecting manually starts a fresh cable lifetime");
	}
	{
		MidiConnections delayedRemoval;
		delayedRemoval.observe({keyboard, synth}, {{1, 2}}, t);
		delayedRemoval.disconnected({1, 2}, t);
		delayedRemoval.observe({synth}, {}, t + 1s);
		require(delayedRemoval.pending().empty(), "a snapshot cannot assign a timestamp to a missing removal notification");
		delayedRemoval.removed(1, t + 2ms);
		require(delayedRemoval.pending().size() == 1, "late delivery uses the recorded removal time");
	}

	MidiConnections graph;
	graph.observe({keyboard, synth}, {{1, 2}});
	graph.disconnected({1, 2});
	graph.removed(1);
	graph.observe({synth}, {});
	require(graph.pending().size() == 1, "unplug must retain the cable");
	require(!graph.resolve(cable.source, true), "absent device cannot resolve");
	Port returned{9, {"system:midi_capture_7", keyboard.endpoint.identity}, true};
	graph.observe({returned, synth}, {});
	require(graph.resolve(cable.source, true)->id == 9, "replug follows identity across JACK renumbering");
	graph.connected({9, 2});
	require(graph.pending().empty(), "success consumes recovery; never continuously enforce a cable");
	const auto disconnectTime = MidiConnections::Clock::now();
	graph.disconnected({9, 2}, disconnectTime);
	graph.removed(9, disconnectTime + MidiConnections::disconnectWindow);
	graph.observe({synth}, {});
	require(graph.pending().empty(), "external disconnect followed by unplug after the window in the same batch must win");

	graph.observe({keyboard, synth}, {{1, 2}});
	graph.removed(1);
	graph.observe({synth}, {});
	graph.saved({});
	graph.observe({returned, synth}, {});
	require(graph.pending().empty(), "saving disconnected graph cancels recovery");

	graph.observe({keyboard, synth}, {{1, 2}});
	graph.removed(1);
	graph.observe({synth}, {});
	graph.saved({cable});
	require(graph.pending().size() == 1, "device removed during save stays recoverable if saved snapshot included cable");
	// Autosave/failed save do not call saved(). Waiting does not expire recovery.
	for (int i = 0; i < 100; ++i) graph.observe({synth}, {});
	require(graph.pending().size() == 1, "offline state must survive autosave and arbitrary delays");
	graph.clear();
	graph.observe({returned, synth}, {});
	require(graph.pending().empty(), "a new set must not inherit old recovery");

	graph.observe({keyboard, synth}, {{1, 2}});
	graph.observe({keyboard, synth}, {}); // graph changed before notifications drained
	graph.removed(1);
	graph.observe({synth}, {});
	require(graph.pending().size() == 1, "snapshot between disconnect and removal must not lose history");
	graph.clear();
	graph.observe({keyboard, synth}, {{1, 2}});
	graph.observe({keyboard, synth}, {});
	graph.saved({});
	graph.removed(1);
	require(graph.pending().empty(), "save must also prune history awaiting a delayed notification");

	graph.observe({keyboard, synth}, {{1, 2}});
	graph.forgetClient("synth-0");
	graph.clear(); // incoming set starts while old instance is still fading
	graph.observe({keyboard, synth}, {{1, 2}}); // fading instance still connected
	graph.removed(2);
	graph.observe({keyboard}, {});
	require(graph.pending().empty(), "fading-out instance must not resurrect forgotten cables");
	graph.observe({keyboard, synth}, {{1, 2}}); // new incarnation after unregister
	graph.disconnected({1, 2});
	graph.removed(1);
	require(graph.pending().size() == 1, "port exclusion must end at unregister, allowing a replacement instance");

	MidiConnections duplicates;
	Port first{10, {"system:midi_capture_10", "name:identical:source"}, true};
	Port second{11, {"system:midi_capture_11", first.endpoint.identity}, true};
	first.aliases = second.aliases = {"identical:midi/capture_1"};
	duplicates.observe({first, second, synth}, {{10, 2}, {11, 2}});
	require(duplicates.savedIdentity(first) == first.endpoint.identity, "save plain identity even with twins present");
	require(duplicates.resolve(second.endpoint, true)->id == second.id, "saved JACK name wins among twins");
	const MidiConnections::Endpoint unnamed{"identical:midi/capture_1", first.endpoint.identity};
	require(duplicates.resolve(unnamed, true)->id == first.id, "otherwise use JACK snapshot order");
	require(duplicates.resolve(unnamed, true, {first.id})->id == second.id, "prefer an unclaimed candidate");
	duplicates.removed(first.id);
	duplicates.observe({second, synth}, {{second.id, synth.id}});
	require(duplicates.resolve(first.endpoint, true)->id == second.id, "one current candidate resolves after observing two");
	require(!duplicates.resolveRecovery({first.endpoint, synth.endpoint}).first,
		"a surviving twin's existing cable must not consume the missing twin's request");
	duplicates.edit(synth.endpoint.name, false, {second.endpoint.name, "unrelated:out"});
	require(duplicates.pending().size() == 1, "editing a twin's peer list preserves the absent twin's cable");
	duplicates.forgetNames(second.endpoint.name, synth.endpoint.name);
	require(duplicates.pending().size() == 1, "disconnecting the surviving twin must not forget the absent twin");
	Port twinReturn = first; twinReturn.id = 12; twinReturn.endpoint.name = "system:midi_capture_12";
	duplicates.observe({second, twinReturn, synth}, {{second.id, synth.id}});
	require(duplicates.resolveRecovery({first.endpoint, synth.endpoint}).first->id == twinReturn.id,
		"restore to the returning twin rather than an already connected one");
	require(duplicates.resolve({"old-alias", "ambiguous:" + first.endpoint.identity + ":" + second.endpoint.name}, true)->id == second.id,
		"old ambiguity markers load and retain their saved JACK name hint");
	duplicates.observe({second, first, synth}, {});
	require(duplicates.resolve(unnamed, true)->id == second.id, "fallback order is snapshot order, not sorted port ID");

	MidiConnections serials;
	first.endpoint.identity = "usb:model:serial:A:source";
	second.endpoint.identity = "usb:model:serial:B:source";
	serials.observe({first, second, synth}, {{10, 2}, {11, 2}});
	serials.removed(10);
	serials.removed(11);
	serials.observe({synth}, {});
	serials.observe({second, synth}, {});
	require(!serials.resolve(first.endpoint, true), "different serial must not match");
	require(serials.resolve(second.endpoint, true)->id == 11, "same model with distinct serials resolves independently");
	require(!serials.resolve(second.endpoint, false), "port direction must match");

	MidiConnections both;
	Port hardwareSink{3, {"system:midi_playback_1", "usb:out:sink"}, false};
	both.observe({keyboard, hardwareSink}, {{1, 3}});
	both.removed(1); both.removed(3);
	both.observe({}, {});
	require(both.pending().size() == 1, "two disappearing endpoints must retain exactly one cable");
	both.forgetNames(keyboard.endpoint.name, hardwareSink.endpoint.name);
	require(both.pending().empty(), "runner can explicitly forget an offline cable");

	// Dirty comparison and saving use live connections, not recovery entries.
	struct SavedEdge {
		std::string source_name, source_port_name, sink_name, sink_port_name;
		std::string source_midi_identity, sink_midi_identity;
	};
	SavedEdge saved{"old", "port", "synth-0", "midiin1", keyboard.endpoint.identity, ""};
	SetDirtyState dirty;
	auto original = SetDirtyState::connections(std::vector{saved});
	dirty.reset({{"connections", original}});
	dirty.update("connections", SetDirtyState::connections(std::vector<SavedEdge>{}));
	require(dirty.dirty(), "unplugging a saved cable dirties the graph");
	saved.source_name = "renumbered";
	saved.source_port_name = "different_port";
	dirty.update("connections", SetDirtyState::connections(std::vector{saved}));
	require(!dirty.dirty(), "identity-equivalent replug restores clean despite changed JACK names");
	dirty.reset({{"connections", SetDirtyState::connections(std::vector<SavedEdge>{})}});
	dirty.update("connections", SetDirtyState::connections(std::vector<SavedEdge>{}));
	require(!dirty.dirty(), "saving missing cables establishes disconnected baseline");

	require(midi_identity::normalized("in-hw-7-0-1-Keys") == "in-hw-*-0-1-Keys", "legacy raw alias ignores only card number");
	require(midi_identity::normalized("in-hw-7-0-1-Keys") != midi_identity::normalized("in-hw-7-0-2-Keys"), "MIDI subports remain distinct");
	require(midi_identity::get("system:midi_capture_1", {}, true, true).empty(), "wait for aliases rather than bind a transient index");
	require(midi_identity::get("bridge:port", {}, false, true, "paired-address/port1") == "bridge:paired-address/port1:source", "bridge identity works without physical flag");

	// Review regressions: aliases, late metadata, config eligibility, software
	// restart, global control routes, and edits while a peer is absent.
	{
		MidiAutoConnect policy;
		Port port{21, {"system:midi_capture_1", {}}, true, {}};
		int attempts = 0;
		auto success = [&] { ++attempts; return true; };
		policy.attempt(port, success, t);
		require(attempts == 0, "system port must wait for an alias");
		port.aliases = {"alsa_pcm:temporary"}; port.endpoint.identity = "name:temporary:source";
		policy.attempt(port, success, t + 1ms);
		policy.attempt(port, success, t + 60ms);
		require(attempts == 1, "late aliases must be eligible on subsequent polls");
		port.aliases.push_back("keys:midi/capture_1"); port.endpoint.identity = "name:keys:source";
		policy.attempt(port, success, t + 100ms);
		require(attempts == 1, "alias upgrades must not reassert a cable");
		policy.portChanged(port.id);
		policy.attempt(port, success, t + 1s);
		policy.attempt(port, success, t + 2s);
		require(attempts == 1, "a deliberate disconnect stays disconnected after replug with final aliases");
		Port other{22, {"app:out", {}}, true};
		policy.attempt(other, [&] { ++attempts; return false; }, t);
		policy.attempt(other, success, t + 1s);
		require(attempts == 3, "failed attempts must not permanently mark a port as handled");
	}
	{
		MidiConnections software;
		Port app{30, {"app:out", {}}, true};
		software.observe({app, synth}, {{30, 2}});
		software.removed(30);
		app.id = 31;
		software.observe({app, synth}, {});
		require(software.pending().size() == 1 && software.resolve(software.pending()[0].source, true),
			"software MIDI recovery works without physical flags or device identities");
		software.connected({31, 2});
		software.disconnected({31, 2}, t);
		software.removed(31, t + window);
		require(software.pending().empty(), "software MIDI manual disconnects obey the same hysteresis");
	}
	{
		MidiConnections control;
		Port ctl{40, {"rnbo-control:midiin", {}}, false};
		control.observe({keyboard, ctl, synth}, {{1, 40}, {1, 2}});
		control.removed(1);
		control.clear(true);
		require(control.pending().size() == 1 && control.pending()[0].sink == ctl.endpoint,
			"set changes preserve absent global control cables only");
		control.saved({});
		require(control.pending().size() == 1, "explicit offline save preserves automatic control recovery");
		control.forgetNames(keyboard.endpoint.name, ctl.endpoint.name);
		control.saved({});
		require(control.pending().empty(), "saving must not recreate an explicitly disconnected control cable");
	}
	{
		// A control cable carried across a set switch is left out of the loaded
		// set's baseline; when its device returns it joins the baseline instead of
		// reading as an edit.
		struct Row { std::string source_name, source_port_name, sink_name, sink_port_name; };
		auto rows = [](const std::vector<Edge>& edges) {
			std::vector<Row> result;
			for (const auto& edge : edges) {
				const auto k = MidiConnections::key(edge);
				result.push_back({k[0], k[1], k[2], k[3]});
			}
			return result;
		};
		auto baseline = [&](const MidiConnections& g) {
			std::vector<Edge> edges;
			for (const auto& edge : g.pending()) if (!edge.carried) edges.push_back(edge);
			return SetDirtyState::connections(rows(edges));
		};
		auto retry = [](MidiConnections& g) {
			for (size_t i = 0; i < g.pending().size();) {
				const auto edge = g.pending()[i];
				const auto [source, sink] = g.resolveRecovery(edge);
				if (!source || !sink) { ++i; continue; }
				g.forget(edge);
				g.resolved(edge, {source->endpoint, sink->endpoint}, MidiConnections::Pair{source->id, sink->id});
				g.connected({source->id, sink->id}, false, &edge);
			}
		};
		const Port ctl{40, {"rnbo-control:midiin", {}}, false};
		const Edge controlCable{keyboard.endpoint, ctl.endpoint};
		const auto live = SetDirtyState::connections(rows({cable, controlCable}));
		for (const bool save : {false, true}) {
			MidiConnections g;
			SetDirtyState state;
			g.observe({keyboard, ctl, synth}, {{1, 40}, {1, 2}});
			g.removed(1);
			g.observe({ctl, synth}, {});
			if (save) {
				// Saved while the controller is away: the snapshot has neither cable.
				g.saved({});
				require(g.pending().size() == 1 && g.pending()[0].carried, "a control cable kept by an offline save is carried");
				state.reset({{"connections", SetDirtyState::connections(rows({}))}});
			} else {
				g.clear(true); // set switch without control connections
				g.defer(cable, cable);
				require(g.pending().size() == 2 && g.pending()[0].carried && !g.pending()[1].carried,
					"only the control cable kept across the set switch is carried");
				state.reset({{"connections", baseline(g)}});
				state.update("connections", SetDirtyState::connections(rows({})));
				require(state.dirty(), "a set loaded without its device is dirty");
			}
			g.registered(1);
			g.observe({keyboard, ctl, synth}, {});
			retry(g);
			require(g.pending().empty(), "the returning controller restores every waiting cable");
			const auto updates = g.takeIdentityUpdates();
			require(updates.size() == 1 && updates[0].before == MidiConnectionUpdate::Key{} &&
				updates[0].after == MidiConnections::key(controlCable), "the carried control cable is reported as joining the baseline");
			state.remapConnectionIdentities(updates);
			state.update("connections", save ? SetDirtyState::connections(rows({controlCable})) : live);
			require(!state.dirty(), "a restored carried control cable does not leave the set dirty");
			// Losing it again is a difference like any other, and it returns clean.
			g.removed(1);
			require(!g.pending().empty() && std::none_of(g.pending().begin(), g.pending().end(),
				[](const Edge& e) { return e.carried; }), "a cable that was live is no longer carried");
			state.update("connections", SetDirtyState::connections(rows({})));
			require(state.dirty(), "unplugging the controller again is a difference");
		}
		{
			// A set that asks for the same control cable supersedes the carried copy.
			const Port out{41, {"rnbo-control:midiout", {}}, true};
			const Port keysIn{3, {"system:midi_playback_1", "usb:keys:sink"}, false};
			MidiConnections g;
			g.observe({out, keysIn}, {{41, 3}});
			g.removed(3);
			g.clear(true);
			g.defer({out.endpoint, keysIn.endpoint}, {out.endpoint, keysIn.endpoint});
			require(g.pending().size() == 1 && !g.pending()[0].carried, "a requested control cable replaces its carried copy");
		}
		{
			// A patcher reload keeps its baseline: a waiting control cable that is in
			// it must not be added a second time when its device returns.
			MidiConnections g;
			SetDirtyState state;
			g.observe({keyboard, ctl}, {{1, 40}});
			state.reset({{"connections", SetDirtyState::connections(rows({controlCable}))}});
			g.removed(1);
			g.clear(true, false);
			require(g.pending().size() == 1 && !g.pending()[0].carried, "a reload that keeps its baseline carries nothing");
			g.registered(1);
			g.observe({keyboard, ctl}, {});
			retry(g);
			state.remapConnectionIdentities(g.takeIdentityUpdates());
			state.update("connections", SetDirtyState::connections(rows({controlCable})));
			require(!state.dirty(), "a baseline cable restored after a patcher reload is counted once");
		}
		{
			// Audio restart: the caller snapshots and reloads the graph it had; only
			// a carried cable, which is in no snapshot, keeps waiting here.
			const Port away{5, {"system:midi_capture_5", "usb:other:source"}, true};
			MidiConnections g;
			SetDirtyState state;
			g.observe({keyboard, away, ctl, synth}, {{1, 40}, {1, 2}, {5, 40}});
			g.removed(5);
			g.clear(true); // set switch: the absent device's control cable is carried
			g.observe({keyboard, ctl, synth}, {{1, 40}, {1, 2}});
			state.reset({{"connections", live}});
			g.restart();
			require(g.pending().size() == 1 && g.pending()[0].carried && g.pending()[0].source == away.endpoint,
				"an audio restart keeps only carried cables waiting");
			require(g.ports().empty() && g.occupied().empty(), "an audio restart forgets every port and live cable");
			// New client, new port ids; the snapshot is loaded with its control cables.
			const Port keys2{11, keyboard.endpoint, true}, ctl2{12, ctl.endpoint, false}, synth2{13, synth.endpoint, false};
			g.observe({keys2, ctl2, synth2}, {});
			g.clear(false, false);
			require(g.pending().size() == 1 && g.pending()[0].carried, "restoring the snapshot keeps the carried cable");
			g.connected({11, 12}, false);
			g.connected({11, 13}, false);
			state.remapConnectionIdentities(g.takeIdentityUpdates());
			state.update("connections", live);
			require(!state.dirty(), "the restored graph compares clean against the kept baseline");
			const Port away2{14, away.endpoint, true};
			g.registered(14);
			g.observe({keys2, away2, ctl2, synth2}, {{11, 12}, {11, 13}});
			retry(g);
			require(g.pending().empty(), "the carried cable is restored when its device returns after the restart");
			state.remapConnectionIdentities(g.takeIdentityUpdates());
			state.update("connections", SetDirtyState::connections(rows({cable, controlCable, {away.endpoint, ctl.endpoint}})));
			require(!state.dirty(), "and it joins the baseline as before the restart");
		}
		{
			// A control cable the save stored stays part of the saved set.
			MidiConnections g;
			g.observe({keyboard, ctl}, {{1, 40}});
			g.removed(1);
			g.saved({controlCable});
			require(g.pending().size() == 1 && !g.pending()[0].carried, "a control cable in the saved snapshot is not carried");
		}
	}
	{
		MidiConnections editing;
		Port aliased = keyboard; aliased.aliases = {"keys:midi/capture_1"};
		editing.observe({aliased, synth}, {{1, 2}});
		editing.removed(1);
		editing.edit(synth.endpoint.name, false, {"unrelated:out"});
		require(editing.pending().size() == 1, "editing live peers preserves absent peers");
		editing.observe({aliased, synth}, {});
		editing.edit(synth.endpoint.name, false, {aliased.aliases[0]});
		require(editing.pending().size() == 1, "connection list alias refers to the actual peer");
		editing.forgetNames(aliased.aliases[0], synth.endpoint.name);
		require(editing.pending().empty(), "explicit disconnect accepts a live alias");
		editing.remember({aliased.endpoint, synth.endpoint});
		editing.edit(synth.endpoint.name, false, {});
		require(editing.pending().empty(), "omitting a present peer cancels its pending cable");
	}
	{
		MidiConnections cached;
		cached.observe({keyboard, synth}, {{1, 2}});
		auto reused = keyboard; reused.endpoint.identity = "usb:different-device:source";
		cached.observe({reused, synth}, {{1, 2}});
		require(cached.ports().at(1).endpoint.identity == keyboard.endpoint.identity, "card reuse cannot reassign an existing USB port");
		cached.removed(1);
		cached.observe({reused, synth}, {});
		require(cached.ports().at(1).endpoint.identity == reused.endpoint.identity, "unregister ends cached USB identity lifetime");
	}
	{
		MidiConnections legacy;
		Port aliased = keyboard; aliased.aliases = {"in-hw-7-0-1-Keys"};
		legacy.observe({aliased, synth}, {});
		const MidiConnections::Endpoint old{"in-hw-2-0-1-Keys", "legacy:in-hw-*-0-1-Keys"};
		require(legacy.resolve(old, true), "legacy raw alias resolves across card renumbering");
		require(legacy.resolve({old.name, "ambiguous:" + old.identity}, true), "old ambiguous legacy markers remain loadable");
		legacy.resolved({old, synth.endpoint}, {aliased.endpoint, synth.endpoint});
		saved.source_midi_identity = old.identity;
		dirty.reset({{"connections", SetDirtyState::connections(std::vector{saved})}, {"meta", "old"}});
		dirty.update("meta", "edited");
		dirty.remapConnectionIdentities(legacy.takeIdentityUpdates());
		saved.source_midi_identity = aliased.endpoint.identity;
		dirty.update("connections", SetDirtyState::connections(std::vector{saved}));
		require(dirty.dirty(), "resolving a legacy cable must preserve unrelated edits");
		dirty.update("meta", "old");
		require(!dirty.dirty(), "legacy restoration compares clean after identity refinement");
		require(legacy.takeIdentityUpdates().empty(), "identity updates are consumed once");
	}
	{
		MidiAutoConnect cache;
		cache.attempt(keyboard, [] { return true; }, t);
		require(cache.cachedUsbIdentity(keyboard.id, keyboard.endpoint.name) == keyboard.endpoint.identity,
			"auto-connect caches USB identity across polling");
		cache.portChanged(keyboard.id);
		require(cache.cachedUsbIdentity(keyboard.id, keyboard.endpoint.name).empty(), "unregister invalidates auto-connect USB cache");
	}
	require(midi_identity::legacyAlias("BlueKeys:BlueKeys MIDI 1 (out)"), "JACK2 seq named aliases support offline legacy loads");
	require(!midi_identity::legacyAlias("alsa_in:capture_1"), "audio bridge name is not a MIDI alias");
	require(midi_identity::legacyAlias("keys:midi/capture_1"), "sequencer aliases support offline legacy loads");
	require(!midi_identity::get("system:midi_capture_1", {"keys:midi/capture_1"}, false, true).empty(),
		"nonphysical sequencer ports retain alias identities");

	{
		MidiConnections paths;
		Port moved{60, {"system:midi_capture_8", "usb:model:path:socket-B:midi:0:0:source", "in-hw-8-0-0-Keys"}, true, {"in-hw-8-0-0-Keys"}};
		const MidiConnections::Endpoint old{"system:midi_capture_1", "usb:model:path:socket-A:midi:0:0:source", "in-hw-1-0-0-Keys"};
		paths.observe({moved, synth}, {});
		require(paths.resolve(old, true)->id == moved.id, "USB socket changes fall back to the normalized alias");
		require(!paths.resolve({old.name, "usb:model:serial:other:midi:0:0:source", old.alias}, true), "serial identities still require the same device");
		paths.resolved({old, synth.endpoint}, {moved.endpoint, synth.endpoint});
		saved.source_midi_identity = old.identity;
		dirty.reset({{"connections", SetDirtyState::connections(std::vector{saved})}});
		dirty.remapConnectionIdentities(paths.takeIdentityUpdates());
		saved.source_midi_identity = moved.endpoint.identity;
		dirty.update("connections", SetDirtyState::connections(std::vector{saved}));
		require(!dirty.dirty(), "path fallback refines the saved baseline and returns clean");
	}
	{
		auto other = saved;
		saved.source_midi_identity = other.source_midi_identity = "name:twins:source";
		saved.source_name = "first"; other.source_name = "second";
		dirty.reset({{"connections", SetDirtyState::connections(std::vector{saved, other})}});
		dirty.update("connections", SetDirtyState::connections(std::vector{other}));
		require(dirty.dirty(), "two identical-device cables must not collapse into one dirty-state entry");
		dirty.update("connections", SetDirtyState::connections(std::vector{other, saved}));
		require(!dirty.dirty(), "restoring both identical-device cables returns clean");
	}
	{
		MidiConnections twins;
		Port a{70, {"twins:a", "name:twins:source"}, true};
		Port b{71, {"twins:b", "name:twins:source"}, true};
		twins.observe({a, b, synth}, {{70, 2}, {71, 2}});
		twins.removed(70);
		twins.observe({b, synth}, {{71, 2}});
		twins.saved({{b.endpoint, synth.endpoint}});
		require(twins.pending().empty(), "saving a sole surviving twin commits the missing twin's cable removal");
	}
	{
		auto other = saved;
		saved.source_name = "first"; other.source_name = "second";
		saved.source_midi_identity = other.source_midi_identity = "legacy:twin-alias";
		dirty.reset({{"connections", SetDirtyState::connections(std::vector{saved, other})}, {"meta", "old"}});
		MidiConnections refinements;
		const Edge old{{"ignored", "legacy:twin-alias"}, synth.endpoint};
		refinements.resolved(old, {{"twins:a", "usb:model:path:A:source"}, synth.endpoint});
		refinements.resolved(old, {{"twins:b", "usb:model:path:B:source"}, synth.endpoint});
		dirty.update("meta", "edit");
		dirty.remapConnectionIdentities(refinements.takeIdentityUpdates());
		saved.source_midi_identity = "usb:model:path:A:source";
		other.source_midi_identity = "usb:model:path:B:source";
		dirty.update("connections", SetDirtyState::connections(std::vector{saved, other}));
		require(dirty.dirty(), "per-cable twin refinements preserve unrelated dirty components");
		dirty.update("meta", "old");
		require(!dirty.dirty(), "two identical legacy aliases can refine to different USB devices and return clean");
	}
	require(midi_identity::get("renamed-0:midiin1", {"renamed-0:rnbomidiin1"}, false, false).empty(),
		"RNBO instance aliases must not become device identities");
	require(midi_identity::get("renamed-0:midiout1", {"renamed-0:rnbomidiout1"}, false, true).empty(),
		"RNBO instance output aliases must not become device identities");

	// Recovery of a previously live cable must wait for a new port incarnation;
	// set-load matching is intentionally allowed to choose a present twin.
	for (const bool raw : {false, true}) {
		MidiConnections twins;
		Port a{80, {"system:midi_capture_80", raw ? "usb:model:path:A:midi:0:0:source" : "name:twins:source", "in-hw-1-0-0-Keys"}, true, {"in-hw-1-0-0-Keys"}};
		Port b{81, {"system:midi_capture_81", raw ? "usb:model:path:B:midi:0:0:source" : a.endpoint.identity, "in-hw-2-0-0-Keys"}, true, {"in-hw-2-0-0-Keys"}};
		Port otherSink{82, {"synth-1:midiin1", ""}, false};
		twins.observe({a, b, synth, otherSink}, {{80, 2}, {81, 82}}, t);
		twins.disconnected({80, 2}, t);
		twins.removed(80, t + 1ms);
		twins.observe({b, synth, otherSink}, {{81, 82}}, t + 2ms);
		require(twins.pending().size() == 1 && !twins.resolveRecovery(twins.pending()[0]).first,
			"different-sink twin must not inherit the absent device's cable, including USB path fallback");
		// A separate explicit connection to the surviving twin must not consume
		// the lost device's pending entry either.
		twins.connected({81, 2});
		require(twins.pending().size() == 1, "observation cannot consume recovery through an excluded twin");
		a.id = 83; a.endpoint.name = "system:midi_capture_83";
		if (raw) a.endpoint.identity = "usb:model:path:C:midi:0:0:source";
		twins.registered(a.id);
		twins.observe({b, a, synth, otherSink}, {{81, 82}, {81, 2}}, t + 3ms);
		require(twins.resolveRecovery(twins.pending()[0]).first->id == a.id,
			"newly registered twin can recover the lost cable, including a moved USB socket");
		twins.connected({83, 2});
		require(twins.pending().empty(), "manual restoration to a returning port consumes its pending cable");
	}
	{
		MidiConnections generations;
		Port a{90, {"twins:a", "name:twins:source"}, true};
		Port b{91, {"twins:b", a.endpoint.identity}, true};
		generations.observe({a, synth}, {{90, 2}}, t);
		generations.registered(b.id); // registered before loss, not in a snapshot yet
		generations.removed(a.id, t + 1ms);
		generations.observe({b, synth}, {}, t + 2ms);
		require(!generations.resolveRecovery(generations.pending()[0]).first,
			"registration events exclude pre-existing ports even before their first snapshot");
		generations.removed(b.id, t + 3ms);
		generations.registered(b.id); // JACK reuses an ID
		generations.observe({b, synth}, {}, t + 4ms);
		require(generations.resolveRecovery(generations.pending()[0]).first->id == b.id,
			"a new incarnation of a reused ID is eligible");
	}
	{
		MidiConnections bothLost;
		Port a{92, {"a:out", "name:a:source"}, true};
		Port b{93, {"b:in", "name:b:sink"}, false};
		Port twin{94, {"b-twin:in", b.endpoint.identity}, false};
		bothLost.observe({a, b, twin}, {{92, 93}}, t);
		bothLost.removed(92, t + 1ms);
		bothLost.removed(93, t + 2ms);
		a.id = 95;
		bothLost.observe({a, twin}, {}, t + 3ms);
		require(!bothLost.resolveRecovery(bothLost.pending()[0]).first, "each disappearing endpoint records its own exclusions");
		b.id = 96;
		bothLost.observe({a, twin, b}, {}, t + 4ms);
		const auto restored = bothLost.resolveRecovery(bothLost.pending()[0]);
		require(restored.first && restored.second->id == 96, "both endpoints recover only to eligible incarnations");
	}

	{
		SetConnectionInfo legacy("system", "midi_capture_1", "synth-0", "midiin1");
		legacy.source_midi_identity = "usb:model:path:A:midi:0:0:source";
		auto oldJson = legacy.toJson();
		oldJson["source_midi_alias"] = "in-hw-7-0-0-Keys";
		auto converted = SetConnectionInfo::fromJson(oldJson);
		require(converted.source_name == "in-hw-7-0-0-Keys" && converted.source_port_name.empty() &&
			converted.source_midi_port == "system:midi_capture_1", "working-tree export converts to alias address plus JACK hint");
		require(!converted.toJson().contains("source_midi_alias"), "new exports do not duplicate aliases in extra fields");
		require(SetConnectionInfo::fromJson(converted.toJson()).toJson() == converted.toJson(), "new JSON format round-trips");
		auto twin = converted; twin.source_midi_port = "system:midi_capture_2";
		dirty.reset({{"connections", SetDirtyState::connections(std::vector{converted, twin})}});
		dirty.update("connections", SetDirtyState::connections(std::vector{twin, converted}));
		require(!dirty.dirty(), "alias-address twins compare clean using their distinct JACK hints");
		dirty.update("connections", SetDirtyState::connections(std::vector{converted}));
		require(dirty.dirty(), "one absent alias-address twin still dirties the graph");
		for (auto key : {"source_midi_identity", "source_midi_port", "sink_midi_identity", "sink_midi_port"}) oldJson.erase(key);
		oldJson.erase("source_midi_alias");
		require(SetConnectionInfo::fromJson(oldJson).source_midi_port.empty(), "all new JSON fields remain optional");
	}

	// Identical devices share an identity, so auto-connect counts units instead of
	// suppressing by identity. Names avoid system:midi_ to skip the alias settle.
	{
		const std::string identity = "name:twin:source";
		const Port a{101, {"twin:a", identity}, true, {"twin:midi/capture_1"}};
		const Port b{102, {"twin:b", identity}, true, {"twin:midi/capture_2"}};
		int connects = 0;
		auto connect = [&] { ++connects; return true; };
		MidiAutoConnect policy; // rnbo-control: returning units are left to recovery
		for (int i = 0; i < 3; ++i) { policy.attempt(a, connect, t + i * 1s); policy.attempt(b, connect, t + i * 1s); }
		require(connects == 2, "both identical devices connect once and are not reasserted");
		auto returned = a; returned.id = 103;
		policy.portChanged(a.id);
		policy.attempt(returned, connect, t + 10s);
		policy.attempt(b, connect, t + 10s);
		require(connects == 2, "an identical device replugged while its twin stays is left to recovery");
		policy.portChanged(returned.id); policy.portChanged(b.id);
		returned.id = 104; auto other = b; other.id = 105;
		policy.attempt(returned, connect, t + 20s);
		policy.attempt(other, connect, t + 20s);
		require(connects == 2, "both identical devices replugged are left to recovery");

		MidiAutoConnect single;
		int singleConnects = 0;
		auto once = [&] { ++singleConnects; return true; };
		single.attempt(a, once, t);
		single.portChanged(a.id);
		single.attempt(returned, once, t + 1s);
		require(singleConnects == 1, "a single replugged device is not auto-connected again");
		auto third = b; third.id = 106; third.endpoint.identity = identity;
		single.attempt(third, once, t + 2s);
		require(singleConnects == 2, "an additional identical device is a new unit");

		// Two devices share a provisional alias key until bridge metadata arrives.
		MidiAutoConnect late;
		int lateConnects = 0;
		auto lateConnect = [&] { ++lateConnects; return true; };
		Port d1{110, {"bridge:one", "name:shared:source"}, true, {"shared"}};
		late.attempt(d1, lateConnect, t);
		d1.endpoint.identity = "bridge:device1:source";
		late.attempt(d1, lateConnect, t + 1s);
		Port d2{111, {"bridge:two", "name:shared:source"}, true, {"shared"}};
		late.attempt(d2, lateConnect, t + 2s);
		d2.endpoint.identity = "bridge:device2:source";
		late.attempt(d2, lateConnect, t + 3s);
		late.attempt(d2, lateConnect, t + 4s);
		require(lateConnects == 2, "a second device sharing a provisional key connects exactly once");
		late.portChanged(d2.id);
		late.attempt(d2, lateConnect, t + 5s);
		require(lateConnects == 2, "and is left to recovery when it returns with its final identity");

		// A returning device keeps its suppression when its identity arrives late.
		MidiAutoConnect upgraded;
		int upgradedConnects = 0;
		auto upgradedConnect = [&] { ++upgradedConnects; return true; };
		Port lone{112, {"bridge:lone", "name:lone:source"}, true, {"lone"}};
		upgraded.attempt(lone, upgradedConnect, t);
		upgraded.portChanged(lone.id);
		upgraded.attempt(lone, upgradedConnect, t + 1s);
		lone.endpoint.identity = "bridge:lone-device:source";
		upgraded.attempt(lone, upgradedConnect, t + 2s);
		upgraded.attempt(lone, upgradedConnect, t + 3s);
		require(upgradedConnects == 1, "late metadata does not undo a returning device's suppression");

		MidiAutoConnect instance(false); // standalone instance: per port incarnation
		int instanceConnects = 0;
		auto count = [&] { ++instanceConnects; return true; };
		for (int i = 0; i < 3; ++i) instance.attempt(a, count, t + i * 1s);
		require(instanceConnects == 1, "an incarnation is connected once and never reasserted");
		instance.attempt(b, count, t);
		require(instanceConnects == 2, "identical devices both connect to an instance");
		instance.portChanged(a.id);
		instance.attempt(a, count, t + 5s);
		require(instanceConnects == 3, "a re-registered port is eligible again, even after a deliberate disconnect");
		instance.attempt(a, count, t + 6s);
		require(instanceConnects == 3, "the new incarnation is not reasserted either");
	}

	// Serial-less devices are bound to a socket (path identity). The alias fallback
	// that follows a device to another socket must not take one that belongs to
	// another cable.
	{
		const std::string pathA = "usb:1234:5678:path:socket-A:midi:0:Keys:source";
		const std::string pathB = "usb:1234:5678:path:socket-B:midi:0:Keys:source";
		const std::string pathC = "usb:1234:5678:path:socket-C:midi:0:Keys:source";
		const std::string alias1 = "in-hw-1-0-0-Keys", alias2 = "in-hw-2-0-0-Keys";
		auto keys = [](uint32_t id, const std::string& identity, const std::string& alias) {
			return Port{id, {"system:midi_capture_" + std::to_string(id), identity, alias}, true, {alias}};
		};
		auto stored = [](const std::string& identity, const std::string& alias) {
			return MidiConnections::Endpoint{"system:midi_capture_" + alias.substr(7, 1), identity, alias};
		};
		const Port s1{201, {"synth-1:midiin1", ""}, false}, s2{202, {"synth-2:midiin1", ""}, false};
		// What midiJackLoadEdge does, without JACK.
		auto load = [](MidiConnections& g, Edge edge, MidiConnections::Selection& taken) {
			const auto requested = edge;
			const auto* source = g.resolve(edge.source, true, taken);
			const auto* sink = g.resolve(edge.sink, false, taken);
			// Like production, a resolved endpoint is rewritten to the live port.
			if (source) edge.source = source->endpoint;
			if (sink) edge.sink = sink->endpoint;
			if (source && sink) {
				taken.insert(source->id); taken.insert(sink->id);
				g.connected({source->id, sink->id}, false, &requested);
			} else g.defer(edge, requested);
		};
		// What midiJackRetryConnections does, without JACK.
		auto retry = [](MidiConnections& g) {
			auto taken = g.occupied();
			for (size_t i = 0; i < g.pending().size();) {
				const auto edge = g.pending()[i];
				const auto [source, sink] = g.resolveRecovery(edge, taken);
				if (!source || !sink) { ++i; continue; }
				g.forget(edge);
				g.resolved(edge, {source->endpoint, sink->endpoint}, MidiConnections::Pair{source->id, sink->id});
				g.connected({source->id, sink->id}, false, &edge);
				taken.insert(source->id); taken.insert(sink->id);
			}
		};

		// (a) set load: K1 (socket A) -> synth1 and K2 (socket B) -> synth2, only K2 present.
		const Edge e1{stored(pathA, alias1), s1.endpoint}, e2{stored(pathB, alias2), s2.endpoint};
		const Port k2 = keys(52, pathB, alias2);
		{
			MidiConnections claimed;
			claimed.observe({k2, s1, s2}, {});
			claimed.claim({e1, e2});
			require(!claimed.resolve(e1.source, true), "a socket claimed by another saved cable is not a fallback");
			claimed.releaseClaims();
			require(claimed.resolve(e1.source, true)->id == k2.id, "load claims do not outlive the load");
		}
		for (const bool reversed : {false, true}) {
			MidiConnections load1;
			load1.observe({k2, s1, s2}, {});
			std::vector<Edge> edges{e1, e2};
			if (reversed) std::swap(edges[0], edges[1]);
			MidiConnections::Selection taken;
			load1.claim(edges);
			for (const auto& edge : edges) load(load1, edge, taken);
			load1.releaseClaims();
			require(load1.pending().size() == 1 && load1.pending()[0].source.identity == pathA,
				"the absent controller's cable stays pending in either processing order");
			require(load1.occupied().count(s2.id) && !load1.occupied().count(s1.id), "K2 drives only its own synth");
			require(!load1.resolveRecovery(load1.pending()[0]).first, "later polls leave K1's cable alone");
			load1.observe({k2, s1, s2}, {{k2.id, s2.id}});
			retry(load1);
			require(load1.pending().size() == 1 && !load1.occupied().count(s1.id), "observation keeps what a live cable was requested under");
			for (const auto& socket : {pathA, pathC}) {
				auto returning = load1;
				const Port k1 = keys(51, socket, alias1);
				returning.observe({k1, k2, s1, s2}, {{k2.id, s2.id}});
				retry(returning);
				require(returning.pending().empty() && returning.occupied().count(k1.id) && returning.occupied().count(s1.id),
					"K1's cable connects when K1 returns, to the same or another socket");
			}
		}

		// (b) hotplug: both live, both removed, K2 returns to socket B first.
		{
			MidiConnections hot;
			const Port k1 = keys(51, pathA, alias1);
			hot.observe({k1, k2, s1, s2}, {{k1.id, s1.id}, {k2.id, s2.id}});
			hot.removed(k1.id); hot.removed(k2.id);
			hot.observe({s1, s2}, {});
			const Port back = keys(62, pathB, alias2);
			hot.registered(back.id);
			for (int poll = 0; poll < 3; ++poll) {
				hot.observe({back, s1, s2}, poll ? std::vector<MidiConnections::Pair>{{back.id, s2.id}} : std::vector<MidiConnections::Pair>{});
				retry(hot);
				require(hot.pending().size() == 1 && hot.pending()[0].source.identity == pathA &&
					hot.occupied().count(s2.id) && !hot.occupied().count(s1.id), "only K2's cable connects, on every poll");
			}
			const Port k1Back = keys(61, pathA, alias1);
			hot.registered(k1Back.id);
			hot.observe({k1Back, back, s1, s2}, {{back.id, s2.id}});
			retry(hot);
			require(hot.pending().empty() && hot.occupied().count(k1Back.id) && hot.occupied().count(s1.id),
				"K1's cable connects when K1 returns");
		}

		// (c) one serial-less controller with two cables, moved between sockets.
		{
			const Edge c1{stored(pathA, alias1), s1.endpoint}, c2{stored(pathA, alias1), s2.endpoint};
			MidiConnections loaded;
			const Port kb = keys(53, pathB, alias2);
			loaded.observe({kb, s1, s2}, {});
			MidiConnections::Selection taken;
			loaded.claim({c1, c2});
			load(loaded, c1, taken); load(loaded, c2, taken);
			loaded.releaseClaims();
			require(loaded.pending().empty() && loaded.occupied().count(s1.id) && loaded.occupied().count(s2.id),
				"both cables of a controller saved in socket A connect at load in socket B");

			MidiConnections hot;
			const Port ka = keys(54, pathA, alias1);
			hot.observe({ka, s1, s2}, {{ka.id, s1.id}, {ka.id, s2.id}});
			hot.removed(ka.id);
			hot.observe({s1, s2}, {});
			const Port inB = keys(64, pathB, alias2);
			hot.registered(inB.id);
			hot.observe({inB, s1, s2}, {});
			retry(hot);
			require(hot.pending().empty() && hot.occupied().count(s1.id) && hot.occupied().count(s2.id),
				"both cables follow the controller from socket A to B");
			hot.observe({inB, s1, s2}, {{inB.id, s1.id}, {inB.id, s2.id}});
			hot.removed(inB.id);
			hot.observe({s1, s2}, {});
			require(hot.pending().size() == 2, "both cables wait for the controller again");
			const Port inA = keys(65, pathA, alias1);
			hot.registered(inA.id);
			hot.observe({inA, s1, s2}, {});
			retry(hot);
			require(hot.pending().empty() && hot.occupied().count(inA.id), "and follow it back from B to A");
		}

		// A partially resolved load must not make the controller block its own other cable.
		{
			const Port kb = keys(57, pathB, alias2), ext{203, {"ext:midi_in", "name:Ext:sink"}, false};
			const Edge toExt{stored(pathA, alias1), ext.endpoint}, toSynth{stored(pathA, alias1), s1.endpoint};
			MidiConnections g;
			g.observe({kb, s1}, {});
			MidiConnections::Selection taken;
			g.claim({toExt, toSynth});
			load(g, toExt, taken); load(g, toSynth, taken);
			g.releaseClaims();
			require(g.pending().size() == 1 && g.occupied().count(s1.id),
				"a cable waiting for its sink does not claim the controller's new socket");
			{
				// Moved back to socket A while the sink is still absent.
				auto moved = g;
				moved.observe({kb, s1}, {{kb.id, s1.id}});
				moved.removed(kb.id);
				moved.observe({s1}, {});
				const Port home = keys(62, pathA, alias1);
				moved.registered(home.id);
				moved.observe({home, s1}, {});
				retry(moved);
				require(moved.pending().size() == 1 && moved.occupied().count(home.id),
					"a deferred cable does not block its own controller returning to its saved socket");
				moved.registered(ext.id);
				moved.observe({home, s1, ext}, {{home.id, s1.id}});
				retry(moved);
				require(moved.pending().empty() && moved.occupied().count(ext.id), "and it connects there when its sink appears");
			}
			g.registered(ext.id);
			g.observe({kb, s1, ext}, {{kb.id, s1.id}});
			retry(g);
			require(g.pending().empty() && g.occupied().count(ext.id), "the waiting cable connects when its sink appears");
			g.observe({kb, s1, ext}, {{kb.id, s1.id}, {kb.id, ext.id}});
			g.removed(kb.id);
			g.observe({s1, ext}, {});
			const Port back = keys(58, pathA, alias1);
			g.registered(back.id);
			g.observe({back, s1, ext}, {});
			retry(g);
			require(g.pending().empty(), "and both cables still follow the controller afterwards");
		}

		// Absent at load, partial recovery in another socket, then a repeated hotplug.
		for (const bool sinkSide : {false, true}) {
			// sinkSide mirrors the case: a serial-less output device and two sources.
			const std::string outA = "usb:1234:5678:path:socket-A:midi:0:Keys:sink", outB = "usb:1234:5678:path:socket-B:midi:0:Keys:sink";
			const std::string oa = "out-hw-1-0-0-Keys", ob = "out-hw-2-0-0-Keys";
			const MidiConnections::Endpoint saved = sinkSide ? MidiConnections::Endpoint{"system:midi_playback_1", outA, oa} : stored(pathA, alias1);
			const Port atB = sinkSide ? Port{71, {"system:midi_playback_71", outB, ob}, false, {ob}} : keys(71, pathB, alias2);
			const Port near{204, {"near:midi", ""}, sinkSide}, far{205, {"far:midi", sinkSide ? "name:Far:source" : "name:Far:sink"}, sinkSide};
			auto edge = [&](const MidiConnections::Endpoint& device, const Port& peer) {
				return sinkSide ? Edge{peer.endpoint, device} : Edge{device, peer.endpoint};
			};
			auto pair = [&](const Port& device, const Port& peer) {
				return sinkSide ? MidiConnections::Pair{peer.id, device.id} : MidiConnections::Pair{device.id, peer.id};
			};
			MidiConnections g;
			g.observe({near}, {});
			MidiConnections::Selection taken;
			g.claim({edge(saved, near), edge(saved, far)});
			load(g, edge(saved, near), taken); load(g, edge(saved, far), taken);
			g.releaseClaims();
			require(g.pending().size() == 2, "both cables wait for the absent controller");
			g.registered(atB.id);
			g.observe({near, atB}, {});
			retry(g);
			require(g.pending().size() == 1 && g.occupied().count(near.id), "the controller appears in socket B and its first cable follows");
			g.observe({near, atB}, {pair(atB, near)});
			g.removed(atB.id);
			g.observe({near}, {});
			g.registered(atB.id);
			g.observe({near, atB}, {});
			retry(g);
			require(g.pending().size() == 1 && g.occupied().count(near.id), "replugged in socket B, the first cable recovers");
			g.registered(far.id);
			g.observe({near, atB, far}, {pair(atB, near)});
			retry(g);
			require(g.pending().empty() && g.occupied().count(far.id),
				"a recovered cable never excludes its own controller from a sibling cable");
		}

		// A twin that registers between a (partial) recovery and the next unplug,
		// with the controller returning to the same or another socket, once or twice.
		for (const bool sinkSide : {false, true}) for (const char* socket : {"A", "B"})
		for (const bool twice : {false, true}) for (const bool sibling : {true, false}) for (const int order : {0, 1, 2}) {
			// 1: the twin registers and the controller leaves between two snapshots.
			// 2: a snapshot already misses the controller before its removal is processed.
			// A lone waiting cable in another socket is only an alias match; with a
			// same-model twin present it is ambiguous and the connect-one rule applies.
			if (!sibling && std::string(socket) == "B") continue;
			const std::string dir = sinkSide ? ":sink" : ":source";
			auto device = [&](uint32_t id, const std::string& where) {
				const std::string alias = std::string(sinkSide ? "out" : "in") + "-hw-" + std::to_string(id) + "-0-0-Keys";
				return Port{id, {"system:midi_dev_" + std::to_string(id), "usb:1234:5678:path:socket-" + where + ":midi:0:Keys" + dir, alias}, !sinkSide, {alias}};
			};
			const Port near{207, {"near2:midi", ""}, sinkSide}, far{208, {"far2:midi", sinkSide ? "name:Far2:source" : "name:Far2:sink"}, sinkSide};
			auto pair = [&](const Port& dev, const Port& peer) {
				return sinkSide ? MidiConnections::Pair{peer.id, dev.id} : MidiConnections::Pair{dev.id, peer.id};
			};
			// `sibling` gives the controller a second cable that recovers while the first waits.
			auto ports = [&](std::vector<Port> list) { if (sibling) list.push_back(near); return list; };
			auto cables = [&](const Port& dev, bool withFar) {
				std::vector<MidiConnections::Pair> list;
				if (sibling) list.push_back(pair(dev, near));
				if (withFar) list.push_back(pair(dev, far));
				return list;
			};
			const size_t waiting = sibling ? 2 : 1;
			const Port k = device(80, "A");
			MidiConnections g;
			g.observe(ports({k, far}), cables(k, true));
			g.removed(far.id);
			g.observe(ports({k}), cables(k, false));
			g.removed(k.id);
			g.observe(ports({}), {});
			require(g.pending().size() == waiting, "the hand-made cables wait");
			Port current = device(81, socket);
			g.registered(current.id);
			g.observe(ports({current}), {});
			retry(g);
			require(g.pending().size() == 1, "the controller returns; the cable to the absent peer still waits");
			if (twice) {
				g.observe(ports({current}), cables(current, false));
				g.removed(current.id);
				g.observe(ports({}), {});
				current = device(84, socket);
				g.registered(current.id);
				g.observe(ports({current}), {});
				retry(g);
				require(g.pending().size() == 1, "and again after a second replug in the same socket");
			}
			const Port twin = device(82, "C");
			g.observe(ports({current}), cables(current, false));
			g.registered(twin.id);
			if (order == 0) g.observe(ports({current, twin}), cables(current, false));
			if (order == 2) g.observe(ports({twin}), {});
			g.removed(current.id);
			g.observe(ports({twin}), {});
			g.registered(far.id);
			g.observe(ports({twin, far}), {});
			retry(g);
			require(g.pending().size() == waiting && !g.occupied().count(twin.id),
				"a twin that stayed takes none of the controller's cables");
			const Port again = device(83, socket);
			g.registered(again.id);
			g.observe(ports({twin, again, far}), {});
			retry(g);
			require(g.pending().empty() && g.occupied().count(again.id) && g.occupied().count(far.id) &&
				(!sibling || g.occupied().count(near.id)) && !g.occupied().count(twin.id),
				"every cable returns to the controller, not the twin");
		}

		// Identical devices (one shared identity): the first one seen keeps the cable.
		for (const bool sinkSide : {false, true}) {
			const std::string identity = sinkSide ? "name:Twin Keys:sink" : "name:Twin Keys:source";
			auto unit = [&](uint32_t id) {
				return Port{id, {"system:midi_unit_" + std::to_string(id), identity}, !sinkSide, {"Twin Keys"}};
			};
			const Port far{210, {"far4:midi", sinkSide ? "name:Far4:source" : "name:Far4:sink"}, sinkSide};
			const Port k = unit(95), returned = unit(96), later = unit(97), again = unit(98);
			MidiConnections g;
			g.observe({k, far}, {sinkSide ? MidiConnections::Pair{far.id, k.id} : MidiConnections::Pair{k.id, far.id}});
			g.removed(far.id);
			g.observe({k}, {});
			g.removed(k.id);
			g.observe({}, {});
			g.registered(returned.id);
			g.observe({returned}, {});
			g.registered(later.id);
			g.observe({returned, later}, {});
			g.removed(returned.id);
			g.observe({later}, {});
			g.registered(far.id);
			g.observe({later, far}, {});
			retry(g);
			require(g.pending().size() == 1 && !g.occupied().count(later.id), "an identical device that arrived later does not take the cable");
			g.registered(again.id);
			g.observe({later, again, far}, {});
			retry(g);
			require(g.pending().empty() && g.occupied().count(again.id), "the cable returns to the next registration");
		}

		// A stand-in twin seen while the controller was away must not lock it out.
		// `batched`: the controller registers and the twin leaves between two snapshots.
		for (const bool sinkSide : {false, true}) for (const bool deferred : {false, true}) for (const bool batched : {false, true}) {
			const std::string dir = sinkSide ? ":sink" : ":source";
			auto device = [&](uint32_t id, const std::string& where) {
				const std::string alias = std::string(sinkSide ? "out" : "in") + "-hw-" + std::to_string(id) + "-0-0-Keys";
				return Port{id, {"system:midi_dev_" + std::to_string(id), "usb:1234:5678:path:socket-" + where + ":midi:0:Keys" + dir, alias}, !sinkSide, {alias}};
			};
			const Port far{209, {"far3:midi", sinkSide ? "name:Far3:source" : "name:Far3:sink"}, sinkSide};
			const Port k = device(90, "A"), twin = device(91, "C"), back = device(92, "A");
			MidiConnections g;
			if (deferred) {
				// Loaded with both ends absent.
				g.observe({}, {});
				MidiConnections::Selection taken;
				load(g, sinkSide ? Edge{far.endpoint, k.endpoint} : Edge{k.endpoint, far.endpoint}, taken);
			} else {
				g.observe({k, far}, {sinkSide ? MidiConnections::Pair{far.id, k.id} : MidiConnections::Pair{k.id, far.id}});
				g.removed(far.id);
				g.observe({k}, {});
				g.removed(k.id);
				g.observe({}, {});
			}
			require(g.pending().size() == 1, "the cable waits for both ends");
			g.registered(twin.id);
			g.observe({twin}, {});
			g.registered(back.id);
			if (!batched) g.observe({twin, back}, {});
			g.removed(twin.id);
			g.observe({back}, {});
			g.registered(far.id);
			g.observe({back, far}, {});
			retry(g);
			require(g.pending().empty() && g.occupied().count(back.id),
				"a twin that came and went does not exclude the returning controller");
		}

		// An explicit save rebases what live cables claim to the saved identities.
		{
			const Port k1c = keys(59, pathC, alias1), k2b = keys(60, pathB, alias2);
			MidiConnections g;
			g.observe({k1c, k2b, s1, s2}, {});
			MidiConnections::Selection taken;
			g.claim({e1, e2});
			load(g, e1, taken); load(g, e2, taken);
			g.releaseClaims();
			require(g.pending().empty(), "K1 moved to socket C and K2 in socket B both load");
			g.observe({k1c, k2b, s1, s2}, {{k1c.id, s1.id}, {k2b.id, s2.id}});
			g.saved({{k1c.endpoint, s1.endpoint}, {k2b.endpoint, s2.endpoint}});
			g.removed(k2b.id);
			g.observe({k1c, s1, s2}, {{k1c.id, s1.id}});
			const Port k2a = keys(61, pathA, alias1);
			g.registered(k2a.id);
			g.observe({k1c, k2a, s1, s2}, {{k1c.id, s1.id}});
			retry(g);
			require(g.pending().empty() && g.occupied().count(k2a.id),
				"after a save, socket A is no longer claimed by the cable that moved to C");
		}

		// (d) a live cable that was only observed does not claim the controller.
		{
			MidiConnections g;
			const Port kb = keys(55, pathB, alias2), ctl{70, {"rnbo-control:midiin", ""}, false};
			g.observe({kb, ctl, s1}, {{kb.id, ctl.id}});
			const Edge e{stored(pathA, alias1), s1.endpoint};
			MidiConnections::Selection taken;
			g.claim({e});
			load(g, e, taken);
			g.releaseClaims();
			require(g.pending().empty() && g.occupied().count(s1.id), "a controller with an auto-connected cable still serves a saved cable");
		}

		// (e) serial, name and legacy identities behave as before.
		{
			MidiConnections g;
			const Port kb = keys(56, pathB, alias2);
			g.observe({kb, s1, s2}, {});
			g.remember({stored(pathB, alias2), s2.endpoint}, true);
			g.claim({e1, e2});
			require(!g.resolve(stored("usb:1234:5678:serial:X:midi:0:Keys:source", alias1), true), "a different serial still never matches");
			require(!g.resolve(stored("name:Keys:source", alias1), true), "name identities never fall back to an alias");
			require(g.resolve(stored("legacy:in-hw-*-0-0-Keys", alias1), true)->id == kb.id, "legacy aliases are not subject to claims");
			require(g.resolve({alias1, ""}, true)->id == kb.id, "identity-less endpoints are not subject to claims");
			require(g.resolve(stored(pathB, alias2), true)->id == kb.id, "an exact path identity is never excluded");
			g.releaseClaims();
		}
	}

	// Fake sysfs exercises real serial/path extraction without touching hardware.
	const auto fixture = std::filesystem::temp_directory_path() / ("rnbo-midi-sysfs-" + std::to_string(
		std::chrono::steady_clock::now().time_since_epoch().count()));
	const auto usb = fixture / "devices/pci0000/usb1/1-2";
	std::filesystem::create_directories(usb / "1-2:1.0");
	std::filesystem::create_directories(fixture / "class/sound/card7");
	std::filesystem::create_directory_symlink(usb / "1-2:1.0", fixture / "class/sound/card7/device");
	std::ofstream(usb / "idVendor") << "1234\n";
	std::ofstream(usb / "idProduct") << "5678\n";
	std::ofstream(usb / "serial") << "device-A\n";
	auto serial = midi_identity::get("system:midi_capture_4", {"in-hw-7-0-1-Keys"}, true, true, "", fixture);
	require(serial == "usb:1234:5678:serial:device-A:midi:0:1:source", "USB identity uses actual serial and MIDI subport");
	std::filesystem::remove(usb / "serial");
	auto path = midi_identity::get("system:midi_capture_4", {"in-hw-7-0-1-Keys"}, true, true, "", fixture);
	require(path == "usb:1234:5678:path:devices/pci0000/usb1/1-2:midi:0:1:source", "serial-less USB binds to physical socket");
	std::filesystem::remove_all(fixture);

	std::cout << "MIDI connection recovery regression tests passed\n";
}
