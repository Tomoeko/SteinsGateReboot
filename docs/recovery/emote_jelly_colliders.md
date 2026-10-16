# Jelly completed-section collision

Reference executable SHA-256: `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.

Native `2fb750` retains each attached collider and forwards the selected mask bits to actual tress models. `316230` snapshots collider pose, prepares the actual subdivision count, then advances each collider before each full model step. Contact clearing belongs to the containing collider-pose phase, not this entity update.

`324f80` first applies acceleration/springs. Each free particle then projects its **last completed `325fc0` section ellipse**, subtracting the current model coordinate offset from its center. It uses `Position-Previous` as displacement and adds `(projectedCenter-sectionCenter)*(seconds*CollisionRepulsion)` to XY velocity. The full numeric/constraint/damping step completes before publishing new sections. A posture setter alone does not refresh that cache.

The shape-aware Bobble constructor owns the preallocated completed section cache. Its nonmoving model outlives that cache; attached colliders borrow the containing group's longer lifetime. Numerical-only Bobble instances reject attachment rather than invent collision geometry.

Original-instruction capture constructs twelve complete `310740` owners with twenty real tresses, attaches half-plane/solid/interior colliders through `2fb750`, and measures 96 reset/update/output phases. It includes moving/disabled colliders, mixed masks and multiple attachments. Every executed non-service instruction maps exact PE bytes; only bounded allocation/free services are supplied. The expected vectors derive only from those native observations. C++ numerical acceptance is recorded separately after execution.

The oversized-interior recursive ellipse-swap branch, zero-area sections, 32-or-more-tress mask wrapping, emission sources and complete containing-driver fidelity remain outside this captured domain.
