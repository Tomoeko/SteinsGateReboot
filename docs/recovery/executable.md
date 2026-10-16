# Executable recovery evidence

This is a read-only analysis of the supplied Windows executable. It establishes
resource decoding and archive lookup behavior; it does not establish full game
fidelity. The original executable, assets, and Binary Ninja metadata were not
patched, renamed, or annotated.

## Target identity

| Property | Observed value |
| --- | --- |
| Repository-relative path | `Files/STEINS.GATE.REBOOT/sgre_steam.exe` |
| SHA-256 | `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8` |
| File format / architecture | PE / x86-64 |
| Platform / pointer size | Windows x86-64 / 8 bytes |
| Preferred image base | `0x140000000` |
| Entry point | `0x14056eacc` |
| Analyzed address range | `0x140000000`–`0x140b0d000` |
| Analysis backend | Binary Ninja 6.0.10601 Personal, headless MCP |
| Completed analysis snapshot | Idle; 15,586 functions, 23,398 symbols |
| Analysis date | 2026-10-12 |

All addresses below are virtual addresses in the preferred PE image, not file
offsets or ASLR-adjusted runtime addresses. Auto-discovered function counts are
analysis results, not counts of application-owned source functions.

The PE view was explicitly selected with its type-qualified handle for every
native target-dependent query. Legacy xref queries also passed the stable view
selector. Other binaries already open in the server were not inspected.

## Native subsystem boundaries

| Evidence | Implication |
| --- | --- |
| `D3D11CreateDevice` import at `0x1405bd8a0`; `D3DReflect` at `0x1405bd048` | The supplied renderer uses Direct3D 11 and compiled shader reflection. |
| `MFStartup` thunk at `0x14056f6bc`, Media Foundation imports | Windows media handling has an operating-system dependency. |
| `DirectInput8Create` at `0x1405bd058`, XInput imports at `0x1405bd888` / `0x1405bd890` | Input has Windows-specific device integration. |
| `CreateFileW` at `0x1405bd180`, `ReadFile` at `0x1405bd198`, `GetFileSizeEx` at `0x1405bd320` | Asset reads use Win32 file handles. |
| `SteamAPI_RunCallbacks` at `0x1405bd900`, Steam initialization at `0x1405bd908` | Store integration is a distinct native boundary. |
| `CryptDecrypt` at `0x1405bd030`; caller `0x140553520` | An AES facility exists separately from the archive metadata XOR path. |

Import namespaces include KERNEL32, USER32, GDI32, ADVAPI32, D3D11,
D3DCOMPILER_47, MFPlat, MFReadWrite, DINPUT8, XINPUT1_4, WINMM, ole32, SHELL32,
NETAPI32, IPHLPAPI, PROPSYS, and steam_api64. Imports alone do not establish every
runtime code path or identify which bundled codec implementation is active.

The main code section is `0x140001000`–`0x1405bb950` (6,007,120 bytes). Read-only
data is `0x1405bd000`–`0x140964782`; mutable data is
`0x140965000`–`0x140a9d3ac`. PE resource and relocation sections are present.

## Bootstrap and archive mounting

The application bootstrap is `sub_140048330`.

| Call site / data | Observed behavior |
| --- | --- |
| `0x1400483da` | Constructs the private archive seed string. |
| `0x1400483ea` → `sub_1404d77a0` | Stores the seed and a keystream period of `0x83` (131 bytes). |
| `0x14004843e`, `0x140048453`, `0x140048465` | Constructs `script_body.bin`, `script_info.psb.m`, and mount prefix `script/`. |
| `0x140048480` → `sub_1404d5e30` | Enqueues that archive mount. |
| `0x1400485cb` → `sub_14023b350` | Loads `script/main.nut` before starting `ADVENGINE`. |

`sub_1404d6360` processes queued mounts: it requests the archive metadata through
the file-read path, then calls `sub_1404d5760` at `0x1404d63a0` to register the
result. `sub_1404d5760` reads the PSB fields `id`, `version`, `file_info`, and
`expire_suffix_list`. Its version checks compare against floating-point 1.0 and
refer to `marchive` and `m2lib` in diagnostic strings. The expected `id` points to
the literal `archive` at `0x140607a40`.

`sub_1404d70f0` resolves a mounted request. It removes the mount prefix, applies
an applicable suffix from `expire_suffix_list`, and looks up `file_info` by the
remaining key. It reads array element 0 as the archive byte offset and element
1 as the byte length. `sub_1404db950` opens the body file and supplies the offset
to an overlapped `ReadFile` request. Consequently, metadata offsets are relative
to the associated body file.

## Recovered metadata decode algorithm

`sub_1404d4960` provides the filename-dependent XOR stage. The native path is
confirmed by its call from `sub_1404d43d0` before decompression. For the observed
MZS metadata files:

