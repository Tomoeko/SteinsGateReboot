# Motion source and icon lookup

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Addresses below are executable virtual addresses. Original resources and executable bytes were preserved.

## Registration and identity

The registration routine at `0x14051cbf0` retains documents in a vector with 64-byte entries. It deduplicates an already registered document pointer and increments that entry's reference count, preserving its original position. A new entry builds separate case-sensitive source and object maps and loads the optional root `sourceIconRenameMap`. A source record holds its icon dictionary and the texture handle returned by the renderer's texture-import callback.

`0x1404d0190` returns a project's PSB value as the pair `{document pointer, root value pointer}`. Equality at `0x1404c98c0` compares both fields. A resource filename, source name, content hash, or root offset alone is insufficient identity. The portable resource owner assigns a nonzero `DocumentInstance` to each loaded document and retains its parsed `RootOffset`; `MotionProjectIdentity` compares that pair. This is the authored representation of the recovered identity relationship, not a native pointer serialization.

The owner supplies `PreparedMotionSourceRegistry` with the live deduplicated entries in native registration order. Preparation validates the names and builds sorted source/icon/alias indexes once. Resource vectors and aliases are borrowed and must remain alive and unchanged. The prepared registry does not implement native registration IDs, reference counts, removal, or texture upload; those remain owner responsibilities.

## Lookup order

`0x1405172e0` first visits the requesting project whose document/root pair matches exactly. Each visit performs these operations:

1. Concatenate the current source name, literal `/`, and current icon name. If `sourceIconRenameMap` contains that exact key, replace both names with array elements 0 and 1. Extra array entries are ignored. Only one rewrite occurs per visit.
2. Search the registration's source map using the resulting name, then search that source's icon dictionary using the resulting icon name.
3. On failure, retain the rewritten names for subsequent visits.

If the local lookup fails or there is no matching project, native lookup visits every registered project from first to last and stops at the first successful source/icon pair. This includes another visit to the requesting project; its alias map can rewrite the names a second time. Rewrites from an earlier registration persist when its source or icon is absent. Keys are not split at `/`: slash-containing source/icon names retain the original concatenation ambiguity. No case folding, path normalization, or character conversion occurs in these operations.

`ResolvedMotionIcon` retains the selected registration index, document/root identity, source, icon, and whether resolution succeeded during the local pass. Successful lookups allocate no storage. The draw material, quad, and grid overloads accept this binding directly, so aliases do not require changing original frame names. Texture owners must use the binding's registration/project provenance; names alone cannot identify a texture across projects.

Malformed alias arrays, embedded NULs, duplicate per-document names, invalid document IDs, and bounded-work violations produce contextual errors. Native code's unchecked malformed-input behavior is not reproduced. The prepared registry requires one registration per document, matching native registration deduplication; it does not generalize the lookup's incidental behavior under a manually corrupted duplicate-document vector.

## Validation and open behavior

The unmodified original lookup and PSB helper instructions ran under the bounded Unicorn harness described in [motion recovery](motion.md). Five hundred synthetic four-document fixtures used native version-4 dictionary/name-trie structures, alias arrays, source trees, and icon dictionaries. Portable selection matched all 500 cases: selected texture registration, selected icon, and local/fallback classification. Six instruction-derived vectors are retained in the independent test suite alongside case, root, slash, UTF-8, missing-name, malformed-map, and alias-chain checks. AddressSanitizer and UndefinedBehaviorSanitizer checks passed for the resolver and binding-aware draw paths.

A streamed original motion archive scan validated all 137 resources, 426 sources, and 11,564 source/icon lookups. Two resources contain rename maps with 356 aliases total; every alias target resolves locally. No decoded archive collection was retained. The original archive hashes are recorded in [motion recovery](motion.md).

The wrapper at `0x14052a670` checks separately registered dynamic source/icon image overrides before this base lookup and has per-project cloned override maps. Their update, replacement, and removal lifecycle remains open. The native registration/texture-import lifecycle, runtime loading order, nested/reference-motion document selection, and scene-level binding traces still need integration and fidelity checks. Base lookup coverage does not establish complete asset lifetime or scene fidelity.
