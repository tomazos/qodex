# qodex Architectural Plan

## Goals

- Build a native Qt desktop client for Codex `app-server`.
- Keep the codebase modular from the start.
- Avoid repeating the main Wodex debt pattern:
  one large backend file and one large frontend file that each absorb unrelated responsibilities.
- Preserve a clean separation between:
  transport, domain state, native shell orchestration, and thread-ui rendering.

## Lessons From Wodex

The main things to avoid repeating:

- A single `app.py`-style "god object" that handles process management, protocol transport, state reduction, and UI shaping.
- A single giant UI file that mixes layout, event handling, transcript reconciliation, markdown, and math rendering.
- Implicit identity rules for transcript items.
  In qodex, every persisted or live item should carry an explicit stable key.
- UI code owning business logic.
  Widgets should render state and emit intent, not implement Codex protocol behavior.

## Chosen UI Strategy

Use **Qt Widgets** for the native shell and a separate **Electron** app for per-thread chat and transcript windows.

Why:

- qodex should stay focused on Codex session ownership, persistence, logging, and the native desktop shell.
- The per-thread UI wants web technologies, Chromium rendering, and direct JavaScript/native bindings.
- Launching one Electron window per active thread is a cleaner fit than embedding a second browser stack inside the Qt process.
- Removing the in-process transcript surface keeps the Qt app simpler and reduces cross-layer coupling.

So the architecture is:

- Native Qt shell for workspace-level UI:
  thread list, API log, menus, window management, status
- qodex as the source of truth for Codex transport, persistence, and thread metadata
- External Electron thread-ui windows launched and supervised by qodex
- Explicit versioned local IPC between qodex and each thread-ui process
- One loopback TCP connection per thread-ui, initiated by the thread-ui process back to qodex

## Current Implemented State

Implemented today:

- Qt application bootstrap and shutdown flow
- persistent multi-window shell state using `KDDockWidgets`
- `codex app-server` transport and generated protocol/client bindings
- SQLite-backed storage, migrations, and API traffic logging
- in-memory thread summary store and Qt models/panes for thread list and API log
- shell actions for refresh, rename, fork, archive, unarchive, and close thread subscriptions
- `LoadedThread` and per-turn/per-item loaded-thread domain model under `src/domain/threadmodel`
- `Loaded Threads` inspector view showing `Thread -> Turn -> Item -> properties`
- `API Log Inspector` view with full payload inspection
- staged Electron thread-ui app built by CMake, including native addon integration
- qodex-side thread-ui loopback TCP server started during splash/startup
- protobuf-defined qodex <-> thread-ui IPC with custom generated service glue
- thread-ui launch, login handshake, and authenticated per-window connection management
- thread resume into thread-ui with full-history `AddItems`
- thread-ui prompt composer with `SendUserInput` routed to `turn/start` or `turn/steer`
- basic thread-ui rendering of completed items, now including all completed item kinds
- thread-list `Resume Thread` context menu action and double-click-to-resume behavior
- automated tests covering transport, generated client wiring, storage, models, and single-instance behavior
- automated thread-ui IPC tests covering listener startup, login, add-items delivery, and send-user-input routing

Removed on purpose:

- the in-process `QWebEngineView` transcript surface
- transcript HTML formatting in qodex
- transcript dock widgets inside the Qt shell
- the temporary `Thread` menu launcher for ad hoc thread-ui windows

Current gap:

- thread-ui currently renders plain text only; no markdown, KaTeX, or rich item-specific rendering yet
- thread-ui currently receives completed items; in-progress/live streaming is modeled in qodex but not yet projected richly into Electron
- non-message completed items are currently summarized minimally for thread-ui rather than rendered with item-specific presentation
- qodex supervises thread-ui windows, but activation/focus behavior still needs more platform-specific work

## Top-Level Module Layout

