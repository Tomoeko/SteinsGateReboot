# Mutable texture ownership

`MutableTextureOwner` recovers the exclusive `MTexture` used by `MRawTex`. It owns a platform texture, optional sampled view and sampler, bounded CPU shadow, and optional sampled companion. It is separate from immutable motion-source cache ownership. The backend lifetime exceeds every owner and companion; mapped byte ranges borrow their texture allocation until explicit unmap or owner destruction.

The oracle is `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`, analyzed in explicit Binary Ninja `view:2`. Native creation is `0x1404f8410` → `0x1404f87a0`, with CPU-shadow branch `0x1404f8887`, staging branch `0x1404f8901` and platform factory continuation `0x1404f8937`. Native release is `0x1404f8520`; the enclosing RawTex deleting destructor `0x1404f90c0` disposes this texture before its remaining task/base state.

The recovered storage policies are:

| Native kind | Policy | Usage | Bind flags | CPU access | Sampled view |
| --- | --- | --- | --- | --- | --- |
| 0 | GPU only | Default | `0x8` | None | Yes |
| 1 | Dynamic write | Dynamic | `0x8` | Write | Yes |
| 2 | CPU shadow | Default | `0x8` | Shadow bytes | Yes |
| 3 | Staging | Staging | `0` | Read/write | No |
| 4 | Staging with companion | Staging | `0` | Read/write | Companion |
| 5 | Render target | Default | `0x28` | None | Yes |
| 6 | Depth/stencil | Default | `0x40` | None | No |

Kinds 5 and 6 force one mip. Kind 4 constructs a real kind-0 companion from the returned texture description. A multisampled view uses native dimension 6; ordinary views use dimension 4. These values describe original [D3D11 texture fields](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_texture2d_desc); they do not establish hardware support for every synthetic descriptor combination.

Native row pitch (`0x1404f8b30`) first uses its cache. An unlocked mappable resource without a CPU shadow attempts a temporary read map, or discard map for dynamic usage, and unmaps only on success. Otherwise it caches the format-derived pitch. Native map (`0x1404f8a70`) uses write, or dynamic discard, even when preserving prior dirty state. A failed map still locks the owner and can mark it dirty; another map is rejected until `Unmap` (`0x1404f8e90`), which calls the platform unmap for a locked non-shadow resource even after the failed map. Mapping a CPU shadow leaves its row-pitch cache unchanged.

Native filter setter `0x1404f8dc0` computes the desired sampler word from the two six-entry filter tables, including the asymmetric 16/4 branch. It does not invalidate an existing sampler. Address setter `0x1404f8e40` maps indices 0/1/2 to clamp/wrap/mirror words 3/1/2 and always invalidates the sampler, even if the addresses are unchanged. A bind (`0x1404f85c0`) uploads a dirty shadow, releases a stale sampler before creating its replacement, then binds the view and sampler to the measured vertex or pixel stage. Companion binds copy changed staging data first, propagate sampler values only on address invalidation, and truncate their slot to eight bits. Null view/sampler creation results remain nullable, and a missing sampler is retried on the next bind.

Release order is companion, CPU shadow, sampled view, sampler, texture. Destruction does not insert an automatic unmap. Backend release implementations must be nonthrowing and clean up their own foreign resources. The portable owner uses RAII to preserve cleanup when a constructor or backend operation throws.

Two independent unchanged-instruction runs cover 1,000 cases and 14,000 operations each: creation, filtering/address changes, pitch queries, successful/failed/repeated maps, unmaps, uploads, companion copies, sampler reuse/replacement, stage bindings and destruction. The second run maps before querying pitch. All 28,000 portable state and ordered platform-event projections match exactly. Canonical native trace SHA-256 values are `a5a9ab079f002deb6b67016bafc9d225ef869f5e7decb414b129a966fa5af2f5` and `6eea4836a79be495c3c344ea1c89b9a228f728e537c5769aaf5686879b0b625f`. The separate 1,000-case factory trace (`89adc619176caba336b9e9c25166cd6ec89755cfa896045360e01fa9232d0e09`) also records native allocation requests, arbitrary prepared initial-data descriptors and true companion construction; those broader initial-data combinations are not all supported by the authored API.

The native instruction harness observes CPU allocation/free and device/context resource creation, description, mapping, upload, copy, binding and release at explicit boundaries. Synthetic device acceptance is not real device execution. Independent portable tests verify actual bounded byte transfers, padded source rows, failed-upload retry, companion data transfer, release order, nullable results and malformed-input cleanup under ASan/UBSan. No original process, GPU or audio device is needed for these tests.

CPU layout is currently recovered for original formats 28/87/88 (32 bits), 65/61 (8 bits) and 49 (16 bits). Source data is restricted to one non-multisampled mip and validated against its row pitch and length. Default authored limits are a 64 MiB shadow, extent 16,384, 32 mips and 32 samples. Unexpected platform descriptions are rejected before exposing incompatible shadow/layout state. These bounds and error policies are explicit safety decisions. Native blank shadow allocation is uninitialized; the safe authored shadow starts zeroed and makes no native blank-pixel claim. Physical GPU creation/map failures, further formats, native initial-data mip arrays, mutable Metal integration and rendered pixel parity remain open.
