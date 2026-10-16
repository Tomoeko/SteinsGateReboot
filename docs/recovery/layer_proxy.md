# LayerProxy ownership and drawing

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Static analysis explicitly uses `candidate:view:1:UEU`; addresses below are executable virtual addresses.

Registration `0x140293040` publishes `LayerProxy`, a genuine SQLayerBase subclass. Its only own script slots are constructor `0x140292d10` and weakref `0x140039f50`; ownership, priority, transforms, clipping and task behavior come from LayerBase. This class is distinct from [LayerDraw and its DrawingProxy task](layer_draw_bindings.md).

Factory `0x140292dc0` accepts one actual LayerBase target, optionally preceded by the existing owner argument. After owner normalization, exactly one target must remain. A weak reference argument, wrong class or detached native target does not satisfy that contract. The original factory neither reparents nor advances its target. The original transition script reparents the target to the new base before constructing `LayerProxy(old_base, target)` and then copies the source layer priority to the proxy.

The `0x160`-byte native owner retains the target's canonical weak ObjectInfo at `+0x148`. The complete `0x140227bf0` → `0x140225dd0` → `0x14024cfd0` chain creates that weak wrapper; later addref calls retain the wrapper, not the target. Its `0x30`-byte MDrawingProxy task is attached to the proxy's own camera group and stores the target's MProcess pointer.

Proxy virtual Draw `0x140292d80` resolves the weak target and checks its live LayerBase native pointer before entering its own common draw groups. The resolved temporary script value is popped before drawing. An expired target or null native pointer skips the entire proxy draw, including unrelated own common tasks. This outer lifetime gate must precede use of the child task's borrowed target pointer.

Task Draw `0x140537560` skips a hidden target or target status 2, otherwise invokes that target's actual virtual Draw with the complete 32-bit argument. The proxy camera is the outer scope; the target's own camera and derived draw behavior remain nested. The child Update is an empty base operation. The proxy still runs its own inherited Base Update; it never advances its target through this task.

Destructor `0x140244c80` releases the target weak ObjectInfo before Base destructor `0x14022c9f0` detaches the owner and retires its task graph. Portable retirement must retain that order and reject active native-owner retirement or unsupported recursive draw before mutation.

A bounded native witness passes 54 cases and 50,360 instructions over 878 independently byte-checked sites. It covers the actual factory, weak capture/resolution, temporary pop, draw predicates/order and proxy destructor. Base graph construction/destruction, external RefTable/error allocation and concrete target drawing are declared prepared boundaries; full VM allocation/GC and target destruction are not native witness claims.

Eleven independent public fixture groups exercise genuine VM/Base/Motion owners, full draw arguments, nested camera state, inherited update, weak expiration, explicit native-target release, exact weak-root deltas, parent/proxy retirement, draw failures, indirect recursion and root/child Screen constructor diagnostics. A private twelfth group executes the unchanged original transition prototype; original instructions remain ignored comparison evidence. These checks and 13 connected regression suites pass ASan/UBSan. The original Object owner already roots its canonical self weak wrapper; proxy ownership checks measure the additional weak root rather than assuming a zero baseline.

Removing a target during the proxy's own common tasks is outside the supported native-borrow domain. Generation checks reject stale dispatch and restore guards; the next top-level draw observes the detached/expired target. Portable process state currently admits the established status 0..2 values; native draw skips exactly 2 and also accepts other words. Actual transition playback, complete target-family coverage and rendered/native pixel fidelity remain acceptance work.
