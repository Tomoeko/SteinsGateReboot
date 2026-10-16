# CF2 contour owner operations

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are executable virtual addresses. The prepared numeric helpers are documented in [hint geometry](cff_hint_geometry.md), [pending contour flush](cff_pending_contour.md), and [mask preparation](cff_charstring_mask.md).

`CffContourOwner` owns copied stem declarations, active mask bytes, the initial/current/first-contour hint maps, pending geometry, logical and device points, and darkening/winding state. Prepared configuration owns its normalized blue zones. Each operation returns at most five typed move/line/cubic callback records in fixed storage. No input span or foreign font pointer survives construction, and ordinary owner operations do not allocate output containers.

## Native order and ownership

Move `0x1403c6830` first closes any active contour through `0x1403c5fa0`, adopts the new first/current logical point, and rebuilds an uninitialized or dirty current map. It copies that current map into the first-contour snapshot. Move alone emits no device callback. Begin `0x1403c68d0` prepares an implicit zero-origin move if the current map is uninitialized, transforms the darkened start, emits callback kind 1, and records its device start. Beginning the following contour also allows the native builder to finalize its preceding contour; close itself does not emit a distinct close callback.

Line `0x1403c6680` captures mask dirtiness before geometry work, suppresses an unchanged logical point only when that captured flag is false, computes darkening/winding, and begins a first segment when necessary. It flushes the preceding pending segment before retaining the new line and rebuilding a dirty map. Current logical position updates last. Closure suppresses dirty-map rebuilding, leaving the dirty mask available for the following move.

Cubic `0x1403c63b0` computes offsets for its first and last tangents. Its middle control edge contributes integer-unit winding unconditionally, including when darkening is disabled. It begins/flushes before retaining all four logical points, then observes current mask dirtiness for rebuilding. The authored fixed arithmetic preserves signed floor, rounding, and wrapping in those native stages.

Close `0x1403c5fa0` sets a temporary closing flag, submits a line to the first logical point, and flushes the last pending segment toward the first darkened tangent. The recovered flush selects the first-contour snapshot for closing line mapping while cubic controls use the current map. It clears the active/pending flags and restores first-segment state. Inactive closure is inert. A counter mask prepares a separate map and may update initial-map/stem caches, preserving the primary mask, current map, and first-contour snapshot.

## Verification and limits

One thousand bounded prepared owners exercised 12,000 original move/line/cubic/close/primary-mask/counter-mask operations in varied order, including implicit beginnings, coincident cubics, moves across active contours, unchanged points with dirty masks, repeated close, and dirty masks during closure. The unchanged CPU functions and original callbacks `0x1403c5040`, `0x1403c4fe0`, and `0x1403c4ea0` ran into the validated preallocated native builder. Observation only copied callback arguments; no allocator, callback, import, game process, or GPU service was substituted or started.

All 1,645,523 callback-coordinate, flag, logical/device point, winding, active-mask, three-map, and stem-cache words agree. Native callbacks comprised 2,667 moves, 8,201 lines, and 2,332 cubics. Compact native output SHA-256: `0d1a363325f12b92c1d5d2c298cd6346bf59c7852e83307071d8939de0019e01`. Focused tests preserve delayed line emission, original/saved map selection, dirty closure, counter isolation, unconditional cubic winding, copied input ownership, and malformed-mask/configuration rejection. Strict conversion/sign-conversion warnings, ASAN/UBSAN, and formatting checks pass.

This is the prepared contour owner, not a whole hinted glyph loader. Native Type2 numeric execution and event-to-owner routing, dynamic declaration timing, font-scale/darkening setup, final builder publication, raster coverage, and resulting screen pixels remain open. Raw lexical hint events alone do not establish their final numeric path. No fresh initialized-game observation was used.
