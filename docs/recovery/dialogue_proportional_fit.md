# Proportional long-dialogue presentation

This is a user-authorized improvement, enabled in the game host. It is not recovered native text fidelity. Blocks of more than three laid-out horizontal dialogue lines fit uniformly inside their original message rectangle. Shorter text and other UI retain their original fitting rules.

## Original evidence

Reference executable: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. The relevant behavior is supplied script bytecode in `wind3d11data/script_body.bin`:

| Entry | Encoded offset / bytes | Decoded SHA-256 |
| --- | --- | --- |
| `basicrender` | 81162 / 6312 | `7f9951af1ba523401554078c00fe800859c6a4c6694bce176f38df6d80edcda7` |
| `message` | 296425 / 6699 | `50e74b63e6415d2cbac069e3d60c143a8efd3106e3554a2c4349231ae54fb060` |
| `motionpanel` | 303124 / 72340 | `e314600087b2a02b0eb55a24b64e7f69455d9235bc75e9bb9c81bfd83902d1fc` |

`BasicRender._updateFit` instructions28–53 compute width and height fits independently; its height branch45–47 installs a separate Y ratio and top offset. `_updatePosition` combines those fields with the original base scale and position. `MyEnvMessage.cbOnInit` instructions0–24 select fit mode2 for the translated message and disable its maximum-line rerender loop. `MotionTextInfo._setText` retains a separate overflow/font-reduction policy for other configurations.

The actual dialogue renderer is `EnvMessage.getText("message").info.render`. The first authored selector incorrectly looked directly for `render` on the returned wrapper; the real-scene capture retained the original compressed output. The corrected receiving fixture includes this wrapper/descriptor relationship.

## Authored policy

`OriginalDialogueLayout` wraps the original synchronous `_updateFit` at module publication. It first runs that original closure. For the selected message renderer only, it then measures the complete prepared glyph queue, unions ink with logical bounds, and uses the lesser of1, the available width ratio, and the available height ratio for both axes. Existing `_updatePosition` applies the result through the original view. No font rasterization, allocation, lookup or fitting is added to ordinary frames.

Bounds include ruby, stroke and shadow from the actual prepared providers. The fit uses the whole block independently of reveal count, so appearing text does not change size. A presentation clip covers the fitted logical rectangle instead of clipping the fourth line against the unscaled native height. Native content/font invalidation retires this clip. Parsing, original line breaks, content geometry, timing, reveal counts and atlas preparation remain unchanged. Native text APIs remain the fidelity implementation; this game-level presentation policy is explicitly separate.

## Validation

Nine ASan/UBSan suites pass, including a connected script-class/text/view fixture with four and six lines, real vector glyphs, ruby, outline, shadow, partial/full reveal, fixed-height clipping, role exclusion and clear-to-shorter-text retirement. It checks equal final matrix scales, all transformed ink inside the original positioned box, stable timing and atlas reuse. Short three-line text retains native independent scaling.

The optimized public game receives the exact reported `resg01_01.ks:684` snapshot in a hidden, silent1280×720 run. The corrected four-line capture shows proportional glyphs within the same message box; the unmodified fitting capture is retained separately. The21.77-second corrected observation has stable executable/core hashes and unchanged protected configuration, saves and checkpoints. It has no gameplay exception. Forced observation retirement still reaches the separately recorded rootless `stopRingtone` failure; this is not a clean shutdown acceptance. Native-versus-Metal text pixels, every language/font and complete-game visual acceptance remain open.
