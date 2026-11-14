# Copied event values example

This ordinary C++ Qt plugin demonstrates a consumer-local `ValueEventAdapter`
built against the installed SDK 1.9 alone. It observes ISessionEvents lifecycle/save, IViewEvents
model/layer/results and IActivityEvents process/UI metadata. The example adds a diagnostic dock
and a command to show it. No scripting runtime, engine library or new host service
is introduced.

Build from this installed example directory:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="/path/to/gui-sdk;/path/to/Qt/6.9.3/platform"
cmake --build build --config Release
```

`OPENSWMM_VALUE_EXAMPLE_ID` defaults to `org.openswmm.values-example`. Configure a
second independent module with `-DOPENSWMM_VALUE_EXAMPLE_ID=org.openswmm.values-second`.
The helper's metaobject and conversion symbols stay private to each module. Only
SDK and Qt headers are used; the adapter is an example, not a frozen host API.

## Adapter and records

Construct `OpenSWMM::Examples::ValueEventAdapter` in C++ with the borrowed
`ExtensionHost::lifetime()` QObject. Connect `stateOccurred`, `viewOccurred` and
`activityOccurred` before querying `stateSnapshot()`, `viewSnapshot(sessionId)` and
`activitySnapshot()`. All queries use the source GUI thread. `isSubscribed()`
requires all three live managed subscriptions; `disconnect()` stops them. Missing,
stopped, destroyed, invalid-target or wrong-thread sources produce an unavailable
snapshot envelope with empty `data`. Zero sessions on an available source is valid.

Every signal and snapshot has this envelope:

```json
{
  "schemaVersion": 1,
  "schema": "org.openswmm.gui.value-events",
  "domain": "state",
  "type": "snapshot",
  "available": true,
  "data": {"sequence": "18446744073709551615", "sessions": []}
}
```

`domain` is `state`, `view` or `activity`; `type` is `event` or `snapshot`.
`data` preserves every field and nested metadata descriptor from the corresponding
frozen SDK value (`StateEvent`/`StateSnapshot`, `ViewEvent`/`ViewSnapshot`, or
`ActivityEvent`/`ActivitySnapshot`). Source field names and small enum/flag values
are unchanged. The explicit conversions in `ValueEventAdapter.cpp` are the complete
mapping; they do not expose QObject properties or discover arbitrary native fields.

Only primitive maps/lists, strings, booleans, small integers, finite real numbers
and null are produced. **All 64-bit integers** are decimal strings, including
sequences, revisions, generations, feature/selection/period counts and cursors.
Parse them as integers before comparison; lexicographic ordering is incorrect.
This retains unsigned 64-bit precision through QVariant-to-JSON conversion and
languages using floating-point JSON numbers. UUIDs are lowercase strings without
braces, or null for absence. Valid times are ISO UTC strings with milliseconds;
invalid times and nonfinite real inputs are explicit null. A null optional field
is distinct from a missing schema field.

Snapshot arrays contain complete current source metadata: sessions, attachments,
rendering-channel descriptors, retained attempts, documents and scoped commands
and docks. They contain no geometry, feature selections, result data arrays,
reader handles, engine addresses or widgets. The adapter adds no replay/history
store or event backlog. Source retention, coalescing, scopes and filters still
govern delivery. IModelEvents' separate host-local sequence is not mixed into this example.

Keep each initial shared sequence cutoff immutable after subscribing. Later
queries refresh current display state; they must not move an event counter's
cutoff. Events are invalidations or historical transitions, not authority to act
on retired sessions/attempts. Deliberate mutations still use the SDK's validated
controllers. Managed source disconnect/stop and receiver destruction suppress
future host delivery. Ordinary downstream Qt queued calls already posted before
disconnect retain Qt's usual behavior; directly connected slots must not throw.

## Optional diagnostic capture

`SDK_VALUE_CAPTURE_PATH` opts into JSONL output. Supply an **absolute filename in
an existing reviewable verification directory**. `{pluginId}` in the filename
expands to the configured ID, allowing two modules to capture separate records.
The example appends initial state/activity/view snapshots and future accepted
events. The dock's **Refresh copied metadata** button appends fresh snapshots.
No output file or directory is created by default. Invalid requested capture paths
fail initialization explicitly; a write failure closes capture and sets the
plugin's `captureFailed` property.

For independent conformance, adapter and label object names are
`ext.<pluginId>.value-adapter` and `ext.<pluginId>.values-status`. Plugin and label
properties `stateEvents`, `viewEvents`, `activityEvents`, `lastSequence`,
`snapshotWatermark`, `lastSnapshotSequence` and `sessionCount` are decimal strings.
They are diagnostic example properties, not host API or command capabilities.

## Binding feasibility boundary

These primitive records are suitable for ordinary Qt value conversion and JSON
decoding without binding SDK structs. A standalone Python standard-library decoder
can verify the captured representation. That establishes representation evidence
only: it does not establish live Python/Qt signal delivery, wrapper ownership,
interpreter lifecycle, GIL behavior, borrowed engine handles or a single loaded
engine image.

The planned private Python plugin still requires matched Qt/PySide/shiboken builds,
engine E2/E5 and the full binding/runtime acceptance matrix. No Python interpreter,
Python product code, pointer-address gateway or alternate scripting transport is
part of this example.