```text
qodex/
  CMakeLists.txt
  PLAN.md
  README.md
  ipc/         # planned thread-ui IPC .proto definitions and codegen inputs
  src/
    main.cpp
    app/        # implemented Qt-side composition and orchestration
    codex/      # implemented transport and client base
    domain/     # implemented thread summaries/store and loaded-thread model
    storage/    # implemented persistence and migrations
    ui/         # implemented native shell widgets, models, and inspectors
    threadui/   # implemented qodex-side thread-ui IPC transport layer
    threadui_native/  # implemented Electron-side native addon / IPC client
  frontend/
    thread-ui/  # implemented Electron thread-ui app source
  cmake/        # implemented Electron build/staging and protoc plugin helpers
  resources/    # implemented Qt resources
  tests/
  build/generated/protocol/  # generated Codex protocol bindings
  build/generated/threadui-ipc/  # generated qodex <-> thread-ui IPC bindings
```

## File and Class Plan

### 1. App bootstrap and composition

These files create the object graph and wire the major services together.

#### `src/app/AppBootstrap.h` / `src/app/AppBootstrap.cpp`

Responsibilities:

- Create long-lived services in the right order
- Load config and paths
- Build and restore the native shell windows
- Own application-wide singletons without making them global
- Stage application startup/shutdown cleanly

Current members already include:

- `AppPaths`
- `AppConfig`
- `TrafficLogger`
- `AppServerTransport`
- `CodexClient`
- `ThreadStore`
- `SessionController`
- `MainWindow`

#### `src/app/SessionController.h` / `src/app/SessionController.cpp`

Responsibilities:

- Act as the main application coordinator
- Own shell-level selected-thread state
- Translate UI intent into Codex actions
- Translate Codex updates into store mutations and UI refreshes
- Coordinate thread-ui launch/focus requests once the Electron host layer exists

Current responsibilities:

- refresh thread list
- rename/fork/archive/unarchive/close threads
- react to Codex notifications and keep `ThreadStore` current
- update shell status text
- create/find `LoadedThread` instances for resumed threads
- route turn/item notifications into the loaded-thread model
- route thread-list and thread-ui user intent to the correct loaded thread

Future responsibilities:

- keep app-level orchestration narrow while pushing per-thread behavior into `LoadedThread`

This is the main orchestrator, but it must stay narrow.
It should not parse raw JSON-RPC, render transcript HTML, or directly own Electron child-process details once a dedicated host class exists.

#### `src/app/LoadedThread.h` / `src/app/LoadedThread.cpp`

Responsibilities:

- represent one resumed/loaded Codex thread in memory
- own that thread’s ordered `Turn` / `Item` model
- track the active turn id for that thread
- mutate the model from `turn/*` and `item/*` notifications
- project completed items into thread-ui `AddItems`
- route thread-ui prompt input into `turn/start` / `turn/steer`

This is now the main per-thread runtime object and should keep growing in preference to pushing thread-local state back into `SessionController`.

#### `src/app/ThreadUiProcess.h` / `src/app/ThreadUiProcess.cpp`

Responsibilities:

- represent one launched Electron thread-ui process
- own its `QProcess`, launch token, authentication state, and pending requests
- queue `AddItems` until the child is authenticated
- route user-input requests and responses between IPC and `LoadedThread`
- surface child stderr / fatal-process failure cleanly

`ThreadUiProcessManager` owns these objects, but the per-process state no longer lives in an anonymous record struct.

### 2. Config and app paths

#### `src/app/AppPaths.h` / `src/app/AppPaths.cpp`

Responsibilities:

- Resolve standard app directories
- Expose paths for:
  - app data
  - app state
  - database
  - staged thread-ui/runtime assets later

#### `src/app/AppConfig.h` / `src/app/AppConfig.cpp`

Responsibilities:

- Load and validate qodex config
- Provide typed config access instead of raw JSON lookups
- Keep startup defaults explicit and inspectable

Current config is intentionally small:

- Codex executable path
- Codex startup arguments
- client name/version

Expand this only when Electron staging/runtime discovery actually needs more.

### 3. Codex transport layer

This layer should be completely UI-agnostic.

#### `src/codex/JsonRpcMessage.h`

Contents:

- small transport structs and enums, not QObject classes
- request/response/notification message shapes
- helper aliases for ids and payloads

