# Packed color script functions

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.

Registration `0x1400455c0` installs `ARGB2RGBA` and `RGBA2ARGB` using wrapper `0x1400437e0`. The wrapper reads physical slot2 through `0x140222ff0`, retains its initialized zero when conversion fails, ignores additional arguments and returns the resulting signed VM integer word. Its captured function userdata remains a genuine extra stack value. The two leaves, `0x140044880` and `0x140049330`, rotate the unsigned 32-bit word left8 and right8 respectively. No channel scaling occurs.

The stateless first-party bindings use C++20 rotations and the recovered native integer conversion, including bool, truncation and the existing exceptional-float sentinel. Sixty-four inputs run through both original leaves:128 calls/384 byte-guarded original instructions. All128 VM results match exactly, with additional missing/null/string/bool/float/extra-argument wrapper checks under sanitizers. This combines native leaf arithmetic with executable-derived wrapper behavior and independent VM fixtures; it is not a complete original-wrapper instruction replay.

The quiet original-script run passes the environment background’s `ARGB2RGBA` call after genuine FillRect construction. It next reaches missing Capture at environment constructor194. Full-game color composition and native pixel comparisons remain open.
