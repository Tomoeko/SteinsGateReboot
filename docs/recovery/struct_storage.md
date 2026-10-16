# Schema-backed Struct storage

Target `sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`; executable virtual addresses refer to the read-only `candidate:view:1:UEU` Binary Ninja view.

`Struct` is a fixed-layout mutable byte buffer configured by an immutable PSB definition table. It is not a mutable PSB tree. Native registration is `23d810`, constructor `23bfb0 → 23ced0`, PSB-backed owner construction `4edd00/4eddb0 → 4ee770`, and root view creation `23d330 → 4f1540`. `StructValue` registration is `23e210`; its 48-byte descriptor borrows the root byte owner and records a schema, packed offset, primitive type and element index. Native `ObjectInfo` copies retain the actual script root when nested containers are wrapped.

The original schema consists of named definition tables whose fields contain declaration arrays. Field order follows the original PSB field enumeration. Byte offsets are packed sequentially without native host padding.

| Declaration tag | Stored field | Parameters after tag |
|---|---|---|
| 1 | One boolean in a byte | None |
| 2, 3, 4 | Signed 8-, 16-, 32-bit integer | None |
| 5 | Float32 bits | None |
| 6, 7 | Fixed byte or UTF-16 string plus terminator | Character capacity |
| 8 | Nested named definition | Definition name |
| 9 | Packed boolean array | Element count |
| 10, 11, 12, 13 | Integer8/16/32 or float32 array | Element count |
| 14, 15 | Byte/UTF-16 string array | Character capacity, element count |
| 16 | Nested definition array | Definition name, element count |

Native getter assembly `4f0010` sign-extends stored byte/short fields; its Pseudo C representation obscures those instructions. Writes `4f1610/4f1730/4f16a0/4f18e0` store bits explicitly in little-endian order, truncate fixed strings and preserve unused trailing bytes. Boolean arrays address one bit per element. Numeric VM setters use the actual scalar adapters: `222c70` tests the entire integer or CVTTSS2SI float result; failed bool/integer/float conversions initialize zero. String setter conversion requires an actual VM string and initializes an empty string on failure, rather than invoking general string formatting.

`getRoot`, nested get/set, array length, field-order iteration, `getValue`, type reporting and `_exist` use actual retained schema views. `_exist` returns native status zero or negative one, not a boolean value. Preserve the original `23c1a4` branch: exact Integer keys skip its array membership check, whereas other keys use the native integer adapter. VM opcode41 consumes that success/failure status. Bound negative/out-of-range accesses before they reach native-style offset arithmetic.

Backing is allocated and zeroed lazily (`4f15d0`). Purge (`4f1480`) releases only those bytes; existing views retain their schema and see a new zero buffer on the next read. Clear (`4f0680/4f06c0`) writes zero to the native view range. Primitive clear uses the descriptor's string-capacity byte count from the view base, including the original UTF-16 quirk. All-flags count/or operations include padding bits in the final byte; all-flags and checks the expected final-byte mask. Explicit selectors preserve duplicate indices.

Serialization `23ed40` copies exactly the root bytes into a real Binary owner. Unserialization `23ef90` requires the exact root byte count. Duplicate `23cd40` creates separate mutable root backing from the same schema. The C++ storage owner is move-only; only value views share its actual mutable state. Struct/StructValue bindings retain the immutable source PSB and actual Struct receiver through VM host roots. Released base/root receivers reject view access; VM close finalizes live records despite retained C++ handles. No Apple type or service is part of these interfaces.

The bounded source validation includes an independent 75-byte vector spanning all16 declaration kinds, malformed/cyclic schema and allocation limits, signed values, float bit payloads, fixed strings, nested/array offsets, shared views across purge/unserialize/clear, deep duplicate ownership and flags. Separate actual first-party VM fixtures exercise constructor payloads, coercion, opcode41 status, Binary serialization, last nested-view root/source release and forced VM finalization. These authored checks establish implementation behavior; native static recovery and original-byte admissions are recorded separately. The additive original-instruction owner witness now passes all 16 layouts, 12 scalar/array/nested read-write pairs and a 75-byte packed root, with 70 actual allocations and 70 releases. Independent byte admission covers 3,083 distinct instructions and 11,796 original bytes. Corrected execution accounting separates 46,116 original instruction dispatches from 408 intercepted allocator/C-memory/cookie boundaries; the earlier 46,524 total included both. This is a bounded owner/primitive comparison; original VM/Stream integration and actual saves remain pending.

Native CopyOnly `StructValue` constructor `04b530` is exactly XOR EAX,EAX; RET: it succeeds with zero results and attaches no descriptor. Such an instance retains its class identity without fabricated backing. Generic payload-dependent method dispatch `04dc10` reports `bad instance` and returns negative one for a null host payload; static `getValue` returns zero results. This differs from `Struct`, whose constructor builds an actual schema-backed owner.

Unsupported domains remain explicit: native Stream ownership for `storeBinary/restoreBinary`, invalid or truncated-surrogate Windows text conversion, unsafe native malformed-layout arithmetic and exact platform-dependent nonzero memcmp magnitude. Valid UTF-16 scalar pairs are supported without normalization; unsafe string reads and split-surrogate truncation reject. No complete persistence, save-file compatibility, native bootstrap or playable-game milestone is closed by this source recovery.
