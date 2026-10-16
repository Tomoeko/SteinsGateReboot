# Bitmap font receiving owner

The font branch of `sgre_steam.exe` at `0x1404d6510` prepares textures before releasing the original pixel allocation. The reference executable SHA-256 is `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.

At `0x1404d68b9`, the branch calls `0x1404cfb10`: copy bytes through the PSB data offset, relocate metadata and retain the original pixel pointer. It then visits the `source` array in order, constructs one MTexture wrapper per value through `0x140547ef0`, and appends each wrapper to the resource owner. At `0x1404d6e9a`, `0x1404cfba0` releases the original allocation. Independently mapped original byte ranges cover file offsets 5069988 (256 bytes), 5041936 (132 bytes), 5042080 (88 bytes) and 5071509 (11 bytes). This is static native evidence, not a new native execution.

`BitmapFontReceivingDocument` owns parsed metadata, the bitmap font model and real preload wrappers in the supplied TextureCache. Construction copies metadata before ordered decode/upload; the owning decoded input then expires. Its document retains original logical chunk ranges, while `MetadataBytes()` explicitly exposes only metadata. The cache and upload/release services must outlive this owner and its providers, and all mutation occurs on the cache's preparation owner.

The typed `BitmapFontTextureOwner` constructor keeps the receiving document alive and creates fresh source wrappers sharing the retained document/value cache entries. Providers never decode retired pixel data. Final destruction retires preload wrappers in source order before disposing metadata. The existing full-backing constructor retains its strict input check.

Private direct-source ASan/UBSan checks passed four fixture groups, including 684 truncated inputs, cache scope, upload rollback, metadata-only provider lifetime and release failures. A streamed `font/textfont12.psb` check also passed with a synthetic CPU upload service: PSB4, 266208 metadata bytes, 4059 glyphs, one 1024×1024 A8 atlas, one upload, distinct receiving/provider wrappers and one final release. These checks do not establish Metal output or full startup fidelity.

ResourceFileRead publication, ScriptResourceFileService producer selection, returned PSB owner pins, worker-to-preparation ordering and platform texture ownership still require integration. The generic derived-PSB guard stays closed; this source tranche alone cannot satisfy the pending startup font request. Native differential purge/lifetime execution and displayed pixels remain open.
