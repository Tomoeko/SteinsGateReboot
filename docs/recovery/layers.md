# Layer ownership and state

Logical target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256
`75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.
Addresses below are virtual addresses in that executable, recovered read-only.

`0x1402309f0` registers `LayerBase` as an Object subclass with a forbidden
constructor. `0x14026ae10` registers `LayerFolder` as its subclass with constructor
`0x14026acc0` and `clear`. A folder owns a child manager and inherits the common
camera, clip, visibility, smoothing, priority, and owner state. The constructor
accepts no owner or one null/recognized layer owner; extra arguments fail.

The portable tree and VM adapter implement this recovered subset. No animation,
drawable layer, frame traversal, or pixel fidelity is
implied by creating the class.

| Operation | Native evidence | Recovered behavior |
| --- | --- | --- |
| Common construction | `0x14022c360`, `0x1404f4c60`, `0x1405321d0` | Visible, no smoothing, priority/center/offset/angle zero, scale `(1,1)`, clip sentinels `(1,1,-1,-1)` |
| Owner selection | `0x14022d5f0`, `0x14022d270` | Null selects the global root manager; a Screen/folder selects its child manager |
| Owner mutation | `0x140234320` | Validate destination, remove old membership, store weak owner identity, append at destination tail even when owner is unchanged |
| Clear | `0x14022dbc0` | Clear direct child owner references and manager pointers; retain child objects, without root reattachment |
| Priority order | `0x14022d4d0`, `0x1400573e0`, `0x14022d3e0` | Sort dirty managers ascending; merge chooses the existing left entry for equal priorities |
| Visibility/smoothing | `0x140234690`, `0x1402345c0` | Visibility stores the inverted hidden flag; smoothing stores the boolean |
| Camera setters | `0x1404f5dc0`–`0x1404f5e50` | Independent center/offset/scale; zoom replaces both scale components |
| Angle conversion | `0x1404f5e20`, `0x1404f5d90` | Degrees multiply float32 `0.0174532924`; getter multiplies radians by `57.2957802` |
| Clip setters | `0x140234010`, `0x1402340f0` | Store origin and separately rounded origin-plus-extent endpoints, including negative extents |
| Clip reset | `0x140233fc0`, `0x140233fd0` | Restore the disabled rectangle sentinel |

Coordinate input uses native general retrieval `0x140222a80` for point objects
and numeric conversion for separate x/y arguments. Output is a fresh x/y table.

Native float argument helpers `0x140222e90`, `0x14022b6d0`, and `0x14022ba00`
accept integer/float/boolean numeric values and leave initialized zero on failed
conversion. The boolean wrapper uses VM truth conversion `0x140225b00`.
Parent VM identities use weak wrappers separately from non-owning portable
handles. Constructor `0x14022c360` calls `0x140227ea0`, which resolves an existing
weak wrapper and weakens the resulting target through `0x140225dd0`. This
retains the weak wrapper, rather than the parent target.
Class metadata and weak owner wrappers are explicit external roots; registry identities do not own script instances. The recovered single Object release bridge invokes the script destructor before folder clear, Object notification and scene release. Independent opt-in reclamation fixtures verify last-strong parent expiration, retained child detachment, generation reuse and rooted/unrooted cycles. Broader finalizer ordering and VM-close integration remain open.

Storage and ordering scratch are bounded and prepared at tree construction.
Reparenting, clearing, sorting, and copying children into caller storage allocate
nothing. Stale/foreign handles, nonfinite state, exhausted capacity, undersized
output, and ownership cycles are explicit authored safety rejections. They are
not claims that the native executable safely rejects these inputs. In particular,
native NaN comparisons do not define a strict weak order; the portable finite
subset avoids asserting `std::stable_sort` equivalence for that case.

Independent tests exercise retained children, nested ownership, same-owner
reappend, sorted tie history, float32 angles, clip sentinels, VM inheritance,
Object properties, and failure atomicity. Stable priority order matches 206 native instruction cases and 187
remove/reappend pairs (report SHA-256
`753180942b21b6ab9377c2c2b0b204925e18b339bc3643b99e8ff6298d81d415`).
Forward/inverse coordinate arithmetic matches 2,000 native measurements, 4,000
float32 words (SHA-256
`5d5c8ef45ea11af29a6502ebeaa3535c9fc2de6893bb4d7d9c74233244556e3a`).
The local camera matrix follows center-relative scale and rotation, then a
negative camera offset, and parent matrices compose in ancestor order. A
1,000-case local-matrix comparison retains 16 differing words out of 6,000;
CRT trigonometry rounding remains open (native result SHA-256
`1a6774c05299ed4370e36afc964fc25cabf83b1ed40cecb57cad7e57360622a6`).
The actual init script initializes folder state and native InputHub/Screen adapters. With genuine configured Layer and locale services, 120 bounded scheduler ticks request 27 original scripts and reach the unrecovered Indicator dependency in basictext at instruction 2. Screen-specific manager roots are distinct from folders and do
not contribute a folder camera transform. Complete scene task flags, animation tasks,
clipping propagation, drawable classes, render order,
and native scene captures remain recovery work.

The common tree now distinguishes Folder and Motion kinds. A Motion leaf uses the recovered common constructor state from `0x14022c360`, also called by SQLayer construction at `0x14022c110`, but owns no child manager. Native owner lookup `0x14022d270` resolves Screen or LayerFolder services; `0x14022b490` checks the LayerFolder type tag. Leaf destinations are rejected before detaching current membership. Independent fixtures verify mixed folder/leaf priority order, ancestor transforms, retained leaves after folder/Screen retirement and generation/kind reset on slot reuse. This adds genuine common leaf ownership; full SQLayer task construction, class binding and frame execution remain separate open work.

The configured motion binding registers the recovered `LayerBase` → `MotionLayerBase` → `Layer` chain. `MotionLayerBase` registration `0x140232e10` forbids construction, and `Layer` registration `0x14022fbb0` selects factory `0x14022d450`/`0x14022e7d0`. Each authored leaf owns common native scene state and two actual motion resource managers through [ScriptMotionResourceOwner](motion_resource_owner.md). Configuration requires live PSB, loaded-asset catalog and shared texture-cache services; it publishes no Layer alias when those services are absent.

Recovered methods cover paired resource registration/unregistration, ordered character name arrays, short-circuit rename/remove, coordinate alignment, mask-region clipping and independent object-clip endpoints/reset. Integer arguments use native numeric conversion and initialized zero on failed retrieval; character names cross the observed NUL-terminated native string boundary. Smoothing dispatch is concrete: SQLayer vtable `0x1405f6a48` entry `+8` selects `0x140234540`, which stores the common smoothing flag, visits SQLayerObject children, then calls `0x14052c710` for both managers. The current leaf has no recovered SQLayerObject children; it updates the common flag and both source-wrapper/shared samplers. Alignment/mask setters at `0x140234240`/`0x140234290` update both managers; their paired authored properties retain the identical values. Object clip `0x1402342d0` uses separately rounded float32 endpoint additions; `0x140233fe0` resets the disabled sentinel.

Independent binding fixtures verify genuine class inheritance, mixed folder/leaf ownership, strict leaf destinations, two-manager deduplication, first PSB owner retention, ordered fresh name arrays, embedded-NUL boundaries, smoothing propagation and separate inherited/object clips. Last-strong release runs the script destructor before paired manager cleanup; resource roots survive GPU retirement and retire before common Object/scene cleanup. Explicit VM shutdown validates native cleanup through retained identities after class metadata finalization. These checks certify the implemented binding/ownership subset, not full SQLayer construction or frame execution. Native serial/parallel task sets, animation/object instances, overrides/reference projects, clip/camera traversal and renderer submission remain open and must be connected before playable acceptance.

## Derived common construction and cleanup preflight

The actual AffineLayer factory (`0x1400583e0`) calls its common SQLayer constructor directly (`0x140057a90` → `0x14022c110`), preserving its original VM argument frame. `ConstructMotionInstance` exposes that existing common owner construction to genuine derived C++ factories without invoking a separate script Layer constructor. It retains the established actual Layer resource/task/tree/Object state. This interface does not register or alias an AffineLayer class.

Derived owners call `RequireCanReleaseInstance` before retiring their native roots. The same preflight now guards ordinary Layer release: Object notifications and borrowed view callbacks must be inactive, all four task groups must permit clearing, and script resource mutation must have ended. The resource owner supplies `CanClose` from its existing mutation/closed state. These are authored safety rules; they do not claim that the original binary safely handles destructive reentry. Previously, a failed active-group clear could be caught while cleanup continued to destroy borrowed state.

Three independent fixtures exercise a real derived instance within its original native constructor frame, update/draw callbacks in each of the four actual groups, and an actual PSB texture upload callback during paired source registration. Rejected cleanup preserves every borrowed owner, and later inactive cleanup retires tasks, shared textures and Object/tree records exactly once. Full affine class/manager/matrix Process and game rendering remain separate recovery work.
