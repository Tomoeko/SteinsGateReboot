# Motion mask pass planning

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses refer to the explicitly selected original PE Binary Ninja view. Original bytes and analysis metadata remain unchanged.

## Recovered phases

Original queue submission `0x140555610` visits eligible commands in prepared order, skipping hidden commands and zero geometry-count commands before inspecting their clip group. A changed non-null group prepares its parent chain. Table `0x140655ba8` selects low mode bits 1 then 2, with both passes visiting the supplied child before its parent. Any mode-2 ancestor initializes the reference to 1; otherwise it starts at 0.

Each mode-1 node configures its operation byte `+0x74` and threshold floats `+0x78/+0x7c`, begins the mask with the current reference, then increments that reference. Each mode-2 node configures its original minimum and float32 `(1 - minimum) - extent`, ends the mask using the final reference, and leaves the reference unchanged. Negative complemented thresholds remain negative. The final resolve receives that reference. Low modes 0 and 3 emit no node phases.

Bit 4 selects the node's member pointer range rather than its own command. Members retain their original order, duplicates and hidden filtering. The node's hidden flag does not hide its selected members. A direct node visits its own command only when visible. Neither path applies the ordinary queue's zero-geometry-count exclusion. These visits are candidates: later scissor/geometry decisions can suppress actual emission.

`PrepareMotionMaskPlan` writes these operations into caller-prepared storage without allocation or hardware calls. It borrows node/member command storage through plan consumption; settings, parents and member spans remain stable during preparation. It rejects parent cycles/range errors, nonfinite used thresholds, mismatched settings and insufficient output before writing any operation. Limits of 65,536 nodes and 1,048,576 node/member/operation work units are authored safety bounds.

The surrounding frame driver finishes masks when an eligible command changes from a non-null group to null, or when the final eligible group remains non-null. A non-null-to-non-null change prepares the next group without that finish callback. At frame end, the original resets mask control to operation 0/minimum 1/maximum 0. Those outer transitions are measured separately from this per-group plan.

## Validation and remaining work

Independent strict ASan/UBSan fixtures cover pass order, reference initialization/increments, unclamped complements, operation bytes, visible members, zero-count candidates, hidden union/direct nodes, ignored modes, empty groups and preflight failures without partial output changes.

One thousand bounded prepared frames execute unchanged original `0x140555610` plus bounds, affine, round/scissor, color rotation, ordinary quad accumulation and batch flush instructions. They include modes 0–3, visible-member masks, hidden/empty commands and mask-region clipping enabled/disabled. The authored plans plus measured outer transitions match every one of 7,113 native phase/candidate operations. The full 4,306,698-instruction trace SHA-256 is `e891aaed2c7c686e52932f91c233ceb0271904ddd68cebd01f735bda2ff984d3`. Only prepared texture dimensions and private buffer/state callbacks are substituted. No OS/GPU call executes; compact synthetic inputs and reports remain ignored under `Files/research/runtime/`.

Actual stencil/ping-pong render-state interpretation, complete command-state/clip orchestration, source-owner/frame integration, meshes/general grids, unusual blend paths and Metal/native pixels remain open. The phase/candidate comparison does not establish that all native batch packets or rendered masks match a complete authored renderer.
