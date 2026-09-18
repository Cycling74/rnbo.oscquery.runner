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
- Connections using the same hidden-port filtering and physical MIDI aliases as
  saving. Ordering and duplicate edges are ignored.
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

## Checks

Enable the tests in the main build to reuse its resolved RNBO package:

```sh
cmake -S . -B build -DRUNNER_BUILD_TESTS=ON
cmake --build build --target set_dirty_state_test
ctest --test-dir build -R '^set_dirty_state$' --output-on-failure
```

The standalone tests require the same RNBO source package used by the runner:

```sh
cmake -S tests -B build/dirty-tests -DRNBO_DIR=/path/to/rnbo
cmake --build build/dirty-tests
ctest --test-dir build/dirty-tests --output-on-failure
```

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
