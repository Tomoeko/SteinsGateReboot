# Script scheduling and frame clock

Target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256
`75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.
Addresses are preferred-image virtual addresses. Binary Ninja queries explicitly
selected the game view. Originals and analysis metadata were preserved.

The first-party scheduler owns independent execution contexts in a shared VM.
At `0x1402686d0`, the original takes a registry snapshot and sorts ascending signed
priority, preserving registration order among equal priorities. A child created
within that snapshot starts on a later tick. `0x140267400` increments elapsed
ticks and decrements a nonnegative timeout; expiry requires a strictly negative
result. Zero is still waiting. Integer counters preserve 32-bit wrapping.

`0x140267790` starts or resumes only a RUN thread with no child, object/string,
or timer wait and no manual-step flag. `0x140268b20` supplies a completed child's
stored return value to its waiting parent; script failures likewise deliver the
stored error value, rather than implicitly throwing in the parent. Matching a
notification clears the remaining waits. The scheduler implements these phases,
loading readiness polling, bounded contexts, and checked generation handles.
Asset workers must perform actual I/O and decoding before a readiness poll;
the poll itself must not decode resources or block an ordinary frame.

Native Thread adapters borrow complete VM identities (owner, slot, generation),
with a separately retained class root. Runtime integration uses the singular
Object release bridge; the generic base-class constructor is only for standalone
fixtures without that bridge. `0x140266f80` appends the actual instance to the
rooted shared array at `0x140993198`; `getThreadList` (`0x140268530`) returns that
same mutable array. Dormant instances receive no registration. Equal-priority
order therefore follows actual array order. Final pruning reads each current
index with the initial count, removes stopped/invalid entries forward, and drops
that entry's temporary value before continuing. The priority tree itself stores
borrowed native pointers (`0x14026886d`, node +0x20), not retained ObjectInfos.
The rewrite checks snapshot handle generations before following an entry removed
and released during a tick; the original dangling-pointer mutation edge is not
reproduced as unsafe memory access.

`0x140227b60` and the stack/copy ObjectInfo constructors retain strong VM values.
The scheduler explicitly retains the callable target, one containing argument
array, stored result, and pending resume value. At `0x140267790`, callable and
argument ownership transfers into the child context before script execution.
`0x1402670c0` stores a callable/file target while child-VM ObjectInfo `+0x78`
remains null. `0x140267400` creates a child through `0x1402673b0` only at first
function advance or file readiness, using the global/root VM and stack size
`0x400`. Dormant and pending rows therefore own no child context; the portable
scheduler now follows that lazy phase. The creating root's error handler is
copied at readiness, with a fresh null last error. Each child independently retains
the creating base VM's current root and error handler, then reinstalls the recovered
base globals in that copied root. Completed contexts survive until native owner
release. Native debug-hook copying remains a separate core VM gap.

Native Thread stores only user parameters before readiness. At `0x140267790`,
`sq_pushroottable` pushes the new child's current root immediately before those
parameters and `sq_call`. The `ChildRoot` scheduler mode therefore creates from
`Vm.RootContext()` explicitly and inserts `RootTableInContext(child)` at first
execution. Generic scheduler calls retain their supplied receiver under the
separate `Explicit` mode. Changing a root after fork but before function/file
readiness changes the native receiver; a host advance invoked by another active
context still uses the base VM as native Thread's creator.

Runtime string readers use immutable `SquirrelString` text helpers. Callback
argument values keep borrowed text alive; bytecode/program and resource-name
strings retain their separate C++ ownership. Native `wait` (`0x140268640` →
`0x1402675c0`) stores each original string in the strong wait array `+0xd8`,
while the earliest nonnegative numeric timeout retains its original value in
resume ObjectInfo `+0xf8`. Scheduler wait roots now retain those canonical
strings until completion or cancellation. `0x140269220` copies every matched
original value to `+0xf8` before removing its entry; the last matching entry
wins, then `0x140266de0` clears all remaining waits. The rewrite transfers the
selected value into its resume owner before dropping any wait roots.

Native notification comparison uses NUL-terminated prefixes: `0x140268620`
reads a string pointer through `0x140228fc0`, `0x140228510` pushes it with length
`-1`, and `sq_cmp` (`0x140222490` → `0x14025a7a0`) uses `strcmp`. A matched
value still returns its full original extent and identity. The native-value
wait entry point follows this behavior; the generic scheduler's existing
byte-string API preserves full-byte equality. Independent true-reclaimer
fixtures cover temporary-value handoff, weak-string lifetime, exact resume
identity, last prefix match, timeout at zero/below zero, cancellation, wait
replacement, and rejected input. A native Thread adapter fixture exercises
its actual script wait/notify callbacks with embedded-NUL values. These are
portable ownership checks backed by static call-order evidence, not a new
original-game run or complete scheduling fidelity claim.

Loading polls borrow no invisible GC roots: the script request service must own
an explicit prepared-closure root until the ready value is handed off. A native
cached self/delegate or wait-child wrapper uses `0x140227bf0`: it roots a weak
wrapper without marking its target. The shared active array retains registered
threads; a child's Object subscriber array retains its waiting parent. Child
notification copies the stored result into the parent's resume owner and removes
the subscription/wait wrapper.

The C++ Thread deleting destructor (`0x140266cd0`) notifies completion first,
then clears waits/subscriptions, pending load/callable/arguments, child-VM
ObjectInfo (`+0x78`), remaining result/wait values, and base Object state. The
Object bridge invokes the script destructor before this native cleanup. Cleanup
continues after a script/notification diagnostic. Release hook `0x140039f80`
returns zero after script dispatch `0x140228e80`, which ignores the status of
`0x140227810` / `sq_call(parameters=1, returnValue=0, raiseError=1)`. The bridge
preserves raw VM last error and continues native destruction for script status
errors; real host/native cleanup failures still propagate through the explicit
release diagnostic. Preflight rejects release of
an executing target or a target currently notifying before changing owned state.
An idle child may be released while another parent executes. Explicit VM Close
must run while adapters remain alive. Class finalization can clear inheritance
before native instance finalization, so registered native cleanup uses verified
native identity rather than revalidating an already finalized script base link.

Implemented callbacks include fork, system, numeric/string wait and notify,
current/list/tick queries, priority, status, and stored result queries.
`execOnBase` invokes the base context, but its successful callback returns script
null: `0x140268120` returns native call status zero and `0x140254850` interprets
zero as no returned values. It pushes the base VM's root, obtained through
`0x140225ff0`, even when the calling child has a different root. The copied nested
value on its scratch stack does not change that contract.

The original loading thread has states NONE=0, LOADING_FILE=1,
LOADING_FUNC=2, STOP=3, RUN=4, WAIT=5. Manual stepping, target replacement/exit,
general native-object waits, stack reporting, and long-session full-game VM/host
reclamation remain open. Missing variants must not become dummy native callbacks.

Clock update `0x140535f20` subtracts the previous millisecond counter modulo
32 bits, converts that unsigned difference to float32, multiplies by 60,
divides by 1000, and adds retained residual ticks. It adds 0.5, truncates, clamps
the increment to at least one, and clamps the new residual to at least -2.
Fixed mode returns one tick; `0x1404e05d0` selects the variable float increment
when enabled. `0x14023b2c0` converts the selected increment back to an integer
before advancing the scheduler. These are logical units of 1/60 second.

System registration at `0x140285f50` creates a Class with static methods, rather
than an invented property table. The implemented subset connects
setVariableFrame (`0x140289d90` -> `0x1404e0840`) and getVariableFrame (thunk
`0x140285320` -> getter `0x140394850`) to the actual portable clock. The supplied
release's getDebugBuild (`0x14004b570`) always returns false. getSpec
(`0x140284f10`, literal `0x1405f7090`) returns lowercase `win`; this identifies
the original resource/script profile even on a macOS host.

Independent sanitizer fixtures cover priority/snapshot ordering, ready/pending
loads, child completion/error values, timeout boundary and notification races,
counter wrap, owner/generation rejection, limits, native instances, expired
callbacks, and Object/Thread inheritance. True-reclaimer ASan/UBSan fixtures
also cover active-array identity/mutation, callable/argument/result roots,
prepared-load handoff, lazy context allocation at readiness, ready-time root
handler inheritance, child-root receiver selection for functions/files, preserved
user parameters and expired spawn-time roots, native creation while a different
context executes, the distinct caller/base-root `execOnBase` contract,
weak delegate/child expiry, completion-before-stop and
forward pruning, pending parent subscriptions, idle-child release from a running
parent, diagnostic-failing script destructors, stale priority snapshots, owner
shutdown, dormant and suspended native Thread Close, and generation reuse.
These portable lifetime checks pass with explicit Close while adapters remain
alive; full-game long-session and original trace comparisons remain open.
A bounded instruction-emulation
comparison of 2,011 counter values matches all 4,022 float32 step/residual words
exactly. Retained private vectors include pauses, zero deltas, and counter wrap;
they are not a native full-game timing capture.

The optional local startup check preloads all 72 compiled script entries before
execution. Real main/include/title threads then execute their original startup
chain, initialize Object/Thread instances, and enable variable frame mode. The
printing callbacks are implemented and match 414 initialized native CRT vectors.
The core-only check stops at unconfigured render-target bounds, main instruction
164. The macOS variant supplies a real 64 × 32 offscreen Metal target and host
calendar service, then uses the recovered srand binding and reaches
the real resource-cache configuration, retaining the original 500 MiB policy.
Bootstrap `0x1400455c0` supplies `TARGET=WIND3D11`,
`ARCHIVE_INFO_EXT=.psb.m`, and `ARCHIVE_BODY_EXT=.bin`. The configured mount
service then registers seven startup archives, and Resource/PSB adapters load actual typed
configuration. Recovered base-constructor receivers and implicit global lookup now allow init
to initialize actual LayerFolder scene state, recognize Screen, format its
actual VM object identity, construct InputHub with eight recovered input states and
original key mappings, and reach init instruction 992, where the native Layer
class remains unimplemented. Main reaches instruction 214
after preserving the completed child error value. This
fixture target is an actual host extent, not a claim about original startup
resolution. Full application creation and gameplay remain open.