1. Take the filename after the last `/` or `\\` separator and lowercase it.
2. Concatenate the private archive seed with that basename, including `.psb.m`.
3. Compute MD5 of those bytes. The hash initializer, update, and finalizer are
   `sub_1404fc350`, `sub_1404fc040`, and `sub_1404fc150`.
4. Interpret the 16 digest bytes as four little-endian 32-bit unsigned words.
5. Seed MT19937 through `init_by_array(words, 4)` at `sub_1404f97b0`.
6. Generate 131 bytes from MT19937 output words, taking each word's least
   significant byte first. The generator is `sub_1404f9c80`.
7. Preserve bytes 0–7. XOR byte `8 + i` with keystream byte `i % 131`.
8. Require an MZS header (`6d 7a 73 00`) and read its little-endian 32-bit
   uncompressed byte count at offset 4. Decompress the decoded Zstandard frame
   beginning at offset 8 and validate the output length.

The MZS branch is `sub_1404d79a0`, called by `sub_1404dbe50` when the separate
MDF branch does not apply. The literal `mzs\0` is at `0x14064f850`.
`sub_1404d79a0` allocates the header's output size and invokes `sub_14054f330`.
The alternate literal `mdf\0` is at `0x14064ff88`; its branch uses the zlib-style
wrapper `sub_140552870`. All nine supplied `*_info.psb.m` files use MZS.

The MT initialization constants are `19650218`, `1812433253`, `1664525`, and
`1566083941`, with 624 state words. The twist table at `0x140651fe8` contains
`0` and `0x9908b0df`. Disassembly of `sub_1404f9c80` confirms logical right
shifts and standard MT19937 tempering. Its compiler-rearranged pre-shift masks
`0xff3a58ad` / `0xffffdf8c` correspond to post-shift masks `0x9d2c5680` /
`0xefc60000`. Unsigned 32-bit wraparound is required during initialization.

The executable uses locale-dependent `tolower`; all observed archive metadata
basenames are ASCII. Applying ASCII lowercasing to arbitrary non-ASCII asset
names remains an unverified compatibility choice.

### Independent validation vector

For `script_info.psb.m` (seed and derived-key values are retained only in ignored local research settings):

| Item | Value |
| --- | --- |
| MD5 input | Private archive seed followed by `script_info.psb.m` |
| Decoded frame prefix at offset 8 | `28b52ffd600d0e9d4500` |
| Uncompressed bytes | 3,853 |
| Uncompressed SHA-256 | `a1963808379020ca57ab7f9f1a55f189bc9cdb2d27cb289ea6e70d315a6e0a6a` |

A small independent analysis script reconstructed the MD5/MT19937/XOR path and
used a locally installed Zstandard command-line decoder as an analysis oracle.
It successfully decompressed all nine supplied metadata files to the exact
header-declared lengths. This validates the resource algorithm and test vectors;
it does not imply a dependency on that tool in the rewritten runtime.

| Metadata basename | Packed bytes | PSB bytes | SHA-256 of uncompressed PSB |
| --- | ---: | ---: | --- |
| `config_info.psb.m` | 845 | 1,276 | `dbc8e5fd2d3d92b3a7dc421eda778715f949b144ca1ffe603da919f1d15b72aa` |
| `font_info.psb.m` | 870 | 1,191 | `04fd5225e89d0a76b5690ca86003c1a968f395c30cfc51c6f980e3d6d8ff0bfd` |
| `image_info.psb.m` | 26,521 | 38,303 | `15bd1ef10500d3aca8929ee7ea5d8192b993f96bbe15826abe436d28c7b59318` |
| `map_info.psb.m` | 206 | 381 | `2b5ff014da15d2e024fa59a41df47526aceb96aa83c099f87d92ed21df60dc6e` |
| `motion_info.psb.m` | 5,191 | 7,677 | `10ac98bb903cbeed4cefbc88453e1a81c1c468efea6937703ac3092585001faf` |
| `scenario_info.psb.m` | 5,462 | 8,230 | `467898f9fd88c8e09d24744a6cfed0b460695ba020a18bbc07406fa05763af06` |
| `script_info.psb.m` | 2,245 | 3,853 | `a1963808379020ca57ab7f9f1a55f189bc9cdb2d27cb289ea6e70d315a6e0a6a` |
| `sound_info.psb.m` | 11,525 | 21,069 | `f81c765f9220471862f396454a0913d9b60f90a17a28b3b420da4c3f18ed957f` |
| `voice_info.psb.m` | 204,438 | 443,508 | `370fb49f241207834bb5be663a1581debf596803fa86c743d473e3bab3c09129` |

## PSB version 3 structure

