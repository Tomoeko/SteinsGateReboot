# Initialized frame render statistics

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are executable virtual addresses in the explicit PE view.

The graphics preloop at `0x1404c4a90` resolves the pipeline through graphics `+0x170`. Instructions `0x1404c4aad..0x1404c4ac5` copy current logical draw count `+0x48c` to previous `+0x490`, copy current submitted-element count `+0x494` to previous `+0x498`, and clear current elements. `0x1404c4b1c` clears current draws. This happens before the pool Begin call at `0x1404c4b29`.

The native draw operation's suffix at `0x140531af8/0x140531afe` increments current draws once and adds the full `r15d` element-count argument. The earlier body may issue multiple device draws for one logical operation; this count is neither GPU primitive count nor number of hardware draw calls. Both DWORDs wrap modulo 2^32. `RenderFrameStatistics` requires the containing renderer's explicitly initialized current and previous counters; it supplies no inferred native constructor defaults.

An unchanged-instruction witness covers 128 initialized counter cases and 512 draw/reset transitions, including zero, large element counts, and wrap at `UINT32_MAX`: 6,656 original dispatches at 26 PE sites. The native oracle executes the preloop prefix only until the pool Begin preparation, and the two-instruction draw suffix only. It does not execute a full native draw or graphics constructor. A direct two-source ASan/UBSan comparison matches all four counter words after every transition; the generated executable is hash-retired.

The remaining pipeline row/cache reset fields, pool/target/model/camera/raster phases, actual draw commit integration, constructor initial statistics, device failure policy, metric display consumption, and full frames remain open. No field in this component represents readiness, a completed scene, or GPU output.
