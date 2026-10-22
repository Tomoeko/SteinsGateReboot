# Development guide

## Purpose

Recover STEINS;GATE RE:BOOT as readable, maintainable first-party C++ from the supplied executable and game resources in `Files/STEINS.GATE.REBOOT/`. This directory is the reference installation, not the destination for authored source. Preserve the originals. Place the rewrite's source, tools, tests, and documentation at the repository root in focused directories.

Recover the entire supplied game and all character models, animation variants, routes and endings. Individual characters are diagnostic examples. Keep runtime algorithms independent of character names, archive ordinals and fixture identities; validate recovered features across the complete resource inventory.

The user authorizes a deliberate improvement to hair physics: preserve smooth visible continuity across animation and pose changes, even when the original engine snaps. Keep this authored policy separate from recovered simulation, independent of character names, and scoped to physical deformation. Preserve authored body poses, facial animation, camera behavior and game sequence. Native comparisons must state whether the continuity policy is enabled.

The user authorizes a dialogue-layout improvement for blocks exceeding three laid-out lines: preserve glyph proportions and fit the entire block inside its actual message box, including ruby, outlines and shadows. Keep this policy separate from recovered layout, preserve dialogue content, wrapping, reveal and game sequencing, and leave shorter dialogue and unrelated UI under their recovered behavior. Native comparisons must state whether this policy is enabled.

The immediate target is macOS on Apple Silicon with Metal. Keep the engine and resource interpretation portable for future Linux, Ubuntu, Windows, and SteamOS ports. Native behavior, executable analysis, original resources, and retained native captures are the fidelity oracles. Recover behavior; do not invent replacement gameplay, dialogue, menus, assets, timing, or animations to close a checklist.

## Working rules

- At the beginning of each task or resumed task, read applicable `AGENTS.md` or `agents.md` files in the project root and its parent, and the working directory's parent when different. Follow repository instructions silently and report task-relevant outcomes.
- Follow `STYLE.md`, `.editorconfig`, and `.clang-format`. The user permits comments: write concise comments for reasoning, invariants, ownership, provenance, and non-obvious format behavior. Do not narrate obvious statements.
- Use C++20, the standard library, and first-party runtime code. Do not add third-party runtime libraries, vendored engines, package-manager dependencies, copied decoders, or downloaded source implementations. Research tools may assist analysis without becoming runtime dependencies.
- System frameworks and OS services are allowed behind a platform boundary. Keep AppKit, Metal, Objective-C++ interop, and native audio/media services in `src/platform/macos/`. Keep public engine interfaces free of Apple types and handles. Do not assume that an OS service decodes an original format until verified.
- Builds and validation are authorized. Configure, compile, test, run targeted sanitizer checks, and repair failures as needed to complete the requested work. Do not carry over the build-approval rule from the example guide.
- For ordinary debugging, enter the recovered gameplay snapshot directly and skip title navigation and prologue movies. Use the original scene-entry hook and restore semantics; keep normal startup available for its own recovery and acceptance checks. Keep observations hidden and game-only audio silent unless the user requests otherwise.
- The application remembers the installation and launch settings in ignored `Files/config.json`. First-time setup takes one installation path, and later launches reuse it. Missing or malformed configuration must recover through validated defaults or its managed last-good copy; never write setup state into the original installation.
- Startup sequence arguments apply to the current launch. A debug observation must not silently change the next ordinary launch; preserve explicit manual configuration separately. Interactive `--jump` launches show the window immediately and retain normal frames, audio and input at the requested original snapshot; only explicit `--headless` selects hidden, silent research.
- Normal end-user launches enable original voice, music, sound effects and movie audio by default through recovered sound owners. Explicit headless research must never create/start a hardware output or alter other applications' audio.
- On macOS, F1–F12 are game input only while the physical Fn key is held. Enforce this for both function shortcuts and gameplay scan/virtual keys; AppKit's function-key character/modifier flag alone does not establish that Fn is held.
- End-user launches default to a centered, resizable macOS window. Keep fullscreen and window settings in the managed configuration, support minimize and close, preserve the game aspect ratio during resizing, and map input through the same presentation rectangle. Provide an explicit argument for the recovered direct-game entry. Diagnostic runs must explicitly retain hidden, inactive, silent behavior regardless of visible launch settings. Keep their game render target at the shared 1280×720 default independently of the personal visible-launch resolution in ignored configuration.
- Default to action within the current repository. Resolve routine implementation choices from evidence and context. Read external examples as references; do not edit sibling repositories or other external locations without explicit authorization.
- Preserve unrelated work and input artifacts. Do not patch the reference executable, overwrite assets, reset the checkout, or delete the user's files as part of recovery.

## Architecture and source quality

