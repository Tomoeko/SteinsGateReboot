# Resource recovery evidence

Evidence date: 2026-10-12. Paths below are relative to `Files/STEINS.GATE.REBOOT`. Source inputs were opened read-only. SHA-256 used 1 MiB streaming blocks; format inspection used bounded reads and seeks. No source asset was modified or copied into this document.

## Input inventory

The supplied tree contains 121 files totaling 7,847,382,592 bytes. Excluding three macOS `.DS_Store` metadata files leaves 118 inputs totaling 7,847,358,004 bytes. The main Windows executable and accompanying DLL are x86-64 PE32+ images. There are nine info/body archive pairs and 85 files named `.webm`.

The manifest records identities of the supplied files; an extension does not establish the decoded format.

## Archive wrappers

All nine `*_info.psb.m` files begin with `6d 7a 73 00` (`mzs\0`). Their next four bytes are consistent with a little-endian declared decoded length. None of their payloads starts with the standard Zstandard frame signature `28 b5 2f fd`. The executable-derived filename-dependent XOR transform and subsequent Zstandard decoding have now been verified for all nine indexes; see [executable recovery evidence](executable.md).

Executable-derived MZS decoding is now verified in `executable.md`; the initial observations above describe their supplied bytes.

The first bytes of config, font, image, motion, scenario, and script bodies also start with `mzs\0`. The paired filenames are consistent with archive containers; recovering wrapped-object boundaries still requires the decoded index. The declared length belongs to the first wrapper and cannot be treated as the total body size. `map_body.bin` is empty. Sound and voice bodies instead start with unfiltered PSB version 3.

