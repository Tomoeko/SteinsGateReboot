# CFF hinted glyph metrics

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Analysis explicitly uses `candidate:view:2:UEU`; no binary bytes or analysis metadata changed.

`MeasureCffHintedGlyph` consumes already hinted 26.6 points and real horizontal/vertical font metrics. Native CFF publication at `0x1403cce27..0x1403cce9d` scales advances with signed 16.16 multiply rounding, obtains bounds from every point including cubic controls, and derives vertical bearing X from the unrounded horizontal advance. It retains the original unscaled vmtx side bearing.

For original horizontal outline load flag 8, FT publication at `0x14039c450..0x14039c538` floors horizontal/vertical left bearings and vertical bearing Y to whole pixels, ceils horizontal top bearing, expands control bounds, and rounds advances to nearest pixels. The vertical bearing quirk remains observable after this rounding. It writes a horizontal advance vector and computes 16.16 linear advances from original font-unit advances and size scales. The code rejects arithmetic outside signed native ranges before conversion.

Glyph-driver outline flags at `0x1403ccd0d..0x1403ccd35` are numeric 4, with bit `0x100` added for Y pixels per em below 24. Native derived flags remain distinct from bitmap/raster decisions. Point/tag/count/contour storage is checked; borrowed outlines remain unchanged.

One thousand bounded independent fixtures executed the unchanged CFF prepared-point publication followed by the actual FT rounding/advance/linear phase without imports, allocator replacement, or a game launch. All 12,000 metric words matched the sanitizer-checked first-party implementation. Native vector SHA-256: `26a8656ef0488b09411f1ce4ab64b4820470dacb04a626b140bad3bf538e5c7a`. Four retained native fixtures cover empty glyphs, negative bearings, fractional pixel bounds and subpixel advances. Separate tests reject unsupported tags, invalid contours/scales and coordinate overflow.

This is the normalized default horizontal outline case. Font/face transforms, missing vertical-table synthesis, nondefault driver properties, TrueType glyph loading and raster pixels remain open. The connected `CffGlyphFont` subsequently matched all 30 guarded actual native size/load records, including these metrics, in the observed default normalized font context; see [font glyph preparation](cff_glyph_font.md).
