# qodex

`qodex` is an early Qt desktop shell for a Codex client.

## Build

Requirements:

- packages listed in `dependencies.txt`
- `codex` on `PATH` if you want the protocol schema / IR / HTML artifacts generated during the build
- run `./setup_python.sh` once to create `.venv` and install the Python generator dependencies

Build and run:

```bash
./setup_python.sh
cmake -S . -B build -G Ninja
cmake --build build
./build/qodex
```

For a headless smoke test:

```bash
QT_QPA_PLATFORM=offscreen ./build/qodex
```

When `codex` is available, the default build also regenerates protocol artifacts into:

```text
build/generated/protocol/
```
