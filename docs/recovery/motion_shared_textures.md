# Motion shared texture registrations

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses below are original virtual addresses. Recovery uses unchanged CPU instructions in bounded private memory; no original installation, GPU, sound, input or analysis metadata is modified.

## Native behavior and portable boundary

`0x140507990` initializes eight 24-byte shared rows in the motion resource manager's vector at `+0x50`. Each row contains a borrowed texture-owner pointer, two raw float dimension words and a signed 32-bit reference count at `+0x10`. The constructor initializes the owner and count only; dimensions and padding have no value until the row is occupied. `0x140501340` copies rows when the extent doubles.

External-image setter `0x14050a350` first releases an existing active image's row, then searches the entire table for the exact incoming owner pointer. A match increments that row's count and retains its first registered dimensions. Distinct wrappers sharing a physical GPU texture remain distinct rows. A new owner uses the first empty row; a full extent doubles from eight to sixteen and onward. Registration neither owns nor releases a texture, and does not apply the local slot's filtering setter.

`0x14051e9c0` clears the active-image flag and decrements the row count. The owner pointer is cleared when the resulting signed count is nonpositive. Actual `INC DWORD` and `DEC DWORD` operations wrap at 32 bits. The portable module admits valid positive counts through `INT32_MAX` and rejects an increment beyond that range as authored safety, rather than reproducing corrupted/overflowed native registrations. Clearing leaves dimensions and image ID bytes stale; inactive images must not resolve them.

Renderer owner selection `0x14055aa30` uses local nonnegative slots or delegates a negative ID to `0x1405188c0`: `~uint32_t(id)` selects a shared row. Thus `-1` selects row zero and `-2` row one. Size lookup `0x14052ad00` uses signed-integer physical GPU dimensions for a local slot, but `0x1405188e0` copies shared float dimensions verbatim. Shared dimensions may differ from physical texture dimensions; no normalization or NaN/zero canonicalization is permitted here.

`MotionSharedTextures` prepares registrations outside ordinary frames, borrowing `MotionTextureWrapper::Borrowed`. `SameIdentity` compares the actual wrapper state; `Texture` resolves its current cache handle and explicitly rejects an expired/closed owner. Neither query extends the stored lifetime or acquires a cache reference. `Replace` releases the old registration before lookup, including replacement by the same owner: a sole registration can refresh dimensions, while a surviving sibling preserves the original dimensions. `Close` drops only borrowed rows and counts.

The configured power-of-two capacity, generation checks, null/expired-owner rejection and signed-count overflow rejection are authored safeguards. Raw negative IDs intentionally carry no generation; only resolve them while the prepared frame's registrations and external owners remain stable. Native image geometry restoration and raw IDs surviving reuse are separate from these guarded registration handles.

## Validation

A deterministic original-code corpus executes 1,000 cases and 32,000 registration/replacement/clear operations, including 1,000 eight-to-sixteen reallocations and paired wrappers sharing physical textures. All occupied table identities, dimension words, reference counts, vector extents, image active/encoded-ID states and local/negative lookups match authored output. Other image geometry fields captured by the setter are not claimed by this table module. The oracle executes 8,170,168 unchanged instructions; semantic trace SHA-256 is `be622a9c0c5f2e693ae8c6d8797b94699977d573f7c6b47103c5bc5ae6032582`.

Twelve independent count-boundary probes plus three raw-dimension pairs execute another 1,920 unchanged instructions. They confirm DWORD wrapping, signed post-decrement branching, untouched padding and bit-preserving dimension copies, including signed zero and NaN payloads. Invalid count states are evidence for the machine operation, not accepted portable table states.

Five independent sanitizer fixture groups cover exact-owner versus physical-texture identity, first dimensions, sole/shared replacement, sparse reuse/growth, owner/generation/capacity failures, expired/closed wrappers, raw dimension bits, replacement failure and disposal without cache ownership. Existing local motion texture tests are rerun against the narrow borrowed accessor addition. CPU upload/release callbacks are synthetic and produce no GPU or audible output.

## Open integration

The native external-image setter also converts its rectangle to integer geometry and resets PSB image geometry. Its restore-flag branch selects keyframe image data. Those paths, parent/shared-atlas producers, command lifetime through a real motion model and backend source selection remain separate checks. This tranche does not install a fallback texture or claim whole-frame Metal/native pixel fidelity. Direct mode zero, meshes, filter targets and Begin render-state stack/shader coupling remain open.
