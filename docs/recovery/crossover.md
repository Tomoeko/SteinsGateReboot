# CrossOver observation host

The supplied game runs in the existing macOS graphical session with its windows hidden and game output silenced. This is a compatibility observation host; serverless execution and native Windows parity remain unverified. First-party research helpers live under ignored `Files/research/crossover/`.

## Target and environment

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe` has SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. The installed CrossOver reports 25.1.1, build `25.1.1.38624`, build tag `cxoffice-25.1.1rc2`, and Wine `10.0-8474-g427b76fe09f`. The existing 64-bit Windows 10 bottle enables D3DMetal and already maps the host filesystem through Z:.

[CodeWeavers documents bottle CLI launch](https://www.codeweavers.com/support/docs/crossover-mac/index), [logging](https://support.codeweavers.com/2-creating-a-debug-log), and [D3DMetal](https://support.codeweavers.com/miscellanous/advanced-settings-in-crossover-mac). The installed wrapper confirms that `--no-update` skips bottle upgrades and `--no-gui` suppresses error dialogs; neither makes game windows hidden. The helpers therefore establish and validate their own process-scoped quiet boundary before starting the original game.

## Quiet and storage controls

`quiet_capture.mm` interposes system APIs in the exact owned Wine executable host. Scope requires the first Windows-drive executable argument to match the supplied target and a fresh token; a later game argument does not scope its console launcher. It rejects the Wine wrapper. No original executable, resource, bottle DLL policy, registry setting, or global input state is patched.

The shim suppresses AppKit ordering, key/main-window selection, fullscreen, application activation/policy, display capture/mode changes, and cursor hiding/association/warping. AudioUnit and AudioDevice callbacks call their originals exactly once and then zero output buffers. Input buffers are untouched. Scoped hardware volume/mute/default-output/sample-rate setters are intercepted without applying the requested hardware setting. Scalar counters retain output-byte activity before silencing; no audio samples are exported.

The native synthetic probe independently checks hidden/inactive windows, cursor/display guards, a GenericOutput AudioUnit rendered manually, and a synthetic AudioDevice callback. Two callbacks return exactly 128 zero bytes; their original 0.25-float values contain exactly 64 nonzero bytes before silencing. Another-target probe preserves nonzero output and forwards hardware setters. An invalid-object hardware setter verifies scope without changing a real device. No hardware audio output starts in these synthetic tests. A console-only PE probe validates the exact Wine target boundary. The game launcher requires successful probes and the exact validated dylib hash.

Each original-game observation lasts at most 240 seconds, retains at most 4 MiB of logs, reserves at most 48 MiB of GPU readback, and keeps the research job below 64 MiB. It reserves storage for logs, metadata, and isolated state before capture; the GPU completion writer rechecks the job footprint before saving. It refuses launch above the repository's 1 GiB additional research/build budget. Superseded raw captures are removed only after hashes and PNG comparison artifacts are retained. Every launch checks original bottle configuration and game-save hashes before/after and stops only its exact game PID and own wrapper group. No game PID remained after the recorded trials.

Apple's [dyld interposing declaration](https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/dyld-interposing.h) defines the injection boundary. This research instrumentation adds work and changes framebuffer usage; use it for behavior observation, not unmodified native timing claims.

## GPU capture evidence

The shim makes selected CAMetalLayer textures readable, appends a blit after the original rendering in the original command buffer, preserves its presentation call, and writes the shared buffer after completion. This follows Apple's [drawable readback contract](https://developer.apple.com/documentation/metal/reading-pixel-data-from-a-drawable-texture). Ordinary frames perform no readback. A hidden synthetic 64 × 32 drawable yields exactly 8,192 BGRA bytes with the independently specified repeating values 32, 64, 128, 255.

The actual backbuffers are 2560 × 1440 BGRA8Unorm, pixel format 80, stride 10,240, and 14,745,600 bytes each. A 60.121-second no-input run reached the title and press-any-button prompt. A 240.122-second isolated-input run retained later prologue imagery; all snapshots remained invisible/inactive, 82,620 original audio callbacks returned 338,411,520 silenced bytes, and 50,926,610 original output bytes were nonzero before silencing. The original save/config hashes were unchanged. Its 53,254,032-byte job stayed within reservation.

| Trial / drawable | Observed content | Raw pixel SHA-256 |
| --- | --- | --- |
| No input / 900 | Publisher logo | `5647db3945fe4dd45f535bf8dac69cafa82f7a0e4233c62c4ea1c2769cf83e1e` |
| No input / 1800 | Title and press-any-button prompt | `4a74686b30fa9d7b223eb184088b5bc4755fbb8a1bbbeac3b22f6a09984509e0` |
| No input / 3000 | Title and press-any-button prompt | `2a0c78a953ec1e581555376113af0be2f74decc04eb2e6f8665a9e8ec03904b4` |
| Isolated input / 13500 | Prologue video | `7dc904e601e644482e2139c9406a11086eabf895b5d1922095c50b853a4ba545` |
| Isolated input / 14500 | Prologue video with original overlay | `54ec7eca1d73bff0e4dcf448c9ec9ce5791f93fc7443f5a7bba2dc82c0fe5e83` |

These establish rendered startup/video progression. A subsequent 240.120-second run captured the first character fade-in at drawable 14700 and the first speaking character/message box at 14850. Raw SHA-256 values are `e852803af27db80f5792cd00532f8d1368126f1791553d8d87f21b164fc63831` and `5329516703651a601113c3edccb567562027f5270d9da2469c1b3216200efb5a`; the same report retains successful WMA 19331-byte/13-packet submission. Original saves/config remained unchanged, no game PID remained, and the job used 54,593,311 bytes before raw-to-PNG pruning. Route transitions, composition parity, and native Windows pixel comparisons remain open. Captures and detailed metadata remain ignored; public notes do not reproduce their dialogue or images.

## Process-local state and input

The console host creates the exact game suspended, injects its first-party `state_isolation.dll`, requires readiness, and then resumes it. Process-local import hooks redirect Documents/Roaming/Local folder queries into ignored `user_state/`. Unknown folders are forwarded. Bootstrap `0x1400484f2` chooses the game's Documents path; expansion `0x1404dd770` resolves `SHGetKnownFolderPath` at `0x1404deadc`, with `SHGetSpecialFolderPathW` fallback at `0x1404de9f2`. Independent fixtures verify both redirected APIs, an untouched Fonts query, local sentinel writes, and negative target forwarding.

The original keyboard sampler `0x140048ef0` compares its HWND with `GetFocus()` before `GetKeyboardState()`. Posted messages alone did not progress the hidden title. The research DLL supplies only its own largest HWND to process-local focus queries and only scheduled Return/X/Left states to process-local keyboard queries. All other key states are zero. Process-local GetCursorPos returns an out-of-client sentinel to prevent the user's real mouse position from changing hidden menu focus; the actual cursor is untouched. It does not change actual foreground focus or use SendInput, AttachThreadInput, physical mouse events, or global keyboard state. The host separately posts paired messages only to a reverified HWND belonging to its child PID. Input requires an observed Documents redirection. The hidden fixture verifies two Return edges, one X edge, one Left edge, four down/up message pairs, the out-of-client cursor sentinel, and no other pressed keys. Microsoft's [GetKeyboardState contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getkeyboardstate) explains why posting messages does not itself update keyboard state.

## Startup and movie-control facts

The reported missing `DialogWindow` in `tips.nut`, source line 1533, is nonfatal for startup/title/prologue. A scan of all 72 compiled scripts finds its class definition only in `debug.nut`, root instructions 0–21. `init.nut` root 1222–1233 loads that module only on the debug-build branch, while `override.nut` root 51–53 still invokes tips. Tips instructions 241–243 attempt to derive `TipsUnlockWindow` from the absent base. This is a supplied release-script debug-helper dependency, not evidence of a missing native registration.

Script body `wind3d11data/script_body.bin` has SHA-256 `29b44eeff7d16f396a3d5aa1e1facf0a8f21f93b7bb9d703be657154ddb3db4d`.

| Script key | Body offset | Packed / decoded bytes | Decoded bytecode SHA-256 |
| --- | ---: | --- | --- |
| `application` | 15576 | 14077 / 63198 | `e52833eedee5a33771b31814a1c55c9b63f15bab1086557e754bc5f50fc4c1d5` |
| `debug` | 104333 | 6813 / 28131 | `42d65754ae616ef63da83893614d700d5a95ce21806cc708f86ff4ba37d467c3` |
| `override` | 378074 | 46411 / 236666 | `5d214f0b2a596a7b4388f65cc89443f89fd1f843692543a7f4c078f6219d461e` |
| `tips` | 558397 | 13687 / 56745 | `b027a6565be1dad5542531fca606e6c866be6d435476011ae4d28421697c6bfc` |
| `titlemenu` | 572255 | 9695 / 41356 | `df365e318b18be38d210ee9104370c912e73fbdd1d2a2448bdf00b7a47f19c96` |

Override `getStartScene` child instructions 0–3 return a table with storage `reSG00_01.ks`. Application `main` child 10–17 guards consumption of parsed `-s`/`-l` arguments behind the debug-build condition, so parsing those arguments does not establish release support. Title `pressedNewGame` child calls `waitMovieStop("start")`. Override `tag_waitmovie` child reads `canskip` with default false, waits 3000 ticks before polling action `cancel`, opens its original `YESNO_MOVIESKIP` confirmation, and exits only on an affirmative response. Repeated Return does not request this skip. Preserve these conditions in recovery.

## WMA acceptance versus decoding

Native sound initialization `0x1403918f0` explicitly loads `xaudio2_8.dll` and resolves `XAudio2Create`. Movie construction `0x14038d960` separately loads that module. The CrossOver trace confirms builtin XAudio2_8 with FAudio `241000`; builtin MFPlat/MFReadWrite also load. Native source loader `0x1403921a0` recognizes PCM, Microsoft ADPCM, WMA Standard, and WMA Professional, passes original format bytes to source creation, and supplies WMA `dpds` to submission.

A 240.143-second isolated original-game run observed successful source creation and submission for ADPCM UI sound, looping PCM, and a WMA voice request. The WMA request was tag `0x0161`, mono, 44,100 Hz, 4,000 bytes/second, block alignment 1487, 16 bits, and no codec extension. Source creation returned HRESULT 0; submission of 19,331 bytes with 13 cumulative packet entries and end-of-stream flag 64 returned HRESULT 0. This proves acceptance, not audible decoding.

A separate console-only capability/playback helper uses a bounded original RIFF xWMA sample (SHA-256 `e92440fae05d46a617cedc774ca1c9bbc2f8d00dfb6884be7eabbfdd5057c24e`). Its exact 18-byte fmt has SHA-256 `bad0a9e4c3f28730b9e3b39ac8f2b96dd465780af241aeb766e9911674ea7330`; data is 17,844 bytes, dpds has 12 entries, and the final cumulative size represents 153,600 decoded mono samples. The exact helper's console-only target scope passes before any audio API is exercised. Engine/master/source creation, buffer submission, and Start all return HRESULT 0. Buffer-start/end and stream-end callbacks each occur once; no voice-error callback occurs.

Despite those successful calls, all 711 observed original AudioUnit callbacks remain byte-zero before silencing. An independent one-second PCM control through the same DLL, mastering voice, source interface, callback observer, and default-device route produces 68,306 nonzero bytes before silencing. Both formats finish with zero queued buffers and reset SamplesPlayed to zero; that final field alone is not a failure signal. The paired observation isolates silent WMA processing from general output-routing silence. It does not identify the compatibility implementation's internal decoder failure or establish native Windows behavior.

Microsoft's [xWMA buffer documentation](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/ns-xaudio2-xaudio2_buffer_wma) defines dpds/lifetime semantics and states that Windows 8.x XAudio2_8 lacks xWMA decoding, while the SDK 2.7 and Windows 10 2.9 versions support it. CrossOver's successful acceptance still requires the measured output distinction above. Native Windows voice captures and the rewrite's first-party WMA decoding remain required. No compatibility DLL substitution or bottle configuration change has been made.

## Reuse and next check

Build `quiet_capture.mm` and `quiet_probe.mm` with the Apple SDK and `-arch x86_64`, C++20, ARC, warnings-as-errors, and AppKit/AudioToolbox/CoreAudio/CoreGraphics/Metal/QuartzCore frameworks as needed. Build the Windows host/DLL/probes with the installed MinGW compiler and static tool runtime; these ignored research binaries are not rewrite dependencies. Strip their debug symbols to preserve the job budget.

```sh
python3 Files/research/crossover/validate_quiet.py
python3 Files/research/crossover/validate_isolation.py
python3 Files/research/crossover/quiet_launch.py --isolated --input
python3 Files/research/crossover/wma_probe.py
```

Read the corresponding validation/capture JSON reports before reuse; any helper edit requires revalidation. The historical graphical launcher remains disabled. An attempted Escape sequence reached the original keyboard-help overlay; its captured bindings identify Escape as Exit and X/backspace as cancel. The validated X cancel input plan with an out-of-client virtual cursor reaches original character dialogue. Retained first-dialogue frames and source metadata support the next renderer/audio recovery checks; full route and native Windows fidelity remain open. A separately gated initialized-CRT formatter probe produced 414 exact native vectors in 34,590 bytes, with module/function-signature/thread-state readiness checks and negative other-module validation; its report SHA-256 is `9ba6fee1bccaca6cb67c71f21d6a59bc2b5ef58fe8891e9c09794a5f6b81d105`.

A separately gated 20.085-second no-input initialized-CRT math probe returns 362 records in 21,944 bytes, SHA-256 `9297a0e9ee79171ab1fba62d552a69bf3dd2d575afe1c4ade28f026b195705ab`: 201 valid-domain unary float results from original double CRT helpers, 32 atan2 pairs, 20 power pairs, 128 RNG outputs and MXCSR metadata. The hook preserves the original thread’s errno, x87 environment, MXCSR, RNG seed and Win32 last-error; signed zero/zero atan2 domain cases are excluded. Negative other-module readiness, quiet scope, unchanged original saves/configuration and PID cleanup pass. Comparison belongs to VM math recovery; this capture does not imply arbitrary-domain math parity.

## Private text/GC/pointer differential capture

A fresh validated 20.098-second run collected twelve private TextRender line cases, five private newly allocated Squirrel VM weak/cycle/root cases, and eleven guarded initialized-CRT pointer-format cases. The original game VM and scene objects are not borrowed; text objects use their exact native constructor/destructor with caller-owned storage, and all private GC roots are released before their separate VM closes. API signatures and initialized CRT/FLS state are checked; errno, native thread RNG, x87 environment, MXCSR and Win32 last-error are restored. New guards reject a synthetic other module before creating any report.

Capture SHA-256 values: text `5b04e44c51b193ed26454ed2bb37f9578f58b76197dd2d1978ba91e7c5356e84`, GC `b392cd0fd962524d43d8e1d92a88a83a46a544f5d6846682f700783f3ba319c4`, pointer `f513aa426cfc1e301505a0df9a65937a28f48b9b19bf5c562c1290893019eab1`. Native `%p` returns sixteen uppercase hexadecimal digits without a prefix; the verified typed template supplies `0x`. Both unrooted self/pair cycles return collector count one; rooted cases return zero before root release and one afterward. Preserve that observed return value rather than assuming it counts every object in a cycle.

The exact scoped shim records 1,070 hidden drawable presentations and 3,188 silenced callbacks/13,058,048 zeroed bytes, always visible=0/active=0/nonzero output bytes=0. No input is sent, original save/configuration hashes remain unchanged, no global audio settings change, and the scoped PID is stopped. No positive array-sort probe runs. Updated isolation DLL SHA-256 is `8b867bb2d000da11066673ae061c26ff8c8b4ad4fc58d0aed02f1d60d067a2d1`; the same validated quiet dylib and isolated host remain in use. Full numeric records and compact process controls stay ignored under `Files/research/crossover/`.

## Prepared private VectorFont measurement

The updated isolation DLL (`cf7e06f8b3294d81d3e2aa1345c449024a45d8d1b1e74197a9ecd3a120a8adca`) adds a separately gated private VectorFont observation; a coordinated positive font measurement has now completed. Its exact header hash is `e1ccf3bfc8972d2713b2c1a43a69ad705c21236b1a33f6c9754aa180ee74bfa8`. The coordinator verifies the retained 16,522,392-byte font (`44af35f7ecf9947a02ae24eb6c254bdb9f1b91c8fa7c801d701ff8a5a6bbeb3c`) before enabling it. After the original PeekMessageA call, the first game message-loop thread checks readiness at most 80 times, spaced 250 ms apart, within 20 seconds. It requires the exact executable/API signatures, initialized native CRT thread state, existing shared HB FT callbacks, and already interned canonical `en`; it never initializes those shared caches. A rejected readiness condition leaves the positive probe unopened.

Three sequential private native font owners cover modes 0/3/4, twelve UTF-16 units, and sizes 12/24/48. Native destructors release their own FT/HB allocations while caller font bytes remain alive. The probe touches no game font/scene object or atlas/GPU callback. It records unhinted shaping metrics and at most sixteen glyph records per case, with 108 expected records and a 256 KiB report cap. Every fallible probe catches C++ exceptions inside the C callback; an independent thrown-exception fixture continues after that catch. Scoped PeekMessage forwarding preserves the original message result and Win32 last-error. All six wrong-module probe controls, hidden input/folder isolation, source metadata forwarding, unmatched-process preservation and helper PID cleanup pass. The quiet dylib additionally re-passes synthetic hidden/fullscreen/audio/Metal and unmatched-process checks.

The prepared no-input, no-readback launch is `python3 Files/research/crossover/quiet_launch.py --isolated --native-vector-font-probe --no-frame-capture --duration 20`. Its normal exact-target/hash/token gates, owned state redirects, game-only audio zeroing, window/activation/cursor suppression, original save/configuration hash comparison, storage caps and owned PID cleanup remain required. Readiness and preparation receipts remain ignored in `Files/research/crossover/`; the completed observation is recorded below.

The exact prepared command completed in 20.100 seconds. All 108 native font records were captured in 7,754 bytes, SHA-256 `a653391c124044bd34d3d3e9872b952ee8964b6a4af3c8a9ef460d7632e2df18`. The final quiet snapshot observes 1,057 hidden presentations, 3,166 silenced callbacks and 12,967,936 zeroed bytes, with visible=0, active=0 and nonzero output=0 throughout. No input or GPU readback occurs; original saves/configuration remain hash-identical and no scoped PID remains. The crossover job is 25,630,822 bytes. The ignored private observation and numeric report are retained for the font subsystem's differential comparison. This is shaping evidence, not atlas/raster/full-scene parity.

## Private CFF outline load capture

A separate size/load-only font probe now records 30 successful flag-8 outline cases from one private mode-zero VectorFont owner, at sizes 12/24/48. It performs no raster, atlas or GPU call. Exact input/font/native signatures, initialized-thread state, existing HB callbacks and canonical `en` are required; source bytes outlive native face/size/slot/library teardown. A shared report budget caps all successful and diagnostic records at 65,536 words/one MiB. Other-module, private-link/driver-adapter and four output-budget rejection fixtures pass before original-game use; foreign callbacks catch C++ exceptions.

The initial guarded attempt rejected an incorrect inner-function comparison, produced an empty report and preserved all quiet/state guarantees. Static data at original `0x14060d4f8` identifies the driver load adapter `0x1403c36d0`, which validates owner links before tail-calling inner `0x1403cc3f0`. Its exact signature is now guarded; no pointer validation is bypassed. The protected prior VectorFont shaping header remains unchanged.

The corrected coordinated command uses isolated state, no input/readback and a 20-second bound. It completes in 20.098 seconds with 30 records/12,184 bytes, SHA-256 `a1ea1fc4507a88cc37888c7d663a6c503438cb8a8a7afe5a3b33a0c52f3ef8f7`. The record layout and caps validate 3,006 words, 663 points and 57 contours; all private driver no-stem-darkening bytes are 1, with hinting/scaling enabled. The first-party full glyph differential subsequently matches all 3,006 words exactly. This is a bounded loaded-outline oracle, not atlas or whole-scene pixel parity.

All snapshots remain visible=0/active=0/nonzero output=0; 3,190 game callbacks return 13,066,240 zeroed bytes. Original saves and bottle configuration are hash-identical, no global audio settings/input/cursor changes occur, and no owned game PID remains. The final capture job is 41,039,858 bytes. Rejected/successful receipts and outline records remain ignored under `Files/research/crossover/`; original resources and analysis metadata are untouched.
