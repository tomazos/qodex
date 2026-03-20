# qodex Architectural Plan

## Goals

- Build a native Qt desktop client for Codex `app-server`.
- Keep the codebase modular from the start.
- Avoid repeating the main Wodex debt pattern:
  one large backend file and one large frontend file that each absorb unrelated responsibilities.
- Preserve a clean separation between:
  transport, domain state, UI orchestration, and transcript rendering.

## Lessons From Wodex

The main things to avoid repeating:

- A single `app.py`-style "god object" that handles process management, protocol transport, state reduction, and UI shaping.
- A single giant UI file that mixes layout, event handling, transcript reconciliation, markdown, and math rendering.
- Implicit identity rules for transcript items.
  In qodex, every persisted or live item should carry an explicit stable key.
- UI code owning business logic.
  Widgets should render state and emit intent, not implement Codex protocol behavior.

## Chosen UI Strategy

Use **Qt Widgets** for the native shell and **QWebEngineView** only for the transcript/document surface.

Why:

- Wodex already proved that markdown + KaTeX rendering is valuable.
- Reimplementing rich markdown + math layout natively in Qt widgets would be more work and lower quality.
- A web view is a good fit for transcript rendering, links, code blocks, images, and math.
- The rest of the app should still be native Qt:
  window, splitters, thread list, toolbar, prompt composer, menus, settings, status.

So the architecture is:

- Native Qt shell around the outside
- Embedded web transcript view inside
- Thin bridge between C++ state and the web transcript

## Top-Level Module Layout

```text
qodex/
  CMakeLists.txt
  PLAN.md
  README.md
  src/
    main.cpp
    app/
    codex/
    domain/
    ui/
    transcript/
    util/
  resources/
    transcript/
  tests/
```

## File and Class Plan

### 1. App bootstrap and composition

These files create the object graph and wire the major services together.

#### `src/app/AppBootstrap.h` / `src/app/AppBootstrap.cpp`

Responsibilities:

- Create long-lived services in the right order
- Load config and paths
- Build the main window
- Own application-wide singletons without making them global

Likely members:

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
- Own selected-thread state
- Translate UI intent into Codex actions
- Translate Codex updates into store mutations and UI refreshes

Examples of responsibilities:

- refresh thread list
- load a thread
- start a new thread
- send prompt
- steer active turn
- handle turn completion/error

This is the main orchestrator, but it must stay narrow.
It should not parse raw JSON-RPC and should not render transcript HTML.

### 2. Config and app paths

#### `src/app/AppPaths.h` / `src/app/AppPaths.cpp`

Responsibilities:

- Resolve standard app directories
- Expose paths for:
  - `~/.qodex`
  - config file
  - logs directory
  - cache directory
  - bundled resources

#### `src/app/AppConfig.h` / `src/app/AppConfig.cpp`

Responsibilities:

- Load and validate qodex config
- Provide typed config access instead of raw JSON lookups

Keep config data in small structs, for example:

- `ServerConfig`
- `WindowConfig`
- `CodexConfig`
- `UiConfig`
- `LoggingConfig`

These can live in `AppConfig.h` as simple value types.

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

#### `src/codex/CodexClient.h` / `src/codex/CodexClient.cpp`

Responsibilities:

- High-level Codex protocol wrapper around `AppServerTransport`
- Expose typed methods for:
  - `initialize()`
  - `listThreads()`
  - `readThread()`
  - `resumeThread()`
  - `startThread()`
  - `startTurn()`
  - `steerTurn()`
- Convert raw JSON into typed Codex/domain objects

Important rule:

- `CodexClient` should know the Codex API, but not UI state.

### 4. Domain state and reducers

This is where the Wodex transcript/state lessons should be captured cleanly.

#### `src/domain/CodexTypes.h`

Contents:

- plain structs for domain objects, for example:
  - `ThreadSummary`
  - `ThreadDetail`
  - `TranscriptItem`
  - `LiveTurnState`
  - `CommandItemData`
  - `StructuredItemData`

Important rule:

- `TranscriptItem` must always have a stable identity field, such as `itemId`.
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

#### `src/domain/TranscriptProjector.h` / `src/domain/TranscriptProjector.cpp`

Responsibilities:

- Convert persisted Codex thread items into transcript items
- Convert command/file/tool/reasoning/plan items into a UI-facing transcript representation

This is the C++ counterpart to the useful Wodex projection layer.

#### `src/domain/LiveTurnReducer.h` / `src/domain/LiveTurnReducer.cpp`

Responsibilities:

- Apply streamed notifications to the store
- Maintain live item state during active turns
- Merge persisted and live transcript state deterministically

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

Likely child widgets:

- `ThreadListPane`
- `TranscriptPane`
- `PromptComposer`
- `StatusBanner`

#### `src/ui/ThreadListPane.h` / `src/ui/ThreadListPane.cpp`

Responsibilities:

- Render the thread/session list
- Expose user intent via signals:
  - thread selected
  - refresh requested
  - new chat requested

