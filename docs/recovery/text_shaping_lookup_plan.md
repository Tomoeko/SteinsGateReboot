# VectorFont lookup scheduling

`PrepareVectorFontLookupPlan` prepares owned feature masks and GSUB/GPOS lookup schedules for the recovered neutral Latin, Cyrillic and Han profiles. It uses the actual font tables and canonical `en` or absent-language configuration. It does not alter glyphs, publish positions, measure text, or render pixels.

## Executable evidence

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. All addresses below are original virtual addresses. Binary Ninja observations target the explicit PE view; no bytes or analysis metadata were changed.

The feature-mask prefix is documented in [text_shaping_feature_map.md](text_shaping_feature_map.md), and real-font selection in [text_shaping_font_map.md](text_shaping_font_map.md). This module implements the remaining neutral schedule path of `0x14040b600`, from `0x14040bb36` through the return at `0x14040bf4b`.

- `0x14040bb43..0x14040bb5b` appends a final null pause to each table through `0x14040b4a0`. The neutral collector already appended a GSUB pause at stage zero. The resulting GSUB schedule has two stages; GPOS has one. Empty stages still publish their cumulative endpoint.
- `0x14040aed0` queries feature variations. All ten original fonts have layout version 1.0, and `0x140402880` returns the no-variation sentinel. Nonzero feature-variation offsets remain explicitly unsupported by the existing mask adapter.
- Each stage appends its selected required feature first, with mask `2` and both policy bits set. Its stage is retained from mask compilation, including the default stage-zero behavior when a zero-valued descriptor was omitted.
- `0x14040b270` fetches each feature's actual lookup indices through `0x14040a6e0` in batches of 32. It ignores an index beyond the actual table's cached lookup count (`0x14040afc0`). The authored metadata reader already rejects malformed feature indices; it never dereferences an unchecked payload.
- Native records are eight bytes: lookup index at `+0` (16 bits), policy options at `+2` (16 bits), and mask at `+4` (32 bits). Mask policy bits one and two become options bits zero and one. Required entries use options `3`. These numeric options are retained without claiming recovered glyph-filter semantics.
- At `0x14040bcf9`, the original sort operates on the range appended for the current stage only. Comparator `0x1403a23c0` uses an unsigned 16-bit lookup index. Repeated indices in that range merge masks with OR and each policy bit with AND (`0x14040bd5c..0x14040bd88`). A lookup repeated in a later stage remains a separate record.
- The original compiled pause stores the cumulative lookup count and the collected callback pointer (`0x14040be6d..0x14040be7d`). All callbacks in the supported neutral profile are null. `StageEnds` retains those cumulative counts; no callback addresses enter the portable interface.

Returned schedules and tags own their values. Inputs may be released after preparation. A named 65,536-record limit bounds the temporary schedule before each stage's duplicate reduction. Preparation belongs to resource/plan construction, not an ordinary frame.

## Validation

Five independent sanitizer fixture groups cover absent tables and retained empty pauses, required-feature stage selection, duplicate-mask merging, stage-local repeated indices, disabled descriptors retaining the actual required feature, vertical selection, unsupported profiles and a maximum-count feature that exceeds the bounded temporary schedule.

A streamed differential executes unchanged original table selection, neutral descriptor selection, feature collection, original sort/comparators, mask compilation and complete schedule tail on all ten supplied fonts. It compares 160 configurations, 830 compiled features, 6,864 scheduled records and 22,958 words. The bounded execution visits 6,756,048 original instructions. Retained output is 91,872 bytes, SHA-256 `3b5d691f242890c60614b5af35900ee2d7f5bc62196003f6bff4cb4d550c0b82`.

A second original/first-party differential uses 16 independently constructed fonts and 256 profiles, including selected/unselected required features, disabled collected tags, duplicates and feature lists of 31, 32, 33, 63, 64 and 65 lookup references. It matches 7,764 scheduled records and 31,322 words across 19,879,986 original instructions. Retained output is 125,352 bytes, SHA-256 `d13f655e3700f4f04f7508a0cb2d095338c090cf4c7aba14af6f321f75ddfbea`.

The research harness supplies preallocated caller buffers, observes only the indirect-sort guard and exact private sort-stack zero operations, and executes no allocation, OS, GPU, or game callbacks. Font bytes are streamed and hash-checked without retaining decoded fonts. Original inputs remain unchanged.

## Remaining acceptance

This closes neutral lookup scheduling only. Glyph information and cluster ownership, normalization, lookup application, contextual substitution, GPOS adjustments, fallback kerning, complete shaping, vector measurement, hinted pixel/atlas integration and final game frames remain separate open checks. Arabic/script-specific callbacks and variable-font feature substitutions are rejected.
