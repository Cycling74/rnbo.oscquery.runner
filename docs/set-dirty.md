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

When a MIDI device's ports disappear, the runner remembers its cables and
restores them when the device returns. This history is separate from the live
graph used for saving and dirty comparison: losing a connected device dirties a
clean set, and restoring its cables returns it to clean. All MIDI cables are
covered, including cables between third-party clients. The runner never
initiates Bluetooth pairing or reconnection; the bridge must recreate its ports.

### Device identity

- Identity is taken, in order of preference, from bridge metadata
  (`http://cycling74.com/ns/rnbo/midi-device-id`), from USB vendor/product plus
  serial (or the topology path when there is no serial) for raw ALSA aliases,
  and from the MIDI alias and direction. Nothing is inferred from a display name.
- Ports without an identity (software clients) are matched by JACK name. RNBO
  instance ports never get a device identity; they follow the instance index.
- A serial-less USB device is looked for in its saved socket first, then by
  alias (ignoring the ALSA card number), so it can move to another socket or Pi.
  Serial and bridge identities require an exact match.
- That fallback never takes a serial-less device that belongs to another cable:
  a waiting cable, a live cable that came from a saved or recovered request, or,
  during a load, any endpoint of the set being loaded. When a device is found in
  another socket, its other waiting cables move there with it.
- With several candidates, a load prefers the saved JACK name, then a candidate
  not already used in that load, then `jack_get_ports` order.
- A USB identity is cached until its port unregisters. When the port of a live
  cable is identified or refined later (a backend naming it after registration,
  a bridge publishing its id), the baseline follows the cable. A set saved under
  the earlier identity is not matched under the new one when loaded later.

### Device loss versus an intentional disconnect

- A disconnect followed by the port unregistering within 250 ms is device loss:
  the cable is kept for recovery. If both ports are still there after 250 ms,
  the disconnect was intentional and is forgotten. Times are taken in the JACK
  callbacks, so a slow event loop does not blur the two.
- Edits through the runner's connection utilities cancel remembered cables at
  once. The window does not delay the live graph or the dirty flag.
- A lost cable is never moved to an identical device that stayed online: ports
  already registered when the endpoint disappeared are excluded. A port that
  registers afterwards is eligible.
- A waiting cable is tied to the one port with its exact identity while that
  port is present, so it follows its own device across replugs.

### Saves, loads and restarts

- A successful explicit save commits the live graph and discards remembered
  cables that are not in it. Autosave and failed saves discard nothing.
- A set load replaces the recovery history. Cables to missing devices enter the
  load baseline and wait: the set is dirty until they are restored. Missing
  application instances do not create waiting cables. Waiting MIDI cables are
  never written to a saved set (unlike deferred Link Audio edges).
- Removing or replacing an instance forgets its remembered cables.
- An audio restart or a patcher reload is not a set change. The graph, including
  waiting cables, is snapshotted and rebuilt; the set name and the baseline stay,
  so the dirty flag returns to what it was once the devices are back. Instances
  come back with their last preset, and control auto-connect starts over.

### Control cables

- Cables to and from `rnbo-control` are only loaded with the first set. A
  control cable kept across a set switch or an offline save, which the current
  set does not contain, is *carried*: it is in the baseline exactly while it is
  live, so it never reads as an edit. This also covers switching the MIDI
  backend (raw or sequencer) and back, where devices change identity.
- A carried cable becomes the set's when a loaded set asks for it or a save
  stores it.
- A carried cable that is disconnected on purpose leaves the baseline too.
- An intentional control disconnect stays disconnected. A control cable made by
  hand or loaded from a set is restored like any other; `ControlAutoConnectMIDI`
  only decides whether the runner creates control cables itself.

### Auto-connect

- Instance auto-connect applies only to patchers started on their own, and only
  until the first explicit save; after that the saved cables are the source of
  truth. Instances loaded in a set never auto-connect.
- A standalone instance connects each port once per registration, so a replugged
  device connects again even after a deliberate disconnect.
- The control client connects each device once and leaves a returning device to
  recovery, so a deliberate disconnect survives a replug. It counts devices per
  identity, so identical devices each connect.
- A port registered before its aliases exist is retried; `system:midi_*` aliases
  are given 50 ms to settle.

### Saved format

- Physical MIDI ports keep the release runner's alias address in the name
  fields, so older runners read the same fields. `*_midi_identity` and
  `*_midi_port` (a JACK-name hint used only to break ties) are optional.
- Migration 24 adds those four columns to `sets_connections` and puts the two
  hints in its unique key, so identical devices that share an alias address
  each keep their own cable. Changing the key means rebuilding the table; row
  ids are kept. Sets without identities still load.
- An exported set in which two cables share an address and differ only in their
  hint cannot be imported by a release runner, whose unique key is the address
  alone: the import fails as a whole.

### Known limits

- Devices the runner cannot tell apart (one shared identity, or a same-model
  serial-less unit in the saved socket): the first one seen is taken to be the
  device. If that was the other unit, the real one is excluded until replugged.
- A serial-less controller in a different socket than saved, whose only cable
  waits on an absent peer, can lose that cable to a same-model unit that is
  present when it is unplugged.
- Two identical devices both away, returning one at a time: the first one back
  can take both sets of cables.
- A notification-thread stall longer than 250 ms between disconnect and
  unregister prevents recovery of that cable.
- Real Bluetooth and USB behaviour, polling cost with many instances on a Pi,
  and PipeWire-JACK compatibility still need verification on target hardware.

## Checks

Enable the tests in the main build to reuse its resolved RNBO package:

```sh
cmake -S . -B build -DRUNNER_BUILD_TESTS=ON
cmake --build build --target set_dirty_state_test midi_connections_test
ctest --test-dir build -R '^(set_dirty_state|midi_connections)$' --output-on-failure
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
alias addresses, and third-party software
port restart. The database migration has no automated test.
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
