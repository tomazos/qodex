# qodex Internals

This document describes the internal architecture that is implemented today in `qodex`.

For future direction and unfinished work, see [PLAN.md](./PLAN.md).

## Overall Shape

`qodex` is split into five main runtime layers:

1. CLI / process entry layer
2. native Qt shell
3. Codex app-server transport/client layer
4. loaded-thread domain state and persistence
5. external Electron ThreadUI windows

The key architectural rule is:

- qodex owns Codex transport, persistence, thread metadata, and loaded-thread state
- ThreadUI owns transcript rendering, prompt UX, and browser-side presentation

ThreadUI is not embedded in the Qt process. Each loaded thread has its own Electron process and a private loopback TCP connection back into qodex.

## Startup Flow

The entry point is [src/main.cpp](./src/main.cpp).

Startup sequence:

1. [src/main.cpp](./src/main.cpp) inspects raw `argv` before any Qt GUI setup.
2. If the invocation is a CLI command, [CliDispatcher](./src/cli/CliDispatcher.h) constructs `QCoreApplication` and dispatches to a command under `src/cli/`.
3. Otherwise qodex enters GUI mode, creates `QApplication`, resolves app paths with [AppPaths](./src/app/AppPaths.h), and parses GUI command-line options.
4. Enforce single-instance behavior with [SingleInstanceManager](./src/app/SingleInstanceManager.h).
5. Open the SQLite database with [DatabaseManager](./src/storage/DatabaseManager.h).
6. Start the ThreadUI IPC listener with [ThreadUiIpcServer](./src/threadui/ThreadUiIpcServer.h).
7. Construct [AppBootstrap](./src/app/AppBootstrap.h), which composes the rest of the application.
8. `AppBootstrap::start()` delegates to [SessionController](./src/app/SessionController.h) to initialize Codex and request initial models/thread lists.

The ThreadUI TCP server is deliberately up during splash/startup so any launched ThreadUI can connect immediately.

## Object Graph

The application-wide object graph is assembled in [AppBootstrap.cpp](./src/app/AppBootstrap.cpp).

Long-lived core objects:

- [AppConfig](./src/app/AppConfig.h)
- [AppServerTransport](./src/codex/AppServerTransport.h)
- [TrafficLogger](./src/codex/TrafficLogger.h)
- generated `CodexClient` under `build/generated/protocol/...`
- [ThreadStore](./src/domain/ThreadStore.h)
- [ThreadUiProcessManager](./src/app/ThreadUiProcessManager.h)
- [SessionController](./src/app/SessionController.h)
- UI models and top-level windows

The important ownership boundaries are:

- `AppBootstrap` owns application-wide services and windows.
- `SessionController` is the app-level coordinator.
- `ThreadUiProcessManager` owns one [ThreadUiProcess](./src/app/ThreadUiProcess.h) per loaded thread.
- `SessionController` owns one [LoadedThread](./src/app/LoadedThread.h) per loaded Codex thread.
- `LoadedThread` owns the in-memory turn/item model for that one thread.
- [ThreadUiProjector](./src/domain/ThreadUiProjector.h) converts completed domain items into ThreadUI IPC display items.

## Repository Layout

### Root-level files

- [CMakeLists.txt](./CMakeLists.txt): main build graph
- [README.md](./README.md): basic build/run instructions
- [PLAN.md](./PLAN.md): architecture plan and roadmap
- [TODO.md](./TODO.md): smaller deferred ideas
- `VERSION`: package/application version

### `src/app/`

Application composition and orchestration.

- [AppBootstrap](./src/app/AppBootstrap.h): creates the object graph, restores windows, rebuilds menus
- [SessionController](./src/app/SessionController.h): coordinates startup, thread/model refreshes, and routes Codex notifications
- [LoadedThread](./src/app/LoadedThread.h): one loaded Codex thread in memory, including active turn tracking and ThreadUI request routing
- [ThreadUiProcessManager](./src/app/ThreadUiProcessManager.h): owns and supervises ThreadUI subprocesses
- [ThreadUiProcess](./src/app/ThreadUiProcess.h): one Electron child process plus its launch/auth state
- [SingleInstanceManager](./src/app/SingleInstanceManager.h): one-qodex-process-per-database-path behavior
- [AppPaths](./src/app/AppPaths.h), [AppConfig](./src/app/AppConfig.h): runtime paths and config

