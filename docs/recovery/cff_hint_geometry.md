# CFF hinted point geometry

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are executable virtual addresses.

`cff_hint_geometry` reconstructs prepared hinted-point transformation and segment darkening offsets. These helpers consume glyph-owned numeric state; they do not execute charstrings, choose a pending segment's map snapshot or claim final raster pixels.

## Point transform

Native `0x1403c65c0` separately multiplies logical X by path+0x48c8 and logical Y by path+0x48cc, then adds those horizontal contributions. It evaluates logical Y through the supplied map (`0x1403c7950`), updating its cursor. Four fields at font+0x40/+0x44/+0x48/+0x4c transform this horizontal/mapped-vertical pair into output XY, followed by path+0x48d4/+0x48d8 translation. Authored fields state those directions explicitly. Each multiply retains its own rounding; collapsing this into one precomputed host-floating matrix changes output.

Native fixed multiply `0x14039cda0` performs a signed 64 product, subtracts its logical sign bit, adds 0x8000 and shifts arithmetically 16. This rounds signed halves away from zero. The caller retains low 32 result bits and adds in wrapping 32 storage. Authored unsigned bit operations preserve those defined native results without C++ signed overflow. Existing map validation still rejects malformed maps and unsupported coordinate extents. Callback conversion from output 16.16 to outline units remains separate.

One thousand unchanged prepared owners each transformed eight sequential points: all 24,000 XY/cursor values match. Fixtures include enabled/disabled/empty maps, forward/backward lookup, ordinary matrices and full signed 32 transform fields. Native output SHA-256 `c3eea994f55cfcadad53eae2f5c21c5f50d4a8a9f20c076f8c3e6d153f78c8d1`.

## Darkening and winding

Native `0x1403c6200` clears the returned offset and, when path+0x48de is enabled, accumulates integer-unit winding into callback owner+0x20. It floors raw start/delta coordinates through arithmetic >>16 and adds deltaY*startX−deltaX*startY in 32-bit storage. Winding accumulation uses the raw segment independently of the original font+0xec reverse flag; that flag reverses the delta only for choosing its offset side.

Directional branches compare signed doubled slope components. Nearly horizontal right-side offsets are zero; opposite-side offsets use twice vertical darkening. Nearly vertical offsets use signed horizontal darkening and one vertical amount. Diagonals retain original fixed factors +0xb333 or −0xb332 horizontally and 0x4ccd or 0x1b333 vertically. The one-unit positive/negative coefficient asymmetry is preserved. Zero-length segments also follow the actual diagonal branch; callers decide when a pending/mask change warrants that calculation. The authored state owns its winding sum and amounts; disabled calls leave its sum unchanged.

One thousand unchanged native offset fixtures include all cardinal/diagonal/two-to-one boundaries, reverse/disabled cases, fractional raw points, winding wrap and full signed 32 coordinate boundaries. All 3,000 offset/winding values match, native SHA-256 `97c0a477e03d48ce613a35697576b4d204f11ad0d979515aad05dd0bbb8f98df`.

Both differentials use preallocated private CPU storage with original map/multiply/offset functions and no substituted callbacks, imported allocation, GPU work or game launch. Focused authored fixtures preserve separate-product rounding, negative halves, map cursor/translation, low-bit multiply/add, asymmetric offsets, integer-unit area and disabled state. Project conversion/sign-conversion warnings, ASAN/UBSAN and formatting pass.

Next: actual pending line/cubic flush at `0x1403c6990`, logical junction adjustment at `0x1403c6010`, first-contour snapshots and callback emission. Native `0x1403c6680` flushes an existing pending segment before replacing a dirty-mask map. That ordering cannot be inferred solely from raw event CommandIndex. Native whole hinted-glyph contours and final raster output remain open.
