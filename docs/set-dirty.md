# Set dirty state

`/rnbo/inst/control/sets/current/dirty` describes structural differences from the
last explicit load or successful save. Reverting all differences clears it.
The untitled recovery autosave does not replace that baseline.

`SetDirtyState` holds normalized, in-memory baseline components. The controller
coalesces invalidations until the end of its event batch and compares only affected
components. There are no database queries or preset serialization in the dirty
comparison path. Device notifications identify the actual instance, so events from
a fading-out instance cannot dirty a replacement using its index. Connection
notifications request a fresh graph snapshot; they never replay an old graph.

Compared state:

- Device index, patcher name, loaded library, alias, MIDI input and preset channels,
  and participation/mode in set presets.
- Parameter, buffer, inport and outport metadata, including MIDI/OSC mappings.
- Set metadata, including graph coordinates.
- Connections using the same hidden-port filtering as saving. MIDI endpoints
  with stored device identities compare by identity, so JACK port renumbering is
  not an edit. Other endpoints compare by name. Ordering and duplicate edges are
  ignored.
- Link Audio slot identities and their order.

Parameter values, last-preset bookkeeping and buffer file contents are preset
state, excluded from structural comparisons. JSON objects compare semantically;
parameter metadata entries are keyed by parameter index. Opaque set metadata
strings remain supported and compare literally.

Explicit loading establishes the baseline from the effective loaded state, after
synchronous defaults/metadata overrides have been applied. Duplicate asynchronous
notifications then compare equal. The outgoing graph is not compared during the
load's fade-out. A patcher reload retains the existing baseline. Audio deactivation
does not interpret the runtime graph teardown as device deletions.

Because the load baseline uses live state, a device that fails to load (for example,
a missing patcher or a failed library load) is absent from that baseline. The set
still reports clean, and saving it would drop the failed device. This trade-off
avoids spurious dirty reports caused by applying defaults and metadata overrides.

Deferred Link Audio edges remain in the save/comparison graph while being restored
or waiting for unavailable ports. Every set connection restore replaces the previous
pending list, including loads of legacy sets. Explicit routing edits cancel removed
pending edges; removing/replacing a device forgets its deferred edges, and slot
edits cancel obsolete slot restoration. Backend connection
notifications use quiet pushes so they cannot be mistaken for those user edits.
Legacy sets with no Link arrangement adopt its first observation, including an
empty arrangement, and track edits thereafter.

## MIDI device recovery and explicit saves

MIDI recovery history is separate from the live connection snapshot used for
saving and dirty comparison. Removing a connected MIDI device removes its live
cables and dirties a previously clean set. Restoring those cables can return the
set to clean, provided no other edits remain. The appearance of an unconnected
hardware port does not itself dirty the set. All MIDI cables are covered,
including cables between third-party clients that the runner does not own.
Software ports without device identities are matched by their JACK names.

Disconnect notifications have a 250 ms hysteresis window. The runner does not
reconnect an undecided cable. If either port is unregistered before the window
expires, the cable is retained for recovery. If the ports remain present through
the deadline, the disconnect is treated as intentional and forgotten. Removal at
or after the deadline does not restore that cable. Timestamps are captured in
JACK notification callbacks, so delayed event-loop processing does not collapse
a long disconnect/removal gap into a short one. Reconnecting manually starts a
fresh cable lifetime; duplicate notifications do not extend the timer.

An external `jack_disconnect` followed by unplugging within 250 ms is deliberately
treated as device loss and may be restored. Explicit edits through the runner's
global connection utilities cancel remembered cables immediately. Editing a live
connection list preserves pending cables to absent peers. Hysteresis does not
delay the live graph or dirty-status updates.

A successful explicit save commits the live graph and discards recovery entries
omitted from that snapshot. Background autosave and failed saves do not discard
in-memory recovery. A device removed during a save remains recoverable if the
saved snapshot included its cable. Saving also cancels omitted undecided cables.

The exception is control MIDI: an explicit offline save always preserves pending
`rnbo-control` cables. Control recovery also survives a set switch that does not
load control connections; that load leaves the preserved cables out of its
baseline, since the loaded set never contained them. Such a carried cable joins
the baseline when its device returns and the cable is restored, so the return
does not read as an edit; the same holds for a control cable kept by an offline
save. An intentional control
disconnect still stays disconnected; neither saving nor auto-connect recreates a
forgotten cable. A control cable made by hand or loaded from a set is restored
like any other, whatever `ControlAutoConnectMIDI` says: the option only controls
whether the runner creates control cables itself. A restored control cable is
visible in the live graph and participates in dirty comparison like other live
connections.

Stopping audio is not a set change. The controller snapshots the live graph,
including cables still waiting for a device, before the JACK client closes, and
rebuilds that graph when audio is active again. The set name, the baseline and
therefore the dirty state stay as they were; a cable whose device is slow to
return after the restart shows as a difference until it is restored. Carried
control cables are in no snapshot and keep waiting across the restart. Presets
are not part of this: instances come back with their last preset, as before.
Control auto-connect starts over with the new client, so a controller that was
deliberately disconnected from `rnbo-control` is connected again.

