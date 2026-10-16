# TrueType shaping metrics

The original phone font aliases select the supplied Sarasa TrueType resources. The flags-2 metric service now accepts that actual outline owner alongside the existing normalized CFF owner; no font substitution is used.

Original constructor/advance evidence (`sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`) covers `0878A0`, `087EE0`, `3A7780`, `3A73D0`/`3A74B0` through `39AF80`/`3B1950`/`3E7040` and `3A1080`. Four actual fonts begin with HB scales zero. Across 2,200 original rows, table advance, current FT scale, axis sign and signed rounding agree. Probe sign variants are explicitly prepared using the original HB setter; they are distinct from fresh-constructor state.

Vertical origin uses the received TrueType unhinted loader and completed phantom/CBox metrics (`39C140`, `3B9D90`, `3B92A0`, origin arithmetic `3A7500`). Thirty actual rows at requested sizes 1 and 33 match points, contour tags, bounds and metrics. Passive observation proves internal/public scale equality for these rows. The existing positive-size, original vertical-table, bounded static outline, composite and ownership guards remain explicit.

`VectorFont` retains the raw bytes and encloses both normalization callbacks and its matching outline reader. New generic default binding retains fresh zero HB layout provenance; explicit CFF binding stays available. CFF calculations and glyph preparation remain unchanged.

This closes only the shaping-metric component. The phone's native hinted flag-8 glyph load, TrueType bytecode, quadratic path preparation, stroke and atlas raster still require their own recovery. `PrepareGlyph` keeps its precise TrueType rejection. Actual covered layout or unsupported composite variants also remain guarded. No CFF outlines or unhinted glyphs replace those stages.
