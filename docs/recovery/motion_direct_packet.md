# Direct motion packet packing

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses are original virtual addresses.

`0x14055a5b0` implements renderer mode zero. It constructs a format-4, twenty-byte vertex packet through `0x140531330`/`0x1405313f0`. A one-cell quad writes four vertices in TL, BL, TR, BR order, with no index allocation and primitive zero (triangle strip). Native `0x140531550` maps packet primitive zero to D3D topology five and primitive one to topology four, matching Microsoft's [topology enumeration](https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/ne-d3dcommon-d3d_primitive_topology). Its UV endpoints add pixel rectangle coordinates before division by physical texture dimensions. The existing portable row-major vertices plus indices `0,2,1,3` expand to that exact packet order. This preserves their measured division arithmetic; mode-two batching has a different operation order.

For a larger grid, `0x140512a30` prepares per-vertex colors, `0x1402b3ad0` builds pixel UV coordinates, and `0x1405596f0`/`0x140559960` pack vertices and sixteen/thirty-two-bit indices. Grid UVs compute `(index * extent / subdivision + origin) * inverse_dimension` in sequential float32 operations. Color interpolation uses an integer factor in 256ths, with vertical interpolation at both ends before horizontal interpolation. Native packet colors have passed the renderer's right-eight-bit color rotation. Primitive one selects triangle lists with the recovered per-cell index order.

The original packet is placed in the current 136-byte renderer state-stack row, copying current blend mode/parameter, selected physical texture, command key and filter/color, then calling `0x140532120` and `0x140531b70`. The CPU oracle verifies those copied state values, observes the downstream preparation/submission boundary, and executes `0x140531360` to release the packet's vertex/index buffers. It does not call Direct3D, bind shaders or access a device.

## Validation and remaining scope

A hash-gated unchanged-code corpus covers 1,000 direct packets: 500 one-cell quads plus 500 small meshes, 18,721 vertices and 68,706 indices, with 3,145,288 original instructions. All vertex position/color/UV words, topology and index sequences match the existing `motion_grid_draw.cpp` under strict ASan/UBSan compilation without a source arithmetic change. Semantic packet trace SHA-256: `e62230cc1db11beac641a08d44d62f6e2c3b6f73c245a465226315f7bd590bca`.

Inputs use finite evaluated positions, valid positive integer physical texture sizes and valid icon rectangles/resolutions. Material modulation is the recovered neutral value with full opacity; the corpus varies every corner color, includes uniform-color cases, and verifies the existing material/packet color rotation boundary. General material evaluation is covered separately. Measured meshes have at most eight subdivisions per axis and use sixteen-bit native index storage; the thirty-two-bit packet branch remains open. Mesh evaluation/deformation precedes this already-evaluated packing scope.

Native shared negative IDs resolve their physical texture owner before direct packing. Their stored shared float dimensions are a separate logical lookup: this direct packet asks `0x1404f8e60` for the selected physical texture's unsigned integer width/height. Full source-owner routing, Begin state-stack/shader coupling, alternate packet backends, filter targets, direct frame orchestration and native/Metal pixel fidelity remain open.

## Bounded large-grid follow-up

Twelve separate prepared native grids cover the index-count selection boundary and indices above 65535: subdivisions `91×60`, `92×60`, `64×96` and `256×256`, three color/geometry variants each. Packet allocation uses two-byte indices for index count 32,760, and four-byte indices for counts 33,120, 36,864 and 393,216. The threshold is requested index count `>32767`, not the largest vertex index. The largest packet includes vertex index66048, with no sixteen-bit truncation.

All 250,917 vertices and 1,487,880 decoded native indices match the unchanged portable grid packer in a fresh strict ASan/UBSan comparison. Native execution covers 37,084,708 instructions, including its original static CRT fill routine within guarded owned memory; semantic trace SHA-256 is `847ad6db6ab2ed24875ee1c4d4bf6900eb94270e4e0b0399742bd7c3aecf1892`. Renderer-owned color and UV caches are preallocated, while original packet buffer allocation/selection/fill/destruction execute unchanged. The emulation heap is eight MiB and each packet is capped at ten million instructions.

The portable grid representation uses 32-bit logical indices. This verifies the decoded index sequences and the measured native width-selection rule, without claiming identical uploaded index-buffer bytes from a Metal backend. Full GPU state/Begin/shader/source-owner orchestration and pixel parity remain open.
