# Curve-entry inner joins

Original sgre_steam.exe SHA-256 75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8; exact Binary Ninja view candidate:view:1:UEU.

Direct assembly: the cubic-derived common outer-join tail 0x1404339F5 flows into 4339FA/4339FF loading the center; 433A08..433A19 selects the inner side by cross sign. It writes a Line tag at 433A12 and center at 433A1E/433A22, advances both arrays, then writes another Line at 433A63 and center minus outgoing offset at 433A6F/433A73/433A77/433A7B. The tail at 433A93 jumps to 433F81. There is no next-control bbox or endpoint replacement. The quadratic branch 43003E..4300D7 has the same two inner lines.

The distinct LINE continuation 435FD0..4360DC and mirror 43690A..436A0F may instead replace its previous inner endpoint using the intersection of segment bounding boxes. Keep that path only in AppendStrokeLineSegment. Generic AppendStrokeSegmentJoin represents curve entry and preserves both center lines.

This corrects a whole-contour discrepancy: actual glyph41 at size26/edge1 had three curve-entry joins where the generic LINE optimization removed two commands each. Native expected contour and tolerance are unchanged. Root owns receiving replay; this source-only package makes no pixel acceptance claim. All prior LINE/arc/cap native captures remain immutable.