#### `src/codex/TrafficLogger.h` / `src/codex/TrafficLogger.cpp`

Responsibilities:

- Write raw JSON-RPC traffic logs
- Reuse the Wodex log format concept:
  timestamp, origin, JSON payload

#### `src/codex/AppServerTransport.h` / `src/codex/AppServerTransport.cpp`

Responsibilities:

- Own `QProcess`
- Start `codex app-server`
- Write newline-delimited JSON-RPC to stdin
- Parse newline-delimited JSON-RPC from stdout
- Correlate responses with pending requests
- Emit notifications as Qt signals

Qt shape:

- subclass `QObject`
- signals:
  - `notificationReceived(...)`
  - `transportError(...)`
  - `processExited(...)`
- async request API returning request ids and completing via callbacks/signals

Important rule:

- This class should know JSON-RPC mechanics, but not Codex thread semantics.

#### generated `CodexClient.h` / `CodexClient.cpp` plus `src/codex/CodexClientBase.*`

Responsibilities:

- High-level Codex protocol wrapper around `AppServerTransport`
- Expose typed methods for currently used requests such as:
  - `initialize()`
  - `thread/list`
  - `thread/name/set`
  - `thread/archive`
  - `thread/unarchive`
  - `thread/fork`
  - `thread/unsubscribe`
- Grow to cover:
  - `thread/read`
  - `thread/resume`
  - turn/prompt actions needed by thread-ui
- Convert raw JSON into typed Codex/domain objects

Important rule:

- `CodexClient` should know the Codex API, but not UI state.

### 4. Domain state and reducers

This is where thread and live-turn state should stay deterministic and UI-agnostic.

#### `src/domain/ThreadStore.h` / `src/domain/ThreadStore.cpp`

Responsibilities:

- Cache thread summaries
- Cache loaded thread details
- Track selected thread
- Track active turn ids and live state per thread
- Provide snapshot-style access for the UI

This is the main in-memory model of the app.

#### `src/domain/threadmodel/`

Contents:

- `Turn`
- `AbstractItem`
- `CompletedItem`
- `InprogressItem`
- concrete completed/in-progress item subclasses for every Codex thread item kind

Important rule:

- item identity is explicit and stable via `itemId`
- in-progress items are mutated by live notifications, then replaced by completed items when `item/completed` arrives
- this model is the source of truth for loaded-thread state, not ad hoc UI projections

#### `src/domain/ThreadUiProjector.h` / `src/domain/ThreadUiProjector.cpp`

Responsibilities:

- Convert persisted Codex thread items into a structured snapshot/delta format for the Electron thread-ui
- Convert command/file/tool/reasoning/plan items into a UI-facing representation without producing HTML in qodex

#### `src/domain/LiveTurnReducer.h` / `src/domain/LiveTurnReducer.cpp`

Responsibilities:

- Apply streamed notifications to the store
- Maintain live item state during active turns
- Merge persisted and live thread-ui state deterministically

This should be a pure-ish logic layer:

- input: typed notification
- input: current state
- output: updated state

That makes it unit-testable.

### 5. Native UI shell

#### `src/ui/MainWindow.h` / `src/ui/MainWindow.cpp`

Responsibilities:

- Compose the overall window
- Own top-level widgets only
- No Codex protocol logic
- No transcript rendering

Current child widgets:

- `ThreadListPane`
- `ApiLogPane`
- `LoadedThreadsPane`
- `ApiLogInspectorPane`

Future shell additions, if needed, should stay shell-oriented rather than per-thread conversation UI.

#### `src/ui/ThreadListPane.h` / `src/ui/ThreadListPane.cpp`

Responsibilities:

- Render the thread/session list
- Expose user intent via signals:
  - thread selected
  - refresh requested
  - rename requested
  - fork requested
  - archive/unarchive requested
  - close requested

#### `src/ui/ThreadListModel.h` / `src/ui/ThreadListModel.cpp`

Responsibilities:

