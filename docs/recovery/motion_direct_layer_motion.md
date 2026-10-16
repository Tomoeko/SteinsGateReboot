# Direct child Motion recovery

Target `sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.

Original `0x1402769A0` checks detached ownership before strict name conversion, performs the actual recursive layer lookup, and returns null for missing or non-kind-3 records. Kind 3 calls `0x1405195A0` for the existing child and constructs a fresh borrowed wrapper through `0x140273300`; it creates no player task or listener. Constructor `0x14022BCE0` retains the same containing Layer and child pointer, with ownership flags cleared. Destructor `0x140273990` unregisters the wrapper without destroying that child.

Eighteen original-instruction cases cover detached/string precedence, missing and wrong kinds, fresh aliases, allocation failure and wrapper release. They execute 4,510 original dispatches across 418 byte-verified PE sites. Full recursive lookup, VM class creation and allocator boundaries remain prepared dependencies; these cases do not establish their complete fidelity.

The C++ binding uses the existing scene map and actual child borrow. Independent VM fixtures cover alias state, exact names, empty children, clear and containing-Layer retirement. Recursive lookup through an already borrowed wrapper remains outside this recovered root-task entry point.

Original 2D setter `0x14027FD40` forwards the existing wrapper+E8 child to `0x14051EEE0`. The borrowed-child route uses the same recovered coordinate kernel and real containing task's mutation/generation guards. It preserves child Z and completed supplied pose until the next update. Independent VM tests cover aliases, ignored extra arguments, numeric conversion, separate empty-child coordinates and clear invalidation; unsupported borrowed 3D/scalar setters retain their existing gates.
