# Motion preparation readiness

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`; Binary Ninja view `candidate:view:1:UEU`.

The original picture constructor creates an Emote owner from the current resource. The recovered chain `0x1402ad380` → `0x1402d4500` → `0x1402b85b0` → `0x1402c0350` resolves `metadata.base` through `0x140525880` and the current registration scan at `0x140516d00`; `0x1402c6e50` then evaluates its plain owner at zero. This finding concerns that captured path; it is not an absence claim over the whole executable.

The rewrite retained cleared players' immutable CPU metadata correctly, but its manager-wide preparation-borrow check also rejected fresh scenes when an older cleared preparation's resource had been unregistered. The actual hidden replay reached this condition while only the new project was registered; preparation was idle and the missing borrow belonged to the older selection.

Generic scenes now check their own complete preparation snapshot: the actual runtime and manager, selected project, complete registration order and every retained model identity. Selected scene contexts also retain their existing asset, catalog, model and alias-span checks. Scene copies validate the source preparation and build the destination's own current snapshot. Task copy and tick-label controls use the same actual scene readiness.

Cleared owners keep their CPU metadata without reviving textures, loaded assets or selected trees. Active dependency loss, including an unselected member of the captured snapshot, remains a failure before traversal or publication. The explicit legacy sprite API retains its strict manager-wide borrow policy and texture lookup checks. This change does not introduce a dynamically mutable source registry.

Independent fixtures reproduce clear → unregister → loaded-asset expiry → fresh selection, then actual update, draw and consumption. They cover same- and cross-manager copies, source-first retirement, complete-snapshot loss and existing queued/identity/selection negatives. A test cleanup error and private diagnostic wording mismatches are retained in research results; corrected receiving preserves the runtime guards and assertions. Quiet Metal checks cover the connected renderer. Native GPU pixels, changing active registry domains and whole-game acceptance remain separate checks.
