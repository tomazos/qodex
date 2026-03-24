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
- Explicit local IPC between qodex and each thread-ui process

## Current Implemented State

Implemented today:

- Qt application bootstrap and shutdown flow
- persistent multi-window shell state using `KDDockWidgets`
- `codex app-server` transport and generated protocol/client bindings
- SQLite-backed storage, migrations, and API traffic logging
- in-memory thread summary store and Qt models/panes for thread list and API log
- shell actions for refresh, rename, fork, archive, unarchive, and close thread subscriptions
- automated tests covering transport, generated client wiring, storage, models, and single-instance behavior

Removed on purpose:

- the in-process `QWebEngineView` transcript surface
- transcript HTML formatting in qodex
- transcript dock widgets inside the Qt shell
- the shell-level `Resume Thread` action that belonged to the old transcript path

Current gap:

- qodex does not yet provide a per-thread chat/transcript window
- that responsibility moves to the planned Electron-based thread-ui component

## Top-Level Module Layout

```text
qodex/
  CMakeLists.txt
  PLAN.md
  README.md
  src/
    main.cpp
    app/        # implemented Qt-side composition and orchestration
    codex/      # implemented transport and client base
    domain/     # implemented thread summaries/store; more live-thread state planned
    storage/    # implemented persistence and migrations
    ui/         # implemented native shell widgets and models
    threadui/   # planned qodex-side Electron host / IPC layer
  frontend/
    thread-ui/  # planned Electron app source
  cmake/        # planned Electron build/staging helpers
  resources/    # implemented Qt resources
  tests/
  build/generated/protocol/  # generated Codex protocol bindings
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

Future responsibilities:

- hand thread-open / prompt / steer intent to the Electron thread-ui side through a narrow host API

This is the main orchestrator, but it must stay narrow.
It should not parse raw JSON-RPC, render transcript HTML, or directly own Electron child-process details once a dedicated host class exists.

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

#### `src/domain/CodexTypes.h`

Contents:

- plain structs for domain objects, for example:
  - `ThreadSummary`
  - `ThreadDetail`
  - `ThreadUiItem`
  - `LiveTurnState`
  - `CommandItemData`
  - `StructuredItemData`

Important rule:

- `ThreadUiItem` must always have a stable identity field, such as `itemId`.
- It should also have an explicit source/origin enum:
  - persisted
  - live
  - synthetic

That avoids the Wodex bug where "has item id" accidentally implied "live item".

#### `src/domain/ThreadStore.h` / `src/domain/ThreadStore.cpp`

Responsibilities:

- Cache thread summaries
- Cache loaded thread details
- Track selected thread
- Track active turn ids and live state per thread
- Provide snapshot-style access for the UI

This is the main in-memory model of the app.

#### `src/domain/ThreadUiProjector.h` / `src/domain/ThreadUiProjector.cpp`

Responsibilities:

- Convert persisted Codex thread items into a structured snapshot/delta format for the Electron thread-ui
- Convert command/file/tool/reasoning/plan items into a UI-facing representation without producing HTML in qodex

This is the C++ projection layer that replaces the old in-process transcript HTML formatter.

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

#### `src/ui/ProgressSplashScreen.h` / `src/ui/ProgressSplashScreen.cpp`

Responsibilities:

- Show startup/shutdown progress without putting that logic into `MainWindow`

### 6. Electron thread-ui surface

This is where per-thread conversation rendering, prompt entry, and native JS/C++ integration will live.

#### `src/threadui/ThreadUiProcessManager.h` / `src/threadui/ThreadUiProcessManager.cpp`

Responsibilities:

- Launch, supervise, and focus one Electron process/window per active thread
- Know where the staged Electron app lives in the build/install tree
- Pass thread identity and IPC connection details to the Electron side
- Keep process-lifecycle policy out of `SessionController`

#### `src/threadui/ThreadUiIpcServer.h` / `src/threadui/ThreadUiIpcServer.cpp`

Responsibilities:

- Own qodex-side local IPC for thread-ui windows
- Send initial thread snapshots and incremental updates
- Receive user intents back from Electron:
  - prompt send
  - steer/cancel
  - open file/link requests
  - window lifecycle notifications

Important rules:

- qodex remains the source of truth for Codex session state and persistence
- Electron owns DOM, renderer timing, prompt UX, and transcript presentation
- communication must be explicit and versioned; no temp-file HTML handoff
- qodex CMake should build and stage the Electron app and any native addon; normal builds should not depend on `npm start`

#### `frontend/thread-ui/`

Responsibilities:

- Electron app source:
  - `package.json`
  - `main.js`
  - `preload.js`
  - `index.html`
  - renderer JS/CSS assets
- Render one thread conversation window
- Connect to qodex over local IPC
- Keep prompt composition and transcript DOM logic out of the Qt shell

#### `src/threadui_native/`

Responsibilities:

- Optional CMake-built N-API addon for the Electron side
- Link external native libraries cleanly through the main qodex build
- Avoid a separate `node-gyp` build graph once integrated into qodex

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
9. qodex updates shell state and, when requested, opens or focuses that thread’s Electron window.
10. `ThreadUiProcessManager` launches the staged Electron app for the thread and connects local IPC.
11. qodex sends a thread snapshot / delta stream to the Electron thread-ui.
12. During active turns, streamed notifications go:
    `AppServerTransport -> CodexClient -> LiveTurnReducer -> ThreadStore -> ThreadUiProjector -> ThreadUiIpcServer`
13. User intents from the Electron window flow back to qodex over the same IPC channel.

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

### `tests/codex/`

- `AppServerTransportTest`
- generated client binding tests
- future thread-read / turn-action coverage

Use fake process output where practical.

### `tests/domain/`

- `LiveTurnReducerTest`
- `ThreadStoreTest`
- `ThreadUiProjectorTest`

This is the highest-value test area.

### `tests/ui/`

- lightweight widget/model construction tests
- shell interaction tests where signals/selection behavior matter
- no heavy golden-image UI testing initially

### `tests/threadui/`

- IPC protocol tests
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

- `CodexTypes`
- `ThreadStore`
- `ThreadUiProjector`
- `LiveTurnReducer`

### Phase 3. Native shell `[mostly implemented]`

- `MainWindow`
- `ThreadListPane`
- `ThreadListModel`
- `ApiLogPane`
- `ApiLogModel`
- `ProgressSplashScreen`

### Phase 4. Electron build/staging `[next]`

- `cmake/QodexElectron.cmake`
- staged Electron app bundle in the build tree
- CMake-built native addon support

### Phase 5. qodex <-> Electron thread-ui boundary `[next]`

- `ThreadUiProcessManager`
- `ThreadUiIpcServer`
- structured thread snapshot/delta model
- initial thread-open / focus lifecycle

### Phase 6. End-to-end interactions `[later]`

- thread reading
- thread resume/open from the new thread-ui flow
- prompt sending
- streaming assistant updates
- steer while active

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
