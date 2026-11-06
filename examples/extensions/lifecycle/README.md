# Session lifecycle extension example

This example builds against the installed OpenSWMM GUI SDK 1.3. It discovers
`ISessionEvents`, displays copied session metadata, and uses the separate
synchronous pre-save hook to collect its own project state. It links only to
the public SDK and Qt; no additional signal adapter is required.

Build with the same Qt major/minor, compiler ABI and architecture as the SDK:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="<installed-sdk>;<matching-qt>"
cmake --build build --config Release
```

Add the resulting module directory to the GUI extension search paths and
restart. The **Session lifecycle example** dock is available in the View menu
and its show command appears in the command palette. For a second independently
configured instance, set `OPENSWMM_LIFECYCLE_EXAMPLE_ID` to another reverse-DNS
identity, such as `org.openswmm.lifecycle-second`.

The initialization order demonstrates the snapshot handshake:

1. Subscribe to future queued state observations.
2. Query `stateSnapshot()` on the same GUI-thread call stack.
3. Retain that snapshot's watermark and discard observations at or below it.
4. On later observations, query a fresh snapshot to display current state.

The initial watermark remains the cutoff for this example's counters. Refreshing
the display does not advance it: an event that follows registration can still be
counted even if a fresh snapshot already represents a later transition. No
historical event delta is applied to the current display.

Session UUIDs identify an open lifetime; model UUIDs persist with project data.
The monitor also displays readiness, load generation, state revision and model
revision. A reopened model has a new session UUID. `resolveSession()` returns
only a current ready lifetime. Before applying deferred results, compare the
copied load generation with a fresh snapshot; this example never retains a model
handle. Ready covers the model and synchronous project/UI restoration; optional
asynchronous map or results resources can still be loading.

Queued observers perform read-only queries and display updates. The separate
short `observeBeforeSave()` collector resolves the request's session immediately,
then stores `lastCollectedSession` in this extension's own `extensionData()`
namespace. It skips recovery operations, does not author model data, and never
starts another save or retains a model handle. Normal project save clears its
usual dirty state after committing; sidecar-only collection can mark project
state dirty according to the existing project-data rules.

For conformance checks, both the plugin QObject and its summary label expose
`queuedEvents`, `readyEvents`, `beforeSaves`, `lastSequence`, `sessionCount`,
`snapshotWatermark` and `lastSnapshotSequence` as dynamic properties. The summary
is named `ext.<plugin-id>.lifecycle-status`, copied session details are named
`ext.<plugin-id>.lifecycle-sessions`, and the explicit refresh button is named
`ext.<plugin-id>.lifecycle-refresh`. Receiver destruction, disconnect and host
shutdown end the managed subscriptions.