### `src/cli/`

Command-line entrypoints and dispatch.

- [CliDispatcher](./src/cli/CliDispatcher.h): global CLI option parsing, command matching, help rendering, and dispatch
- [CliCommand](./src/cli/CliCommand.h): abstract command interface
- [HelpCommand](./src/cli/HelpCommand.h): `qodex help`
- [DbQueryCommand](./src/cli/DbQueryCommand.h): readonly SQLite query command for inspecting qodex state while the GUI is running

### `src/debug/`

Optional runtime debug instrumentation.

- [DebugLog](./src/debug/DebugLog.h): global `--debug` plumbing, timestamped stdout log handler, and the `QODEBUG(...)` macro
- Used for live transport/debugging traces, especially raw app-server stdin/stdout/stderr capture that complements the persisted API log

### `src/codex/`

Codex transport/client infrastructure.

- [AppServerTransport](./src/codex/AppServerTransport.h): runs `codex app-server` as a subprocess and speaks newline-delimited JSON-RPC over stdio
- [TrafficLogger](./src/codex/TrafficLogger.h): persists raw API traffic into SQLite
- [JsonRpcMessage.h](./src/codex/JsonRpcMessage.h): transport-level message helpers
- [CodexClientBase](./src/codex/CodexClientBase.h): base for the generated typed client

The generated protocol bindings live under `build/generated/protocol/...` and are produced from the Codex service schema during the build.

### `src/domain/`

Persistent-ish in-memory application state.

- [ThreadStore](./src/domain/ThreadStore.h): thread summary cache used by the shell models
- [ThreadUiProjector](./src/domain/ThreadUiProjector.h): stateless projection from `src/domain/threadmodel` into `QodexToUi.AddItems`
- `threadmodel/`: loaded-thread item hierarchy

`threadmodel/` contains:

- [Turn](./src/domain/threadmodel/Turn.h)
- [AbstractItem](./src/domain/threadmodel/AbstractItem.h)
- [CompletedItem](./src/domain/threadmodel/CompletedItem.h)
- [InprogressItem](./src/domain/threadmodel/InprogressItem.h)
- one completed/in-progress subclass pair for each Codex thread item kind

This model is the authoritative in-memory representation of a loaded thread once `thread/resume` has happened.

### `src/storage/`

SQLite persistence.

- [DatabaseManager](./src/storage/DatabaseManager.h): all database reads/writes used by the app
- [MigrationRunner](./src/storage/MigrationRunner.h): schema migration application
- `db/migrations/`: SQL migrations

Persisted responsibilities include:

- API log rows
- window state and dock layout
- shell view state

### `src/ui/`

Qt shell widgets and models.

- [MainWindow](./src/ui/MainWindow.h): top-level shell window and dock composition
- [ThreadListModel](./src/ui/ThreadListModel.h), [ThreadListPane](./src/ui/ThreadListPane.h)
- [ApiLogModel](./src/ui/ApiLogModel.h), [ApiLogPane](./src/ui/ApiLogPane.h)
- [ApiLogInspectorPane](./src/ui/ApiLogInspectorPane.h)
- [LoadedThreadsModel](./src/ui/LoadedThreadsModel.h), [LoadedThreadsPane](./src/ui/LoadedThreadsPane.h)
- [ModelsModel](./src/ui/ModelsModel.h), [ModelsPane](./src/ui/ModelsPane.h)
- [ProgressSplashScreen](./src/ui/ProgressSplashScreen.h)

The shell is intentionally responsible for workspace-level UI only. Transcript/chat rendering is not in this layer.

### `src/threadui/`

qodex-side ThreadUI IPC transport.

- [ThreadUiIpcServer](./src/threadui/ThreadUiIpcServer.h): loopback TCP listener, login handshake, socket routing by launch token
- [ThreadUiIpcFraming](./src/threadui/ThreadUiIpcFraming.h): varint-length protobuf envelope framing
- [ThreadUiRpcSupport.h](./src/threadui/ThreadUiRpcSupport.h): helper glue used by generated IPC service code

