# Emote named easing

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Read-only Binary Ninja view `candidate:view:1:UEU`.

The shared scalar evaluator at `0x14053A800` clamps the input ratio to 0 or 1 before dispatch. Easing zero is linear; values below 100 use the existing continuous power curve. Otherwise it truncates the float code to an integer and dispatches codes 101..140. Unlisted codes are linear. The forty entries are in/out/in-out/out-in variants of polynomial degrees 2..5, sine, exponential, circle, back, elastic and bounce. The bounce primitive is `0x14053A750`.

The implementation preserves scalar float operations and native constants. Back and elastic outputs can lie outside [0,1]; they remain unclamped. Exponential out-in code124 at ratio .5 returns .50048828125, and elastic out-in code136 returns .499755859375. These are literal native branch results. Bounce's third threshold compares the promoted float against a double constant. Named code101.75 truncates to101.

The native receipt executes all800 calls through unchanged instructions: all40 branches, 16 ratios including signed-zero/endpoints, and continuous/out-of-table controls. All63,623 executed instructions at1,094 sites match the executable bytes; there are no substituted math services. Expected test data contains only input/output float words. A separate primitive trace compares all321 native powf,60 sinf and28 cosf calls against Darwin scalar libm exactly. Authored named trigonometry uses the previously recovered affine SSE2 profile. The existing continuous extreme-underflow CRT error-service boundary and native FMA profile remain unverified.

The genuine variable publisher uses this evaluator for scalar, eye, eyebrow and mouth transitions. Queued selectors still start in their native phase before advancing those scalar transitions. The timeline transition owner uses the same evaluator for difference-track values and blend ratios; the existing duration, clock, interpolation and occurrence lifetimes are retained. Connected tests publish named selector/scalar and additive blend values through real parent/child Motion parameter registrations, including an unclamped back-out overshoot.

This is evaluator and owner-join coverage. It does not establish every resource timeline, complete character animation, physical trajectory or rendered pixel fidelity.
