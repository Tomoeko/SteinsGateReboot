# Motion player velocity

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`; explicit Binary Ninja view `candidate:view:1:UEU`.

Original `0x14050c550` advances each nonzero player velocity component (`+0x1A8/+0x1AC/+0x1B0`) by the actual scaled delta at `+0x1A4`, adds the result to the corresponding retained root coordinate, and dirties that root even when elapsed delta is zero. It then multiplies velocity by `powf(attenuation, deltaTicks / 60.0f)` when attenuation at `+0x1B4` differs from one. The separate float32 division precedes original power routine `0x140581160`. Composition copies the updated coordinates; assigned-tick state zeros displacement while retaining velocity advancement.

The portable root owns velocity and attenuation across selection and Clear. Real scene contexts advance them immediately before ordinary root composition, including initially unselected nested roots and cleared/root-only updates. Default velocity is zero and attenuation is one. Unsupported nonfinite arithmetic and negative attenuation reject explicitly before partial coordinate/velocity publication.

The retained unchanged-original witness executes 24 genuine player constructors, 96 complete root compositions and retirement: 849,600 instructions at 2,212 verified PE sites. Attenuation-one words compare exactly; platform power output is checked within the stated scaled 8e-6 bound rather than claimed bit-identical. The C++ witness also checks actual Progress/Draw/Clear/Progress ownership; the focused native-vector and actual task suites pass. Full regression validation remains separate. Full kind4 emitted-child ownership is independent.
