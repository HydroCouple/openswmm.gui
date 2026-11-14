# Process and document example

Build against installed SDK 1.5 with matching Qt, compiler ABI and architecture:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="<installed-sdk>;<matching-qt>"
cmake --build build --config Release
```

Add the module directory to the GUI extension paths and restart. The **Process
and document example** panel appears in the View menu. Its buttons explicitly
start a short private task and open a model-independent document. Observers only
refresh copied metadata. A second independent module can be configured using
`-DOPENSWMM_ACTIVITY_EXAMPLE_ID=org.openswmm.activity-second`.

`openswmmvis_add_activity_adapter()` compiles the typed QObject adapter into each
consumer with private metaobject symbols. The module links only the SDK and Qt.
It requires neither private GUI headers nor an engine image.

Process UUIDs identify attempts; titles and status row IDs are presentation data.
Jobs are private to their host lifetime; shared native runs belong to the main
window. Cancellation is a request, distinct from its final outcome. The initial
subscribe-before-snapshot watermark remains the observation cutoff; refreshes
query current retained state, not historical deltas or replay.

Actual active-document metadata distinguishes utility documents from cached model
context. Documents, docks and commands use runtime identities rather than saved
names. Dock logical open state differs from exposed tab visibility. UI commands
validate current target ownership and state. Queued downstream Qt slots retain
Qt's normal disconnect behavior and must not throw.

The plugin and status label expose `processEvents`, `uiEvents`, `ownJobTerminals`,
`sharedRunTerminals`, `lastSequence`, `lastJobId`, `activeDocumentId` and
`documentCount` as diagnostic properties for independent consumer acceptance.
