# Motion player command phase

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses below are executable virtual addresses. Native analysis explicitly targeted `candidate:view:1:UEU`; extended disassembly used `view:1`.

`MotionPlayerCommands` implements the recovered name, request, selection and timing-control phases above the existing live `MotionResourceManager` and `MotionTimelineClock`. It owns bounded UTF-8 byte strings and control values, borrowing the manager and immutable selected registrations. It performs actual character/timeline resolution. No script Motion class, native task, layer evaluator, draw callback or event callback is supplied by this component.

## Direct executable evidence

| Player offset | Recovered state |
| --- | --- |
| `+0x30`, `+0x50`, `+0x70` | Current character, ordinary requested character, deferred stealth character strings |
| `+0x90`, `+0xb0`, `+0xd0` | Current motion, ordinary requested motion, deferred stealth motion strings |
| `+0xf0`, `+0x110` | Latest delayed motion string and complete delayed flags word |
| `+0x140`, `+0x150` | Cached ordinary timeline atom and selected document/root atom; these are distinct |
| `+0x190`, `+0x191`, `+0x193` | Playing, assigned tick/first evaluation, pending initial tag dispatch flags |
| `+0x198`, `+0x19c`, `+0x1a0` | Tick count, bounded evaluation time, speed float32 words |

Character command `0x14051eb70` converts input through native C-string length. Ordinary changes compare the requested character; stealth changes compare the current character. A changed name assigns current, assigns requested only for ordinary mode, clears both current/requested motion strings, and stops playing. Selected project, cached timeline and timing remain intact. Stealth input with an empty current character only replaces the deferred character. Every nondeferred character command subsequently consumes that deferred string through `0x140525b50`, even when the first comparison was unchanged. Thus an ordinary request can remain `a` while current character becomes a deferred stealth override; repeating ordinary `a` preserves that override.

Play command `0x14051ba50` first checks either delay bit. It replaces delayed name/flags without selecting. Next, stealth play with an empty current character replaces the deferred stealth motion. Otherwise it invokes `0x140525880`, then consumes any deferred stealth motion using only `Stealth`, including after unchanged, blocked or failed first selection.

Core play compares ordinary requested motion or stealth current motion. Without `Force` or `AsCan`, the same name does nothing even after Stop or failed lookup. `AsCan` blocks selection while playing unless `Force` is also set. Actual selection uses `0x140516d00` with current character and the previous selected root, preserving the independently recovered preferred-project lookup. Successful ordinary selection assigns current motion and, unless stealth, requested motion; obtains its owning root through `0x1404d01b0`; reads required `type` through `0x1404cae40`/`0x1404ca8a0`; and invokes `0x14051bd50` for ordinary metadata/control preparation. `Chain` preserves both prior tick words; other selections reset them. Failed lookup clears only root `+0x150` and Playing, leaving names, ordinary atom `+0x140` and cached clock fields unchanged. Numeric tick assignment `0x14051f270` is guarded by root `+0x150`, so the authored command owner retains the cached timeline but guards its clock setter separately.

The beginning of `0x14050eb80` executes the delayed command with bits `0x60` removed, then clears the delayed string without clearing the saved flags. Bit `Delay2` exits before the remaining update. An empty delayed string is inactive regardless of flags. `ProcessDelayedPlay` exposes this one real update-entry phase and its skip decision, without advancing time or pretending to evaluate layers. Stop `0x140524970` only clears Playing; speed `0x14051f260` stores the raw float32 value.

Native flag values: Force `1`, Chain `2`, AsCan `4`, Join `8`, Stealth `16`, Delay1 `32`, Delay2 `64`. Other high bits are inert in this command phase and preserved in delayed flags.

## Clone and ownership limits

Fresh assembly at `0x140510c00` copies manager `+8` directly and separately assigns character strings `+0x30/+0x50/+0x70`, requested/deferred/delayed motion strings `+0xb0/+0xd0/+0xf0`, delayed flags, root atoms and clock words. It does **not** assign current motion `+0x90`: instructions at `0x140510cab` instead copy a vector at `+0x318` before continuing with `+0xb0`. A default value copy would therefore misrepresent native player cloning. Command-owner copying is disabled. Full clone constructor reconstruction, listener references, layer/variable snapshots, cross-manager rebinding and source lifetime remain open.

The manager and every cached selected registration must outlive this component, including the cached timeline retained after a failed lookup. Native script PSB rooting and frame-owner retirement are separate recovery work; this bounded component adds no invented retain or task callback.

## Validation and remaining work

The retained native command oracle executes the real character/play/control functions and delayed prefix over 256 independent initial states with 24 sequential operations each. It retains actual PSB type/root accessor execution. Its boundaries observe short native string assignment, the separately proven manager selection, the separately proven ordinary preparation/control phase, bounded C memory/string calls and the security cookie. The remaining native update stops before evaluation. This is a command-phase differential oracle, not a complete MMotionPlayer capture.

6,144 transitions executed 412,700 original/boundary instructions, 857 lookups and 306 ordinary preparations with zero independent expectation differences. Fixture `player-commands-native.bin` is 288,780 bytes, SHA-256 `94773955e5af58019117357c563485e5062921f78ee622741e72859e3805e460`; semantic sequence hash `fc8811018a5cbe533e317c057e77f33a9137d3891ea722f252b42a3af7a5ea07`. The C++ test streams fixed records with bounded counts and exact float32 comparisons. Strict C++20 syntax checks with conversion warnings pass. Portable execution awaits the parent coherent integration; this job linked no core/library and changed no public source or build configuration.

Selecting `Join` fails explicitly because `0x14051c0a0` can evaluate and snapshot outgoing layers/variables when the first-evaluation flag is clear. Division playback fails explicitly because `0x140525880` transfers root-layer angle to/from division offset and recursively prepares the selected child timeline. Neither is replaced by an empty action. Same-name or AsCan-blocked requests still return before reaching these unsupported selection phases, matching native guards. Actual Update/PostUpdate/Draw, pending tag dispatch, label seek through an invalid selected root, full clone and script binding remain unsupported here. The next concrete check is the native update path `0x14050ec60`, its real layer/variable evaluation and queued delivery `0x14051e640` before any task publication.