The [FreeMote archive documentation](https://github.com/UlyssesWu/FreeMote/wiki/PSB-Shells%2C-Types%2C-Platforms) describes MZS as a Zstandard wrapper with optional MT19937 masking, using a key plus filename seed and a repeating generated buffer. It describes info files as filename/offset/length indexes. The game-specific basename handling and 131-byte period are independently validated in the executable evidence. Preserve request spelling until the verified basename/ASCII-lowercase step, and verify decoded signatures and lengths before accepting a candidate.

| Archive | Info bytes | Info declared decoded bytes | Body bytes | First body signature | First body declared decoded bytes |
| --- | ---: | ---: | ---: | --- | ---: |
| config | 845 | 1,276 | 423,334 | MZS | 8,079 |
| font | 870 | 1,191 | 91,942,866 | MZS | 8,876,268 |
| image | 26,521 | 38,303 | 2,256,039,796 | MZS | 2,228,896 |
| map | 206 | 381 | 0 | empty | — |
| motion | 5,191 | 7,677 | 932,647,643 | MZS | 21,753,504 |
| scenario | 5,462 | 8,230 | 24,454,212 | MZS | 4,652 |
| script | 2,245 | 3,853 | 610,749 | MZS | 83,505 |
| sound | 11,525 | 21,069 | 458,716,140 | PSB v3 | — |
| voice | 204,438 | 443,508 | 322,515,702 | PSB v3 | — |

## Plain PSB observations

Both audio bodies contain concatenated PSB objects. A sequential walk using the maximum stream end (or stream entity offset for empty stream tables) reached the exact file end: 672 PSB objects in sound and 14,475 in voice. This establishes container boundaries, not playback compatibility. The first metadata object has zero streams and lists names with null values; following objects hold actual audio metadata and streams.

| First PSB field | sound_body.bin | voice_body.bin |
| --- | ---: | ---: |
| Version / flags | 3 / 0 | 3 / 0 |
| Header size | 44 | 44 |
| Key trie offset | 44 | 44 |
| String offsets / entity | 1707 / 1712 | 210209 / 210214 |
| Stream offsets / sizes / entity | 1722 / 1725 / 1728 | 210224 / 210227 / 210230 |
| Root value offset | 1529 | 166622 |
| Header Adler-32 | 0x48690507 | 0x2d0703bb |
| Trie key count | 77 | 14519 |
| Archive name count | 73 | 14515 |
| First object extent | 1728 | 210230 |

Every one of the 15,147 audio PSB objects has a 44-byte v3 header with flags zero and a matching Adler-32 over bytes `[8, 40)`. The complete content census distinguishes five `id = sound` index/group objects from 15,142 `id = sound_archive` objects; their entries contain 15,147 audio channels. The five group objects list null archive entries. The supplied sound archives use `spec = win` and floating-point `version ≈ 1.02`. A new PSB signature occurs at each recorded first-object extent.

The second sound object spans 2,336,336 bytes and the second voice object spans 18,544 bytes. Both have `id = sound_archive` and store `voice → entry → channelList → archData`, whose `fmt`, `dpds`, and `data` values reference streams. Their `fmt` streams are 18-byte WAVEFORMATEX headers. The sound sample is tag `0x0161`, stereo, 48,000 Hz, block alignment 1,008; the voice sample is tag `0x0161`, mono, 44,100 Hz, block alignment 1,487. Microsoft identifies [tag 0x0161 as Windows Media Audio Standard](https://learn.microsoft.com/en-us/windows/win32/medfound/windowsmediaaudiodecoder). A native audio backend will require a verified decoder or a documented import conversion; these streams are compressed audio, not PCM.

The [M2 PSB format specification](https://gitlab.com/modmyclassic/sega-mega-drive-mini/marchive-batch-tool/-/blob/master/psb_v4.md) describes little-endian headers, compact arrays, a key trie, UTF-8 strings, value trees, and indexed streams. A reader must check section offsets against its bounded object, validate array element widths/counts before allocating, bound trie traversal and value recursion, and reject unsupported filtering/version flags. Do not reinterpret a C++ struct over file bytes.

## Movie observations

All 85 movie files lack the normal EBML magic `1a 45 df a3` at byte zero. Bounded FFprobe checks (`-probesize 65536 -analyzeduration 0`) on `op.webm` and `ct01a.webm` failed with EBML header parsing errors. The files therefore are not directly playable as conventional WebM with their supplied bytes. A transform, encryption, or custom container is possible; no movie decoder or key has been verified by this inventory.

## SHA-256 manifest

Types are observations from file signatures or text-file extensions. `Opaque movie` deliberately does not claim an underlying codec.

| Input | Bytes | Observed type | SHA-256 |
| --- | ---: | --- | --- |
| `controller_config/cfg_ps5.vdf` | 16,536 | Text configuration | `b87e6b56acabd81e907ce3c5019d0648319545048b090ad8e7e8d2c90ea99f41` |
| `controller_config/cfg_xbox.vdf` | 16,008 | Text configuration | `161e9fe330f1089eaed7e9c95c57de2875fa6068894aabc2c63b54b1be90e90c` |
| `controller_config/game_actions_4012810.vdf` | 1,471 | Text configuration | `17e2a2366257d943b55f4936397313be42f854884b41eb19b265d73d9b5f0a59` |
| `sgre_steam.exe` | 10,457,088 | PE32+ x86-64 | `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8` |
| `steam_api64.dll` | 11,411,880 | PE32+ x86-64 | `42b8d7211866d060f342e382168f7a7bf660c9e8a294bc5028b15372a3161431` |
| `steam_settings/achievements.json` | 12,554 | Text configuration | `3d5392d09739e8d1024bf18551b0fda803f3ebc37653c0ed3b0a56364246ee60` |
| `steam_settings/branches.json` | 145 | Text configuration | `e5e8d59bf1e851d0b208e3318c3cfa3665fee649df49cca2aeae8f52fa28c6ad` |
| `steam_settings/configs.app.ini` | 406 | Text configuration | `9e598c0aa58308d85f1e654c02c1775b6035f4a3663db9df2d296c1709d340cf` |
| `steam_settings/configs.main.ini` | 681 | Text configuration | `a78aa33ac06b354e520885a7214517db3a8e1a5a4c59f4d5d6a535ad4830bda4` |
| `steam_settings/configs.overlay.ini` | 120 | Text configuration | `05965a56b54d8b78f3d2d9fbfd4f08f2e85ec9001bd122eecc12109a4f28a346` |
| `steam_settings/configs.user.ini` | 603 | Text configuration | `31293a3ae7c66def8d2434bb207505b6dde780d582a1a261a653f833feec4fb5` |
| `steam_settings/depots.txt` | 16 | Text configuration | `425a1d7545058e66afdb09915061d87af038b3e202160718c25ab6d4b082290e` |
| `steam_settings/steam_appid.txt` | 7 | Text configuration | `4f02005f4f451a8957d5be204a7040b9a58ee9cf561e7305acb9e61885f6696b` |
| `steam_settings/steam_interfaces.txt` | 555 | Text configuration | `561c3e07700ff09d7abc737c5366ff7775eaee8c614b294ded819108ab1a09db` |
| `steam_settings/supported_languages.txt` | 37 | Text configuration | `2936fd427b32b9117ff43c2c7f68c004f818d4d4d82aab363a20a6fbe0977ac9` |
| `wind3d11data/config_body.bin` | 423,334 | MZS wrapper | `e5ff7ddda031d8e5a43c4e5f33adbc14811f06ecce374ae54db891f9cb3d2015` |
| `wind3d11data/config_info.psb.m` | 845 | MZS wrapper | `cd9c0e848cf18b9936b54291be4f8b99bff86fd5ebb0aaa28e677365e71f1211` |
| `wind3d11data/font_body.bin` | 91,942,866 | MZS wrapper | `4a0104da32f8da3eeb8cff77c205cde1034a70ff5ab27cda1cfb74a8acc44114` |
| `wind3d11data/font_info.psb.m` | 870 | MZS wrapper | `a0af491d78890010eca4c98c835beb4f9035a126fc5be9064f4d1f96ae2b95ef` |
| `wind3d11data/image_body.bin` | 2,256,039,796 | MZS wrapper | `8e07f4dfd6de95c4b4cbd928266c463316533de065e521a0d272fe9c1c4924e9` |
| `wind3d11data/image_info.psb.m` | 26,521 | MZS wrapper | `804e604c450b418c152f46ea8f79ebe96a085ddb59e224cd37c1c38834564108` |
| `wind3d11data/map_body.bin` | 0 | Empty | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `wind3d11data/map_info.psb.m` | 206 | MZS wrapper | `e787c196dc64b1165f4aaed6aad17a0fee8b1cfd14ef9aac85d0057bdbc2dd3d` |
| `wind3d11data/motion_body.bin` | 932,647,643 | MZS wrapper | `a7b8b064a38f46597b97b7c6799d3ffbece0e9a86e6b037b6a80a4fdb1e63945` |
| `wind3d11data/motion_info.psb.m` | 5,191 | MZS wrapper | `699a81ba365a008e7262e824bb513bd266d0b6420069a7856b967492d5efec65` |
| `wind3d11data/movie/ct01a.webm` | 14,737,219 | Opaque movie | `931b147835372f37984ce747a2d403bce996ece1420df735cc62a8f9b2c17e2c` |
| `wind3d11data/movie/ct02a.webm` | 14,738,781 | Opaque movie | `82ef9030a302f61c182d44dab90c28493b88600c0b9cd67c78edc44e1b559516` |
| `wind3d11data/movie/ct03a.webm` | 14,719,492 | Opaque movie | `4c175bdfc4a7d39e6f7da4742584e211742c07aa2168b33743d6389fcce9e5ad` |
| `wind3d11data/movie/ct04a.webm` | 14,796,685 | Opaque movie | `b2561cca5f396f7d3cc5af2d85df4baa84ee11ecc8e59bd3448cee8f15457eb1` |
| `wind3d11data/movie/ct05a.webm` | 14,701,594 | Opaque movie | `58dd34e06c3777ded0ec03244d71a69d4a5f656aa6fe07fe67e454a0936fa169` |
| `wind3d11data/movie/ct06a.webm` | 14,743,770 | Opaque movie | `2b1b5757eba2e35a64bdce9fd02d05f0ea6692997f784951871d73bd721f753f` |
| `wind3d11data/movie/ct07a.webm` | 14,708,500 | Opaque movie | `67304e2b693fc328330bda72bb4992fff349c51c34f4658a10bc6df3d8196bee` |
| `wind3d11data/movie/ct08a.webm` | 14,834,477 | Opaque movie | `8152c535f6eca418672c4f58b4ac33acfb8b0a6c629ae51bdf1727af3f82b793` |
| `wind3d11data/movie/ct09a.webm` | 14,833,266 | Opaque movie | `b28ca977eb33200554ae7b4c62efb8caab9b74a8895b6216f877cac0f7a229c0` |
| `wind3d11data/movie/ct10a.webm` | 14,691,237 | Opaque movie | `aad85a5f26a141da0827f287cb736ade724b926867ddd15eff71779a6a630f45` |
| `wind3d11data/movie/ct10b.webm` | 14,768,194 | Opaque movie | `32c63f71073e615df05574b8d67672f8d1427430d5848af0a69b1c992359c66d` |
| `wind3d11data/movie/ct11a.webm` | 14,634,249 | Opaque movie | `625210fa416d9959b35f03381b5ca82117472813f9e005dd0fa3944eec720745` |
| `wind3d11data/movie/fake_end.webm` | 173,883,121 | Opaque movie | `366a351c4637f15a9ded3a4029f84e01de624bcde8dda877d4dcd51d42bae77f` |
| `wind3d11data/movie/fake_end_en.webm` | 173,881,273 | Opaque movie | `ad4f9a0bfe5c20958e34927f635d66ec77f85bf57c6a6d7ebda3c7c5e1aea3bf` |
| `wind3d11data/movie/fake_end_sc.webm` | 173,886,715 | Opaque movie | `c57d1e0ab4fb5f9b21ebf7f9c58df57d0e668bbed2b89a197b742740935c6883` |
| `wind3d11data/movie/fake_end_tc.webm` | 173,881,406 | Opaque movie | `62019159c4bd776319acce39218c5fd6f31df94196bd375d6ebce40a88c9ccab` |
| `wind3d11data/movie/faris_end.webm` | 126,929,377 | Opaque movie | `60e411f71ce6ce610b19a6e0ee2e328963815ef5e5022b65962a664e9e6ce1a0` |
| `wind3d11data/movie/kurisu_end.webm` | 130,461,228 | Opaque movie | `40d8f1769c30483f93b7a38378a61c3d0da899faca786b19980642e3748e7c47` |
| `wind3d11data/movie/luka_end.webm` | 126,784,339 | Opaque movie | `674f97d37a91383a2f759f065ce578d37da2f0e89db39750384c9def3582be61` |
| `wind3d11data/movie/mayuri_end.webm` | 126,555,083 | Opaque movie | `4c5cd78a3430dd86011537b469070db63e4d79956ebcc5457ce62d92c3d5fd1b` |
| `wind3d11data/movie/moeka_end.webm` | 126,577,886 | Opaque movie | `3fbb050fc518dbaddbedb0705cc9c90ea15b1a380bcdbf28698687a447cc1425` |
| `wind3d11data/movie/op.webm` | 115,112,374 | Opaque movie | `3433cf7883a2a6cec8fe3d21b62d6e7ed31c1b47f1d34326904476be8bd4f3d5` |
| `wind3d11data/movie/prologue01.webm` | 136,101,921 | Opaque movie | `9d1dfd8c957bc92a868fda13abd8f8d59f4700c1ffca5d6c824fe05f4837b5d3` |
| `wind3d11data/movie/prologue01_en.webm` | 136,102,791 | Opaque movie | `ce895836c14543a7b6443a383fc0ca2a592ce90526077518161bc1099dfcf5a3` |
| `wind3d11data/movie/prologue01_sc.webm` | 136,072,788 | Opaque movie | `2b42f68cb6f9ad15f3d74ae04bd541957d8f41e9b8c5dd2f2c0d9e3bf1ca270a` |
| `wind3d11data/movie/prologue01_tc.webm` | 136,127,522 | Opaque movie | `0c38d66840a851b5f66c3932bd7328a06ae5f9858f9030b59c450d1cf8cc7f0b` |
| `wind3d11data/movie/prologue02.webm` | 57,693,612 | Opaque movie | `47f5b38c9dff827c114d7bece9531bd98a8af4d6191d446b7a5bd053303940fd` |
| `wind3d11data/movie/prologue02_en.webm` | 57,874,317 | Opaque movie | `bc5677d364ecc7b6b8ff8d2e7bf195250f20fd645ef851d07767e25e7ada17bb` |
| `wind3d11data/movie/prologue02_sc.webm` | 57,845,344 | Opaque movie | `211ec5dd76bcbabf270f204c677bece4c723b973fdcbd5cc3dd86af5d5d2c2d7` |
| `wind3d11data/movie/prologue02_tc.webm` | 57,853,518 | Opaque movie | `a926d77613dad94bc89d2c15fb253c82b261570ad6ee312e6277120b8b9adc42` |
| `wind3d11data/movie/prologue03.webm` | 105,347,872 | Opaque movie | `c0e7052a6523edd463c456354314859e1adbd6c86333729617cac03e05534e55` |
| `wind3d11data/movie/prologue03_en.webm` | 105,369,120 | Opaque movie | `346973afc70afca371b54a3002eb13f26a18d1e8b857795a6c2ff9ac0b5e08e0` |
| `wind3d11data/movie/prologue03_sc.webm` | 105,344,036 | Opaque movie | `e560aef5542484331c903098b723b32b4b2234d9e3e02df1f4624c4464f1e87e` |
| `wind3d11data/movie/prologue03_tc.webm` | 105,386,594 | Opaque movie | `84117c0638ef7772d0bea37fef978e19d417a8dc05a2abefecfc7962d74be75a` |
| `wind3d11data/movie/sg_titlebg.webm` | 234,734,338 | Opaque movie | `b5169e4504ea8604a4ca2b019a6f78df18bda1bdc3441ea38656167a8640f929` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt01.webm` | 10,683,233 | Opaque movie | `72695cafc6784237d6c244a77bf3d6cda402a593079265aae8cf1756e4e29652` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt01_en.webm` | 10,683,012 | Opaque movie | `b31ca5386b054e2feff1ac29a0f845cf1e39e792999f22bdba7667c97a882eef` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt01_sc.webm` | 10,697,792 | Opaque movie | `a3b532948874bdf45f61389df5362e7e73102b70cb96aeaa340908b12f11cfcb` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt01_tc.webm` | 10,686,329 | Opaque movie | `8b0994aa5e015b95ad2792f8818342d09a8b63ba0cd288e180557d62e4c4314e` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt02.webm` | 5,544,468 | Opaque movie | `0dde1e254d6b4ff39527a33463f9f34634a9a5f607478b512e6783c7a278c36b` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt03.webm` | 5,537,361 | Opaque movie | `0d4d3bc2c2aef8b5ac910d57398b794b5b35ee7375d3245c8969ea53d82852b4` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt04.webm` | 5,545,119 | Opaque movie | `99108b1cae695e613bd8f0f5438d92bbe444fcd23defd545a0eb33067420a0ef` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt05.webm` | 5,546,062 | Opaque movie | `95a62baa3e3fb87a7de16a1a55b2e6f53a0242d985c2c43af9acf74cc1614975` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt06.webm` | 5,535,451 | Opaque movie | `db018dc4a5edf4cba31f982fbe75e0b9aeb40e6de77f13930637b0246ba534ea` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt07.webm` | 5,544,964 | Opaque movie | `ccb5d0d212ac23d1c4fc8a9f39de588ec67c9e55e4d5f6b6900d032262253b3f` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt08.webm` | 5,538,340 | Opaque movie | `d0abd8de7e19948fdec5549b622b40f0f7d5a7a686111b21f87a43b44b176d87` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt09.webm` | 5,547,056 | Opaque movie | `d67b8f724e3476ad246b398d2ea4112aff16f1bcf2aaa1831aab403254e217e7` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt10.webm` | 5,535,394 | Opaque movie | `d7e9bd277c76ceb67a6d330216a17d764a152c1d1b78175e2c1f6764718e7f91` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt11.webm` | 5,537,640 | Opaque movie | `a0a1f9c6dfa5af07c592ea867243f7d04af6ef32ff33fb90970f0bca84e9b1f6` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt12.webm` | 5,538,220 | Opaque movie | `781b187fc8ce76d6219bcc517bac2cbeac2800756c8ea15cecd09207bba741fa` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt13.webm` | 5,540,076 | Opaque movie | `5c7603eb05a2a63b5a02e37c9830be41b238c01e5e8aa710d3f7e18fc8a054a4` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt14.webm` | 5,539,878 | Opaque movie | `089efad3327d910b555559e16a8f769c33ca47d3f4a60d354c988009f6f419f4` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt15.webm` | 5,538,056 | Opaque movie | `9f7fe1a60d89467ef53c5f34998cca227e667e64da77d364131480c8fe4eeec6` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt16.webm` | 5,536,415 | Opaque movie | `7bffe67b55b4b7cf331fc2a92d2d69a1c7973a75cbeaa587f29d6635d5c4431c` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt17.webm` | 5,545,586 | Opaque movie | `0fb68d9ba5f627929d4c0603373e4f4be9db61c9cf95df14f130faaa8e9740ae` |
| `wind3d11data/movie/sgreboot_worldlinechange_pt18.webm` | 10,605,367 | Opaque movie | `6f2bed58aee1d15f2642a126f33368ff6577c629ebd4997a76846ab0017e80c6` |
| `wind3d11data/movie/suzuha_end.webm` | 126,690,822 | Opaque movie | `d660b9673a1c3a8771e9406f81ac4a17d2f78db3b946d1e0c2707609242fff3a` |
| `wind3d11data/movie/timeleap_10_01a.webm` | 5,195,663 | Opaque movie | `08270b7fd97abc7e45d530ed6194f8cbb90ed63a6a8d3ad9136cba588d8dd9d7` |
| `wind3d11data/movie/timeleap_10_10ka.webm` | 5,250,661 | Opaque movie | `18fd47ee87e9b26f168d2b08b32e3736bacedb2638e95d5dbb1d6f2ced97b0a7` |
| `wind3d11data/movie/timeleap_10_10kb.webm` | 2,483,482 | Opaque movie | `57d4db48605017ba3882fb1120c62e73a7da3f70affad5333f91c265a17a74cd` |
| `wind3d11data/movie/timeleap_5_17a.webm` | 5,219,388 | Opaque movie | `3e1e0a8be563c64c0e2f4352d904888467db4e643acff43bd63d9983e190be56` |
| `wind3d11data/movie/timeleap_6_04a.webm` | 5,186,802 | Opaque movie | `3064e922756888541136076637fbca961808184f1c1496a305352bd6461723aa` |
| `wind3d11data/movie/timeleap_6_07a.webm` | 5,276,582 | Opaque movie | `bd380e6522653aee0e685cc5f28561eec0f672e96c2f751c64bd8b289e38e246` |
| `wind3d11data/movie/timeleap_6_08b.webm` | 5,215,667 | Opaque movie | `2c3ae45e07865f7a399ef9c9f80fee708712c55ec20a1d26d53d488525327790` |
| `wind3d11data/movie/timeleap_6_09a.webm` | 5,259,049 | Opaque movie | `ea1f9702572b433c5fdf2c2385de4fb6c73d56c0e5aa9c3ef99a69c114d3c84e` |
| `wind3d11data/movie/timeleap_6_10a.webm` | 5,240,151 | Opaque movie | `c213d9d68df2ea7b9a532087bcfe6357e3bd14b8b0c06f6eca3e8e33b97dde6c` |
| `wind3d11data/movie/timeleap_6_10b.webm` | 5,228,825 | Opaque movie | `25ac8d997224407e7984b3b47fad52af25ec2fce30b7455ca5f9fbf338e26c61` |
| `wind3d11data/movie/timeleap_6_11b.webm` | 5,271,804 | Opaque movie | `1ef13133dc6c44a746b77504d1675d71c3e51b2fca4d9cf3cab1dc1e7f23a4dd` |
| `wind3d11data/movie/timeleap_6_12a.webm` | 5,207,300 | Opaque movie | `d4610bf0cde3898813e61223e1921c9805725f96ec4b89977fa826a580af72e3` |
| `wind3d11data/movie/timeleap_6_16a.webm` | 5,215,576 | Opaque movie | `d6a3109e0e7c1073af7777b2cfdda94ed1dbbff46779568a88e4c4fa41ac5b01` |
| `wind3d11data/movie/timeleap_6_20a.webm` | 5,287,827 | Opaque movie | `1f20c10efbf9b733042fd75e137e1f7b92b22792475719f1ea8473b69084dfa9` |
| `wind3d11data/movie/timeleap_6_23a.webm` | 5,169,084 | Opaque movie | `f42316f9f3c9fc4c552faae393460c16ad76febb9eaf5427d35852deac9d01be` |
| `wind3d11data/movie/timeleap_7_04a.webm` | 5,210,727 | Opaque movie | `4cdd351bc1f4c7a84880d4b58184ca47a5749662fdc9c7124e40ca2a7f75be16` |
| `wind3d11data/movie/timeleap_7_05a.webm` | 5,195,112 | Opaque movie | `24194ffaf8514ce1a1e1aef8967729bdc634fdd56cdecf8c08cdeffa647b1e53` |
| `wind3d11data/movie/timeleap_8_03a.webm` | 5,187,660 | Opaque movie | `36bfec42f5d7128f0e9ae4903169c3aa7d39a1e45b12d0b6bcc0d15d1905cdfc` |
| `wind3d11data/movie/timeleap_8_03b.webm` | 5,251,518 | Opaque movie | `5522902d1b5a56082703449ea93eb9dd8eae88444dc47bb7b2d5011443a943b1` |
| `wind3d11data/movie/timeleap_8_06a.webm` | 5,254,484 | Opaque movie | `324a6b40fc3cb6134e51363fd1d026c378542feace9db440bca7dfcceafb6964` |
| `wind3d11data/movie/timeleap_8_10a.webm` | 5,191,518 | Opaque movie | `b0b7407a586139260aceda1016a8b4ce1bffb6b1ff5b80db97c137a4267ef714` |
| `wind3d11data/movie/timeleap_9_03a.webm` | 5,204,305 | Opaque movie | `ac18d8e361fa9c4943ad4a033a641c9260333121188de3248e26a9e125ceab79` |
| `wind3d11data/movie/timeleap_9_03b.webm` | 5,221,686 | Opaque movie | `73214323d810cdccae89858b9e50ff9f4637c9f8ab69e082ac1bcbabf37d06d5` |
| `wind3d11data/movie/timeleap_9_07a.webm` | 5,227,639 | Opaque movie | `fd08785ca814a636d5ea56b0a0aac6c5494507d82a32b65538937c021517caf0` |
| `wind3d11data/movie/timeleap_9_07b.webm` | 5,267,906 | Opaque movie | `3ae8da5f3a6fcacc2aa261e75f4e8d00dcc17ae045dffe4edc849f3ed622427d` |
| `wind3d11data/movie/timeleap_9_11a.webm` | 5,238,419 | Opaque movie | `20460b9445f6ac6b8495ea63700713da6b87558be14227f766d00b1636548416` |
| `wind3d11data/movie/timeleap_9_12a.webm` | 5,234,731 | Opaque movie | `9d7d9c3c7dddcfa79321192369e94527a7d14be74600a75a826cbe5a06f348da` |
| `wind3d11data/movie/true_end.webm` | 274,027,906 | Opaque movie | `8318786de8d064b47af87f4d1ddbb3f97566b66510fa3221a0bcd54dfc4d2954` |
| `wind3d11data/scenario_body.bin` | 24,454,212 | MZS wrapper | `b2ea22c0b906ed39c981c52ee67647341916f1aa187236fb60772233b4947d5b` |
| `wind3d11data/scenario_info.psb.m` | 5,462 | MZS wrapper | `85803a0a83b19387f2105db3b05a774b7561b9ce9b290a22851e8d9dfd7f566e` |
| `wind3d11data/script_body.bin` | 610,749 | MZS wrapper | `29b44eeff7d16f396a3d5aa1e1facf0a8f21f93b7bb9d703be657154ddb3db4d` |
| `wind3d11data/script_info.psb.m` | 2,245 | MZS wrapper | `1238a0710a4c53d7d8a4befcfd8d2703614a330c51112922a9a3266a77b41553` |
| `wind3d11data/sound_body.bin` | 458,716,140 | PSB v3 sequence | `07e4e4dfb7c9577c3996a7369a353d6a6bb0982432be791f5e1cbef5714fd7a6` |
| `wind3d11data/sound_info.psb.m` | 11,525 | MZS wrapper | `9c971dd21f76232c3f90b7766cac5a035bf67b8b1b15dd47bdfae032458752c3` |
| `wind3d11data/voice_body.bin` | 322,515,702 | PSB v3 sequence | `5d255dc8657fdf9813a1b3a298b52ba449507077b2fe2e8b9a127e8b62bd6889` |
| `wind3d11data/voice_info.psb.m` | 204,438 | MZS wrapper | `3ebdf27a4e18daf6cd613e34750c59d8eb0192edf03ca7e774f8d2558af97fed` |

## PSB version 4 body sample

The checked first-party MZS/Zstandard decoder extracted the `bg_black` image entry using original lookup basename `bg_black.psb.m`. Its decoded size is 2,228,896 bytes. It has an unfiltered version 4 PSB header of 56 bytes, 15 names, five strings, two regular chunks, no extra chunks, and root id `image`. The version 4 header adds extra chunk-offset, length, and data fields at byte offsets 44, 48, and 52. For this sample, Adler-32 over `[8, 40)` followed by `[44, 56)` matches stored value `0x6c930524`.

Native loader evidence is in `runtime.md`; the C++ reader now handles both observed versions with bounds, checksum, extra-table ordering, and extra-resource reference checks. Synthetic tests include a nonempty extra entity, truncated extended headers, invalid extended checksums, references beyond the extra table, and overlap with normal tables. Header integrity is implementation validation; the original loader's checksum validation has not been established. Image pixel/layout/render interpretation remains a separate recovery task.
