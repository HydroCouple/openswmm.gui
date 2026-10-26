# Typed event extension example

This example builds against the installed OpenSWMM GUI SDK 1.2. It discovers
`IEventSourceV1` through the host's public lifetime object, subscribes to typed
settings events, and compiles a plugin-local `EventSignalAdapter` to expose the
same events as normal Qt signals.

The dock displays separate counters for the callback and signal paths. The
**Change example setting** button issues a public settings command; both
observers update the display. Event handlers do not write settings, avoiding
notification loops. The host scopes settings notifications to this plugin.

Build with the same Qt major/minor and compiler ABI as the SDK:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="<installed-sdk>;<matching-qt>"
cmake --build build --config Release
```

Install the resulting module through the GUI extension search directories and
restart the application. Its dock is available in the View menu; its command
is available in the command palette.

For conformance testing, build this source in two separate directories using
different identities. Each directory produces an independent native module
with its own adapter metaobject and settings scope:

```sh
cmake -S . -B build-first -DCMAKE_PREFIX_PATH="<installed-sdk>;<matching-qt>" \
  -DOPENSWMM_EVENTS_EXAMPLE_ID=org.openswmm.events-first
cmake --build build-first --config Release
cmake -S . -B build-second -DCMAKE_PREFIX_PATH="<installed-sdk>;<matching-qt>" \
  -DOPENSWMM_EVENTS_EXAMPLE_ID=org.openswmm.events-second
cmake --build build-second --config Release
```

The plugin QObject exposes `typedEvents`, `adapterEvents`, `lastSequence`,
`lastKey` and `eventAdapter` dynamic properties for tests. Its dock label mirrors
the counters and has the object name `ext.<plugin-id>.events-status`; the button
is named `ext.<plugin-id>.events-pulse`. Initialization changes
`typedExampleReady` after both subscriptions are installed, so each counter
starts at one. A test can click one instance's button and verify that its own
counters advance while the other instance's counters stay unchanged.

Callbacks execute synchronously on the GUI thread. The adapter is owned by the
plugin and removed during shutdown. Its ordinary Qt signal receivers must not
throw; managed callback exception isolation belongs to the host subscription
boundary. These examples demonstrate the current typed event surface, not
additional model-readiness, revision or operation-lifecycle guarantees.
