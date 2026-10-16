# Selected ordinary tween evaluation

Target: supplied `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Native analysis explicitly uses `candidate:view:1:UEU` / `view:1`, read-only. Addresses below are executable virtual addresses. The private native probes execute unchanged bounded instruction ranges against synthetic decoded frames; they do not launch the original program.

The additive selected-index evaluator consumes the actual current/following pair from `MotionLocalFrames`. It does not repeat a stateless search: native seek selects the first exact duplicate, while forward equality crosses duplicates and reverse equality retains the current frame. The previous static constructor remains available and rejects genuine type3 frames. The additional constructor requires the immutable actual `MotionResource` for curve resolution; no substitute curve owner or callback is created.

## Executable observations

`0x14050bc40` reads the selected slot at layer `+0x350` and decoded frames at `+0x110` / `+0x230`, stride `0x120`. Hidden current frames, current tween disabled, or a hidden following frame use the exact current attributes. A present type3 pair uses player evaluation position `+0x19c`; the per-layer parameter target is a separate owner and remains unsupported.

Elapsed time is evaluation position minus current time. Nonzero `interval` quantizes elapsed time before division by adjacent duration: the original uses float32 division, signed `CVTTSS2SI`, a 32-bit product, then unsigned product conversion to float. The authored domain rejects conversion counts at or above `2^31` and retains the 32-bit unsigned product. Present adjacent tween frames require a positive finite duration. Hidden following frames do not enter this division.

An absolute ratio strictly below float32 epsilon (`2^-23`) takes the exact-copy branch. At epsilon, interpolation occurs. The exact-copy branch retains the current uniform/default-color flags; an actual tween combines current and following flags. This distinguishes a tiny nonzero ratio from an ordinary weighted evaluation when coordinates are large.

The original operation order is angle (`0x140528220`), zoom and shear (`0x140528740`), coordinate (`0x140528530`), rounded opacity, color flags, packed corner colors and optional modifier color (`0x140528360`), then optional mesh. Each curve channel uses its own decoded frame tuple. In the force-all no-mesh domain, the portable value operations produce no callbacks or externally observable partial state.

Opacity uses the raw ratio with `(1-ratio)*start + ratio*end`, then `0x1405823e0` rounds halfway away from zero. Serialized key `occ` resolves curve slot 1 and affects packed corner/modifier colors, not opacity. The public evaluator already used raw linear opacity; this tranche preserves that behavior and names the internal slot `kColorEasing`. Packed channels use the existing native-derived fixed-point operation, including truncation rather than opacity rounding.

`0x140515d40` binds current source/icon without consulting the tween bit. Type3 does not interpolate resource names. Static source cache acceptance can therefore include present selected tween frames while retaining its registered-source, undeformed geometry and genuine texture-owner requirements. `blank`, direct replacement, source `+4` bypass, other geometry and teardown remain outside that cache domain.

## Curve and string ownership

`0x140512e40` resolves `ccc`, `occ`, `acc`, `zcc`, and `scc` from the actual document's root `easing` data. Curve tuples occupy current-frame `+0x70`, `+0x88`, `+0xa0`, `+0xb8`, and `+0xd0`, with corresponding owner fields at `+0x80`, `+0x98`, and subsequent strides. The resource manager lookup `0x14051e470` keys by the PSB document/value identity, increments existing curve `+0x10`, or allocates a `0x30` object and calls `0x140504780`. That constructor stores the PSB identity, initializes count 1, and owns vector-of-vector storage containing float32 `x`, `y`, and `p` triples. Final release `0x14051dac0` removes the map entry, disposes vectors through `0x140319ad0`, and frees the curve. The PSB identity copy alone does not prove a document pin.

Portable `MotionResource::Easing` owns decoded curve values. `PreparedMotionFrame` owns resource-name strings and curve indices; selected samples and the source cache borrow those strings. The immutable raw/prepared timelines and actual resource must outlive the selected owner, cached binding, samples and detached queued events. This additive constructor does not claim native PSB lifetime across listener-triggered unregister. The event-string copying boundary remains documented in [local-frame recovery](motion_local_frames.md).

The native cached-ratio test can suppress recomputation when force is false and change is below epsilon. This tranche supports the explicit force-all composition domain already admitted by the recovered composition owner. It does not implement cached dirty scheduling, per-layer parameter targets, ground correction, variables, type1 frames, division/packed streams, nested/reference layer producers, complete task/class ownership or final packet publication. Existing stateless coordinate-path/mesh algorithms remain separate; the fresh numeric vector scope has no coordinate path or mesh.

## Bounded validation

The fresh 768-case probe executes 534,101 original instructions, including the actual nonlinear curve helper `0x14051c910` and rounding helper. It covers all 32 combinations of five curve channels, both coordinate planes and rotation modes, modifier enabled/disabled, quantization, exact epsilon boundaries, and following hidden/static frames. Independent raw-opacity and tiny-ratio start checks have zero differences. All numeric words and flags are retained for the parent coherent C++ replay; that replay remains pending at handoff. The unused native modifier field is compared only when the layer actually has a modifier.

Numeric fixture: 148,232 bytes, SHA-256 `f5dcf4d066faf0784a8826eafd96f192995e076beb2eceeed314e265812c9608`; output semantic SHA-256 `533ce78a248523be2670a13ca261793672911123787df9afc42ca35658fb8fd4`. Curves use the independently inspected decoded object layout and one four-knot nonlinear segment; this probe does not substitute for original curve decoding coverage.

The source-cache probe sets the current tween bit throughout all 2,048 transitions and executes 174,520 original instructions with 1,446 real lookup-boundary visits. Its output is byte-identical to the accepted static-source fixture. A separate type3-decoding cursor/action probe retains the original seek/forward/reverse instructions and compares its output with the accepted static local-frame fixture; numeric tween duration remains outside that cursor-only probe.

Default authored fixtures cover independent color versus opacity mapping, exact selected duplicates, epsilon flag preservation, quantization before snapping, following-hidden preservation, bounded numeric rejection, actual resource-backed local sampling, required binding services and retained variable/ground boundaries. Strict C++20 syntax and formatting pass. No private core/library links, public source/CMake changes, saved Binary Ninja database or original-installation modifications occur in this tranche.
