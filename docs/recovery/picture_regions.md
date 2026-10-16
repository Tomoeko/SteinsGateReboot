# Picture image ranges

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`, explicit Binary Ninja view `candidate:view:2:UEU`.

The genuine Object-derived `LayerPicture` registration at `0x140050b00` constructs a scale view around an `MPictureLayer` task. This module recovers its prepared image-range operations; it does not install a script class or render pixels.

## Recovered operations

`0x1404f60a0` appends pieces to the task's region vector. It intersects the requested rectangle with source tiles in their existing order, retaining only intersections with strictly positive width and height. Each piece preserves destination bounds, tile-local source bounds, and the borrowed source identity. Source bounds use the original sign-bit negation and float addition order.

Logical width and height retain the maximum of the previous dimension and the truncated requested extent plus destination offset. A request with no intersecting tile can still expand those dimensions. The original has separate clear and append operations: `0x1404f6390` clears the region vector and both dimensions. `0x1404f63b0` tests the destination pieces after the task's coordinate translation, with inclusive left/top and exclusive right/bottom bounds.

Source tile bounds borrow the immutable Image model, which must outlive the picture task. Region preparation reserves its bounded storage at construction. Capacity and unmeasured nonfinite or overflowing coordinates reject before mutation; these are authored safety rules, not a claim about the original allocator's failure behavior.

## Validation and remaining work

The independent replay executes unchanged native append/clear/contains instructions and real Image dimension getters over prepared source tiles and preallocated output storage. It uses no substituted functions, allocator, game process, audio, or graphics services. All 1,000 cases, 8,000 operations, 16,000 hit tests, and 2,432,096 original instructions match the portable implementation. The projection SHA-256 is `a8f42bddeae8b761aea61f7ca70f0e9169d56de8331a9b229a0a2f28f5e0dd26`; the full native report SHA-256 is `ffce77559b768c483d54dfa5675429a7b2ec3df0633090ebe153e58ff1b4e98f`. Two additional sanitizer fixture groups cover cross-tile cropping, append/clear semantics, boundary hit tests, and preflight failure state.

Image PSB selection/import, shared texture ownership, wrapper-held strong Image and vertex-modulator roots, deferred task retirement, script adapters, modulated mesh drawing, shader selection, and physical pixel output remain separate acceptance checks. The native constructor uses the same full-image or explicit-rectangle append operation; it is not substituted with a RawImage or mutable RawTex owner.