Set loads replace per-set recovery history. Removing or replacing an instance
forgets its recovery entries, including while the old instance is fading out.
Missing device cables requested by a loaded set enter the load baseline and
remain pending until the devices appear. They report dirty while unavailable
and can return to clean after restoration. Missing application instances do not
create deferred cables during load. Pending MIDI cables are never added to saved
sets; this differs from deferred Link Audio edges.

RNBO instance ports never receive device identities from their `rnbomidi` aliases.
Their addresses continue to follow the instance index when a patcher is renamed.
The loader also ignores erroneous instance identities from earlier exports.
External device identities prefer bridge metadata
`http://cycling74.com/ns/rnbo/midi-device-id`, then USB vendor/product and serial
(or topology path without a serial) for raw ALSA aliases, then a MIDI alias and
direction. No Bluetooth address or USB serial is inferred from a pretty name.

A stored USB path identity is tried first. If absent, the stored alias is matched
by direction, ignoring only the volatile ALSA card number in recognized raw
aliases. This allows moving a serial-less device to another socket or Pi. Serial
and bridge identities continue to require an identity match. Legacy aliases and
USB fallback matches refine one baseline cable at a time, preserving unrelated
edits and allowing duplicate aliases to resolve to different device identities.

That fallback never takes a serial-less device that belongs to another cable.
Ports whose own path identity is claimed in that direction are skipped. Claims
are the current sockets of other pending cables, the current sockets of live
cables that came from a saved or recovered request or survived an explicit save
(a cable that was only observed or auto-connected claims nothing), and, during
one set load, every saved endpoint, including cables not processed yet. Cables
of the same device never claim against each other. With K1 (socket A) -> synth1
and K2 (socket B) -> synth2 saved and only K2 present, K1's cable stays pending
instead of landing on K2, whatever the processing order and also on hotplug; it
connects when K1 returns, in any socket. When a serial-less device is found in
another socket, its other waiting cables are moved there with it, so one
controller with several cables follows to a new socket and back. Legacy aliases
and endpoints without identity are not excluded.

Ambiguity depends only on the ports present now. One candidate is accepted. With
several candidates, loading prefers the saved JACK name, then a candidate not
already selected by another cable in that load, then the first candidate in
`jack_get_ports` order. A previously observed collision does not block matching.
Old `ambiguous:` markers are accepted; new saves write plain identities.

Recovery of a cable that was live has an additional restriction: for each lost
endpoint, ports already registered when it disappeared are excluded. This prevents
moving A's cable to an identical B that stayed online, even when A and B use
different synth inputs or different serial-less USB sockets. A new registration
(including reuse of a JACK port ID) becomes eligible. Each disappearing endpoint
records its own exclusions. New connections and snapshots apply those exclusions
as well as the retry loop. Set-load requests retain the ordinary matching rules.

A cable that is still waiting (for example on an absent peer) follows its device
across replugs. While exactly one port with the cable's exact identity is
present, the cable is tied to that port, and when the port leaves, every port
then registered is excluded. An alias stand-in is never tied this way; if a
stand-in was the only candidate when it left, only ports seen in the last
snapshot are excluded, so the real device registering at the same moment is not
locked out.

Known limits, all involving devices the runner cannot tell apart:

- Identical devices sharing one identity (the same `name:` identity, or a
  same-model serial-less unit plugged into the saved socket): the first one seen
  after the cable started waiting is treated as its device. If that was the other
  unit, the real device is excluded when the other unit leaves, until it is
  replugged.
- A serial-less controller in a different socket than saved, whose only cable is
  waiting on an absent peer, with a same-model unit also present when it is
  unplugged: the cable may go to the other unit.
- Two identical devices that are both away and return one at a time: the first
  one back can take both sets of cables.
Recovery also excludes pairs already carrying the cable, retaining an extra
request for a returning twin rather than consuming it on an existing connection.

Physical MIDI ports keep the release runner's saved alias address in
`source_name`/`source_port_name` and `sink_name`/`sink_port_name`, preferring a
non-`alsa_pcm` alias. Older runners can therefore keep reading those same fields.
Optional `source_midi_port`/`sink_midi_port` fields hold actual JACK names only as
tie-break hints. Nonphysical application port addresses retain their JACK names.
All added JSON fields remain optional.

Migration 24 stores MIDI identities. Migration 26 replaces the intermediate
migration-25 alias fields with JACK-name hints, converts earlier working-tree
rows to the compatible alias address form, and keeps same-alias twin cables
distinct in the database's unique key. Earlier working-tree JSON with a JACK
address and `*_midi_alias` also loads; new exports no longer emit that alias
field. Dirty comparison counts distinct cables using their hints, without making
renumbering an edit. Saving and comparing one device or twins remains clean.
Old sets without identities or hints still load through name/alias matching.