- Separate resource I/O, container decoding, scripts/scenarios, game state, scene evaluation, rendering, audio, input, saves/settings, and platform services. Add modules when their behavior is recovered; avoid empty abstractions and pretend implementations.
- Prefer values, scoped objects, standard containers, and RAII. Use `std::unique_ptr` for genuinely dynamic exclusive ownership. Borrow through references, `std::span`, or `std::string_view` with a documented owner lifetime. Use shared ownership only when required by a real lifetime relationship.
- Use strongly typed enums, named limits, explicit units, initialized state, narrow interfaces, and small cohesive operations. Keep orchestration phases visible. Avoid mutable globals, singleton service access, casts that hide assumptions, and broad inheritance hierarchies.
- Validate file lengths, offsets, counts, arithmetic, recursion, and allocation limits before use. Decode file fields explicitly; never cast serialized bytes to native structs. Parse errors must include useful context and must not become silent fallbacks.
- The current recovery tools report parse/I/O failures through exceptions caught at the CLI boundary. Destructors must not throw. Never let C++ exceptions cross OS callbacks or Objective-C/C ABI boundaries. Revisit a runtime error model when a playable engine exists and record the decision.
- Never author minified source, shaders, tables, scripts, or configuration. Do not check in raw decompiler output as rewritten source. Reconstruct names, ownership, types, and invariants from evidence.

## Rendering and portability

Keep scene traversal, logical coordinates, timing, resource identities, clipping, compositing, and draw order in portable C++. The Metal backend consumes those decisions. Document original Direct3D behavior before translating shader semantics, texture sampling, blend modes, and pixel conventions.

Recover original shaders with a 1:1 fidelity goal while keeping their authored source readable and editable. Preserve original shader bytecode as an ignored comparison artifact; disassemble and reflect it to recover stages, inputs/outputs, constants, resources, instructions, precision, and render-state coupling. Do not claim bit-identical cross-API floating-point output without measurements. Retain unresolved differences in the shader acceptance checks.

Inspect the existing `DXBCSandbox` and `Codex_Testing/tools/D3DCompiler` tools read-only as references. `DXBCSandbox` currently targets Unity 2021.3.35f1; first establish this game's bytecode container, shader profiles, and compiler provenance before reusing its assumptions. A matching executable/PDB pair identifies that tool, not automatically the game's compiler. Verify any required compiler against shader/executable evidence. If another inspection tool is necessary, download it from an authoritative source into one clearly named subfolder under ignored `Files/tools/`, retaining its version, provenance, and hash. Do not modify sibling tool repositories without explicit authorization.

Do not assume pointer width, endianness, unaligned access, Windows path syntax, host character encoding, case-insensitive filesystems, or native struct packing. Preserve resource lookup names exactly. Normalize only where the executable proves normalization is part of the lookup or decoding algorithm. Use UTF-8 at portable interfaces and native conversions at OS boundaries.

Avoid decoding, allocation, shader compilation, blocking I/O, and GPU readback in ordinary frames. Measure startup, scene transitions, dialogue, animated scenes, and video on representative Apple Silicon hardware before making performance claims. Optimize only after output is stable and measured.

## Evidence, validation, and completion

Record the logical target path, SHA-256, file offsets or virtual addresses, source of each recovered behavior, and known uncertainty in `docs/recovery/`. Distinguish direct byte observations, executable-derived conclusions, format references, and inferences. Always explicitly target the correct Binary Ninja view when analyses run concurrently. Do not mutate analysis metadata or patch binary bytes unless requested.

Use bounded synthetic fixtures for parser safety and independent format vectors for algorithms. Keep integration tests against local game files optional and assets ignored. Build/test success proves the authored implementation's behavior; native captures and differential traces are required for fidelity claims.

`ROADMAP.md` is the private root recovery checklist. Keep it gitignored and update it as evidence changes. A milestone is complete only when its stated acceptance checks pass. Keep unsupported formats, unimplemented features, and unverified fidelity open. Never redefine a full rewrite as a scaffold, asset inspector, or title screen to claim 100% completion.

## Files and documentation

Keep private inputs, decoded assets, temporary research scripts, captures, user state, logs, analysis databases, and build products under ignored `Files/`. Keep analysis jobs bounded; stream large archives and avoid duplicate copies of multi-gigabyte assets. Public evidence should contain compact facts, hashes, and addresses, not extracted scripts, dialogue, images, music, movies, user identifiers, or private absolute paths.

The user sets a 10 GB (10,000,000,000 bytes) cap for individual generated files, research jobs, and the combined additional research/tool/capture/build footprint. The original supplied installation is exempt. This supersedes the earlier 256 MiB job, 1 GiB combined, and smaller inspection-file caps. Check storage around substantial jobs, avoid duplicate installations and unnecessary large dumps, and retain useful evidence. Do not create small-cap workarounds or repeatedly compact artifacts when ordinary work fits within the 10 GB cap; prioritize recovering and running the game.

Use `README.md` for actual build/run commands and current implemented behavior. Keep recovery notes short and actionable; prioritize source recovery and validation over repeated documentation. Record platform limitations honestly and keep the next concrete recovery step visible.
