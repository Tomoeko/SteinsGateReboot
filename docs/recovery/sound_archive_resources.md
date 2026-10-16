# Sound archive resource owners

`SoundConfigurationLoads` now owns each loaded configuration's active and retiring archive resource owners. `ReadSoundArchiveDefinitions` interprets the native archive-definition fields while borrowing the original PSB `archData` values. It does not decode channels or create cues, voices, sources, an audio service, or loop/focus hooks.

The executable oracle is `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Thirty-two original function ranges, 8,570 bytes, match their section-mapped PE bytes. These are static executable observations; no native game process or playback witness was run.

| Recovered operation | Native virtual address | Portable resource lane |
| --- | --- | --- |
| Archive availability | `0x140390b90` → `0x1404cf6b0` → `0x1404ce460` | Exact key membership in the config root `archive` object; also preserve existing active archive lookup |
| First configuration selection | `0x140381080`, `0x140380ac0`, `0x140380b80` | Visit exact filename maps in their ordered traversal, using the first loaded, non-unloading declaration |
| Archive construction/request | `0x1403911a0`, `0x140394640`, `0x1403948a0` | Stable containing-config owner, one original manager reference, actual load/parse serial pair |
| Resource filename | `0x1403948a0` | Prefix + `/` + exact archive name + `.psb`; empty prefix still inserts `/` |
| Completion before status/parse | `0x140394c30` | Archive completion latch is set before cancellation, null delivery, and definition parsing |
| Loaded/loading queries | `0x140390c50`, `0x140390cd0`, `0x140394850`, `0x140394860` | Query completion independently of delivery, metadata compatibility, decoder or audio readiness |
| Archive/config unload | `0x1403916d0`, `0x140391590`, `0x140395680` | Cancel pending archive cache tasks; retain their callback owners until completion. Config's own pending load is retained without cancellation |
| Deferred retirement | `0x140391500`, `0x140390d60`, `0x140394870` | Retiring archive owners must finish before their containing config and PSB reference retire |

The connected cache task reports status 1 because its output-cell address exists, including a completed failed read with no delivered PSB. Archive completion still becomes true, while a separate failed-delivery diagnostic prevents definition access. Cancellation reports status 3, balances its original reference, and skips parsing; the reader's queued or selected I/O is owned by the real reader/cache receiving graph. Direct-file status 2/4 self-unload paths remain outside this connected-cache lane.

Native `0x140394c30` diagnoses id/spec/exact float32 version, then continues to the `voice` table. The expected archive id is `sound_archive`; compatible version is `1.02f`. `ReadSoundArchive` is an eager decoded-resource helper with stricter identity/version/format admission, so it is not substituted for this definition producer.

Definitions own device, volume, pitch, priority, group, uniqueness and channel arrays. Their channel views borrow the actual PSB `archData` values until last use. Native channel rows initialize eight pan coefficients to `{1,1,0,0,0,0,0,0}` and read only the first two resource `pan` entries; the remaining six stay zero. Absent volume remains one even on the native loudness-conversion branch. Loop/type/quality, `fmt`/`data`/`dpds` decoding, source submission and sample traversal belong to later producers. Malformed field/coercion parity remains open; safe rejection carries explicit context. Voice/channel/name work and owned field storage are checked before allocation.

This lane admits no cue/source users. A completed archive can therefore retire immediately after its load/parse captures retire. It cannot be connected to future playback by adding a synthetic reference count: native active cue nodes retain actual config/archive/definition/channel pointers, and retiring cue nodes retain those pointers until every real source callback becomes inactive. `0x140383b80` moves stopped active cue payloads to the retiring list after stop/flush requests; `0x140380970` polls native slot activity and only then releases pool slots and cue records. Archive readiness `0x140394870` requires completion plus absence of a matching retiring cue at `0x140380850`.

Private ASan/UBSan validation passes four archive fixture groups plus five predecessor configuration groups against the changed first-party source closure. Fixtures cover real reader/cache delivery, borrowed PSB identity and backing, metadata diagnostics followed by definition production, ordered config selection, pending archive/config unload, cancellation and reload of the actual receiving entry, and failed/malformed delivery. The fixture driver retires the previous reader batch before the next serial-empty gate begins a later batch. No published-core or original-resource fidelity claim follows from these checks.

Next: recover the containing active/retiring cue owner and its full source/callback lifetimes, then connect genuine platform voice creation/destruction and audio startup partial failure states. Startup spec, archive/group/mute parameter state and animation, ordinary continuous mix/commit, resampling, type2 streaming, loop/focus order and native global shutdown remain open.
