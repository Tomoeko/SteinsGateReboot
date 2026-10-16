# Emote physical math and retained conic state

Target: `sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.

The full owning Jelly collider replay previously failed case 5 / phase 7 / tress 1 / particle 1 / velocity Z: C++ word `1040280810`, original `1040280235`. Its original bound remains `8e-6 * max(1, abs(expected))`.

Three executable-derived corrections address separate causes:

- `0x14057CFB0` / `0x14057D5A0` / `0x14057C3F0` use the SSE2 sine/cosine/atan2 profile already recovered by `PrepareAffineTrigonometry` and `PrepareAffineAngle`. All 1,758 primitive observations from the original case-5 trajectory match those helpers bit for bit. Darwin standalone sine and Clang's combined sine/cosine can produce different float words; neither is the fidelity oracle.
- `0x14058C9D0` computes arccosine through float rational arithmetic and explicitly placed double final reductions. The independent 1,124-input original replay covers signed endpoints/zero, exponent boundaries, random finite unit values and actual trajectory inputs. The implementation matches every output word. Invalid/nonfinite results preserve the previous unordered path. Native FMA dispatch and large-angle reduction remain outside the recovered finite SSE2 profile.
- `0x1402F9E90` retains transformed conic coefficients while invalidating only the principal-axis cache at `0x1402FA038`. The actual contact sequence transforms the world conic into its axes and resolves radii again at `0x1402F96F3`; it does not reuse the earlier principal radii or reconstruct coefficients from those rounded radii. Contact 55 of case 5 has identical back matrix/unit point but the omitted transformation changes one radius by one ULP. Keeping the actual conic value restores the original contact words.

Original `0x140325FC0` publishes horizontal section radius as `radius * normal.z`. Whole case 11 produces three negative horizontal axes during phase 7, with positive vertical radii. Original `0x1402F8C60` consumes them and returns no contact in all three cases. The particle validator now admits this signed, nonzero axis; collider and recovered principal radii remain positive, and zero section axes remain rejected.

A controlled diagnostic ran the original update with live Darwin primitive returns and the real C++ contact function. It reproduced all 23 differing particle words through the former failure, without changing the solver or looking up captured outputs. This was a diagnosis only; original expected captures are unchanged.

Private isolated ASan/UBSan receiving passed the 2,882 primitive words, 75 bit-exact native contact cases, default 3-case / 24-phase replay and full 12-case / 20-tress / 96-phase replay. Worst scaled error is `7.56979e-6`; maximum absolute error `8.58307e-6` belongs to a value whose allowed bound scales above one. The original per-word tolerance was not modified. This is numerical component evidence, not whole-game or GPU parity.

The coherent public 15-suite regression and unchanged full 12-case trajectory replay also pass. The earlier 137-entry resource sweep completed all 40 containing Emote owners through six CPU phases and retirement. Its successor now completes every main/difference label with a fresh actual owner: 3,283 timeline cases and 19,698 CPU phases, zero rejection. These sampled diagnostic inputs reach the recovered math through the actual player/PartsTree/physics chain; they do not establish full animation duration, gameplay timing or native pose/GPU pixel parity. See [timeline coverage](emote_timeline_coverage.md) for the sealed receiving evidence and its historical input-provenance limits.