- `QAbstractItemModel` over grouped `ThreadSummary` data
- Keep tree/grouping/rendering logic out of the pane widget

#### `src/ui/ApiLogModel.h` / `src/ui/ApiLogModel.cpp`

Responsibilities:

- Present persisted API traffic from the database
- Keep API log shaping out of the pane widget

#### `src/ui/ApiLogPane.h` / `src/ui/ApiLogPane.cpp`

Responsibilities:

- Render the API log table and preserve its view state

#### `src/ui/LoadedThreadsModel.h` / `src/ui/LoadedThreadsModel.cpp`

Responsibilities:

- present the loaded-thread domain model as a tree:
  `Thread -> Turn -> Item -> properties`
- keep introspection and overview logic out of `LoadedThread`

#### `src/ui/LoadedThreadsPane.h` / `src/ui/LoadedThreadsPane.cpp`

Responsibilities:

- host the loaded-thread inspector tree view
- preserve shell-level view behavior while keeping model logic in `LoadedThreadsModel`

#### `src/ui/ApiLogInspectorPane.h` / `src/ui/ApiLogInspectorPane.cpp`

Responsibilities:

- inspect one API log entry in full detail, including the full JSON payload
- act as the detail companion to the API log table view

#### `src/ui/ProgressSplashScreen.h` / `src/ui/ProgressSplashScreen.cpp`

Responsibilities:

- Show startup/shutdown progress without putting that logic into `MainWindow`

### 6. Electron thread-ui surface

This is where per-thread conversation rendering, prompt entry, and native JS/C++ integration will live.

#### `src/app/ThreadUiProcessManager.h` / `src/app/ThreadUiProcessManager.cpp`

Responsibilities:

- Launch, supervise, and focus one Electron process/window per active thread
- Know where the staged Electron app lives in the build/install tree
- Coordinate with the qodex-side IPC listener and per-process `ThreadUiProcess` objects
- Pass thread identity and qodex IPC endpoint details to the Electron side
- Keep process-lifecycle policy out of `SessionController`

Current note:

- the process manager exists and is wired, but focus/activation policy still needs more refinement, especially on Wayland

#### `src/threadui/ThreadUiIpcServer.h` / `src/threadui/ThreadUiIpcServer.cpp`

Responsibilities:

- Own the qodex-side loopback TCP server for thread-ui windows
- Accept one authenticated bidirectional connection per launched thread-ui
- Validate the launch token / instance nonce during handshake
- Send initial thread snapshots and incremental updates
- Receive user intents back from Electron:
  - prompt send
  - steer/cancel
  - open file/link requests
  - window lifecycle notifications

Important transport rules:

- qodex listens; thread-ui connects back
- only one full-duplex socket is used per thread-ui window
- the transport is local-only and bound to loopback
- handshake happens over that socket; thread-ui does not expose its own listener address
- request/response and notification traffic share the same framed transport
- authentication is via a launch token / instance nonce passed on the command line and echoed in the handshake

Important rules:

- qodex remains the source of truth for Codex session state and persistence
- Electron owns DOM, renderer timing, prompt UX, and transcript presentation
- communication must be explicit and versioned; no temp-file HTML handoff
- qodex CMake builds and stages the Electron app and native addon; normal builds do not depend on `npm start`

#### `ipc/`

Responsibilities:

- Define the qodex <-> thread-ui IPC schema in `.proto` files
- Separate common transport/handshake messages from the two RPC surfaces:
  - `UiToQodex`
  - `QodexToUi`
- Treat `.proto service` definitions as qodex-owned IDL, not as a commitment to gRPC
- Generate typed client stubs, dispatchers, and message descriptors via a custom `protoc` plugin

Current implemented surface:

- `UiToQodex.Login`
- `UiToQodex.SendUserInput`
- `QodexToUi.AddItems`

Important rules:

- generated code should target qodex’s chosen transport, not assume gRPC
- the service IDL should be transport-agnostic
- framing, socket I/O, and connection lifecycle stay outside the generated service layer
- protocol versioning starts with the handshake and is explicit

#### `frontend/thread-ui/`

