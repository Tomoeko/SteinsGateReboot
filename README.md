# SteinsGateReboot

First-party C++20 recovery of STEINS;GATE RE:BOOT, targeting macOS on Apple Silicon with Metal and a portable engine core.

Keep the original game installation in `Files/STEINS.GATE.REBOOT/`. Assets, configuration, research and build products stay gitignored under `Files/`.

## Build tools

Requires CMake 3.24+, Ninja and a C++20 compiler. macOS uses Apple's system frameworks.

```sh
cmake -S . -B Files/build/recovery -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build Files/build/recovery --target sgre-inspect
```

## Run

Set the installation path once with the local application:

```sh
Files/build/original-startup-release/original_startup Files/STEINS.GATE.REBOOT
```

Later launches reuse `Files/config.json`:

```sh
Files/build/original-startup-release/original_startup
```

Use `--skip-startup` to enter gameplay directly, `--normal-start` for ordinary startup, or `--headless` for hidden, silent debugging. Startup arguments apply only to that launch.

The default window is centered and resizable, with aspect ratio preserved. Configure `fullscreen`, `window_centered`, `window_width` and `window_height` in `Files/config.json`. Voice, music, effects and movie audio are enabled by default.

## Checks

```sh
python3 tools/build_validation.py Files/build/recovery --validate-and-retire
cmake --build Files/build/recovery --target sgre-check-format
```

Use `-DSGRE_ENABLE_SANITIZERS=ON` when configuring a sanitizer build. Retained native comparison checks require their local fixtures under `Files/`.

See [AGENTS.md](AGENTS.md), [STYLE.md](STYLE.md) and [recovery notes](docs/recovery/) for development guidance.
