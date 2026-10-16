# String correction selection

The original `sgre_steam.exe` SHA-256 is `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.

Native `0x1402EC840` defaults `inheritShape` to true. Its branch at `0x1402ED28D` zeros full-control bytes `+0xDC/+0xDD` when inheritance is enabled. Only the noninherit branch reads `correctFix` and `correctScale`, each with a false default. Native copy `0x1402DE8E0` transports those bytes into physical-definition `+0xBC/+0xBD`; `0x1402EF485` passes that definition to `0x140309AF0`, whose stores at `0x14030B59E..0x14030B5BA` retain the flags in entity `+0x394/+0x395`.

The driver at `0x1402F2FD4` runs fix prediction only when effective `correctFix` is set and the current fix vector is nonempty. Its `0x14030DFC0` call copies predicted XY targets into entity storage; the portable output owner receives those same targets through its preparation argument.

The former port inverted serialized inheritance selection, rejected the native noninherit correction case, and gated prediction on inheritance instead of effective fix correction. The correction changes those three predicates. Manually constructed definitions that combine inheritance with enabled correction remain rejected before UID subscription. Existing zero-time and Skip fixtures now explicitly choose noninherit when they intentionally enable correction; their native vectors and assertions are unchanged.

A bounded original-instruction witness covers all 27 absent/false/true flag triples, copy and constructor flag stores, and empty/nonempty prediction gating. The metadata Boolean accessor is a prepared input service, so this witness does not validate PSB lookup/conversion or whole native construction. Synthetic metadata and actual UID tests cover suppression, defaults, independent options, moving poses, completed-frame isolation and retirement. General positive-time whole-String numeric parity and the reported hat's final visual alignment remain separate checks. The authored physical continuity policy is disabled for these native comparisons.

The unchanged decoder fails the added inheritance matrix. The correction passes five connected strict sanitizer suites; 27 original flag cases execute 135 slices, with all 44 executed sites and 203 bytes independently matching the PE. The supplied hat-brim metadata selects inheritance with both raw correction flags enabled, so the original disables both effective passes. A hidden replay checks the resulting scene separately.