Responsibilities:

- Electron app source:
  - `package.json`
  - `main.js`
  - `index.html`
  - renderer JS/CSS assets
- Render one thread conversation window
- Connect back to qodex over the loopback TCP endpoint passed on the command line
- Keep prompt composition and transcript DOM logic out of the Qt shell

Current implemented UI:

- custom frameless window chrome
- scrollable transcript
- basic completed-item display
- auto-growing composer
- fatal-error dialog then process exit on internal errors

Expected startup flow:

- qodex launches thread-ui with:
  - thread identity
  - qodex IPC host/port
  - launch token / instance nonce
- thread-ui native code connects to qodex
- thread-ui sends the first handshake request over the connected socket
- once qodex accepts the handshake, normal bidirectional RPC begins

#### `src/threadui_native/`

Responsibilities:

- CMake-built N-API addon for the Electron side
- Link external native libraries cleanly through the main qodex build
- Avoid a separate `node-gyp` build graph once integrated into qodex
- Host the thread-ui side of the IPC connection on the native side rather than in renderer JS

Current implemented state:

- standalone `Asio` TCP client on its own thread
- protobuf frame encode/decode
- login handshake
- `AddItems` receive path
- `SendUserInput` request path
- renderer polling bridge for pending items and errors

#### `cmake/QodexElectron.cmake`

Responsibilities:

- Encapsulate Electron dependency staging
- Build the native addon with CMake
- Copy/stage the Electron app into a deterministic runtime location
- Support both build-tree execution and install-tree packaging

The old `QWebEngineView` / transcript-resource plan has been retired.

### 7. Utility classes

#### `src/util/Json.h` / `src/util/Json.cpp`

Responsibilities:

- shared JSON helper functions
- typed extraction helpers with explicit error messages

#### `src/util/ScopeExit.h`

If needed, small header-only helper for cleanup.

#### `src/util/Log.h` / `src/util/Log.cpp`

If needed later, structured app logging separate from raw RPC traffic logging.

## Qt Ownership and Object Rules

- Each `QObject` subclass gets its own `.h/.cpp`.
- Widgets are owned by parent-child relationships.
- Non-Qt service classes should prefer normal value semantics or `std::unique_ptr`.
- Avoid hidden globals.
- Cross-layer communication should use:
  - typed method calls for synchronous domain logic
  - signals/slots for async transport and UI events

## Threading Model

Initial plan:

- Keep UI on the main thread.
- Use `QProcess` asynchronously.
- Do not block the UI thread on request/response waits.

Possible later upgrade:

- If transport/logging ever becomes measurable UI work, move `AppServerTransport` and `TrafficLogger` onto a dedicated `QThread`.

The public interfaces should be designed so this move does not force a rewrite.

## Proposed Initial Runtime Flow

1. `main.cpp` creates `QApplication`.
2. `AppBootstrap` loads config, database-backed state, and creates services.
3. `AppServerTransport` starts `codex app-server`.
4. `CodexClient` performs `initialize`.
5. `SessionController` requests thread list.
6. `ThreadStore` is populated.
7. `ThreadListModel` updates.
8. User selects a thread in the Qt shell.
9. User resumes a thread from the thread list (context menu or double click).
10. `SessionController` creates or reuses a `LoadedThread` and issues `thread/resume`.
11. `ThreadUiProcessManager` allocates a launch token, reuses the already-listening loopback TCP server, and launches the staged Electron app with the endpoint details.
12. thread-ui connects back to qodex over that socket and sends `UiToQodex.Login`.
13. qodex validates the handshake, binds the connection to the launched process, and sends a full-history `QodexToUi.AddItems`.
14. During active turns, Codex notifications mutate the loaded-thread model:
    `AppServerTransport -> CodexClient -> SessionController -> LoadedThread -> domain/threadmodel`
15. Completed items are projected from `LoadedThread` to `ThreadUiProcess -> ThreadUiIpcServer -> Electron`.
16. User prompt input flows back from Electron over the same bidirectional IPC channel and becomes `turn/start` or `turn/steer`.