This layer knows about the local qodex <-> ThreadUI protocol, but not browser DOM or Qt shell widgets.

### `src/threadui_native/`

Electron-side native addon.

- [ThreadUiAddon.cpp](./src/threadui_native/ThreadUiAddon.cpp): N-API bridge exposed to JavaScript
- [ThreadUiEngine](./src/threadui_native/ThreadUiEngine.h): native IPC client and pending-item bridge
- [NativeAdd](./src/threadui_native/NativeAdd.h): small test/demo native function kept from the initial scaffold

`ThreadUiEngine` runs a standalone Asio TCP client on its own thread. It logs in to qodex, receives `AddItems`, sends `SendUserInput`, and exposes pending items/errors back to the renderer.

### `frontend/thread-ui/`

Electron application source.

- [main.js](./frontend/thread-ui/main.js): Electron main process, window creation, fatal-error handling, context menus, and execution of qodex-resolved link actions
- [init.js](./frontend/thread-ui/init.js): renderer bootstrap, transcript wiring, composer behavior, native-addon polling, and qodex-backed link interaction plumbing
- [index.html](./frontend/thread-ui/index.html), [styles.css](./frontend/thread-ui/styles.css): UI structure and styling
- [transcript-rendering/TranscriptView.js](./frontend/thread-ui/transcript-rendering/TranscriptView.js): stable-id transcript upsert and virtualization layer that preserves per-item DOM identity while keeping only a bounded window mounted
- [message-rendering/MessageRenderer.js](./frontend/thread-ui/message-rendering/MessageRenderer.js): markdown/KaTeX renderer
- [diff-rendering/FileChangeRenderer.js](./frontend/thread-ui/diff-rendering/FileChangeRenderer.js): file-change diff presentation
- [command-rendering/CommandExecutionRenderer.js](./frontend/thread-ui/command-rendering/CommandExecutionRenderer.js): command execution presentation
- [context-menu/ContextMenu.js](./frontend/thread-ui/context-menu/ContextMenu.js): standard right-click edit menu logic
- [context-menu/LinkContextMenu.js](./frontend/thread-ui/context-menu/LinkContextMenu.js): native link-specific context menu template
- [link-handling/LinkInteractionController.js](./frontend/thread-ui/link-handling/LinkInteractionController.js): shared hover/click/context-menu link behavior driven by qodex link resolution

The Electron app is staged into `build/runtime/thread-ui/app` by CMake and launched from there by qodex.

### `ipc/`

ThreadUI IPC schema.

- [common.proto](./ipc/common.proto)
- [ui_to_qodex.proto](./ipc/ui_to_qodex.proto)
- [qodex_to_ui.proto](./ipc/qodex_to_ui.proto)

These define the qodex-owned local RPC/message schema. They are not gRPC services at runtime; they are IDL inputs to qodex’s custom code generation.

ThreadUI link handling now also goes through this IPC layer: the renderer asks qodex to resolve a link target, then uses the returned descriptor for tooltip text, default click behavior, and the link context menu.

### `scripts/`

Build-time generators.

Important pieces:

- `scripts/protocol_schema/`: extracts Codex schema into qodex-owned intermediate artifacts
- `scripts/service_translation/`: generates the Qt/C++ Codex protocol/client bindings
- `scripts/protobuf_service_codegen/`: custom `protoc` plugin used for ThreadUI IPC service glue

### `third_party/codex/`

Pinned Git submodule of the Codex repository, used as a reference copy matching the current installed client release.

This is not built as part of qodex itself; it exists so qodex can inspect the upstream client/app-server implementation and protocol source in-tree.

## Runtime Data Flow

### Codex thread list / model list

The normal app-server path is:

`SessionController -> CodexClient -> AppServerTransport -> codex app-server`

Responses and notifications then flow back:

`AppServerTransport -> CodexClient -> SessionController -> ThreadStore / LoadedThread / UI models`

The shell views read from `ThreadStore`, `SessionController`, or the database-backed models depending on the pane.

### Loaded thread lifecycle

When a thread is resumed:

