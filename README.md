# SteinsGateReboot

First-party C++20 recovery of STEINS;GATE RE:BOOT, targeting macOS on Apple Silicon with Metal and a portable engine core.

Keep the original game installation in `Files/STEINS.GATE.REBOOT/`. Assets, configuration, research and build products stay gitignored under `Files/`.

## Build tools

Requires CMake 3.24+, Ninja and a C++20 compiler. macOS uses Apple's system frameworks.

```sh
cmake -S . -B Files/build/recovery -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build Files/build/recovery --target sgre-inspect
```

## Build game

```sh
cmake --fresh -S . -B Files/build/game-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DSGRE_BUILD_TESTS=OFF -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG -g0" -DCMAKE_OBJCXX_FLAGS_RELEASE="-O2 -DNDEBUG -g0"
cmake --build Files/build/game-release --target sgre-game --parallel
```

## Run

Set the installation path once with the local application:

```sh
Files/build/game-release/sgre-game Files/STEINS.GATE.REBOOT
```

Later launches reuse `Files/config.json`:

```sh
Files/build/game-release/sgre-game
```

Use `--skip-startup` to enter gameplay directly, `--normal-start` for ordinary startup, `--jump SCENE.ks:LINE` to restore a recorded scene checkpoint, or `--headless` for hidden, silent debugging. Startup arguments apply only to that launch.

Jumps restore the most recently recorded complete state, including the phone and route flags. Missing checkpoints are reconstructed in the background without showing startup movies. Checkpoints stay in ignored `Files/checkpoints/`.

Windowed mode is the default, centered and resizable with aspect ratio preserved. Configure `fullscreen`, `window_centered`, `window_width` and `window_height` in `Files/config.json`. Voice, music, effects and movie audio are enabled by default. Press **Esc** for the translucent volume menu; press it again to close. Changed volumes are remembered in `Files/config.json`.

Use `--res 720|1080|1440|2160|4k` (`--resolution` also works) for rendering and matching window dimensions. `--windowed [SIZE]` selects windowed mode with an optional separate window size, for example `--windowed 720 --res 1440`. These overrides do not change `config.json`.

## Find a scene

Search dialogue, characters and scene names, then copy a result's jump command:

```sh
Files/build/recovery/sgre-inspect search "phone" --language en --limit 20
Files/build/recovery/sgre-inspect --gui
```

The macOS inspector browses scenario text, translations and archive entries. Jumps restore the requested original scene snapshot.

Press **Cmd+Shift+I** in the game to search and jump from a draggable, resizable inspector panel. Use its close button to return to the game.

## Checks

```sh
python3 tools/build_validation.py Files/build/recovery --validate-and-retire
cmake --build Files/build/recovery --target sgre-check-format
```

Use `-DSGRE_ENABLE_SANITIZERS=ON` when configuring a sanitizer build. Retained native comparison checks require their local fixtures under `Files/`.

See [AGENTS.md](AGENTS.md), [STYLE.md](STYLE.md) and [recovery notes](docs/recovery/) for development guidance.
