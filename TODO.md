# TODO

## Markdown Conformance Runner

- Add a first-class CommonMark conformance runner for the ThreadUI message renderer, similar in spirit to `wodex`'s spec runner.
- Keep the current sanitizer and raw-HTML-disabled policy, but make the exact accepted/rejected fixture set explicit.
- Benefit:
  - stronger regression protection for markdown rendering
  - easier to reason about deliberate deviations from the spec

## Optional Raw Protocol Capture

- Consider an optional debug mode that records raw app-server JSON-RPC traffic in addition to the SQLite API log.
- This should be opt-in and meant only for transport/debugging cases where persisted API log rows are not enough.
- Benefit:
  - easier diagnosis of logger/parser bugs
  - easier diagnosis if the app-server process crashes before a payload is fully persisted

## ThreadUI DOM Update Strategy

- Consider a more incremental DOM update strategy in ThreadUI for very large transcripts.
- Preserve stable node identity and update only the affected items instead of always appending/rebuilding larger sections.
- Benefit:
  - better scalability for long threads
  - cleaner foundation for richer live item updates later

## Transcript Virtualization

- Consider transcript virtualization once ThreadUI is rendering enough rich item types that very large threads become expensive.
- This is separate from incremental updates: virtualization is about limiting mounted DOM, not just smarter patching.
- Benefit:
  - better performance on extremely long threads
  - reduced layout/paint cost in Electron

## Local File Link Policy

- Decide whether ThreadUI markdown links to local files should keep opening via the OS directly, or go through a qodex-owned policy layer.
- If needed, add an explicit qodex-side file-link policy instead of letting renderer behavior imply the security model.
- Benefit:
  - clearer control over local file navigation behavior
  - cleaner future integration with editor/file-opening features

## Event / Transport Inspector

- Consider a focused live protocol/event inspector view that complements the existing API Log and API Log Inspector.
- This would not replace the API Log database view; it would be for understanding event ordering and live session behavior.
- Benefit:
  - easier debugging of turn/item lifecycle issues
  - better visibility into live sequencing bugs than static row inspection alone
