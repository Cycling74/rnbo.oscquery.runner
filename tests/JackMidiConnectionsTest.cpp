#include "JackMidiPorts.h"
#include "SetConnectionInfo.h"
#include "SetDirtyState.h"
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

static void require(bool condition, const char* message) {
	if (!condition) throw std::runtime_error(message);
}

// Run against a separate dummy JACK server: JACK_DEFAULT_SERVER=rnbo-midi-test.
// No audio hardware or existing user graph is needed.
struct Harness {
	jack_client_t* client = nullptr;
	MidiConnections graph;
	struct Event {
		uint32_t a, b;
		bool removal, connected;
		MidiConnections::Time when = MidiConnections::Clock::now();
		bool registration = false;
	};
	std::mutex mutex;
	std::vector<Event> events;
	// As in the runner: a policy forgets a port when it registers or unregisters.
	std::vector<MidiAutoConnect*> policies;

	Harness() {
		client = jack_client_open("midi-recovery-test", JackNoStartServer, nullptr);
		require(client, "start an isolated JACK dummy server before running this test");
		jack_set_port_registration_callback(client, [](jack_port_id_t id, int reg, void* self) {
			auto& h = *static_cast<Harness*>(self);
			Event event{id, 0, reg == 0, false};
			event.registration = reg != 0;
			std::lock_guard<std::mutex> lock(h.mutex);
			h.events.push_back(event);
		}, this);
		jack_set_port_connect_callback(client, [](jack_port_id_t a, jack_port_id_t b, int connected, void* self) {
			auto& h = *static_cast<Harness*>(self);
			Event event{a, b, false, connected != 0};
			std::lock_guard<std::mutex> lock(h.mutex);
			h.events.push_back(event);
		}, this);
		require(!jack_activate(client), "activate observer");
	}
	~Harness() { jack_client_close(client); }
	void poll(bool reconnect = true) {
		std::vector<Event> batch;
		{ std::lock_guard<std::mutex> lock(mutex); batch.swap(events); }
		for (auto e : batch) {
			if (e.registration || e.removal) for (auto* policy : policies) policy->portChanged(e.a);
			if (e.registration) graph.registered(e.a);
			else if (e.removal) graph.removed(e.a, e.when);
			else if (e.connected) graph.connected({e.a, e.b});
			else graph.disconnected({e.a, e.b}, e.when);
		}
		auto snapshot = midiJackSnapshot(client, &graph);
		graph.observe(snapshot.ports, snapshot.edges);
		if (reconnect) midiJackRetryConnections(client, graph);
	}
	void settle(bool reconnect = true) {
		for (int i = 0; i < 20; ++i) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			poll(reconnect);
		}
	}
};

