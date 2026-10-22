# Original phone routes during reconstruction

This is an authored launch/test policy. It drives original input handlers while the original VM executes scenario commands. Candidate reachability is not proof of a route's live conditions or complete recovery.

## Reference inputs

All paths are relative to `Files/STEINS.GATE.REBOOT/wind3d11data/`.

| Input | SHA-256 |
| --- | --- |
| `scenario_body.bin` | `b2ea22c0b906ed39c981c52ee67647341916f1aa187236fb60772233b4947d5b` |
| `scenario_info.psb.m` | `85803a0a83b19387f2105db3b05a774b7561b9ce9b290a22851e8d9dfd7f566e` |
| `config_body.bin` | `e5ff7ddda031d8e5a43c4e5f33adbc14811f06ecce374ae54db891f9cb3d2015` |
| `config_info.psb.m` | `cd9c0e848cf18b9936b54291be4f8b99bff86fd5ebb0aaa28e677365e71f1211` |
| `script_body.bin` | `29b44eeff7d16f396a3d5aa1e1facf0a8f21f93b7bb9d703be657154ddb3db4d` |
| `script_info.psb.m` | `1238a0710a4c53d7d8a4befcfd8d2703614a330c51112922a9a3266a77b41553` |

Packed entries: scenario `resg01_03.ks` offset 344553/length 100600; config `phone_master` offset 260729/length 5450; scripts `motionpanel` offset 303124/length 72340, `override` offset 378074/length 46411 and `phone` offset 424485/length 35824. Source line numbers below come from original bytecode debug information; these are script/resource observations and actual rewrite traces, not native pixel comparisons.

## Navigation and live input

The keypad in `resg01_03.ks:*start` has one unconditional successor, `*select`, containing literal number comparisons. `PhoneNumbers` follows unique unconditional staging to that dispatch. It rejects unknown conditions, multiple staging exits, missing scenes and cycles. Reverse reachability excludes the input and dispatch scenes so wrong-number retries cannot make another number appear viable.

Literal phone-trigger activation commands join the 35 original `phone_master` definitions as separate candidate event links. They preserve original `nexts`; they do not set flags, propagate guessed trigger history or execute events. Original `override.nut:openUserInteractionPhone` at 1776 catches `PhoneJumpException`, disables the trigger, and passes the current storage plus the original event target to the scene loader.

For an outgoing call, reconstruction queries the actual application's integer/boolean trigger flag, original contact inventory and `getActiveTrigger` result. Only one live call that can reach the requested target is accepted. Original PSB-backed contact properties use general property lookup. The original command matcher handles its action array; toolbar queries can supply a single name. Other inputs continue through their original handlers. No function-key samples, flags or scene commands are fabricated.

Original `PhoneFunction.onPhoneMain` and `PhoneDialogFunction.onPhoneMain` remain the update owners. The controller follows actual Object delegates to the expected player/application, bounded against malformed cycles, and checks the original motion lock and button readiness. Power/root/contact controls and call-only/state 13 or mail/call/state 14 confirmation use original `getButton`, focus and `pressButton` paths. Main and nested dialog actions have separate in-flight ownership. Automatic phone owners and unrelated dialogs receive no traversal input.

Both original updates and button expressions run in containing script frames: resource expressions can wait during transitions. Their return values, suspensions and exceptions propagate unchanged. The exact typed `PhoneJumpException` for the pending target is an expected transfer; other failures stop reconstruction. The subsequent original target-scene snapshot releases the pending route and restores sequential skip input. Complete read-only suspended-stack lookup avoids mistaking a deep phone continuation for a truncated diagnostic stack; it changes no VM opcode or suspension rule.

The original panel input loop at `motionpanel.nut:6981` consumes initial movement to enable mouse mode. Independent headless pointer replay supplies bounded tracking/mode/focus movements before pressing; ordinary game timing/input is unchanged.

## Validation and remaining checks

Independent ASan/UBSan fixtures cover staged keypad routes, event metadata, ambiguity, integer trigger flags, PSB properties, delegated panel lookup, cyclic/foreign owners, readiness, nested dialogs, actual coroutine suspension, preserved callback results, reentrant scene transfer, expected typed events and genuine errors. A 41-frame suspended stack verifies inspection beyond the copied diagnostic window. Original sequential/action/yielding-release and six-script movie regressions pass.

Quiet 720p receiving previously passed New Game through the separated keypad dispatch to `resg01_04.ks:14` in 211.49s with 1,085 snapshot receipts. A later original pointer replay selected the actual active trigger and entered the call scene. A private trace confirmed the selected contact identity; the dialog caption discrepancy remains a native visual comparison item, not grounds to change an asset or invent a caption.

Automated receiving from a retained original predecessor now reaches `resg01_12.ks:361` (resolved original snapshot line 362/point 13) in 45.545s: 276 inventory-matched receipts, five original phone actions including opening, 271 ordinary post-arrival frames, exit 0, no runtime or retirement failure. Protected configuration, user saves and shared checkpoints remain unchanged. The earlier rootless forced-close failure in other live UI states remains open. Scrolled contacts, incoming/mail/reply choices, other live histories, all routes/endings, menus and native visual/audio fidelity still require receiving.
