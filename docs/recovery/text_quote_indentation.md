# Quote indentation

The target is `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Native render `0x1400328e0` uses the ordinary markup branch `0x140034793`; `0x140030fa0` owns each shaped unit, `0x14002dc40` handles wrapping, and `0x14002e330` finishes lines.

`Indent == 0` bypasses quote matching. Other signed values search the configured Begin and End sequences by first index. Begin takes priority. The parser keeps signed nesting depth, an owned saved opener, first eligible unit, and first ordinary unit as per-render locals. The retained line-start value is separate core state at `+0xb8`; line finishing reads it and actual Clear resets it.

A negative mode measures the first ordinary opener through the actual font callback at `0x14003489b`, subtracts its width, then calls the shaped-unit producer. This extra measurement precedes shaping and may change live formatting/options. Successful append of a first eligible depth-zero opener saves the current Begin[index] and actual post-append cursor as line start, then increments depth. Successful End decrements depth even on mismatch or underflow. Only depth one with matching saved-opener/End indices clears line start. Native empty saved-opener search returns index zero. Failure mutates neither depth nor line start; both eligibility flags still clear.

Explicit LF restores only first-eligible state. Leading LF leaves first-ordinary state intact; automatic wrapping does not reset parser locals. `%C` preserves them. Escaped code units call the ordinary append producer directly and clear eligibility without quote matching or negative measurement. Each punctuation unit remains an independent owned element; this branch produces no ruby/group owner.

The original oracle has 12 positive/negative/zero, wrap, LF, nested, mismatch, underflow, late-opener, `%C` and Clear cases (90,187,692 unchanged instructions; 4,139 verified PE basic-block sites), plus six independent escape/failure cases. Actual native CFF providers at sizes 24/10 supply glyphs and advances; provider selection is an explicit fixed service boundary. Expected geometry, glyph IDs, processed text and counts remain immutable for receiving replay. Synthetic callback fixtures distinguish the extra measurement from shaped advance and preserve callback/reentry ownership.

Vertical text, ruby/group markup, other markup effects and appended render-segment timing remain explicit gates. No whole font-selection factory, GUI, GPU pixel or cross-API fidelity claim is implied.
