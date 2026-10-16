# Neutral shaping caller through glyph publication

`PrepareNeutralGlyphIndices` recovers the complete default neutral caller at `0x1403f4440` through its final glyph-word publication. It connects the genuine first-party `SfntFont`, existing owned `VectorFontFeatureMap`, scoped `UnicodeNormalizationBuffer`, user requests and actual caller context values. The record owner/state layout and earlier APIs remain unchanged; one narrowly scoped friend permits the caller phases to mutate current records and scratch flags. No encompassing native font/buffer lifecycle, fake shaping plan or replacement font is introduced.

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`, image base `0x140000000`. Binary Ninja reads explicitly use `candidate:view:1:UEU`. Original executable/resource bytes and analysis metadata remain unchanged. Addresses below are virtual addresses; algorithms/types are first-party reconstructions from native instructions and existing independently proven font readers.

## Inputs and ownership

Input records must already have the actual Unicode properties, global feature masks, initial clusters and direction-adjustment/reversal applied. This is the original `0x1403f3d80` boundary preceding its call at `0x1403f4038`; this module does not fabricate earlier dotted-circle or general buffer ownership behavior. The same font's prepared feature map comes from the recovered feature compiler/font metadata adapter. Its owned tag/mask records remain alive through this call. The `SfntFont` and immutable resource-byte owner also remain alive throughout all nominal/variation queries.

The original caller stores the pre-adjustment direction at context `+0x30` in `0x1403f3e40`, while direction adjustment may flip the buffer's direction at `+0x60`. `NeutralGlyphPreparationContext` keeps `OriginalDirection` and `BufferDirection` separate. It also carries the actual cluster level and fallback-mark-positioning decision. The latter is the original caller's boolean at `+0x2c`, selected earlier using its genuine layout service/descriptor state. It is an explicit caller input, not a guessed font fallback or manufactured native cache. The lazy native face/layout service and its lifetime remain open.

Prepared metadata must contain unique sorted four-byte ASCII tags. Author work limits are 4096 features, 128 user requests and 4096 live records/maximum length. Tags, counts, cluster level and directions are validated before mutation. Feature preparation/configuration remains limited to the existing adapter's recovered neutral Latin/Cyrillic/Han and canonical `en`/absent-language profiles. The glyph caller consumes prepared metadata; it does not expand that adapter's supported configurations or compile lookups.

## Actual plan masks and phase order

After the actual feature compiler, `0x1403f2cf0` through `0x1403f2f92` queries the compiled sorted 36-byte feature rows. It copies the row's `OneMask` at `+0x1c` for these exact tags:

| Native plan field | Tag | Caller use |
| --- | --- | --- |
| `+0x430` | `rtlm` | Mirror fallback mask |
| `+0x434` | `frac` | Fraction mask |
| `+0x438` | `numr` | Numerator mask |
| `+0x43c` | `dnom` | Denominator mask |

Plan `+0x444` mask `0x1` is set iff `frac` is nonzero or both `numr` and `dnom` are nonzero. The portable caller uses these same proven `VectorFontFeatureMask::OneMask` fields, not arbitrary invented mask numbers.

`0x1403f4440` executes these phases in order:

1. Mirror acceptance using the saved original direction and actual default Unicode callback.
2. Complete recovered neutral mode-2 normalization at `0x14040bf50`.
3. Fraction feature masks at `0x1403f4250`.
4. Descriptor post-normalization hook at `+0x48`; the actual neutral descriptor `0x140623700` has a null hook.
5. User feature masks in supplied order.
6. Optional fallback mark-class rewriting at `0x14040d510` when caller `+0x2c` is nonzero.
7. Full nominal-word-to-`GlyphIndex` copy and content tag 2 at `0x1403f4616`.

The descriptor's 104-byte SHA-256 remains `59d8d1d5dee371edeb516afb2f26e0054e36d895ce70a2e42a11f064df90abba`. Only that actual neutral profile is supported. Failed normalization does not skip later caller phases or final mapping, unless execution reaches an explicitly unsupported nonprogress/outside-resident boundary.

## Mirrors and fractions

Mirroring is skipped only for forward original directions 4/6. For backward directions 5/7, every current codepoint is queried through the actual recovered default Unicode mirroring primitive. A different mirrored codepoint is accepted only if the same font's actual nominal query succeeds. Acceptance changes only the codepoint; the returned temporary glyph index is not written into the record's scratch word at this phase. An unchanged mirror or missing mirrored glyph ORs `rtlm` into the record mask. The supplied character state is not reinitialized here, including the default callback's proven low-sixteen-bit key behavior for high input aliases.

Fraction processing requires scratch flag `0x1` (`HasNonAscii`) and the plan's fraction enable condition. It finds U+2044 and extends across adjacent records whose supplied category is 13, decimal digits. This uses the retained Unicode codepoints before final glyph mapping. The range is marked unsafe when clusters differ: records whose unsigned cluster is not the range minimum receive mask flag `0x1`, scratch flag `0x10` is set, and clusters remain unchanged. This is direct `0x1403a6b50` behavior and applies regardless of cluster level.

For forward buffer directions 4/6, preceding digits receive `frac | numr`, following digits receive `frac | dnom`; backward buffer directions 5/7 swap numerator/denominator. The slash itself receives `frac`. The scan resumes after the following digit run, preserving repeated-slash behavior. Existing masks are ORed, not replaced.

## User masks and mark positioning

The caller skips requests with global range `[0, 0xffffffff)` because that request already affected compiled initial masks. Each remaining request searches the actual sorted feature map. Missing features use zero shift/mask and produce no mutation. Value shifts use the original 32-bit x86 shift count modulo 32; only bits in the compiled `Mask` survive. Records are selected by unsigned `StartCluster <= Cluster < EndCluster`, including the native finite-range treatment of cluster `0xffffffff`. Mask replacement is `(old & ~mask) | (shifted_value & mask)`, preserving supplied request order after fraction masks. Scratch flags are not repaired when user masks change mask bits.

`0x14040d510` is **fallback mark positioning**, not record filtering. It visits only supplied category 12, preserving upper character-state bits and its low property byte. Modified classes 200–255 remain unchanged. Classes below 200 are remapped through the actual original jump tables to position classes 214/220/222/228/230/232 where specified. Thai/Lao overrides in U+0E00–U+0EFF use the original codepoint gates: class-zero U+0E31/U+0E34–37/U+0E47/U+0E4C–4E become 232; U+0EB1/U+0EB4–7/U+0EBB/U+0ECC–D become 230; U+0EBC becomes 220. U+0E3A with nonzero class below 200 becomes 222. Other values follow the exact generic class table. No record is removed or nominal glyph substituted by this phase.

The final operation copies each complete four-byte `+0x0c` scratch word into `GlyphIndex` and returns `NormalizedGlyphContent::GlyphIndices` with native value 2. Other words, owner counts, active/error state and pointer aliasing remain intact. The future enclosing lifecycle/content owner must retain that returned tag; this stage does not invent it.

## Validation and remaining scope

Four ordinary `-O1` strict C++20 ASan/UBSan groups cover nominal mirror acceptance and unchanged/missing mirror masks, independent original/buffer directions, fractions followed by range overrides, unsafe clusters, Thai/Lao/class rewriting, sticky failed publication through final mapping, and preflight rejection without record mutation. Metadata/cmap fixtures are authored first-party and interpreted through the genuine SFNT and feature-map readers.

The native differential executes complete unchanged `0x1403f4440`, actual plan mask extraction, actual nominal/variation/Unicode callbacks, neutral normalization, fraction unsafe marking, user feature search/mask writes, fallback class jump tables and final mapping. Both physical resident arrays are bounded/fenced. Font/resource tables, function owners, compiled plan/feature rows and font contexts remain immutable. Full owner state, every input/output record word, callback order and phase order are compared to C++ and an independent table-decoded model. The original fonts are streamed one at a time without retained copies or full output streams. The complete comparison matched 104,390 cases and 3,278,410 words (13,113,640 streamed output bytes), including 10,530 accepted mirrors, 52,910 mirror-mask fallback observations, 1,230 failed profiles and 360 out-of-resident checkpoints. Native/C++ and independent model SHA-256 both equal `d1a94bc87272dc28236643ae6261e571e9a974eae6876e525f255b40c112f097`. All three authored metadata/cmap profiles and ten original fonts use three original directions and two request profiles, producing 78 datasets. The final sanitizer executable passed 423 retained native replays and 15 malformed requests. Nine additional whole-caller selector nonprogress cases matched before later phases/final mapping, stream SHA-256 `37eb433da0b39dedfac74391be1e3aed1e5bfe7fd5f636c91be67b664afa01b6`. Original fallback-class table hashes remain in the compact private receipt.

Allocator-driven growth/failure, the genuine lazy face/layout cache and whole font/buffer lifecycle, prior dotted-circle policy, other descriptors/complex-script profiles, GDEF/lookup application, positions and final-frame parity remain open. The earlier selector nonprogress and out-of-resident native continuations remain explicit unsupported errors. This is a coherent recovered glyph-preparation caller, not complete shaping or text rendering, and it uses no guessed Unicode version, host normalization/font fallback, copied implementation or whole-core library link.

The coherent public sanitizer executable passes423 retained native snapshots,15 malformed-driver requests and9 original nonprogress checkpoints. Integrated and portable Release fixtures pass at300/275; effective sanitized font/cache ownership and full shaping remain open.
