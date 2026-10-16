# Native parallel task list

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses identify the explicitly selected original PE view. Reference bytes and analysis metadata remain unchanged.

The actual MParallelTaskSet protocol appends through `0x1404bfd20`, prepends through `0x1404c0020`, updates through `0x1404c0280`, draws through `0x1404c0120` and clears through `0x1404c0090`. The prepared scope uses the native owner-thread branch; its optional critical-section/worker dispatch is not implemented.

Task status is a DWORD at `+0x18`, distinct from draw-paused byte `+0x1c` and threaded-update byte `+0x1d`. Disassembly resolves misleading decompiler qword views. Status 0 updates, status 1 remains linked without updating, and status 2 skips drawing and retires during update. Draw-paused affects drawing only. Drawing also calls status-1 tasks and forwards the complete 32-bit argument. Both update and draw fetch the next link after callbacks; a callback's tail append is visited in that same traversal.

Update unlinks a retiring task before its virtual deleting destructor. Clear invokes deleting destructors in list order. Native retire-when-empty byte `+0x1f` marks the set status 2 during update only; Clear and later append do not reset or implicitly recompute that status.

`ParallelTaskSet` owns concrete task callbacks in bounded preallocated slots and preserves list order with stable indices. Handles borrow their parent set's lifetime and check owner/generation while that parent exists. Callbacks retain or borrow their actual decoded resources; borrowed services must outlive the set. Ordinary traversal itself allocates no storage. Task creation and callback-owned work have separate allocation responsibilities.

Authored safety caps capacity and traversal visits at 65,536, accepts only recovered statuses 0..2, rejects recursive traversal and mutation during retirement, and rejects prepend during traversal. Native threaded dispatch and unsafe callback mutations are not claimed. A callback failure leaves already committed task state visible. Clear attempts every retirement before reporting its first failure; destructors do not throw.

A private original-instruction comparison covers 1,000 cases and 32,000 operations with 772,824 original/boundary instructions and zero differences. Semantic SHA-256: `3e1fba76136931df78d707ab4c3ec5de936ec3e1a56e6eadb8bf9b630d205817`. Prepared task update/draw/destructor callbacks are observation boundaries; updating callbacks can tail-call unchanged native append. Complete list order, status/paused words, set status, callback order, forwarded arguments and native tail-link invariants are checked. No game process/window/audio runs.

Five independent sanitizer groups cover paused updates, completed drawables, delayed retirement, stable prepend/append order, append during update/draw, empty-set retirement, failure-completing cleanup, stale slot reuse and safety guard restoration.

Indicator destruction `0x14026c2f0` marks its task status 2 and clears script resource roots. Its later task destructor `0x14052cf20` deletes the owned font provider before elements and inherited task groups. The parent Layer owns nested task groups; exact group ordering, actual script/provider attachment, worker jobs and renderer submission remain separate integration checks. This component does not stand in for those owners or complete the native frame pipeline.
