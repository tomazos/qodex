# TODO

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