static void basicRecovery() {
	Harness h;
	auto device = jack_client_open("fake-midi-device", JackNoStartServer, nullptr);
	auto instrument = jack_client_open("fake-instrument", JackNoStartServer, nullptr);
	require(device && instrument, "open test clients");
	jack_activate(device); jack_activate(instrument);
	auto sink = jack_port_register(instrument, "in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	auto add = [&](const char* name) {
		auto port = jack_port_register(device, name, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput | JackPortIsPhysical, 0);
		require(port, "register fake device");
		jack_port_set_alias(port, "test-keys:midi/capture_1");
		return port;
	};
	auto source = add("out1");
	h.settle();
	require(!jack_connect(device, jack_port_name(source), jack_port_name(sink)), "initial cable");
	h.settle();
	jack_port_unregister(device, source);
	h.settle();
	require(h.graph.pending().size() == 1, "real JACK unregister must retain cable");
	source = add("out2");
	h.settle();
	require(jack_port_connected_to(source, jack_port_name(sink)), "replug restores to renamed JACK endpoint");
	require(h.graph.pending().empty(), "successful restoration consumed");

	// External tool disconnect, without letting the runner poll before unplug.
	require(!jack_disconnect(device, jack_port_name(source), jack_port_name(sink)), "external disconnect");
	std::this_thread::sleep_for(MidiConnections::disconnectWindow + std::chrono::milliseconds(100)); // main thread hasn't polled
	jack_port_unregister(device, source);
	h.settle();
	source = add("out3");
	h.settle();
	require(!jack_port_connected_to(source, jack_port_name(sink)), "external edit survives unplug/replug");

	jack_connect(device, jack_port_name(source), jack_port_name(sink));
	h.settle();
	jack_port_unregister(device, source);
	h.settle();
	h.graph.saved({});
	source = add("out4");
	h.settle();
	require(!jack_port_connected_to(source, jack_port_name(sink)), "explicit offline save cancels recovery");

	// Removal within the hysteresis window is deliberately classified as device
	// loss, even when an external client issued the preceding disconnect.
	jack_connect(device, jack_port_name(source), jack_port_name(sink));
	h.settle();
	jack_disconnect(device, jack_port_name(source), jack_port_name(sink));
	std::this_thread::sleep_for(std::chrono::milliseconds(30));
	jack_port_unregister(device, source);
	h.settle();
	source = add("out5");
	h.settle();
	require(jack_port_connected_to(source, jack_port_name(sink)), "short disconnect/removal gap is recovered");

	// An explicit save while still undecided must win before removal arrives.
	jack_disconnect(device, jack_port_name(source), jack_port_name(sink));
	std::this_thread::sleep_for(std::chrono::milliseconds(30));
	h.poll(false);
	h.graph.saved({});
	jack_port_unregister(device, source);
	h.settle();
	source = add("out6");
	h.settle();
	require(!jack_port_connected_to(source, jack_port_name(sink)), "save during hysteresis cancels recovery");

	jack_connect(device, jack_port_name(source), jack_port_name(sink));
	h.settle();
	jack_disconnect(device, jack_port_name(source), jack_port_name(sink));
	h.settle(); h.settle();
	require(!jack_port_connected_to(source, jack_port_name(sink)), "ports remaining present must stay disconnected");
	jack_port_unregister(device, source);
	h.settle();
	source = add("out7");
	h.settle();
	require(!jack_port_connected_to(source, jack_port_name(sink)), "expired disconnect must survive later hotplug");
	// Exercise the same set-load resolver used by ProcessAudioJack, not a
	// parallel implementation in the harness.
	{
		auto audio = jack_client_open("alsa_in", JackNoStartServer, nullptr);
		require(audio, "open audio bridge client"); jack_activate(audio);
		auto out = jack_port_register(audio, "capture_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
		auto in = jack_port_register(instrument, "audioin", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
		h.settle();
		midiJackLoadEdge(h.client, h.graph, {{jack_port_name(out), {}}, {jack_port_name(in), {}}});
		require(jack_port_connected_to(out, jack_port_name(in)), "audio alsa_in route must load normally");
		require(h.graph.pending().empty(), "audio must not enter MIDI recovery");
		jack_client_close(audio);
	}
	{
		const auto present = midiJackPort(source).endpoint;
		midiJackLoadEdge(h.client, h.graph, {present, {"failed-instance:midiin1", {}}});
		require(h.graph.pending().empty(), "a missing application must not leave deferred device cables");
		midiJackLoadEdge(h.client, h.graph, {{"missing-device:out", "bridge:missing:source"}, {"failed-instance:midiin1", {}}});
		require(h.graph.pending().empty(), "missing both a device and an application must not defer");
	}
	{
		jack_port_unregister(device, source); h.settle();
		MidiConnections::Edge legacy{{"test-keys:midi/capture_1", {}}, {jack_port_name(sink), {}}};
		midiJackLoadEdge(h.client, h.graph, legacy);
		require(h.graph.pending().size() == 1 && h.graph.pending()[0].source.identity.starts_with("legacy:"),
			"loading an absent legacy device must defer its cable");
		source = add("legacy-return"); h.settle();
		require(jack_port_connected_to(source, jack_port_name(sink)), "legacy set-load cable restores on registration");
		const auto identities = h.graph.takeIdentityUpdates();
		require(identities.size() == 1 && identities[0].before[0] == "legacy:test-keys:midi/capture_1" &&
			identities[0].after[0] == midiJackPort(source).endpoint.identity, "production retry reports baseline identity refinement");
	}
	{
		jack_disconnect(device, jack_port_name(source), jack_port_name(sink));
		std::this_thread::sleep_for(MidiConnections::disconnectWindow + std::chrono::milliseconds(50)); h.settle();
		auto duplicate = add("duplicate"); h.settle();
		midiJackLoadEdge(h.client, h.graph, {{"test-keys:midi/capture_1", {}}, {jack_port_name(sink), {}}});
		require(jack_port_connected_to(source, jack_port_name(sink)) + jack_port_connected_to(duplicate, jack_port_name(sink)) == 1,
			"ambiguous legacy load must connect exactly one current candidate");
		jack_disconnect(device, jack_port_name(source), jack_port_name(sink));
		jack_disconnect(device, jack_port_name(duplicate), jack_port_name(sink));
		h.graph.saved({});
		jack_port_unregister(device, duplicate);
	}
	{
		// The default JACK2 sequencer uses these aliases, including for bridges
		// whose endpoints may lack JackPortIsPhysical.
		const char* alias = "BlueKeys:BlueKeys MIDI 1 (out)";
		midiJackLoadEdge(h.client, h.graph, {{alias, {}}, {jack_port_name(sink), {}}});
		require(h.graph.pending().size() == 1, "absent JACK2 named seq alias is deferred");
		auto port = jack_port_register(device, "seq-bridge", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
		jack_port_set_alias(port, alias); h.settle();
		require(jack_port_connected_to(port, jack_port_name(sink)), "nonphysical sequencer bridge can restore legacy cable");
		jack_port_unregister(device, port); h.settle(); h.graph.saved({});
	}

	{
		auto app = jack_client_open("software-midi", JackNoStartServer, nullptr);
		require(app, "open software MIDI client"); jack_activate(app);
		auto port = jack_port_register(app, "out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
		h.settle(); jack_connect(app, jack_port_name(port), jack_port_name(sink)); h.settle();
		jack_port_unregister(app, port); h.settle();
		port = jack_port_register(app, "out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0); h.settle();
		require(jack_port_connected_to(port, jack_port_name(sink)), "software MIDI port restart restores prior cables");
		jack_client_close(app);
	}
	jack_client_close(device); jack_client_close(instrument);
	std::cout << "Live JACK MIDI recovery tests passed\n";
}

static void reviewedRecovery() {
	Harness h;
	auto device = jack_client_open("review-device", JackNoStartServer, nullptr);
	auto instrument = jack_client_open("renamed-0", JackNoStartServer, nullptr);
	auto control = jack_client_open(CONTROL_CLIENT_NAME.c_str(), JackNoStartServer, nullptr);
	require(device && instrument && control, "open review clients");
	jack_activate(device); jack_activate(instrument); jack_activate(control);
	auto sink = jack_port_register(instrument, "midiin1", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	auto output = jack_port_register(instrument, "midiout1", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
	jack_port_set_alias(sink, "renamed-0:rnbomidiin1");
	jack_port_set_alias(output, "renamed-0:rnbomidiout1");
	auto controlIn = jack_port_register(control, "in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	auto controlOut = jack_port_register(control, "out", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
	auto add = [&](const char* name, const char* alias = "Twin:Keys (out)") {
		auto port = jack_port_register(device, name, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput | JackPortIsPhysical, 0);
		require(port, "register review device"); jack_port_set_alias(port, alias); return port;
	};
	auto disconnect = [&](jack_port_t* source, jack_port_t* target) {
		h.graph.forgetNames(jack_port_name(source), jack_port_name(target));
		if (jack_port_connected_to(source, jack_port_name(target))) jack_disconnect(device, jack_port_name(source), jack_port_name(target));
		h.settle();
	};
	auto first = add("first"); h.settle();
	require(midiJackPort(sink).endpoint.identity.empty() && midiJackPort(output).endpoint.identity.empty(),
		"actual RNBO port aliases do not produce saved device identities");
	MidiAutoConnect instancePolicy(false); // standalone instances suppress per port incarnation only
	h.policies.push_back(&instancePolicy);
	midiJackAutoConnect(instrument, sink, output, first, instancePolicy, {false, false, true, false, false});
	h.settle();
	require(!jack_port_connected_to(first, jack_port_name(sink)) && midiJackSnapshot(h.client).edges.empty(),
		"set-loaded instances do not auto-connect unsaved hardware cables or change the graph");
	midiJackAutoConnect(instrument, sink, output, first, instancePolicy, {true, false, true, false, false});
	h.settle();
	require(jack_port_connected_to(first, jack_port_name(sink)), "standalone instance hardware option connects on first hotplug");
	disconnect(first, sink);

	SetConnectionInfo renamed("review-device", "first", "renamed-0", "midiin1");
	renamed.source_midi_identity = midiJackPort(first).endpoint.identity;
	renamed.sink_instance_index = 0;
	midiJackLoadEdge(h.client, h.graph, midiJackSavedEdge(renamed));
	require(jack_port_connected_to(first, jack_port_name(sink)), "an indexed instance is addressed by its name");
	disconnect(first, sink);
	renamed.sink_name = "failed-instance-0";
	midiJackLoadEdge(h.client, h.graph, midiJackSavedEdge(renamed));
	require(h.graph.pending().empty(), "failed indexed instance does not become a missing device");

	auto second = add("second"); h.settle();
	MidiConnections::Selection selection;
	const auto target = midiJackPort(sink).endpoint;
	midiJackLoadEdge(h.client, h.graph, {midiJackPort(second).endpoint, target}, &selection);
	require(jack_port_connected_to(second, jack_port_name(sink)) && !jack_port_connected_to(first, jack_port_name(sink)),
		"exact saved JACK name selects the named twin");
	midiJackLoadEdge(h.client, h.graph, {midiJackPort(first).endpoint, target}, &selection); h.settle();
	jack_port_unregister(device, first); h.settle();
	require(h.graph.pending().size() == 1, "surviving twin must not satisfy the missing twin's same-target cable");
	first = add("first-returned"); h.settle();
	require(jack_port_connected_to(first, jack_port_name(sink)) && jack_port_connected_to(second, jack_port_name(sink)),
		"returning twin regains its cable while the other stays connected");
	require(h.graph.pending().empty(), "one restoration consumes exactly one pending twin cable");
	disconnect(first, sink); disconnect(second, sink);
	selection.clear();
	MidiConnections::Edge unnamed{{"Twin:Keys (out)", midiJackPort(first).endpoint.identity}, target};
	midiJackLoadEdge(h.client, h.graph, unnamed, &selection);
	midiJackLoadEdge(h.client, h.graph, unnamed, &selection); h.settle();
	require(jack_port_connected_to(first, jack_port_name(sink)) && jack_port_connected_to(second, jack_port_name(sink)),
		"two saved alias-only cables choose different twins where possible");
	disconnect(first, sink); disconnect(second, sink);
	auto marked = midiJackPort(first).endpoint;
	marked.alias = "Twin:Keys (out)";
	midiJackLoadEdge(h.client, h.graph, {marked, target});
	require(jack_port_connected_to(first, jack_port_name(sink)), "a saved JACK name hint picks its twin");
	disconnect(first, sink);
	jack_port_unregister(device, first); h.settle();
	midiJackLoadEdge(h.client, h.graph, {marked, target});
	require(jack_port_connected_to(second, jack_port_name(sink)), "sole current candidate connects after a session that saw twins");
	disconnect(second, sink);

	MidiAutoConnect controlPolicy;
	h.policies.push_back(&controlPolicy);
	midiJackAutoConnect(control, controlIn, controlOut, second, controlPolicy, {true, true, false, false, true}); h.settle();
	require(jack_port_connected_to(second, jack_port_name(controlIn)), "control automatically connects initial device");
	jack_port_unregister(device, second); h.settle();
	h.graph.saved({});
	second = add("control-return"); h.settle();
	require(jack_port_connected_to(second, jack_port_name(controlIn)), "offline explicit save preserves control recovery");
	jack_disconnect(device, jack_port_name(second), jack_port_name(controlIn));
	std::this_thread::sleep_for(MidiConnections::disconnectWindow + std::chrono::milliseconds(50));
	h.settle(); jack_port_unregister(device, second); h.settle(); h.graph.saved({});
	second = add("control-after-edit"); h.settle();
	midiJackAutoConnect(control, controlIn, controlOut, second, controlPolicy, {true, true, false, false, true}); h.settle();
	require(!jack_port_connected_to(second, jack_port_name(controlIn)), "intentional control disconnect survives save and hotplug");

	// ControlAutoConnectMIDI only gates whether the runner creates control cables;
	// a hand-made one is recovered, and survives an explicit offline save, like any other.
	jack_connect(device, jack_port_name(second), jack_port_name(controlIn)); h.settle();
	jack_port_unregister(device, second); h.settle();
	require(h.graph.pending().size() == 1, "hand-made control cable is remembered when its device is unplugged");
	h.graph.saved({});
	second = add("control-handmade"); h.settle();
	require(jack_port_connected_to(second, jack_port_name(controlIn)), "hand-made control cable is restored without control auto-connect");
	require(h.graph.pending().empty(), "restored control cable leaves no recovery entry");

	MidiConnections::Edge absent{{"missing:out", "name:Absent:Keys (out):source", "Absent:Keys (out)"}, target};
	midiJackLoadEdge(h.client, h.graph, absent);
	require(h.graph.pending().size() == 1, "loaded device cable remains pending while absent");
	auto appearing = add("arrived-after-load", "Absent:Keys (out)");
	midiJackAutoConnect(instrument, sink, output, appearing, instancePolicy, {false, false, true, false, false});
	h.settle();
	require(jack_port_connected_to(appearing, jack_port_name(sink)), "a set-loaded instance restores its saved cable when hardware appears");

	// Synthetic USB identities exercise socket fallback through production load,
	// snapshot caching, and retry glue without requiring USB hardware in the VM.
	auto pathPort = add("path-A", "in-hw-1-0-0-Keys"); h.settle();
	auto snapshot = midiJackSnapshot(h.client);
	for (auto& port : snapshot.ports) if (port.endpoint.name == jack_port_name(pathPort))
		port.endpoint.identity = "usb:model:path:socket-B:midi:0:0:source";
	h.graph.observe(snapshot.ports, snapshot.edges);
	MidiConnections::Edge pathEdge{{"saved:port", "usb:model:path:socket-A:midi:0:0:source", "in-hw-7-0-0-Keys"}, target};
	midiJackLoadEdge(h.client, h.graph, pathEdge); h.settle();
	require(jack_port_connected_to(pathPort, jack_port_name(sink)), "set loads after moving a serial-less device to another socket");
	h.graph.takeIdentityUpdates();
	jack_port_unregister(device, pathPort); h.settle();
	pathPort = add("path-C", "in-hw-9-0-0-Keys"); h.settle(false);
	snapshot = midiJackSnapshot(h.client);
	for (auto& port : snapshot.ports) if (port.endpoint.name == jack_port_name(pathPort))
		port.endpoint.identity = "usb:model:path:socket-C:midi:0:0:source";
	h.graph.observe(snapshot.ports, snapshot.edges); h.settle();
	require(jack_port_connected_to(pathPort, jack_port_name(sink)), "hotplug into another socket restores the remembered alias cable");
	const auto updates = h.graph.takeIdentityUpdates();
	require(updates.size() == 1 && updates[0].before[0].find("socket-B") != std::string::npos &&
		updates[0].after[0].find("socket-C") != std::string::npos, "socket fallback reports a scoped dirty-baseline refinement");
	jack_client_close(device); jack_client_close(instrument); jack_client_close(control);
}

static void distinctSinkTwins(bool raw) {
	Harness h;
	auto device = jack_client_open("different-sink-twins", JackNoStartServer, nullptr);
	auto synth = jack_client_open("two-synths", JackNoStartServer, nullptr);
	require(device && synth, "open different-sink twin clients");
	jack_activate(device); jack_activate(synth);
	auto sinkA = jack_port_register(synth, "first", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	auto sinkB = jack_port_register(synth, "second", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	auto add = [&](const char* name, const char* alias) {
		auto port = jack_port_register(device, name, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput | JackPortIsPhysical, 0);
		require(port, "register different-sink twin");
		jack_port_set_alias(port, alias); return port;
	};
	auto a = add("A", raw ? "in-hw-1-0-0-Twin" : "Twin:Keys (out)");
	auto b = add("B", raw ? "in-hw-2-0-0-Twin" : "Twin:Keys (out)");
	h.settle();
	auto assignUsb = [&](jack_port_t* port, const char* identity) {
		auto snapshot = midiJackSnapshot(h.client, &h.graph);
		for (auto& p : snapshot.ports) if (p.endpoint.name == jack_port_name(port)) p.endpoint.identity = identity;
		h.graph.observe(snapshot.ports, snapshot.edges);
	};
	if (raw) {
		assignUsb(a, "usb:model:path:socket-A:midi:0:0:source");
		assignUsb(b, "usb:model:path:socket-B:midi:0:0:source");
	}
	jack_connect(device, jack_port_name(a), jack_port_name(sinkA));
	jack_connect(device, jack_port_name(b), jack_port_name(sinkB)); h.settle();
	auto saved = [&] {
		std::vector<SetConnectionInfo> result;
		const auto snapshot = midiJackSnapshot(h.client, &h.graph);
		for (const auto& [src, dst] : snapshot.edges) {
			SetConnectionInfo info;
			midiJackStoreEndpoint(info, h.graph.ports().at(src), true);
			midiJackStoreEndpoint(info, h.graph.ports().at(dst), false);
			result.push_back(info);
		}
		return result;
	};
	SetDirtyState dirty;
	dirty.reset({{"connections", SetDirtyState::connections(saved())}});
	dirty.update("connections", SetDirtyState::connections(saved()));
	require(!dirty.dirty(), "saving and comparing twins with alias addresses is clean");
	jack_port_unregister(device, a); h.settle();
	require(!jack_port_connected_to(b, jack_port_name(sinkA)), "unplugging A must not add its different-synth cable to B");
	require(jack_port_connected_to(b, jack_port_name(sinkB)) && h.graph.pending().size() == 1,
		"B retains only its original cable while A's cable waits");
	dirty.update("connections", SetDirtyState::connections(saved()));
	require(dirty.dirty(), "missing twin remains visibly dirty");
	a = add("A-returned", raw ? "in-hw-9-0-0-Twin" : "Twin:Keys (out)"); h.settle(false);
	if (raw) assignUsb(a, "usb:model:path:socket-C:midi:0:0:source");
	h.settle();
	require(jack_port_connected_to(a, jack_port_name(sinkA)) && !jack_port_connected_to(b, jack_port_name(sinkA)),
		"A's returning incarnation recovers its different-synth cable, including a new USB socket");
	require(h.graph.pending().empty(), "return consumes the pending cable");
	dirty.remapConnectionIdentities(h.graph.takeIdentityUpdates());
	dirty.update("connections", SetDirtyState::connections(saved()));
	require(!dirty.dirty(), "different-sink twins return clean after recovery");
	jack_client_close(device); jack_client_close(synth);
}

// Two serial-less controllers of one model, bound to sockets by path identity.
static void serialLessSockets() {
	Harness h;
	auto device = jack_client_open("socket-controllers", JackNoStartServer, nullptr);
	auto synth = jack_client_open("socket-synths", JackNoStartServer, nullptr);
	require(device && synth, "open socket clients");
	jack_activate(device); jack_activate(synth);
	auto sink1 = jack_port_register(synth, "first", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	auto sink2 = jack_port_register(synth, "second", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	auto add = [&](const char* name, const char* alias) {
		auto port = jack_port_register(device, name, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput | JackPortIsPhysical, 0);
		require(port, "register socket controller");
		jack_port_set_alias(port, alias); return port;
	};
	auto assignUsb = [&](jack_port_t* port, const char* identity) {
		auto snapshot = midiJackSnapshot(h.client, &h.graph);
		for (auto& p : snapshot.ports) if (p.endpoint.name == jack_port_name(port)) p.endpoint.identity = identity;
		h.graph.observe(snapshot.ports, snapshot.edges);
	};
	const std::string pathA = "usb:model:path:socket-A:midi:0:0:source", pathB = "usb:model:path:socket-B:midi:0:0:source";
	const std::string pathC = "usb:model:path:socket-C:midi:0:0:source";
	// K1 was saved in socket A, K2 in socket B. Only K2 is plugged in.
	const MidiConnections::Edge e1{{"saved:k1", pathA, "in-hw-1-0-0-Keys"}, {jack_port_name(sink1), {}}};
	const MidiConnections::Edge e2{{"saved:k2", pathB, "in-hw-2-0-0-Keys"}, {jack_port_name(sink2), {}}};
	auto k2 = add("K2", "in-hw-2-0-0-Keys"); h.settle(false);
	assignUsb(k2, pathB.c_str());
	for (const bool reversed : {false, true}) {
		std::vector<MidiConnections::Edge> edges{e1, e2};
		if (reversed) std::swap(edges[0], edges[1]);
		h.graph.clear();
		h.graph.claim(edges);
		MidiConnections::Selection selection;
		for (const auto& edge : edges) midiJackLoadEdge(h.client, h.graph, edge, &selection);
		h.graph.releaseClaims();
		h.settle();
		require(jack_port_connected_to(k2, jack_port_name(sink2)) && !jack_port_connected_to(k2, jack_port_name(sink1)),
			"the controller in socket B drives only its own synth, in either load order");
		require(h.graph.pending().size() == 1, "the absent controller's cable stays pending, also on later polls");
		auto k1 = add("K1", "in-hw-9-0-0-Keys"); h.settle(false);
		assignUsb(k1, pathC.c_str()); h.settle();
		require(jack_port_connected_to(k1, jack_port_name(sink1)) && !jack_port_connected_to(k2, jack_port_name(sink1)),
			"the returning controller, in a third socket, gets its own cable");
		require(h.graph.pending().empty(), "both cables are consumed");
		jack_disconnect(device, jack_port_name(k1), jack_port_name(sink1));
		jack_disconnect(device, jack_port_name(k2), jack_port_name(sink2));
		jack_port_unregister(device, k1); h.settle(false);
	}
	jack_client_close(device); jack_client_close(synth);
}

static void savedFormatCompatibility() {
	Harness h;
	auto device = jack_client_open("compat-device", JackNoStartServer, nullptr);
	auto instrument = jack_client_open("compat-instrument", JackNoStartServer, nullptr);
	require(device && instrument, "open compatibility clients");
	jack_activate(device); jack_activate(instrument);
	auto sink = jack_port_register(instrument, "in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	auto add = [&](const char* name, const char* alias) {
		auto port = jack_port_register(device, name, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput | JackPortIsPhysical, 0);
		jack_port_set_alias(port, "alsa_pcm:temporary");
		jack_port_set_alias(port, alias); return port;
	};
	auto source = add("original", "in-hw-2-0-0-Keys"); h.settle();
	SetConnectionInfo info;
	midiJackStoreEndpoint(info, midiJackPort(source), true);
	midiJackStoreEndpoint(info, midiJackPort(sink), false);
	require(info.source_name == "in-hw-2-0-0-Keys" && info.source_port_name.empty(),
		"physical source address matches HEAD's preferred non-alsa_pcm alias");
	require(info.source_midi_port == jack_port_name(source), "actual JACK name is a separate optional preference");
	auto physicalSink = jack_port_register(device, "physical-in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput | JackPortIsPhysical, 0);
	jack_port_set_alias(physicalSink, "out-hw-2-0-0-Keys");
	SetConnectionInfo outputInfo;
	midiJackStoreEndpoint(outputInfo, midiJackPort(physicalSink), false);
	require(outputInfo.sink_name == "out-hw-2-0-0-Keys" && outputInfo.sink_port_name.empty(),
		"physical sink also retains HEAD's alias address");
	auto json = info.toJson();
	for (const auto* key : {"source_midi_identity", "sink_midi_identity", "source_midi_port", "sink_midi_port"}) json.erase(key);
	const auto releaseForm = SetConnectionInfo::fromJson(json);
	jack_port_unregister(device, source); h.settle();
	source = add("renumbered", "in-hw-7-0-0-Keys"); h.settle();
	midiJackLoadEdge(h.client, h.graph, midiJackSavedEdge(releaseForm)); h.settle();
	require(jack_port_connected_to(source, jack_port_name(sink)), "JSON stripped to the release format still resolves a renumbered physical device");
	SetConnectionInfo live;
	midiJackStoreEndpoint(live, midiJackPort(source), true); midiJackStoreEndpoint(live, midiJackPort(sink), false);
	SetDirtyState dirty;
	dirty.reset({{"connections", SetDirtyState::connections(std::vector{live})}});
	dirty.update("connections", SetDirtyState::connections(std::vector{SetConnectionInfo::fromJson(live.toJson())}));
	require(!dirty.dirty(), "saving and comparing one physical MIDI cable remains clean");
	h.graph.forgetNames(jack_port_name(source), jack_port_name(sink));
	jack_disconnect(device, jack_port_name(source), jack_port_name(sink)); h.settle();
	jack_client_close(device); jack_client_close(instrument);
}

int main() {
	basicRecovery();
	reviewedRecovery();
	distinctSinkTwins(false);
	distinctSinkTwins(true);
	serialLessSockets();
	savedFormatCompatibility();
	std::cout << "Reviewed JACK MIDI load, identity, control, and auto-connect tests passed\n";
}
