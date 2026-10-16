# Text completed cursor queries

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`; read-only view `candidate:view:1:UEU`.

Registration `03CE90` installs `getRenderCurX`/`getRenderCurY` through integer bridge `039FB0` and leaves `03C830`/`03C860`. Live queries use `CVTTSS2SI` on task `+24C/+250`, embedded core `+19C/+1A0`; extra operands are ignored. Detached wrappers return integer0 without accessing the task. Existing native integer conversion preserves truncation and integer-indefinite for NaN or out-of-range floats.

These are completed cursor snapshots. Original `02F280` stores current core `+B0/+B4` at `02F2C0/02F2CC`, before final-line alignment, history transfer and block geometry. Rendering alone does not publish them; position, reveal and time controls do not replace them. Clear writes them before font callbacks (`02EF96/02EF9E`). Horizontal clear uses0; empty vertical clear uses requested width for X and0 for Y, preserving the width word. A callback failure retains that already-written state. Populated vertical rendering remains unsupported.

Independent unchanged-original evidence covers9 whole parser/done/clear cases (7,636,252 instructions),18 getter conversion cases (492 instructions), and5 empty clear/done cases (2,047,084 instructions). Whole cases use explicit actual Hiragino CFF normal24/ruby10 prepared providers; this is not a font-factory or pixel claim. Expected-only replay checks216 snapshot, working-cursor and integer-query words. Compiled receiving belongs to the integrating task.

Actual compiled `message.nut` calls these queries while evaluating borrowed child `setCoord` arguments in `setWaitCursorPosMsgwin`, line652, after `getLayerMotion("wait")` at line621. `calcWaitIconOfsX` enters `getCharacters` only for language0; English1 retains offset0 and higher language IDs use20. Character-array production stays separate.
