# CPU glyph atlas packing

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. All addresses are executable virtual addresses.

`PrepareGlyphAtlasRow` exposes the native row/height gate before CPU mapping; it mutates only a required row wrap, leaving successful allocation advancement to `AllocateGlyphAtlasRegion`. This lets the owning texture avoid marking a failed height request dirty.

`glyph_atlas` reproduces existing CPU-backed atlas allocation and clearing. It exposes packing state, a borrowed pixel surface and allocation coordinates/byte offsets. It does not construct a GPU texture, rasterize an outline, manage a glyph cache or submit draw commands.

## Native evidence

Text-owner constructor `0x140035752` writes extent 1024 at owner `+0x418` and zero pen X at `+0x41c`; `0x14003575d` clears pen Y `+0x420` and current row height `+0x424`. Lazy creation `0x140086bb0` uses that extent for both dimensions. Texture initialization `0x1404f87a0` selects DXGI format 87, BGRA8_UNORM; this value is also defined in Microsoft's [DXGI_FORMAT reference](https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format).

Allocator `0x1400369c0` adds two pixels to width and height. If pen X plus padded width is **greater than or equal to** atlas width, it adds the old row height to pen Y and clears pen X/row height. It only maps when pen Y plus padded height is **strictly less than** atlas height. That row wrap persists if the later height or mapping check fails.

For a successful existing CPU texture, `0x1404f8a70` requires enabled byte `+1` and unlocked byte `+2`, returns CPU storage `+0x18`, locks byte `+2`, and marks upload-required byte `+0xc`. Pitch accessor `0x1404f8b30` returns byte pitch `+8`. The allocator uses unchanged memset `0x1405accf0` to clear every row of the full padded rectangle, including its one-pixel transparent border. Content starts at pen `(X+1,Y+1)`; pen X advances by padded width and row height becomes the larger padded height. Native CPU unlock `0x1404f8e90` clears the lock while retaining upload state. Foreign GPU mapping/unmapping behavior is outside this module.

## Safety and verification

The authored API validates dimensions, row state, BGRA pitch and full pixel-storage extent before any successful write. It requires nonempty glyph dimensions and native image extents below 65535 (`0x140427b10`). Native allocation does not recheck an oversized padded width after wrapping; the authored boundary rejects that unsafe request. Invalid buffer storage is an error, while unavailable height, disabled/locked mapping or absent CPU pixels returns no allocation. No buffer allocation occurs inside the helper.

One thousand independent fixtures ran the unchanged native allocation, dimension, CPU-map, pitch and memset helpers with preallocated private storage. They cover border clearing, pitch padding, trailing sentinels, horizontal/vertical equality, failed row wraps and disabled/already-locked mapping. The authored C++ matched all 9,000 numeric offset/state/flag words and all 1,000 whole-buffer SHA-256 values. Combined result SHA-256 is `3b88f081409474142fb68abaa41ae358ea843963093abd6fd6999c2ce159129b`. Buffers were streamed for comparison and never retained as a bulk pixel corpus. Focused tests, strict warnings, ASAN/UBSAN and project formatting pass.

The caller owns the CPU pixel bytes and must keep them alive while using returned coordinates/offsets. Unlocking the portable CPU surface does not upload pixels or authorize a native GPU operation. Glyph raster coverage, fill/stroke colors, cache identity/eviction, atlas upload timing and final shader compositing remain open acceptance checks.
