# Native Thread.exit

Reference `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`; Binary Ninja view `candidate:view:1:UEU`.

Class registration `0x1402268a0` binds `exit` to `0x140267bd0` through wrapper `0x14004dc10`. The wrapper pops its eight-byte function capture before checking physical argument two. The member stores the optional result before actual Object completion notification, then clears waits, conditionally retires an existing compiled-file buffer and filename, clears arguments, and writes status None last. Context, callable, resume value and native call result survive. The scheduler tail prunes the existing active array; exit does not replace it with destructive Stop behavior.

The first-party binding preserves that ordering and existing Object subscriptions. Its normal sanitizer checks pass alongside scheduler and binding regressions. A retained, instruction-byte-guarded replay executes 72 original cases, 9,900 original instructions at 156 sites, including the real empty NotifyAll and ClearWait paths. Typed VM conversion, ObjectInfo access, empty-array services and buffer deletion are explicit replay boundaries. Nonempty reentrant native notification parity is unverified. Executing self-exit remains rejected; the separate global native `exit` at `0x140268280` requires its own suspension recovery.

The hidden original-script run passes the former missing Thread.exit route and reaches the actual Motion.clear boundary. This component acceptance does not establish playable startup or whole-game fidelity.