Auto-connect options keep their existing scope. Instance auto-connect is enabled
only for patchers started on their own (export/run, an instance load without an
index, or built-in startup). Instances loaded in a set or at a specific index do
not auto-connect, including on hotplug: only saved or previously connected cables
are recovered. Until its first explicit save, an eligible standalone instance
applies the hardware and port-group options on hotplug as well as startup. A
successful explicit save ends that for every running instance: it then behaves
like a set-loaded one, the saved cables are the source of truth, and a new device
is not auto-connected.

Broad auto-connect connects each port incarnation once. Disabled options do not
consume that opportunity. Registration before aliases are available is retried;
`system:midi_*` aliases settle for 50 ms first. Alias updates after success
retain suppression within the incarnation, so a cable the user removes stays off
while the port stays registered. For standalone instances nothing outlives the
incarnation: after unplug and replug the device connects again, even after a
deliberate disconnect, and identical devices each connect. The control client
instead leaves a returning device to recovery, so an intentional disconnect
survives replug. It counts units (the most devices of one identity present at
once), so identical devices each connect and a twin replugged while the other
stays is a returning unit. The counts last for the client lifetime; expiring them
merely because time passed would undo edits.

Established USB identities are cached until unregistration, so a stale port cannot
adopt another device's reused ALSA card number. Indexed endpoint lookup makes
retries of absent devices cheap without expiring their pending cables.
Notification queue producers are serialized for JACK's in-process clients as
well as external clients. A notification-thread stall between disconnect and
unregister can still exceed the hysteresis window and prevent recovery.

No Bluetooth pairing or radio reconnection is initiated: the bridge must recreate
its ports. Real Bluetooth/USB behavior, polling cost with many instances on a Pi,
and PipeWire-JACK port-ID compatibility still require target verification.

## Checks

Enable the tests in the main build to reuse its resolved RNBO package:

```sh
cmake -S . -B build -DRUNNER_BUILD_TESTS=ON
cmake --build build --target set_dirty_state_test midi_connections_test
ctest --test-dir build -R '^(set_dirty_state|midi_connections|midi_migration)$' --output-on-failure
```

The standalone tests require the same RNBO source package used by the runner:

```sh
cmake -S tests -B build/dirty-tests -DRNBO_DIR=/path/to/rnbo
cmake --build build/dirty-tests
ctest --test-dir build/dirty-tests --output-on-failure
```

The optional live regression harness uses synthetic physical MIDI ports on an
isolated dummy JACK server. Start that server in a separate terminal:

```sh
jackd --no-realtime --name rnbo-midi-test -d dummy
```

Then build and run the harness (the executable and JACK library must have matching
architectures):

```sh
cmake -S tests -B build/dirty-tests -DRNBO_DIR=/path/to/rnbo -DRUNNER_JACK_TESTS=ON
cmake --build build/dirty-tests --target jack_midi_connections_test
JACK_DEFAULT_SERVER=rnbo-midi-test ctest --test-dir build/dirty-tests -R '^jack_midi_connections$' --output-on-failure
```

The unit tests check exact timer boundaries and delayed notification processing
without sleeping, auto-connect unit counting for identical devices, and the
serial-less socket claims (load in either order, hotplug, one controller with
two cables, observed cables, serial/name/legacy identities). The live harness checks actual disconnect/unregister callbacks,
hotplug, short/long gaps, and explicit saves while offline or undecided. It shares
the production load/retry and auto-connect helpers. Cases include audio bridge
loads, renamed/missing instances, legacy and USB-path fallback, twin-device
selection/replug on the same and different sinks, serial-less controllers in
different sockets loaded through the production claims, control recovery after an
offline save and of a hand-made control cable, standalone versus set instance
auto-connect, release-compatible
alias addresses, loading earlier working-tree JSON, and third-party software
port restart. The migration test runs the actual SQL against legacy rows and
checks twin uniqueness and foreign keys; it is enabled when Python is available.
Real USB and Bluetooth hardware still need target-device verification.

For live JACK/OSCQuery verification:

1. Load a set, let initial presets and port notifications finish, and verify clean.
2. Resend identical metadata with reordered keys/whitespace; verify clean.
3. Add/remove/replace a device, change each metadata category, and move a device;
   verify dirty, then restore the original state and verify clean.
4. Add/remove audio and MIDI connections, including physical MIDI aliases; verify
   dirty and revert. Delete an entire source port to exercise disappearance events.
5. Save, then allow delayed notifications to arrive; verify clean. Make an edit and
   allow recovery autosave; verify it remains dirty.
6. Load with a Link peer unavailable, then bring it online; verify restoration does
   not dirty the set. Disconnect a deferred edge or edit slots while restoration
   is pending; verify the edit wins and is dirty.
7. Switch sets while the first set has pending Link edges and fading devices;
   verify the old callbacks/restoration do not modify the incoming set's flag.
