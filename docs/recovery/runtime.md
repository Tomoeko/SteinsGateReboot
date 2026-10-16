# Script runtime and startup recovery

Evidence date: 2026-10-12. Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`,
SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.
Executable addresses are preferred-image virtual addresses. Body ranges below
are file-relative byte offsets. Binary Ninja queries explicitly targeted the
game PE view; no executable bytes or analysis metadata were changed.

## Script identity and resource lookup

The scripts are serialized Squirrel bytecode, not plaintext `.nut` source.
Native base-library registration at `0x1402a50e0` installs the version value
`Squirrel 2.2.4 stable` (literal at `0x1405fc0d8`) and the character, integer,
and floating-point widths 1, 4, and 4 respectively. This is executable evidence,
not a guess based on filenames.

`script_info.psb.m` has `expire_suffix_list = [".nut.m"]`. The loader at
`0x140242ec0` appends `.m` to a requested script filename, using the literal at
`0x1405f7bd4`, before calling the resource reader. Thus the bootstrap request
`script/main.nut` resolves through `script/main.nut.m`, the archive key `main`,
and the filename-dependent XOR key for basename `main.nut.m`. Decoding with
basename `main.nut` fails the recovered Zstandard-frame check. This distinction
also applies to `init`, `startup`, and every other script entry tested.

The body file is `wind3d11data/script_body.bin`, SHA-256
`29b44eeff7d16f396a3d5aa1e1facf0a8f21f93b7bb9d703be657154ddb3db4d`.
The selected samples below remain in ignored research storage; their source and
literal tables are not copied into public documentation.

| Archive key | Body offset | Packed bytes | Decoded bytes | SHA-256 of decoded payload |
| --- | ---: | ---: | ---: | --- |
| `main` | 294911 | 1514 | 3930 | `528e8e449e53e0b568ce94c47f6012e9afe744ee29d2cac66c2b21fc45583d03` |
| `startup` | 526256 | 936 | 2654 | `e768f51ce79c60242dfa37c18024105228074fc1010a6dbce3803f3cbaca02d0` |
| `include` | 254244 | 286 | 1040 | `a7b92193661ece7de6eacfb1513d6e8268de8ea24ad7aeb0e63876f7cc73dc78` |
| `init` | 254530 | 16665 | 74011 | `8b063a410867aabf9659e6756a0e78f3db20a799d3c872020c4aa947251c00b6` |

## Recovered bytecode serialization

The native closure reader is `0x140224960`; it verifies the two-byte `0xfafa`
tag and calls `0x14024d2a0`. The latter verifies little-endian signature
`0x53514952`, a 32-bit character width equal to 1, a recursive function
prototype, and tail signature `0x5441494c`. The observed prefix is
`fa fa 52 49 51 53 01 00 00 00`. The serialized integers and floats are
32-bit despite the host executable using 64-bit pointers.

The prototype reader `0x14024d4d0` reads these sections, each preceded by the
little-endian part signature `0x50415254`:

1. Source name and function name as serialized objects.
2. Eight 32-bit counts: literals, parameters, outer bindings, local variables,
   line records, default parameters, instructions, and child prototypes.
3. Literal objects, then parameter objects in a separate section.
4. Outer records: 32-bit binding type, source object, name object.
5. Local-variable records: name object, 32-bit stack position, start
   instruction, and end instruction.
6. Line records: 32-bit line number and instruction index.
7. Default-parameter words.
8. Eight-byte instruction records.
9. Recursively serialized child prototypes.

The prototype ends with a 32-bit stack size and two one-byte flags for generator
and variable-parameter behavior; there is no further part signature before
these fields. Allocation/layout evidence is `0x14024cb20`. The object reader
`0x14024e580` recognizes null `0x01000001`, integer `0x05000002`, float
`0x05000004`, and string `0x08000010`. Strings carry a 32-bit byte length and
exactly that many bytes, without an appended NUL. String bytes are retained
without case folding or transcoding. Their complete character encoding still
needs validation before exposing every script string as portable UTF-8 text.

The VM dispatcher at `0x140255630` advances its instruction pointer by eight
bytes. The first word is argument 1; the following four bytes are opcode,
argument 0, argument 2, and argument 3. Native opcode dispatch recognizes
`0x00`–`0x3c`. For example, its integer and float immediate cases are `0x02`
and `0x03`; both use the first word as payload and byte 5 as the destination
register. These checks establish the record layout; they do not establish all
instruction semantics or make the reader a VM.

`ReadSquirrelBytecode` is an owned, bounded first-party C++ serialization
reader. It verifies tags, character width, sections, exact closure extent,
object types, opcode range, truncation, aggregate records/strings, and nesting.
It preserves names, literals, metadata, operands, and child prototypes for
further recovery. It does not execute instructions, resolve native bindings,
or validate the eventual VM's register and control-flow relationships.

ASan/UBSan validation passed independent synthetic structures, every prefix
truncation of a mixed nested fixture, invalid signatures/flags/types/opcodes,
negative or oversized counts/string lengths, unavailable records, trailing
bytes, and the accepted/rejected nesting boundary. A streamed integration run
decoded and parsed all 72 local script entries, totaling 2,942,203 decoded
bytes, 3,880 prototypes, and 139,899 instructions. The observed corpus uses
59 of the 61 dispatcher opcode values; `0x00` and `0x35` are absent. Only the
four selected decoded script samples above were retained.

The main prototype has 233 instructions, 63 literals, no child prototypes, and
stack size 12. The `init` root has 1,356 instructions, 286 literals, 84 direct
child prototypes, and stack size 14. Recovered names identify archive mounting,
screen bounds, input/action sets, rendering/motion support, language selection,
application construction, and game startup. These names guide targeted code
recovery; literal presence alone does not prove execution order.

An ignored readable trace of the main prototype establishes this static
startup order by instruction indices: 0–10 invoke the include and title scripts
and enable variable-frame mode; 11–32 install motion-resource defaults and
language/region/seen state; 37–160 parse launch arguments and retain positional
parameters; 161–185 query screen bounds and seed randomness from local time;
186–203 invoke specification setup, load exception/util/init scripts, and set
the exception class; 204–216 optionally install the debug hook and initialize
global definitions; 217–231 call `gameMain` with parameters and an optional
`init.root`. This is control-flow recovery from deserialized instructions and
native opcode cases, not a retained trace of actual game execution.

The argument branches distinguish region/language, starting scene/line/state,
seen/no-effect/skip/debug flags, and `-D` global definitions. The line argument
keeps a leading-asterisk value as text, otherwise calls `tonumber`. The
`initGlobalVariable` child handles key/value definitions and converts values
through `toint`. The `gameMain` child creates an application, initializes it,
conditionally registers it with the base-screen zoom target, calls its main
routine, and clears the root application's reference. Application initialization
and that main routine are still unimplemented.

The [official Squirrel 2.2 manual](https://squirrel-lang.org/doc/squirrel2.html)
and [official 2.2.4 release listing](https://sourceforge.net/projects/squirrel/files/squirrel2/squirrel%202.2.4%20stable/)
identify useful external references. The reader was authored from the native
loader and local byte observations; no Squirrel implementation is vendored or
linked into the runtime.

## Native startup and scheduling contract

The existing bootstrap evidence in `executable.md` identifies `0x140048330`
and its `script/main.nut` / `ADVENGINE` setup. `0x14023b350` creates the driver;
constructor `0x14023a340` creates/registers the Squirrel-facing engine modules.
`0x140226020` opens the root VM with an initial stack of `0x400`, installs
native library support, and returns the root handle. The constructor's
registration includes engine object services, threads, and platform bindings;
these still need individual recovery before script execution is faithful.

Driver startup `0x14023b2f0` calls `0x140267c60`, which resolves the root-table
`Thread` object and passes the initial filename and arguments. The per-frame
driver step `0x14023b2c0` advances script ticks then runs `0x1402686d0`.
Tick accumulation `0x1402694c0` adds an integer delta to the current tick and
stores that delta separately. Callbacks `0x1402684f0` and `0x140268510` return
those values to scripts. The delta originates in `0x1404e05d0`; its time unit
and wraparound contract remain unverified.

Thread registration `0x140268d00` installs `fork`, `exec`, `exit`, `wait`,
`notify`, and `execOnBase`, a default priority of 10, and these state values:
NONE 0, LOADING_FILE 1, LOADING_FUNC 2, STOP 3, RUN 4, WAIT 5. `exec` requires
an active thread and schedules either a filename or closure; `0x1402670c0`
distinguishes strings from non-string call targets. `wait` uses
`0x1402675c0` to retain numeric deadlines and notification targets. It must not
be implemented as an operating-system sleep: resumption and notification
behavior need to be recovered with the scheduler.

The driver constructor registers 51 named native-facing classes. Focused
registration queries establish these implementation boundaries; registration
names identify the exposed API, while their method semantics remain open unless
stated below.

| Boundary | Representative classes | Registration evidence |
| --- | --- | --- |
| Values, resources, scheduling | `Object`, `Thread`, `PSBValue`, `PSBObject`, `Binary`, `Resource` | `0x140226080`, `0x1402268a0`, `0x14022a850`, `0x14022a340`, `0x14022afc0`, `0x14024bb20` |
| Scene and motion | `LayerBase`, `Layer`, `LayerView`, `Motion`, `MotionLayerGetter`, `MotionLayerSetter` | `0x1402309f0`, `0x14022fbb0`, `0x1402320a0`, `0x140278cf0`, `0x14027d3c0`, `0x14027e250` |
| Text, images, screen | `Indicator`, `Console`, `Image`, `RawImage`, `Picture`, `Screen`, `ScreenProjection`, font classes | `0x14026cb60`, `0x14026f5a0`, `0x14026a410`, `0x1402388c0`, `0x14028fd40`, `0x140245410`, `0x140245920`, `0x14026b910`–`0x14026beb0` |
| Input, persistence, platform | `InputHub`, `Input`, `Struct`, `StructValue`, `BackupManager`, `BackupSegment`, `System`, `Steam`, `SteamController` | `0x140241f00`, `0x14023fee0`, `0x14023d810`, `0x14023e210`, `0x14028abb0`, `0x14028b630`, `0x140285f50`, `0x140398820`, `0x140398ee0` |

Additional registrations exist outside that constructor: `Sound` at
`0x140386210`, `LayerMovie` at `0x140073080`, and `DrawMovie` at `0x140073ff0`.
Their audio/movie behavior must be recovered too; the constructor inventory is
not a complete inventory of the playable engine.

Concrete native contracts: `System.getScreenBounds` callback `0x140284da0`
returns left/top/right/bottom and computes width as right minus left and height
as bottom minus top. These renderer bounds must be distinguished from the
configured 1920 × 1080 logical dimensions. Layer base-clip setter `0x140234010`
and clip setter `0x1402340f0` convert x/y/width/height to x/y/right/bottom before
forwarding the rectangle. Motion registration exposes transform ordering,
opacity/color weights, mesh-division ratio, source-icon resources, and mask
clipping; their coupling to native rendering and shaders remains unresolved.

## Configuration schema observations

`wind3d11data/config_body.bin`, SHA-256
`e5ff7ddda031d8e5a43c4e5f33adbc14811f06ecce374ae54db891f9cb3d2015`,
uses archive suffix `.psb.m`. Two selected entries decoded directly as PSB v3:

| Archive key | Body offset | Packed bytes | Decoded bytes | SHA-256 of decoded payload |
| --- | ---: | ---: | ---: | --- |
| `config` | 8874 | 1386 | 1999 | `2c8294623a8840bfe288a3617e2d12501f4b4016ba78aaff503f4a08c1661f9b` |
| `init` | 40897 | 1412 | 2333 | `1745ffc8fabf5a581ab6e751901fd93394ef02ad885d788bb12c880b22f2044e` |

The `init` root has `archives`, `constant`, `fontList`, and `saveSystem` objects.
Its constants include SCWIDTH 1920, SCHEIGHT 1080, and TARGET_SYSTEM `D3D11`.
The archive set names font, image, motion, scenario, sound, and voice. The font
list references `textfont12` and Hiragino Pro W6 OTF resources for text, Chinese
variants, and ruby. Save defaults declare six ordinary pages, eight slots per
page, six automatic-save pages, and a 284 × 160 thumbnail. These are observed
configuration values; save-file layout, language-specific overrides, and UI
behavior are still unverified.

The `config` root has an `init` object containing saved-setting defaults. These
include text/auto/skip speeds, volumes, window/screen settings, voice categories,
motion mesh/bust scales, and text/selection options. Do not map these numeric
values to seconds, gain curves, or renderer behavior until the script/native
consumers establish their units and transformations.

## Next recovery checks

Native PSB v4 corroboration found during this work: header loader
`0x1404cf560` selects 56 bytes for versions above 3. Pointer initialization
`0x1404cd530` resolves header fields 44, 48, and 52 against the PSB base; the
offset-52 load is visible at `0x1404cd6fd`–`0x1404cd726` and its pointer store
at `0x1404cd735`. Pseudo-C omits that final field, so the assembly is the
authority. Binary getter `0x1404caa60` resolves tags 34–37 through the added
offset table and added data base. This corroborates the extra resource-table
layout; it does not establish native checksum-validation behavior.

- Recover the dispatcher semantics and control flow for every required opcode;
  retain signed operands, float32 behavior, closure captures, generators,
  exceptions, delegates/classes, and script thread interactions.
- Use the retained ignored main/init/startup instruction traces to reconstruct
  startup in C++ with native-binding evidence. Do not substitute a literal-table
  scan for control-flow recovery or claim these static traces as native captures.
- Trace the scheduler's tick unit, priority ordering, wait deadlines, file
  completion, notification matching, cancellation, and exception propagation.
- Recover configuration consumers and scenario execution. Validate startup,
  menu, dialogue, and route behavior with native captures/differential traces.
  Successful deserialization alone does not complete the gameplay rewrite.
