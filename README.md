# qodex

`qodex` is an early Qt desktop shell for a Codex client.

## Build

Requirements:

- CMake
- Ninja
- Qt 6 Widgets development packages
- C++ compiler

Build and run:

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/qodex
```

For a headless smoke test:

```bash
QT_QPA_PLATFORM=offscreen ./build/qodex
```
