# Unindexed Unicode position initialization

This derivative requires the frozen owned-storage stage6d695dc9. It adds `EnsurePositionRows()` as the ordinary unindexed lazy transition: call existing `ClearPositionRows()` only while positions are inactive. It returns no pointer, span, font owner or shaping result. The existing checked `PositionRow(index)` calls it only after its unchanged live/physical index guards, so an empty indexed read still rejects before mutation.

Retained executable evidence `0x1403a6d90` checks positions byte+0x82 and initializes the live auxiliary range only once, including count0; null and non-null count-output forms have the same transition. Static connection receipt5427af58 and genuine nine-owner/64-snapshot result2d5edfc5 retain this boundary. This source work performs no new native analysis or execution and does not claim native pointer ABI parity for its void/value interfaces.

Two additional source fixtures use the real default owner and public operations. They check empty activation with no arrays/allocation, unchanged checked-empty rejection, repeated active access preserving all private words, and output restart followed by shorter-prefix reclear while retaining actual cursor, sticky failure, limits, glyphs and previously written tail. The original five groups are retained. All seven groups pass at the 367/339 checkpoint using the dedicated twelve-source-unit executable boundary. The controlled nine-owner/64-snapshot comparison also passes; it compares initialized storage only.

Default advance/origin initialization, font callbacks, GSUB/GPOS application, complete shaping and renderer/application wiring remain open. This method initializes storage availability only.