#### `src/ui/ThreadListModel.h` / `src/ui/ThreadListModel.cpp`

Responsibilities:

- `QAbstractListModel` over `ThreadSummary`
- Keep list rendering logic out of the pane widget

#### `src/ui/PromptComposer.h` / `src/ui/PromptComposer.cpp`

Responsibilities:

- Prompt text entry
- Enter-to-send / Shift+Enter newline behavior
- busy/disabled state

#### `src/ui/StatusBanner.h` / `src/ui/StatusBanner.cpp`

Responsibilities:

- Render transient status and error text
- Keep status handling out of `MainWindow`

### 6. Transcript web surface

This is where markdown, math, images, and transcript DOM updates live.

#### `src/transcript/TranscriptPane.h` / `src/transcript/TranscriptPane.cpp`

Responsibilities:

- Own `QWebEngineView`
- Load transcript resources
- Connect web-channel bridge
- Expose a narrow API to the native side:
  - `setTranscriptSnapshot(...)`
  - `applyTranscriptDelta(...)`
  - `setTheme(...)`

#### `src/transcript/TranscriptBridge.h` / `src/transcript/TranscriptBridge.cpp`

Responsibilities:

- `QObject` bridge for Qt <-> JavaScript communication
- Send transcript updates to JS
- Receive events back from JS, such as:
  - link clicked
  - local file requested
  - maybe copy/open actions later

#### `src/transcript/LocalSchemeHandler.h` / `src/transcript/LocalSchemeHandler.cpp`

Responsibilities:

- Register and serve a custom `qodex://` URL scheme
- Handle safe local asset and file serving inside the app

This is the preferred answer to the `file://` browser restriction problem.
Do not rely on raw `file://` links.

#### `resources/transcript/index.html`

Responsibilities:

- Minimal transcript document shell

#### `resources/transcript/app.js`

Responsibilities:

- DOM update logic for transcript rendering
- Message patching keyed by stable item id
- No protocol logic

#### `resources/transcript/app.css`

Responsibilities:

- Transcript styling only

#### `resources/transcript/markdown.js`

Responsibilities:

- Reuse the CommonMark-based markdown renderer idea from Wodex
- Keep this as a standalone module, not inline in HTML

#### `resources/transcript/math/`

Contents:

- KaTeX runtime and styles

Important rule:

- No large inline HTML/CSS/JS blobs inside C++ string literals.
- Ship transcript assets as resource files through `.qrc`.

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
2. `AppBootstrap` loads config and creates services.
3. `AppServerTransport` starts `codex app-server`.
4. `CodexClient` performs `initialize`.
5. `SessionController` requests thread list.
6. `ThreadStore` is populated.
7. `ThreadListModel` updates.
8. User selects a thread.
9. `SessionController` requests `thread/read`.
10. `TranscriptProjector` converts thread items into transcript items.
11. `TranscriptPane` pushes a transcript snapshot to the web view.
12. During active turns, streamed notifications go:
    `AppServerTransport -> CodexClient -> LiveTurnReducer -> ThreadStore -> TranscriptPane`

## Testing Plan

Add tests early. Wodex’s bugs were mostly state/reconciliation bugs, which are testable.

### `tests/codex/`

- `AppServerTransportTest`
- `CodexClientTest`

Use fake process output where practical.

### `tests/domain/`

- `TranscriptProjectorTest`
- `LiveTurnReducerTest`
- `ThreadStoreTest`

This is the highest-value test area.

### `tests/ui/`

- lightweight widget construction tests
- no heavy golden-image UI testing initially

### `tests/transcript/`

- keep markdown/math tests close to the web assets
- if we reuse the Wodex CommonMark harness, preserve that as a standalone runner

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

### Phase 1. Foundation

- `AppPaths`
- `AppConfig`
- `TrafficLogger`
- `AppServerTransport`
- `CodexClient`

### Phase 2. Domain model

- `CodexTypes`
- `ThreadStore`
- `TranscriptProjector`
- `LiveTurnReducer`

### Phase 3. Native shell

- `MainWindow`
- `ThreadListPane`
- `ThreadListModel`
- `PromptComposer`
- `StatusBanner`

### Phase 4. Transcript surface

- `TranscriptPane`
- `TranscriptBridge`
- `LocalSchemeHandler`
- transcript web assets in resources

### Phase 5. End-to-end interactions

- thread list loading
- thread reading
- prompt sending
- streaming assistant updates
- steer while active

### Phase 6. Advanced item rendering

- reasoning
- plan
- command execution
- file changes
- tool calls
- images

## Initial Non-Goals

These can wait:

- multi-window session restoration
- plugin system
- full theme editor
- review mode UI
- advanced settings editor

The initial goal is a clean, maintainable single-window desktop client.

## Summary

qodex should be built around four clean layers:

1. transport
2. domain state
3. native shell
4. transcript rendering

If we keep those boundaries, qodex can grow into a serious desktop client without repeating the Wodex "2000-line file" pattern.