1. `SessionController` sends `thread/resume`.
2. `SessionController` creates or reuses a [LoadedThread](./src/app/LoadedThread.h).
3. `LoadedThread::resume(...)` rebuilds its in-memory `Turn` / `Item` model from the returned thread snapshot.
4. `ThreadUiProcessManager` launches or relaunches the Electron ThreadUI for that thread.
5. qodex passes a launch token and loopback endpoint on the command line.
6. ThreadUI connects back and sends `UiToQodex.Login`.
7. qodex authenticates the connection and routes it to the correct `ThreadUiProcess`.
8. qodex sends a full-history `QodexToUi.AddItems`, with stable per-item ids.

After that, live Codex notifications mutate the `LoadedThread` domain model. Completed items are projected by `ThreadUiProjector` into ThreadUI as additional `AddItems`, and the Electron transcript view upserts affected items in place while virtualizing long transcripts so only a bounded item window remains mounted.

### Prompt input from ThreadUI

Input path:

1. Renderer `init.js` calls native `sendUserInput(...)`.
2. [ThreadUiEngine](./src/threadui_native/ThreadUiEngine.h) sends `UiToQodex.SendUserInput` over the authenticated socket.
3. [ThreadUiIpcServer](./src/threadui/ThreadUiIpcServer.h) routes the request by launch token.
4. [ThreadUiProcess](./src/app/ThreadUiProcess.h) forwards it to its owning [LoadedThread](./src/app/LoadedThread.h).
5. `LoadedThread` chooses `turn/start` or `turn/steer`.
6. Responses are translated back into a `SendUserInputResponse`.

The retry-on-steer-race behavior is implemented in `LoadedThread`: if qodex tried `turn/steer` but app-server says there is no active turn, it retries once as `turn/start`.

## Persistence Model

SQLite is the main local source of truth for persisted shell state and logs.

The database stores:

- API log entries
- window layout/state
- shell view state

The database is not the live loaded-thread model. Loaded thread state lives in memory in `LoadedThread` and `src/domain/threadmodel`.

## Build-Time Generation

There are two important generated-code paths:

### Codex protocol generation

qodex generates typed C++/Qt bindings from the Codex service schema into `build/generated/protocol/...`.

These generated files are the basis for:

- `CodexProtocol.*`
- generated `CodexClient.*`

### ThreadUI IPC generation

qodex also generates C++ glue from the local ThreadUI `.proto` service definitions into `build/generated/threadui-ipc/`.

This generation is driven by:

- [scripts/protobuf_service_codegen/plugin.py](./scripts/protobuf_service_codegen/plugin.py)
- Jinja templates in `scripts/protobuf_service_codegen/templates/`

The generated code provides schema-dependent RPC helpers and dispatchers used by both qodex and the ThreadUI native addon.

## Testing Layout

Tests live under [tests/](./tests).

Major areas:

- `tests/codex/`: transport and generated client coverage
- `tests/domain/`: thread store and domain-model coverage
- `tests/storage/`: migrations and database behavior
- `tests/threadui/`: IPC server, native addon, renderer, and smoke-path coverage
- `tests/ui/`: Qt model/view behavior

ThreadUI also has Node-based renderer tests under `frontend/thread-ui/test/...`, plus a standalone CommonMark conformance runner at [run-commonmark-conformance.cjs](./frontend/thread-ui/scripts/run-commonmark-conformance.cjs). Both are wired into CTest.

## Architectural Boundaries That Matter

These boundaries are intentional and already implemented:

- The Qt shell does not render the chat transcript.
- ThreadUI does not talk to `codex app-server` directly.
- `AppServerTransport` knows JSON-RPC transport, not thread semantics.
- `SessionController` coordinates app behavior, but per-thread runtime state lives in `LoadedThread`.
- `LoadedThread` owns loaded-thread domain state, but browser rendering belongs in ThreadUI.
- ThreadUI IPC is explicit, versioned, protobuf-based, and local-only over loopback TCP.

## What This Document Is Not

This is not a roadmap and not a design wishlist.

It describes the structure that exists in the repository now:

- what the major modules are
- who owns what at runtime
- how the implemented data/control flow moves through the system

For future extraction, cleanup, and unimplemented design goals, use [PLAN.md](./PLAN.md) and the GitHub issue tracker.