All nine decompressed archive metadata files begin with `PSB\0`, version 3,
flags 0. `sub_1404cf560` reads the version/flags and selects a 44-byte header for
version 3. `sub_1404cd530` converts header offsets into pointers relative to the
decompressed PSB buffer.

| Header offset | Interpretation for version 3 |
| --- | --- |
| `0x00` | `PSB\0` signature |
| `0x04` | 16-bit little-endian version |
| `0x06` | 16-bit little-endian flags |
| `0x08` | Header/body boundary offset |
| `0x0c` | Name trie: three consecutive packed arrays |
| `0x10` | String offsets packed array |
| `0x14` | NUL-terminated UTF-8 string data |
| `0x18` | Resource offsets packed array |
| `0x1c` | Resource lengths packed array |
| `0x20` | Resource data |
| `0x24` | Root value |
| `0x28` | Adler-32 checksum of header bytes `[8, 40)` |

`sub_1404d0400` parses a packed array. The first tag `13`–`16` encodes the
little-endian count width as `tag - 12`, followed by that count. The next tag
`13`–`16` encodes element width as `tag - 12`, followed by the elements. It
returns the complete encoded array length. Bounds and tag validation should be
explicit in the rewrite; the original often assumes valid inputs.

For names, the three arrays are `base`, `parent`, and `name_leaf`.
`sub_1404ce120` looks up a key with the double-array trie: each byte transition
uses `next = base[node] + byte`, checks `parent[next] == node`, and treats the
NUL transition's `base[next]` as the key ID. Independently reversing a name ID
works by starting at `parent[name_leaf[id]]`, collecting
`node - base[parent[node]]` while walking parent links, then reversing the
collected bytes.

`sub_1404ce460` resolves an object key through two packed arrays: key IDs and
value offsets. Value offsets are relative to the byte after the second array.
`sub_1404c9a90` does the corresponding indexed-array lookup using one offset
array. `sub_1404cacf0` resolves tags 21–24 through the global string-offset
table. `sub_1404d0190` selects the root from the header's `0x24` field.

An independent parser confirmed that archive roots expose `id = "archive"`,
`version = 1.0`, `file_info`, and `expire_suffix_list`. The empty map archive
encodes `file_info` as the PSB null value, not an empty object: a first-party
decode confirmed that field exists and has the null alternative, with root
offset 325 in its 381-byte PSB and an empty body file. For example,
`config_info.psb.m` maps `backup` to `[0, 5374]` and `cgmode` to `[5374, 1692]`.
Those are body-file byte ranges, not offsets in the PSB metadata. The metadata
contains asset descriptions and indices, not the complete application behavior.

## First-party implementation validation

The first-party C++20 Zstandard decoder was authored from
[RFC 8878](https://www.rfc-editor.org/rfc/rfc8878.html) and the
[XXH64 algorithm specification](https://github.com/Cyan4973/xxHash/blob/dev/doc/xxhash_spec.md).
It covers raw, RLE, and compressed blocks; direct and FSE-compressed Huffman
weights; one and four literal streams; predefined, RLE, compressed, and repeated
FSE sequence tables; repeated offsets; frame sizes and checksums; and concatenated
and skippable frames. External dictionaries are explicitly unsupported.

A standalone warning-clean build passed synthetic fixtures, and an ASan/UBSan
differential run passed 234 independently compressed synthetic cases. The corpus
used six distributions, lengths from one byte through 500,000 bytes, and
compression levels 1, 6, and 19. The oracle was the local Zstandard CLI; no game
content was included in these synthetic fixtures or checked into the tests.

Integrated first-party shell/Zstandard/PSB decoding successfully read all nine
local metadata indices. The decoded entry counts were config 21, font 12, image
769, map 0, motion 137, scenario 187, script 72, sound 672, and voice 14,475.
Metadata decoding and container coverage are resource validation; they do not
verify game state, script execution, media playback, or rendering fidelity.

## Remaining recovery work

- Recover and validate the script/scenario instruction semantics and execution
  state transitions; a `script/main.nut` bootstrap is evidence of a scripting
  subsystem, not a recovered implementation.
- Trace each body asset's transform/decompression path and resource schema.
- Verify rendering, motion, font, sound, video, input, saves, menus, and every
  story route against observable behavior on a supported original runtime.
- Recover PSB versions or flags actually needed by body assets, with
  negative-input tests; the observed unfiltered version 3 header checksum is
  independently validated as Adler-32 over bytes `[8, 40)`.
- Determine the AES facility's callers and format separately if those assets
  are required. Its presence does not make the MZS archive XOR algorithm AES.

The current evidence supports a clean resource subsystem implementation. A full
game rewrite remains unverified until the behavior and fidelity checks above
have passed.
