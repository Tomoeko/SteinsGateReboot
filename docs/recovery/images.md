# Image and bitmap font recovery

Evidence date: 2026-10-12. Original inputs were read only. Archive entries were decoded and processed one at a time, without retaining a decoded installation. The portable implementation uses first-party C++20 and the standard library.

## Target identity and native interpretation

The executable target is `sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses below are loaded executable virtual addresses in the explicitly targeted Binary Ninja PE view.

| Address | Recovered behavior |
| --- | --- |
| `0x1404d6510` | Dispatches PSB resources by `id` and `spec`. The image branch walks `imageList → texture → image`; the font branch walks `source`. Both use the texture loader below. |
| `0x140547ef0` | Reads texture `type`, `width`, `height`, and parent `spec`. Native Windows format mappings include `A8 → DXGI 65`, `L8 → DXGI 61`, and `BC7 → DXGI 98`. |
| `0x14054af20` | Gets the `pixel` chunk's address and length and passes the stored dimensions and format to the native texture creation function. |
| `0x1404f78c0` | Looks up an image label exactly, reads its logical width/height, and constructs tile rectangles from `left`, `top`, `width`, and `height`, retaining the corresponding `image` texture. An empty requested label selects the first image. |
| `0x1404c0990` | Constructs a PSB bitmap font, checks `spec=win` and schema version `1.08`, and reads global bounds, `code`, and `source`. |
| `0x1404c0fd0` | Looks up a character through a UTF-8 key derived from a UTF-16 code unit, then reads `id,x,y,width,height,w,h,a,b,d`. |
| `0x1404c1700` | Reports the glyph's nominal `width` and `height`, multiplied by the font's scale. |
| `0x1404c1970`, `0x1404c1d10` | Queue and emit glyph quads. Atlas `w/h` determine the sampled rectangle and drawn extent. Metric `a` determines the vertical bearing. |

These mappings are executable-derived. Microsoft's [DXGI format enumeration](https://learn.microsoft.com/en-us/windows/win32/api/dxgiformat/ne-dxgiformat-dxgi_format) identifies these values as A8_UNORM, R8_UNORM, and BC7_UNORM. The decoder preserves their sampled channel conventions: A8 gives `(0,0,0,a)`, L8 gives `(r,0,0,1)`, and BC7 gives RGBA. It does not substitute grayscale RGB or premultiply alpha. Shader channel use and blending are separate rendering behavior.

## Complete archive observations

The original `wind3d11data/image_body.bin` has SHA-256 `8e07f4dfd6de95c4b4cbd928266c463316533de065e521a0d272fe9c1c4924e9`. Its paired index has SHA-256 `804e604c450b418c152f46ea8f79ebe96a085ddb59e224cd37c1c38834564108`.

All 769 image archive entries passed MZS decoding, PSB parsing, image schema reading, and texture decoding. Processing consumed 3,617,045,700 decoded container bytes and produced 14,466,191,360 RGBA bytes transiently. No complete decoded archive or pixel collection was saved.

| Stored type | Texture payloads |
| --- | ---: |
| BC7 | 1,178 |
| A8 | 83 |
| L8 | 103 |
| Total | 1,364 |

Every observed texture has `height,pixel,truncated_height,truncated_width,type,width`; no texture has a compression or palette field. Every observed truncated dimension equals its stored dimension, and every tile extent equals its texture extent. Tiles may extend beyond the logical image, so assembly clips the padding. The sum of the clipped tile areas equals each image's logical area. This area check is not a native rendering comparison. Movie-reference entries can contain an empty `imageList`; the reader preserves that case.

Observed BC7 block modes were 0, 1, 3, 4, 5, 6, and 7. No reserved mode-zero-byte block occurred. The authored decoder also implements mode 2 and the format's reserved transparent-black result.

The original font body has SHA-256 `4a0104da32f8da3eeb8cff77c205cde1034a70ff5ab27cda1cfb74a8acc44114`; its index has SHA-256 `a0af491d78890010eca4c98c835beb4f9035a126fc5be9064f4d1f96ae2b95ef`. All 12 font entries decode successfully. Ten contain sfnt OTF/TTF fonts; two, `textfont12` and `textfont24`, contain PSB bitmap fonts with A8 atlas textures. The bitmap reader accepts all 8,118 glyph records and decodes both atlases, producing 20,971,520 RGBA bytes transiently. System-font registration and rasterization are not implemented by this portable bitmap reader.

## Format implementation and independent checks

The BC7 decoder was authored from the [Khronos BPTC format specification](https://github.com/KhronosGroup/DataFormat/blob/main/bptc.txt) and Microsoft's [BC7 mode reference](https://learn.microsoft.com/en-us/windows/win32/direct3d11/bc7-format-mode-reference). It reads bounded 128-bit blocks, applies all eight endpoint/partition/index modes, resolves omitted anchor bits, expands endpoint precision, interpolates integer channels, and applies channel rotation. Partition and anchor arrays are explicit numeric format constants. The explicit anchor tables take precedence over two inconsistent bold markers in the specification's partition diagrams: two-subset partition 35 uses anchor 8, and three-subset partition 1 uses second-subset anchor 3.

Pillow 12.3.0 was used only as an independent inspection oracle. All 8,192 deterministic arbitrary BC7 blocks, 1,024 for each mode, matched its DDS decoder byte for byte. Eight synthetic oracle vectors are retained in `tests/image_tests.cpp`; they contain no game assets. Separate tests cover block row order, partial edge blocks, A8/L8 channels, padded tile assembly, extra chunk references, unsupported types, nonfinite/fractional dimensions, truncated input, integer limits, and aggregate allocation limits. Both image and font suites passed AddressSanitizer and UndefinedBehaviorSanitizer.

The original `attention` resource starts at image body offset 0, occupies 609,095 wrapped bytes, and decodes to 2,228,896 bytes with SHA-256 `17161a9da58a0a3aee9ff44be61a9ab30e67fc11673e8f9ca341564b84cf68b8`. Its logical 1920×1080 image uses BC7 textures of 2048×1024 and 2048×64. Their decoded PSB pixel ranges are `[131744,2228896)` and `[672,131744)`. The fully assembled RGBA image matched independently decoded DDS tiles byte for byte; its pixel SHA-256 is `f240b928225ddcf8d74a5d04c5d92d51f242ec9ff2c795461472460b79fe85e5`. The temporary RGBA file was removed after comparison.

For a bitmap glyph with atlas position `(u,v)`, ink extent `(w,h)`, metric `a`, draw position `(x,y)`, and final scale `(sx,sy)`, the recovered quad is `(x, y-a*sy, x+w*sx, y+(h-a)*sy)`. The native queue first rounds `u+w` and `v+h` to float. Emission subtracts `u/v` from those rounded endpoints before scaling, then applies bearing and draw position. Its UV bounds use the rounded endpoints divided by atlas dimensions. `EvaluateGlyphQuad` preserves these operation phases; integer atlas rectangles give the familiar formula above, while fractional metadata can expose queue-rounding differences.

One thousand original queued-glyph vertex blocks at `0x1404c1de0..0x1404c1ecd` matched all 8,000 portable quad/UV words. Half the fixtures use integer atlas metadata and half valid fractional metadata; the latter discriminate endpoint subtraction from algebraically simplified ink-width scaling. Three retained native instruction vectors and existing safety cases passed sanitizers. This compares numeric geometry without claiming complete native text batch/shader fidelity. The roles of stored metrics `b` and `d` remain unverified; they are retained without invented meanings.

`MeasureBitmapGlyph` separately implements nominal sizing at `0x1404c1700`: stored `width*scaleX` and `height*scaleY`, or zero dimensions for a missing glyph. This operation ignores ink extents and bearing. All 2,000 output words matched 1,000 complete original cached-glyph measurement executions, including finite mirrored/collapsed scales. Six independent instruction vectors and invalid-metric checks are retained in the font suite.

`FindGlyphCodeUnit` implements the original one-unit lookup at `0x1404c0fd0`. Its converter at `0x1404c75d0` calls `WideCharToMultiByte(CP_UTF8,0)` on a one-unit NUL-terminated UTF-16 string. Ordinary BMP units become their exact UTF-8 keys; NUL selects the empty key. Each lone surrogate selects U+FFFD under the modern Windows [documented conversion behavior](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte). Therefore this bitmap lookup does not combine a supplementary character's two units into one scalar. Tests cover encoding-width boundaries, Japanese lookup, missing keys, and all 2,048 surrogate units. The replacement rule is derived from the observed OS call and its documented contract; it is not an original Windows text capture.

## Remaining acceptance checks

Native captures must still verify image placement, pixel-center conventions, sampling, shader channel use, alpha compositing, color transfer, and complete text layout. DDS equality verifies the resource decoding and tile cropping; it does not establish scene or GPU fidelity. Supplementary-character rendering through the complete layout/font chain, glyph fallback, and system-font shaping remain open. Motion resources reuse `ReadImageTexture`; their recovered traversal, arithmetic, and remaining acceptance checks are recorded in the motion recovery notes.
