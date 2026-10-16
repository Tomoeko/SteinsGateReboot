# Affine depth and hierarchy predicate

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.

Native depth helper `0x140059660` recursively adds position-Z from the completed parent depth toward the child. It does not include ancestor offset/origin values. Native predicate `0x1400599b0` then adds the current node's offset-Z and origin-Z and compares effective depth descending.

At equal depth, a direct child's signed order chooses whether it precedes its parent: negative comes before, nonnegative after. Siblings compare their own signed orders. Different link parents cause ancestor traversal, recomputing depth at each step, until a direct relationship or common parent determines the result. This is a recovered native predicate; it does not establish that arbitrary graph/value inputs satisfy a standard-library sort's ordering requirements.

A 1,000-graph corpus covers eight nodes per forest, all 64 ordered node pairs and all eight recursive depths. All 72,000 output words match unchanged original code across 4,616,223 instructions. Only resolved parent lookup is a prepared boundary, invoked 295,727 times. Plan SHA-256: `2c006f4f1ae97eb577492849f1fc4908eccba54b27d09908c9e26fa5649f3b27`. Output SHA-256: `d1e81b0af8c911db814aa31c2caa62de539b1a6778ba0c0a0f07a28e244b55e4`.

Three independent sanitizer groups cover recursive binary32 association, separate offset/origin depth, parent/child and subtree tie order, self comparison and explicit unsafe-domain rejection. The portable helper borrows resolved immutable link records and allocates no heap storage. Node count, link depth, ancestor traversal, indices, cycles and finite arithmetic have explicit limits; these are authored safety boundaries, not native rejection claims.

Weak parent/strong child roots and link mutation/invalidation now have a separately validated link-state owner. The recovered manager list traversal is documented separately; actual manager dirty membership, AffineLayer class/task updates, callbacks and complete scene draw order remain open. Native NaN predicates are excluded from this finite subset rather than silently normalized.
