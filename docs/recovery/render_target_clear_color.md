# Original target clear-color conversion

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`, read-only Binary Ninja view `candidate:view:1:UEU`. This is additive to the immutable render-target layout stage; its viewport/projection behavior is unchanged.

Virtual-screen Draw `0x1404fa250` passes the four stored background words at +58..64 to color clear `0x1404c31b0` with color enabled and depth/stencil disabled. Clear zero-extends each 32-bit word, converts that unsigned value to binary32, then multiplies by the exact binary32 constant `0x3b808081` at `0x14064afc4`. It forwards the resulting four words to the actual target view through device slot+190. The CPU conversion does not clamp to0..1 and does not premultiply by alpha. Negative signed background fields therefore preserve their raw unsigned bit patterns. Division by255 is not the recovered operation.

`PrepareRenderTargetClearColor` consumes the actual signed fields retained by Screen state and preserves that conversion. The bounded witness executes original clear and security-check instructions and observes arguments at the Direct3D clear boundary. All512 cases match bit for bit, including0,255,256, signed boundaries and deterministic full-word samples. Direct first-party ASan/UBSan also rechecks the1920 original viewport/projection cases unchanged. Clear fixture SHA-256 `ff794dc6c4b99878a3335903139ddc0e590fd891797877c2ad189620fc28b8c3`,16392 bytes.

Actual device clear/clamping behavior, depth/stencil clear, target binding, shared graphics context restoration and complete virtual-screen Draw remain open. This helper prepares concrete clear arguments; it supplies no target owner or placeholder drawing service.
