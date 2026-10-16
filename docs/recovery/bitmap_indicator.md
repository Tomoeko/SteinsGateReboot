# Bitmap Indicator logical owner

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses identify the explicitly selected original PE Binary Ninja view. Reference bytes and analysis metadata remain unchanged. Element parsing and placement are documented in [Indicator text](indicator_text.md) and [Indicator placement](indicator_placement.md).

## Font and task coupling

The PSB constructor branch of `0x14026c490` creates its own MPSBFont through `0x1404c0990`, then an MIndicator through `0x14052cd70`. `BitmapIndicator` reconstructs the CPU font/task state with owned validated bitmap-font metadata and owned prepared text. Its caller separately retains the original PSB and physical texture resources; it has no GPU, script class, or borrowed Layer registry hidden inside it.

Print `0x14026ca90` stops at the first NUL before UTF-8 conversion, replaces the font elements and updates measured size. The bitmap provider `0x1404c16c0` measures only the first unit of each prepared cluster through nominal measurement `0x1404c1700`; richer clusters do not change that bitmap lookup rule. A lone surrogate lookup's replacement-key interpretation is documented with its OS boundary in [font recovery](images.md). Printing does not make the view visible or move it.

Uniform/axis scale setters `0x14026dcc0/0x14026dd00/0x14026dd60` update the provider scales and remeasure existing elements. Its maximum dimensions, copied at construction into MPSBFont `+0x20/+0x24`, remain unchanged: actual scale methods `0x1404c1540/0x1404c1550` write only `+0x18/+0x1c`. Spacing setters `0x14026de70/0x14026dec0/0x14026df10` preserve the other axis and remeasure through `0x14052d200`.

Font color `0x14026dc90` changes the font's incoming color without rewriting the existing element snapshots. Tag recognition `0x14026ddf0` changes the parser flag and remeasures the existing elements; it does not parse their original text again. Both settings affect the next print. Opacity, alignment and integer-coordinate drawing setters store their native values independently of measured size. Opacity remains a full 32-bit word, without an invented 0..255 clamp.

Detached placement is inert and reports zero/false values. Detached font/style getters use different native sentinels: scales, maximum dimensions and spacing return 1; color returns `0xffffffff`; tag recognition and integer-coordinate drawing return true; opacity returns 255; alignment returns 0. Mutations return before decoding or validation when detached. Prepared elements are not offered to an authored renderer after detachment.

`IndicatorTextMeasurement` maintains bounded line state on the stack, allowing actual bitmap metrics to stream into measurement without a temporary glyph array. Scale/spacing refresh performs no allocation. Printing allocates owned UTF-16 elements during preparation. Malformed text, nonfinite scales or overflowing measurement fail before replacing visible text/settings; that atomic rejection is an authored safety policy.

## Validation and remaining work

Four independent strict ASan/UBSan fixture groups cover copied font ownership, initial hidden state, axis preservation, constructor-cached maxima, style snapshots versus next print, recognition changes without reparsing, full opacity/alignment values, C-string truncation, atomic invalid-input rejection and all detached sentinels. Incremental text fixtures verify repeatable snapshots and state preservation after rejected appends; all 4,000 prior native measurement words remain exact after the allocation-free change.

One thousand prepared native bitmap owners execute 24,000 mixed print/settings/placement/detachment operations through the real SQIndicator, MIndicator and MPSBFont vtables. The original cached glyph table, nominal lookup/measurement, UTF conversion, text parser, setters, getters and recomputation execute 28,296,920 unchanged instructions. Only private allocation/release boundaries are substituted. All 456,000 defined getter words and all 21,328 active owned-element snapshots agree with the authored sanitizer probe. Boolean returns observe their defined low byte. Native result SHA-256: `64153b3a31c331f951d3356712f49b7bea834793aae0ac5d562903d8c76a895f`. Synthetic inputs, cached glyph fixtures and reports remain ignored under `Files/research/runtime/indicator/`; no original game process, window or audio output runs.

Script Indicator/LayerView classes, PSB/Font root ownership, borrowed view retirement, actual font texture upload/filtering, vector Font providers, draw positioning/opacity/alignment, animations, task scheduling and native pixels remain open. This CPU owner supplies concrete behavior for that integration; it does not publish an imitation native class or make the game playable.
