# Per-voice volume and loudness policy

Target: `STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Read-only executable analysis uses explicit Binary Ninja view `candidate:view:1:UEU`.

Native `38d2e0` calls selector `389b50` with one trailing value: Integer ID, String name, or paired archive/name. Successful selectors return the value's physical stack index3/4. Numeric/string/weak volume conversion follows the actual loudness option; the callback returns one without pushing, preserving the original value cell. Mutation `3837a0` ignores missing IDs, compares the logical gain, writes cue+60, recomputes channel mixing through `383ec0`, and commits sources through `391880`. The portable owner retains that logical gain even when individual source preparation failed; it updates only active selected channels and preserves sample positions.

Native `37f380` initializes the Sound loudness byte+15d to one. Setter `38ca30→3830f0` only replaces that byte; getter `38b260→380d50` reads it. Captured setter `043b70` uses `225b00→25a420` scalar truth and returns zero values. Policy changes affect future numeric/string setting conversions and getter inversion; retained linear gains are unchanged.

Focused VM/resource tests exercise actual decoded PCM, strict selectors, original result identity, signed/zero gains, weak values, explicit units, scalar truth, policy changes and unrelated-cue isolation. These component tests do not establish native device startup, complete playback, resampling, streaming or full-game audio fidelity.
