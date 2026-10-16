# Indicator placement and retained views

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses identify the explicitly selected original PE Binary Ninja view. Reference bytes and analysis metadata remain unchanged.

## Recovered behavior

Indicator registration `0x14026cb60` derives from LayerView, whose registration `0x1402320a0` derives from Object. It is a view borrowed by an actual Layer; it is neither a LayerFolder nor a motion Layer. The constructor `0x14026c490` creates a fixed-size task through `0x14052cd70` and starts hidden with zero coordinate and measured size. Coordinates and measured extents are separate values.

The concrete task vtable `0x140653128` stores coordinates through `0x14052d1b0`, exposes measured size through `0x1404c0900`, and adds those values for bounds through `0x1404c0740`. Internal text measurement writes intrinsic size through `0x1404c08c0`. Its ordinary size-request entry is empty function `0x140030f20`: LayerView width, height and size requests leave the measured Indicator extents unchanged. Right and bottom setters reposition by subtracting the corresponding intrinsic extent; they do not resize.

LayerView setters/getters test the detached flag through `0x14022d3d0`. A retained detached view reports false visibility and zero coordinates/extents, and ignores subsequent mutations. Layer retirement `0x14022cb60` marks borrowed views detached after earlier task/resource disposal and clears the borrowed registry. A view destructor `0x14022cac0` unlinks itself while its owner is live. The placement value models the inert state; the script/task ownership registry remains a separate integration step.

`IndicatorPlacement` performs these finite logical-coordinate operations without allocation. Its rejection of nonfinite active values and overflowing bounds is an authored safety policy; arbitrary nonfinite native propagation is not a fidelity claim. Resize requests remain inert, including values that would otherwise be rejected. Detached setters return before conversion/validation of placement values.

## Validation and remaining work

Independent ASan/UBSan fixtures retain native snapshots from a complete mixed sequence and cover initial hidden state, measured-size ownership, fixed resize behavior, right/bottom repositioning, detached zero/false results and atomic finite-input rejection.

One thousand bounded original-object sequences execute 20,000 operations through the real SQIndicator and MIndicator vtables. The unchanged native constructors' prepared private object fields, native LayerView helpers, task methods and virtual dispatch execute 5,441,587 instructions with no substituted function boundaries. All 160,000 result words match the authored sanitizer probe exactly. Trace SHA-256: `ff64b86a5398384a630df16bfaaa55b67a41a2c5ef1805abaf2169ddee3ff480`. Synthetic inputs and receipts remain ignored under `Files/research/runtime/indicator/`; no original game process, window or audio output runs.

The actual Indicator/LayerView script classes, borrowed owner retirement, Font/PSB provider, print-time block measurement, animations, task submission, hinted raster and native pixels remain open. This placement comparison does not implement those services or establish complete text fidelity.
