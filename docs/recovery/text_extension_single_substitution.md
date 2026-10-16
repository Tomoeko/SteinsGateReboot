# Single substitution behind a GSUB extension

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`; explicit Binary Ninja view `candidate:view:1:UEU`.

Canonical Japanese selects SourceHanTC's actual Latin `JAN ` local-form feature. Its flags-zero lookup38 is GSUB type7: format1 wrapper at GSUB+0x67da, extension lookup type1, positive unsigned32 relative offset0x1b4c, and format2 single payload at GSUB+0x8326. The decoded font SHA-256 is `44af35f7ecf9947a02ae24eb6c254bdb9f1b91c8fa7c801d701ff8a5a6bbeb3c`; the target lies at font+0xf5fdee.

Fresh unchanged whole FT/HB constructor plus thirteen private actual-script units executes6,933,088 original instructions. Every observed single callback is original format2 application `0x140401720`, with RCX pointing to that exact extension target and return address `0x140401a88` in the original ordinary forward loop. Thus the actual extension uses the existing single replacement operation, including coverage, GDEF and record mutation helpers; no replacement callback is introduced. The complete native glyph/measurement/position rows are independently retained for root's successor replay.

The authored executable-lookup query admits direct type1 or type7 only when every actual subtable is format1 and resolves immediately to type1. Checked positive relative offsets and the existing full single-payload validation precede application. Non-single and recursive extension targets, filters, feature variations and metadata-only admitted readers remain outside execution. Applying a resolved target uses the same complete glyph/replacement preflight and preserves the existing first-covering-subtable, mask, cluster, GDEF and opaque-word behavior.

Independent fixtures compare direct versus wrapped formats1/2 over real initialized records, preserve preflight atomicity on a later bad glyph, reject foreign targets and zero offsets, and verify that actual wrapped replacement changes the subsequent unit advance. C++ acceptance is pending at this source checkpoint. This recovers one extension-to-single path, not general GSUB extension execution or multi-glyph shaping.
