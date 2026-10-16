# Text character queries

Target `sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`; read-only Binary Ninja view `candidate:view:1:UEU`.

Registration `0x14003E9F8` installs `TextRender.getCharacters` with member `0x14003B650`. A detached wrapper returns a fresh empty array. The live producer `0x14003AE30` reads the finalized element pointer order, independently of shown count. Its signed start/count endpoints use wrapping 32-bit arithmetic; count zero extends to the original rendered-element counter (`0x140030E30`), bounded by the finalized vector. Negative dereferences remain explicit errors.

Each returned table owns `graph`, `text`, `x`, `y`, `cw`, `size`, `face`, `rubyface`, `color`, `bold`, `italic`, `shadow`, `shadowColor`, `shadowDiff`, `edge`, `edgeColor`, `edgeSize` and `vertical`. Geometry is local finalized geometry; colors retain their stored signed words before drawing conversion. Face lookup is `0x1400306B0`. UTF-16 conversion `0x1404C75D0` uses the original NUL-terminated CP_UTF8 boundary. An optional `ruby` array (`0x14003C980`) contains owned text, relative x/y and size for each annotation. Query results do not borrow native elements, font owners or mutable content.

The current content owner admits ordinary horizontal text and rejects graph/vertical producers before publication. This query preserves those existing boundaries; it does not enable unsupported text producers. Broader source encodings and whole native query-output comparisons remain open.

Eight whole native functions totaling 11,637 bytes independently match the PE. Focused ASan/UBSan checks with the actual vector-font, VM and content owners cover fields, annotations, signed ranges, reveal independence, fresh snapshots, clearing and detached wrappers. The selected query implementation and fixture are sanitized; the linked optimized core is not. The actual phone send-mail path exposed this missing query after source2095; continuation receiving is separate from full phone-state restoration for `--jump`.
