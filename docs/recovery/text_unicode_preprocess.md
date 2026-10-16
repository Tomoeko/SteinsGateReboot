# Original Unicode glyph preprocessing

`InitializeUnicodeGlyphProperties` recovers the supplied executable's default-callback preprocessing pass. It borrows the existing `SubstitutionGlyph` records while `GlyphIndex` still holds the Unicode codepoint, replaces only the low word of `CharacterState`, and returns accumulated buffer scratch flags. Every Unicode codepoint and 512 mixed synthetic runs have passed exact comparisons with the unchanged original routine. Normalization and complete shaping remain open.

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses below are original virtual addresses unless identified as file offsets. Binary Ninja inspection explicitly selected the supplied executable's PE view without metadata or byte mutation.

Executable-derived control flow: shaping orchestrator `0x1403f3d80` clears buffer scratch flags and initializes record masks, then calls preprocessing `0x1403f4670` at `0x1403f3e8b`. Direction/cluster operations and later glyph/GDEF initialization follow. The 692 original preprocessing bytes have SHA-256 `ff428bc3f239a74d3025cdee987f40e47f08ea06de2ea62291b997a93b7c66f2`.

The preprocessing buffer supplies its Unicode owner at `+0x38`, scratch word at `+0x4c`, record count at `+0x88`, and records at `+0x98`. Category and combining queries use owner callbacks `+0x58/+0x48`, recovered in [the Unicode query evidence](text_unicode_properties.md). The routine writes two bytes at record `+0x10` with a 20-byte stride. The rewrite updates the corresponding `CharacterState` low word through typed fields, without serialized/native-struct casts.

The low five property bits initially hold the original category. Codepoints below `0x80` retain that category alone. Any codepoint `>=0x80` sets buffer bit 0 (`HasNonAscii`), including values outside the Unicode range. Recognized default ignorables also set buffer bit 1 (`HasDefaultIgnorables`) and property bit `0x20`. Existing scratch bits and every record byte outside the two-byte property word remain unchanged.

The original ignorable predicates are exactly:

| Inclusive codepoint range | Inclusive codepoint range |
| --- | --- |
| `00AD` | `034F` |
| `061C` | `17B4–17B5` |
| `180B–180E` | `200B–200F` |
| `202A–202E` | `2060–206F` |
| `FE00–FE0F` | `FEFF` |
| `FFF0–FFF8` | `1D173–1D17A` |
| `E0000–E0FFF` | |

Within those cases, `200C` additionally sets `0x200`, `200D` sets `0x100`, and `034F`, `180B–180D`, and `E0020–E007F` set `0x40`. This last bit is named `IgnorableSpecialCase`; its downstream consumption has not been recovered. The recognized unassigned values remain ignorable because the native predicate establishes that behavior. Ignorable properties bypass the combining-class stage.

Only native categories 10, 12 and 24 (spacing mark, nonspacing mark and modifier symbol) enter combining processing. Category 11 is excluded by the original mask `0x1001400`. Normal combining values occupy the property word's upper byte. The same bit positions carry joiner flags in ignorable format records, so downstream interpretation requires the category and branch context.

Direct byte observations: the 256-byte modified-combining map at virtual address `0x140621930`, file offset `0x61ff30`, has SHA-256 `fbec0c927a5357f2a5fcc70eaeb12e99bbe30a06012875ce195a89b9a9af5609`. It differs from identity at 28 positions. The first-party source expresses those differences as a readable 24-value low-class array and five explicit switch cases; it does not depend on host Unicode data or a downloaded implementation.

The original code additionally uses `103A`'s combining query for codepoint `1037`, forces modified class 254 for `1A60/0FC6`, and forces 127 for `0F39`. After combining processing, emoji modifiers `1F3FB–1F3FF` replace the complete low property word with `0x0B`. This is an exact native property override, not a change to their recovered Unicode category data.

Validation: four strict C++20 O1/dead-strip test groups pass with ASan/UBSan and warnings treated as errors. There are 68 explicit codepoint/property checks, plus scratch accumulation, empty-input retention and record-count preflight rejection. The portable pass accepts at most 65,536 borrowed records and allocates no runtime storage.

The research-only Unicorn 2.1.4 harness executes unchanged `0x1403f4670` with its actual default callbacks, original classification trie and original modified-combining map. It compares all 1,114,112 Unicode codepoints as full 20-byte records in 272 bounded chunks, then 512 mixed boundary/random/empty/out-of-range runs containing 32,500 records. All 1,146,612 records, 5,733,060 serialized words and returned scratch flags match the first-party sanitizer executable exactly. Exhaustive output SHA-256 is `6072c985c9cb80802bf43762c14838ad64874a0d3bf7420707d698af05862e62`; synthetic output SHA-256 is `5dab753e0ccfca83978a2152243493a3936f513f8aa90e2f17321e8cad58319e`.

Original code/tables are read-only; all executed blocks are whitelisted. The harness independently checks 20,639,016 retained record bytes, unchanged Unicode owner and unrelated buffer fields, output fences, nonvolatile Windows x64 registers and the return stack. It retains only 1,280 bytes of replay samples and compact receipts. No game process, window, audio, OS/game callback or font copy is involved.

This closes this default-callback property initialization pass. Unicode version identification, custom callback owners, the upper `CharacterState` word's ownership, bit `0x40` consumers, normalization/composition/decomposition, direction/cluster processing, contextual substitutions, positioning, complete shaping and final-frame fidelity remain open. The next concrete step is the following `0x1403f2fa0` pass and the cluster operations that consume these initialized properties.