## Testing Plan

Keep adding tests early. Wodex’s bugs were mostly state/reconciliation bugs, which are testable.

Current automated coverage already includes:

- `AppServerTransportTest`
- `TrafficLoggerTest`
- `CodexClientGeneratedTest`
- `ThreadStoreTest`
- `SingleInstanceManagerTest`
- `MigrationRunnerTest`
- `DatabaseManagerTest`
- `ThreadListModelTest`
- `ApiLogModelTest`
- `ThreadUiIpcServerTest`
- `ThreadUiEngineConnectionTest`

### `tests/codex/`

- `AppServerTransportTest`
- generated client binding tests
- future thread-read / turn-action coverage

Use fake process output where practical.

### `tests/domain/`

- `LiveTurnReducerTest`
- `ThreadStoreTest`
- `ThreadUiProjectorTest`

This is still the highest-value test area, but today some loaded-thread reduction logic lives under `src/app/LoadedThread.*` and deserves additional focused tests too.

### `tests/ui/`

- lightweight widget/model construction tests
- shell interaction tests where signals/selection behavior matter
- no heavy golden-image UI testing initially

### `tests/threadui/`

- IPC transport and handshake tests
- generated service / dispatcher tests from the thread-ui `.proto` definitions
- CommonMark conformance runner with an explicit accepted/rejected example set for the markdown renderer
- Electron staging/addon smoke tests
- optional end-to-end launch smoke tests against the staged thread-ui bundle

## File Size and Responsibility Guardrails

To prevent another "single huge file" outcome:

- target max:
  - `~300` lines for most `.cpp` files
  - `~150` lines for most `.h` files
- if a file approaches `400-500` lines, split it before continuing
- no widget class should also implement protocol transport
- no transport class should also shape transcript presentation
- no inline JS or CSS larger than a few lines in C++

## Suggested Implementation Order

### Phase 1. Foundation `[implemented]`

- `AppPaths`
- `AppConfig`
- `TrafficLogger`
- `AppServerTransport`
- generated `CodexClient`

### Phase 2. Domain model `[partially implemented]`

- `ThreadStore`
- loaded-thread `Turn` / `Item` hierarchy
- `LoadedThread` mutation from resume + live notifications
- `ThreadUiProjector` extracted
- separate reducer extraction still pending

### Phase 3. Native shell `[mostly implemented]`

- `MainWindow`
- `ThreadListPane`
- `ThreadListModel`
- `ApiLogPane`
- `ApiLogModel`
- `LoadedThreadsPane`
- `LoadedThreadsModel`
- `ApiLogInspectorPane`
- `ProgressSplashScreen`

### Phase 4. Electron build/staging `[implemented]`

- `cmake/QodexElectron.cmake`
- staged Electron app bundle in the build tree
- CMake-built native addon support

### Phase 5. qodex <-> Electron thread-ui boundary `[partially implemented]`

- `ThreadUiProcessManager`
- `ThreadUiIpcServer`
- thread-ui `.proto` service definitions and custom `protoc` plugin integration
- loopback TCP handshake and bidirectional request/response plumbing
- initial thread-open / focus lifecycle
- completed-item `AddItems` projection
- richer in-progress/live projection still pending

### Phase 6. End-to-end interactions `[partially implemented]`

- thread resume/open from the new thread-ui flow
- prompt sending
- steer while active
- live incremental renderer updates still pending
- richer error recovery / restart policy still pending

### Phase 7. Advanced item rendering `[later]`

- reasoning
- plan
- command execution
- file changes
- tool calls
- images

## Initial Non-Goals

These can wait:

- embedding transcript rendering back into the Qt process
- plugin system
- full theme editor
- review mode UI
- advanced settings editor

The near-term goal is a clean, maintainable Qt shell plus a separately staged Electron thread-ui.

## Summary

qodex should be built around four clean layers:

1. transport
2. domain state
3. native shell
4. external thread-ui rendering

If we keep those boundaries, qodex can grow into a serious desktop client without repeating the Wodex "2000-line file" pattern.
