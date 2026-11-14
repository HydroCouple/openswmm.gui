# Map and results extension example

Build against the installed SDK 1.4 using the same Qt, compiler ABI and
architecture. This example links to the SDK and Qt without engine or private GUI
headers. `openswmmvis_add_view_adapter()` compiles its typed QObject adapter into
each consumer module, with private symbols.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="<installed-sdk>;<matching-qt>"
cmake --build build --config Release
```

Add the module directory to GUI extension paths and restart. The **Map and results
example** panel appears in the View menu. Its metadata display updates through
typed signals; the visibility button separately issues a validated native command.
Observers do not issue writes. Layer choices use per-attachment UUIDs rather than
possibly duplicated saved layer IDs.

Configure another independent instance with
`-DOPENSWMM_VIEW_EXAMPLE_ID=org.openswmm.views-second` to demonstrate shared state
and independent adapter metaobjects. The panel exposes `viewEvents`, `lastSequence`,
`layerCount`, `resultCount` and `commandsAccepted` as diagnostic QObject properties.

Subscribe before taking the initial snapshot and retain its watermark as the
initial cutoff. Later refreshes query current copied metadata, not historical
deltas. Validate session/load generation and attachment availability again before
deferred actions. A successful 2D seek command accepts a request; displayed frame
notifications arrive only when that frame has been adopted. Source-native units
remain separate from project display units.

The managed adapter subscription ends on disconnect or receiver/host destruction.
Ordinary downstream Qt connections follow Qt's normal rules; queued slots already
posted may still run after disconnect, and slots must not throw.
