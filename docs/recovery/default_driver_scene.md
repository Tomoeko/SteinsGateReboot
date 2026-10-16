# Actual default SQDriver scene ownership

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Read-only static analysis explicitly targets Binary Ninja `candidate:view:1:UEU`; no new native runtime probes were made.

The default factory `0x14023b3d0` allocates a 0x160-byte SQDriver through `0x14023a340`. It has MProcess at object offset0, not the SQLayerBase Object/MProcess layout. It creates no script Layer receiver. Its +0x118 accessor `0x14022d090` is the actual owner used by Affine manager registration `0x140059740`, after deferred scene setup has initialized that group. A surrogate Layer's similarly placed common group does not establish this native owner identity.

Scene setup `0x14023b020` appends seven concrete children to embedded parallel +0x68 in this physical order:

| Order | SQDriver field | Actual owner |
| --- | --- | --- |
| 0 | +0x100 | ResourceManager, constructor `0x140247870` |
| 1 | +0x118 | ParallelTaskSet containing the global Affine priority manager |
| 2 | +0x128 | Distinct ParallelTaskSet before root layers |
| 3 | +0x130 | Distinct ParallelTaskSet before root layers |
| 4 | +0x108 | Actual root LayerManager, constructor `0x14022c0a0`, reset-renderer flag1 |
| 5 | +0x110 | MResourceMeter, constructor `0x140532490(0)`, task DrawPaused set1 |
| 6 | +0x120 | Distinct ParallelTaskSet after the resource meter |

The +0x110 task is a resource metric/indicator owner, not a renderer compositor. Its paused Draw and the root manager's genuine renderer-reset branch are separate. ResourceManager vtable `0x1405f7ef0` selects generic MProcess Update/Draw and actual onProcess `0x140249510`; ResourceMeter vtable `0x140653518` selects generic MProcess Update and actual Draw `0x1405326b0`. Their original file-service, font/indicator and loop-hook producers are not yet rebuilt here. Required Update/Draw/Retire boundaries must come from those real concrete owners; none defaults to an empty callback.

`DefaultDriverScene` owns the four distinct recovered parallel groups and composes the seven-node graph with the existing actual LayerTree root manager. Group Priority is the genuine supplied +0x118 task owner. Caller-owned actual controller MProcess and native service owners outlive this graph. Setup may publish during the controller's onProcess while its serial is empty and before embedded parallel update begins; the same native frame then traverses the newly published graph. Setup requires an unused parallel graph and empty root list. This avoids silently attaching existing script roots to a controller that did not exist when they were constructed.

Native controller onProcess vtable slot+0x30 calls `0x140081480`, which jumps through SQDriver +0xa8. Constructor phase `0x14023b2b0` arms setup `0x14023b020`; setup advances to optional startup `0x14023b2f0`; startup advances to steady `0x14023b2c0`. This source requires the actual phase operation when dispatching an established graph. It does not substitute a fake startup, scenario scheduler, class registration, clock or steady update. Native full SQDriver phase construction remains open.

Deleting root manager `0x14022cd50` nulls each child's weak owner at +0xf8, clears manager +0x138, and frees list nodes while preserving child objects. Additive LayerTree ClearRoot reuses the actual owner list and existing active-traversal clear guard. Root-attached script Layers have null owner identity already; their actual registry adds no receiver root here. The graph's native parallel retirement invokes resource cleanup, each group cleanup, actual root clear, meter cleanup and final group cleanup in physical order, including later appended tasks. Close does not delete controller serial tasks or run VM/scenario/logger shutdown.

Native full destructor `0x14023a640` shuts down scenario services and VM, deletes auxiliary +0x158 error then +0x150 print SQDriverLogger owners, destroys the still-unrecovered +0x138 aggregate and two startup strings, and destroys embedded parallel before serial. Each logger owns 100 zeroed 256-byte buffers; the actual VM print/error callbacks append at most 255 bytes and advance their cursor modulo 100. The resource meter owns its separate loop hooks. The caller must preflight the real controller/service lifetime before those phases. This stage supplies no invented retirement default, deferred-finalization policy, automatic VM closure or opaque receiver registry. Real native ResourceManager/Meter producers, class/bootstrap/steady services, shutdown coupling and full engine frames remain open.

Supported dispatch is single threaded, with stable graph adapters and live callback owners. Close rejects active graph/group/Layer traversal and unreleased transferred root leases. Only Close retires/transfers attached roots in supported integration; an external extracted lease must release while the graph and original controller stay alive. Unsafe destruction fails fast before callbacks can dangle. Explicit Close reports the first real service failure after attempting later owners; each Retire service must complete structural retirement before reporting a failure. Destruction suppresses only a failure after all callback-bearing graph nodes have retired. These are authored C++ safety/error rules, not recovered native exception behavior. Automatic current-member finalization, VM Close/GC during traversal and threaded dispatch remain unsupported.

Source-only strict C++20 syntax/warnings and ClangFormat21 checks pass. Three independent fixture groups exercise onProcess setup/same-frame publication, exact Update/Draw/pause order and separate serial ownership, transferred-root and first-error retirement, mandatory native services and actual root clear preflight. Coherent integrated ASan/UBSan and portable Release fixtures pass at285/260; native static evidence does not certify complete scene frames or resource/rendering fidelity.
