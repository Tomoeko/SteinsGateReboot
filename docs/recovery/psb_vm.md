# Native PSB script adapters

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256
`75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.
Addresses below are executable virtual addresses; analysis targets `view:2`.

`0x14022a340` registers `PSBObject` as a native `Object` subclass. Its script
constructor (`0x14005ebf0`) fails with `can't create instance`; resources create
it through `0x1402295e0`. `getRoot` (`0x140229fc0`) returns bool, int32, float32,
or a C-terminated string directly. Array/table roots become real `PSBValue`
instances (`0x14022a0e7`), whose `ObjectInfo` strongly retains the `PSBObject`
instance (`0x140227580`). Nested wrappers copy that owner (`0x140229c4a` /
`0x140227510`). The authored adapter shares immutable document ownership and
retains checked VM instances; no container is flattened into a VM table.

`PSBValue` registration is `0x14022a850`; native type dispatch is
`0x1404d0840`. `_get` (`0x140229ab0`) indexes arrays through native integer
conversion (including float truncation and bool conversion), while tables
require C-terminated string names. Invalid indices fail. Scalar children
return directly. Integers retain their low signed 32 bits (`0x1404ca8a0`),
double atoms narrow to float32 (`0x1404ca7b0`), and string values terminate at
NUL (`0x1404cacf0` / `0x140228510`). Native containers and nested chunk atoms
produce `PSBValue` instances. A nested chunk's `_typeof` is `null`, lookup
returns null, and iteration fails. A chunk root returns null rather than a
wrapper. Original atom tags remain in `PsbValue.SerializedTag`; normalized
integers do not conceal packed-array/key atoms rejected by native dispatch.

`len` (`0x14022a200`) accepts arrays only. `_nexti` (`0x140229cd0`) advances
signed numeric array cursors and enumerates table keys in their serialized
key-index order (`0x1404cf040`), retaining the key list on each proxy. `_exist`
(`0x1402299b0`) reports success/failure status; present null children count as
existing. It excludes float array keys even though ordinary indexed reads
accept them. `_typeof` (`0x140229f60`) returns `array`, `table`, or `null`.
`getValue` (`0x14022a1a0`) uses the same getter with the PSBValue instance in
native receiver slot 1.

Independent typed fixtures run under ASan/UBSan and cover root properties,
document lifetime, scalar narrowing, NUL behavior, typed nested containers,
array/table foreach, existence status, chunk distinctions, construction
failure, bounded proxy counts, and expired host callback owners. Parser
fixtures verify that atom identity survives decoding. This establishes the
authored adapter contract, not full native runtime equivalence.

Open: native stream purging (`0x14022a280` / `0x1404cfb10`, `0x14022a290` /
`0x1404cfba0`), resource retention/reclamation, stale native error payloads,
and dynamic native proxy differential traces. The implemented `totalStreamEntitySize`
(`0x14022ae40`) reads the signed wrapping difference of the original file size
and chunk-data offset, initialized at `0x1404cd79d`. It requires complete
retained backing; metadata alone does not substitute an inferred full stream
length. Malformed cursors which produce
negative native array indices fail safely in the authored adapter.

The PSB adapter now distinguishes borrowed registry identities from explicit native host roots under opt-in VM reclamation. PSBObject uses the Object release bridge; plain PSBValue uses a direct native release hook and explicitly retains its script document owner until proxy release. Class metadata remains rooted independently. This ownership migration passes its integrated ASan/UBSan true-reclaimer fixtures, including child traversal after the original document handle is dropped and cache-backed release; the integrated validation checkpoint is recorded in the Resource note. It does not establish all native garbage-collector/finalizer ordering.
