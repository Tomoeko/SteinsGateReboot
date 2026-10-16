# VectorFont feature collection

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses below are executable virtual addresses in the explicitly selected original PE view. No binary bytes or analysis metadata were changed.

`CollectVectorFontFeatures` recovers the feature-record collection step for the native neutral shaper. It supports the original Latin, Cyrillic and Han configurations. Native selector `0x1403f3950` chooses the descriptor at `0x140623700` for those scripts; its collection and override callbacks are null. Arabic selects another descriptor and remains unsupported here. This module collects preparation metadata; it neither shapes text nor changes the current VectorFont provider.

Executable-derived order from `0x1403f3730`:

- `rvrn` is enabled globally in substitution stage zero. A null GSUB pause then advances the substitution stage to one.
- Left-to-right adds global `ltra` and `ltrm`. Right-to-left adds global `rtla` and per-cluster `rtlm`. Vertical adds no direction prefix.
- `frac`, `numr` and `dnom` have value one and per-cluster policy. They do not set a global enabled value.
- `ccmp`, `locl`, `mark`, `mkmk` and `rlig` are global. Original constant tables are at `0x14096a658` and `0x14096a670`.
- Horizontal defaults continue with `calt`, `clig`, `curs`, `kern`, `liga` and `rclt`. `kern` retains native flag word three; the other defaults use one. Vertical instead adds `vert` with flag word seventeen.
- Caller feature records follow in exact input order, including duplicates and zero values. Only range `[0, 0xffffffff]` receives the global bit; all other ranges use per-cluster policy. The collection step does not reject reversed ranges or apply them to glyphs.

Append helper `0x14040b140` writes seven 32-bit words per collected record: tag, one-based insertion order, value, flags, global value, GSUB stage and GPOS stage. The global value is the requested value only when flag bit zero is set. Pause helper `0x14040b4a0` publishes a stage/callback pair before incrementing that table's stage. All neutral collected records retain GPOS stage zero. Original compiler `0x14040b600` confirms that flag bit one retains a fallback feature even when the selected language has no matching feature, while bit four searches both layout tables outside the selected language. Compilation is a later operation: no tag sorting, duplicate consolidation, mask allocation, lookup selection or callback execution happens in this collector.

The authored implementation owns its tag strings, bounds records to a caller limit no larger than 65,536, and rejects non-four-character/non-ASCII tags before allocation. These are explicit safety constraints. Native tag zero can reserve an unwritten record, and arbitrary non-ASCII native tag words are outside the supported portable input scope. The neutral collection module does not recreate allocator failure or uninitialized records.

Four independent sanitizer fixture groups cover exact default order/stages, RTL mirroring versus global alternates, vertical table search, duplicate/disabled user records, caller ownership, and unsupported directions/scripts/tags/capacities. A separate read-only executable harness ran the unchanged selector, collector and append helpers against bounded preallocated caller buffers: 1,000 cases, 18,617 collected feature records, 131,319 compared words and 1,218,078 native instructions. All words match the first-party collector. Retained output SHA-256: `a7b94893c50169d2936500bbc308a1b1ff3c64d302255dd75c10f732bdc3df25`. The harness executes no allocator, table provider, OS callback or initialized game code.

Next: recover native feature-map compilation and actual GSUB/GPOS application, Unicode normalization/cluster transitions, complex shapers and glyph-position publication. Matching collection records does not establish vector unit measurement, full-run shaping, glyph raster/atlas drawing or original-frame parity.
