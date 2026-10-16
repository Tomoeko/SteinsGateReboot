# Motion stencil state

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses refer to the explicitly selected original PE Binary Ninja view. Original bytes and analysis metadata remain unchanged.

## Recovered native path

Graphics mask mode 0 uses the existing depth/stencil attachment. Prepare `0x1404c2910` clears stencil only when the attachment exists, has a texture, and carries original format 0x14 or 0x2d. It clears to the low eight bits of the supplied reference; depth is not cleared. The portable planner receives whether that concrete attachment supports stencil from its platform owner.

Begin `0x1404c5120`, End `0x1404c5260` and Resolve `0x1404c4420` call descriptor helper `0x1404c6450`. It reads the current descriptor, preserves depth enable/write/comparison, enables stencil and sets read/write masks to 0xff. Both faces use Equal comparison and Keep on stencil failure. Begin assigns IncrementWrap to depth-failure and pass; End assigns DecrementWrap; Resolve assigns Keep. These numeric meanings follow Microsoft's [stencil operation](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_stencil_op), [comparison](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_comparison_func) and [descriptor](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_depth_stencil_desc) definitions. Wrapping must not become saturation.

The helper releases the retrieved state before creating a new state. On a nonnegative creation result it binds the new state with the full supplied 32-bit reference, then releases its local state reference. A negative result leaves the old device binding and reference intact. Finish `0x1404c3150` calls `0x1404c4320`, which disables stencil while preserving the rest of the descriptor and the current bound reference. COM reference bookkeeping belongs to the platform owner, not the portable value representation.

Prepare sets the mask-active flag and phase 1, clears the resolved-mask flag and stores scalar word 0x3e808081. Begin/End set phase 2. Resolve sets the resolved flag, scalar word 0x3b808081 and phase 6. Finish clears mask-active and phase 0, retaining the resolved flag/scalar. These logical effects occur even when state creation fails. The scalar's shader consumer remains open; its value is preserved without claiming a unit or alpha interpretation.

`PrepareMotionStencilTransition` returns logical control changes, an optional stencil clear and an optional binding request. It makes no allocation, OS call or hardware call, and never commits a binding. Its caller must perform the clear or state request before applying the recovered control changes, and commit the bound descriptor/reference only when state creation succeeds. Rejected invalid enum values are authored safety diagnostics; unused prior stencil faces are replaced rather than inspected by Begin/End/Resolve. Initial engine state is supplied by the owner rather than inferred from these value defaults.

## Validation and remaining work

One thousand bounded native sequences run Prepare, Begin, End, Resolve and Finish: 5,000 transitions and 425,719 unchanged original instructions. Synthetic callbacks provide valid current descriptors and record get-description, release, create, bind and clear operations. All portable descriptor fields, requested/committed references, clear bytes and logical control words match with zero differences, including 3,429 successful and 571 failed state creations. Windows struct padding is retained by native code but omitted from portable semantic comparison. Trace SHA-256: `ae19d656b1de8ea098b7de910bc7c5e13279120d86355529a81db008abfce8e9`.

Independent strict ASan/UBSan fixtures cover low-byte clear versus full binding reference, absent attachments, depth preservation, both-face wrapping operations, Resolve/Finish retention, uncommitted requests and invalid enum diagnostics. Compact synthetic vectors and reports remain ignored under `Files/research/runtime/`. No game launch, speaker output, GPU call, OS callback or original patch occurs.

Texture ping-pong mask mode 1, initial attachment selection, render-scalar/shader linkage, stencil rasterization, complete scene submission and native/Metal pixel equivalence remain open. Descriptor and phase agreement does not establish complete masked rendering parity.
