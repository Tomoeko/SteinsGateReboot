# First-party Squirrel VM recovery

Evidence date: 2026-10-12. Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`,
SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.
Addresses are preferred-image virtual addresses. Binary Ninja queries used the
explicit game PE view. No original bytes or analysis metadata were changed.

The portable VM is independently authored C++20. It consumes the owned
serialization model documented in `runtime.md`; it does not link, vendor, or
copy the original Squirrel implementation. The official
[Squirrel 2.2 manual](https://squirrel-lang.org/doc/squirrel2.html) is a language
reference. The executable and original-code measurements establish this
installation's behavior, including differences from ordinary C++ arithmetic.

## Recovered contracts

The dispatcher at `0x140255630` reads eight-byte records and advances before
executing them. Arguments 0, 2, and 3 are bytes; argument 1 is signed 32-bit.
Branches add argument 1 to the next instruction index. Return uses argument 0
as a null-result sentinel (`255`), with the result register in argument 1.
Generator yield instead tests argument 1 against `255`.

| Opcode range | Recovered operations | Important operand contract |
| --- | --- | --- |
| `0x00`–`0x0a` | Line, literal/integer/float loads, dual loads, tail call, call, prepared calls, literal get, move | Calls take callable register in argument 1, contiguous receiver/arguments starting at argument 2, count in argument 3. |
| `0x0b`–`0x12` | New/delete/set/get slot, equality/inequality, arithmetic, bitwise | Arithmetic left operand is register argument 2; right operand is register argument 1. Argument 3 selects the operation. Equality can select a literal right operand. |
| `0x13`–`0x21` | Return, null range, root/bool loads, dual move, jumps, capture/varargs, table/array creation, append, parent | Null range count is argument 1. Append selects register or literal through argument 3. New table preserves the requested native bucket capacity. |
| `0x22`–`0x2f` | Compound slot/local arithmetic, pre/post increments, order, exists, instance-of, short-circuit operators, negation/not/complement | Compound slot argument 1 packs receiver in its high word and arithmetic operand register in its low word. Increment delta is signed byte argument 3. |
| `0x30`–`0x3c` | Closure, yield/resume, foreach/guard, delegation, clone/type, traps/throw, class/member creation | Foreach has key/value/cursor at arguments 2, 2+1, 2+2. Class argument 1 is base register or `-1`; argument 2 is attributes register or `255`. Member argument 0 bits select attributes/static. |

Arithmetic helper `0x140253580` performs wrapping 32-bit add/subtract/multiply,
signed division/remainder, and float32 coercion for mixed operands. Zero
integer divisors produce a script error. Native `IDIV` overflows for
`INT_MIN / -1`; the portable VM rejects that pair safely and retains this
malformed-input behavior difference as an open acceptance check.

Equality helper `0x14025a240` compares identical numeric types through raw
payloads: `+0.0f` and `-0.0f` differ, while equal NaN payloads compare equal.
Mixed integer/float equality uses float32 conversion. Ordering helper
`0x14025a7a0` uses wrapping subtraction for identical integer operands. Its
same-type float ordering first compares payload identity, then floating order;
unequal NaNs and opposite signed zeros do not follow ordinary C++ relational
semantics. String ordering uses NUL-terminated `strcmp`; strings themselves
retain their full serialized extent. Same-type objects without a successful `_cmp` use unsigned pointer order; a failed comparator falls back, while a successful noninteger result produces the exact native diagnostic `comparsion between 'type' and 'type'`. Thirty-eight bounded original-instruction vectors cover eight object tags, unsigned pointer direction, controlled metamethod return/failure boundaries, and mixed/null handling (1,746 instructions, zero differences; report SHA-256 `dbaf092ee96ac86387256636b26f217d30ae87c8ef27a648c7f3544af72e936e`). The authored VM compares `uintptr_t` representations of its stable owned object allocations, also exposed by its native-format pointer diagnostic; these are not the original Windows allocations. Cross-run and allocator-relative order remain a fidelity difference requiring native traces. Independent fixtures verify native/script/noncallable comparator failure, wrong-result errors and bytecode relational continuations, with reclamation both disabled and explicitly enabled. Bitwise cases `0x140256770`–`0x140256861`
require integers and mask shift counts with `31`.

Closure construction `0x140253ca0` captures values at creation. Outer kind 0
copies a local register, kind 1 reads a named receiver/root slot, and kind 2
copies an outer value. Default parameters copy parent registers. Call setup
`0x14025b3b0` retains extra variable arguments outside ordinary registers,
requires the recovered parameter extent, and fills missing trailing defaults.

Table lookup `0x14025ca70` requires exact key type and payload identity; integer
`1` and float `1.0` are distinct keys. Native table construction `0x14025c590`
rounds capacity to a power of two with minimum four. Insertion `0x14025cb60`
relocates colliding nodes, scans its free position downward, and rehashes through
`0x14025cf60`. Enumeration `0x14025ce40` scans physical bucket order. The VM
reconstructs these operations for numeric/string keys, including shrink after
deletion at `0x14025d0b0`. Pointer-key enumeration remains unsupported because
the native bucket depends on that run's allocator addresses.

String hashing at `0x14024fe30` uses 64-bit state initialized with byte length,
step `(length | 32) >> 5`, and successive prefix bytes. Each byte is sign
extended, then narrowed to unsigned 16-bit before mixing. Disassembly is the
authority here: the decompiler misrenders the pointer element width. String
lookup `0x140259580` and enumeration `0x14024e4c0` return signed 8-bit character
values. Negative string indices require absolute magnitude strictly below
length; the last character is selected by `-1`.

Class creation `0x140253920` and `0x140264130` copy inherited member data.
Instance creation `0x1402551c0` / `0x140264710` copies default fields, locks the
class and every base (`0x140264c10`), looks up `constructor`, and replaces the call receiver with the new
instance. Member creation `0x140264c40` treats closures and static values as
methods, preserves ordinary-field identity when replacing its value, and
rejects writes once locked. Basic classes,
constructors, inheritance, field assignment, and instance-of are implemented.
Cached class metamethods remain separate from ordinary fields, as recovered
from `0x140264c40` and `0x140264b80`; replacing a method with a field does not
erase an inherited cached hook. Native adapters
can create genuine classes and instances through the public API. Checked host
identities borrow a type tag and retain an integer registry identity; the host
owns that registry and its lifetime. Create-instance skips constructor, while a
class call runs constructor and returns the instance regardless of its result.

Class creation `0x140253920` invokes `_inherited(new_class, attributes)` before
storing the attributes. It ignores a failed hook call. Opcode `0x3c` calls
`_newmember(class, key, value, attributes)` with four arguments and no static
flag argument. Success consumes normal insertion; failure falls back to
ordinary insertion with the instruction's static/attribute bits. Authored
continuations and independent fixtures cover both outcomes for native/script
hooks. Shared-state initialization `0x140250ee0` confirms 19 cached hook names,
including the game-specific `_exist` entry.

Foreach dispatch `0x140258f40` is implemented for strings, arrays, tables,
classes, generators, and instances with `_nexti`. Generator keys start at zero;
each yielded value runs through opcode `0x34`, which excludes a final return
after the generator becomes dead. Instance iteration preserves the returned
key as its next cursor, stops on null, and reads the value through ordinary
lookup or `_get`. Script and native callbacks use owned return continuations;
failed `_nexti` and invalid-index lookup retain the native error distinction.
Class enumeration `0x140264fc0` traverses its physical member-index table and
returns actual field/method values. Class inheritance and table clone use the
physical reinsertion order from `0x14025c7a0`; allocator-dependent object-key
enumeration remains unsupported.

Table metamethod lookup uses its direct delegate (`0x14024cf90`); instance
lookup uses its class (`0x140264b80`). Owned continuations now cover `_get`,
`_set`, `_newslot`, `_delslot`, arithmetic, `_cmp`, `_unm`, `_typeof`, `_cloned`,
`_tostring`, and `_call`. Assignment hooks retain the assigned operand. `_cmp` must return
an integer; equality calls it only for a left instance. `_call` prepends the
object before the original receiver and arguments. Native suspension
`0x14025b950` rejects suspension through native calls/metamethods. Compound
slot assignment and pre/post increments now preserve owned receiver/key/old
value state through `_get`, arithmetic, and `_set` continuations. Local
compound/pre/post operators use the same arithmetic continuation and retain
their original expression results. Native helper `0x1402554b0` ignores a
failed setter after computation, so the expression returns its computed or
previous value while a failed setter's mutation remains absent.
Existence helpers `0x140258e70` / `0x1402592f0` first check ordinary membership;
an `_exist` call's success status then counts as true, regardless of its
returned value. Failure counts as false. Independent fixtures cover these
error/status distinctions. Failed `_get` calls now retry the receiver's default
delegate (`0x140259580`, instance branch `0x140259839`) and then the root
table only when the receiver is the current frame's `this`
(`0x140259e20`, `0x140259f2e`–`0x140259f63`). Successful null results consume
lookup without fallback. Root lookup reads its own table slot and resolves a
stored weak reference once. Missing numeric array indices bypass these retries.
Owned lookup continuations retain compound-assignment and iterator actions
through native or script getter failures. Independent fixtures also verify that
recovery does not construct an additional missing-index exception. The original
startup constructor's implicit global call on an Object-derived instance uses
this path; the native Object attachment check remains unchanged.
Class GET checks ordinary members and the same `this` root fallback.
A successful general member lookup determines the current-instance call receiver, including inherited members copied into the derived class snapshot.
Class default delegates are a separate PREPCALL retry at `0x14025619e`;
they cannot shadow a successful root lookup or become ordinary class properties.
Failed bytecode GET/PREPCALL constructs the native missing-index message via
`0x14025c230`: strings stop at NUL and 50 bytes, integers use decimal,
floats use promoted `%.14g`, and other keys use their type name without `_tostring`.
Native `0x140221000` reads the copied member-index and does not recurse through the parent. Ordinary lookup now preserves that snapshot: late base additions remain absent, and later base method replacement does not overwrite the derived copy. Independent fixtures cover both distinctions. Host bindings register base members before constructing derived classes.
Native API `0x140222a80` performs general get with raw and root-retry flags both zero. It permits instance/scalar default delegates, excludes Class default properties, and does not resolve an implicit root name. On failure it pops the key and replaces the last error with the fixed string `the index doesn't exist` via `0x140225a60`; a failed `_get` error is not retained. Public `ReadSlot` implements this distinction from bytecode GET and the nonexecuting `GetSlot` reflection helper. Independent fixtures cover typed getter failure, default retry, class exclusion, and absent root retry.
Remaining delegated-table get chains and set/newslot error fallbacks,
captured-name lookup hooks, and debug behavior remain open.

Base registration `0x1402a50e0` uses records at `0x14096a190`. Default delegate
records begin at `0x140969410`. `InstallRecoveredBaseLibrary` installs the
verified subset: root lookup, assertion, suspension, array construction, type,
explicit native `dummy`, the shared constant table, width/version constants; numeric conversions; lengths,
raw table operations, array extension/removal/slicing, overlapping string search,
ASCII case conversion, class/member attributes, instance identity, and closure
call/acall/pcall/pacall/bindenv/getenv. The native
`dummy` is an actual registered callback, not an invented engine placeholder.
The shared constant table is independent of the root table and shared across execution contexts. Initializer `0x140250ee0` creates an empty table at shared-state `+0x60` (`0x1402529c4`). API `0x140224060` and base `getconsttable` (`0x1402a0510`) push that table. `setconsttable` (`0x1402a0590` → `0x140224cf0`) accepts only a table, replaces the shared reference, and reports the exact typed error `ivalid type, expected table` for other types. Its successful base return is null: it retains a pointer to its second argument, then the API pops and clears that cell through `0x14025ab60`, and finally pushes the cleared cell. `ConstantTable()` and `SetConstantTable()` expose this ownership slot to host registration without putting constants into the root table. Independent fixtures verify context sharing, replacement identity, no merging, invalid-type rollback, and foreign-owner rejection.

Array insertion `0x1402a1f80` / `0x1402210c0` accepts indices from zero through
the current size, truncates float indices, retains the raw supplied value, and
returns no value. Sort `0x1402a2390` / `0x1402a4c80` captures the first element
as pivot, scans left for positive comparison and right for nonpositive comparison,
and processes the left partition before the right. The authored iterative range
stack preserves that comparison and unstable equal-key permutation order without
using the C++ call stack for each partition. Custom comparators receive
`(root, raw_value, raw_pivot)` through `sq_call(..., raise_error=false)`;
integer, float, and boolean results use `sq_getinteger` conversion at `0x140222ff0`.
Non-null comparator errors become `compare func failed`; a null error is retained.
The adapter rejects the original unchecked nonnumeric comparison result and array
shrinking below the captured range. Its named safety limit is 1,048,576 comparisons
per sort. Fixtures cover scan order, equal-key identity permutation, raw weak values,
script callbacks, null/error distinction, defective comparisons, and safety limits.
Native sort also reads the slot after its receiver without checking the native
argument count. Omitted-comparator calls currently use normal value comparison;
parity when that unused original stack slot contains a stale callable remains open,
as does a differential native array corpus.
Numeric parser `0x1402a5280` recognizes leading `0x`/`0b`, selects floating
parsing when a dot occurs, and accepts a consumed numeric prefix. Thus `1e3`
without a dot is parsed as integer prefix `1`. Overflow, special floating
spellings, and the remaining delegate methods stay open.

Native signature registration (`0x140250950` / `0x140225040`) retains exact or
minimum parameter counts, optional names, and owned type-mask vectors. Dispatch
`0x140254850` validates supplied arguments before replacing a bound receiver.
`SquirrelNativeSignature` models those records; unrestricted host callback
creation preserves null native name metadata rather than promoting a diagnostic
label. Base/delegate/format registrations use recovered signatures. `getinfos`
(`0x1402a3e60`) returns native `name`, `paramscheck`, and `typecheck`, or script
`name`, `src`, `parameters`, and `varargs`. Script variadic parameters append the
literal `...`. Independent fixtures verify reflection and pre-binding checks.
Remaining host registrations require their own evidence; they are not assigned
guessed masks or names.

Container search (`0x1402a1430`, `0x1402a15a0`, `0x1402a17f0`, `0x1402a19b0`)
compares enumerated values against the query through native order comparison,
retaining physical table bucket order and resolved weak values. `removeValue`
(`0x1402a2140` / `0x140221b20`) reverses comparison operands, removing all matches
by default. Its actual registration has an exact count of two, despite an
optional third argument in the dormant callback body; normal calls reject that
extra argument. The authored callbacks preserve this distinction. Generator
search resumption and native comparison failures with an uninitialized output
remain open; the rewrite reports unsupported failures explicitly.

String replacement (`0x1402a3450`) scans non-overlapping matches, returns the
original string for an empty needle, and copies the full replacement byte
length. Split (`0x1402a3670`) uses a substring delimiter and preserves empty
segments, scanning the source only through C-string termination. Multibyte
helpers (`0x1402a4af0` / `0x1402a4be0`) advance encoded bytes, including the
observed continuation-run and obsolete five/six-byte lead rules. `mbnext`
returns an advance length; `mblen` counts advances; `mbsubstr` uses a character
start and count. Fixtures verify these contracts and bounded output. Empty
split delimiters, replacement inputs whose embedded NUL exposes uninitialized
native output, negative byte cursors, and slices beyond original string storage
fail safely. Runtime locale changes and malformed-source differential fidelity
remain open.

## Ownership and validation

Values own strings and carry checked handles into a VM-owned object arena.
Closures share immutable owned program storage because child prototypes must
outlive calls and retained functions. Tables can form cycles without circular
`shared_ptr` ownership. Each execution context owns a physical operand-cell
array; active frames borrow checked register offsets into it, while suspended
generators own detached snapshots. Frames separately own variable arguments
and trap metadata. Host callbacks return owned results and can suspend an
invocation without sleeping or crossing an OS callback. Limits bound objects,
container capacity, strings, frames, registers, traps and executed instructions.
Arena reclamation during a long-running game and aggregate heap accounting are
still required before runtime memory acceptance.

Native root setup `0x140226020` requests 1,024 initial cells, as does the recovered engine child setup. Script StartCall `0x14025b3b0` places callee registers at the caller's argument offset without clearing non-parameter cells. LeaveFrame `0x14025adc0` restores the caller, drops variable arguments and the call closure, writes the result, then clears cells from the old top down through the restored top inclusively; when the old top is lower, no cells are cleared. The old one-past-top cell participates in this cleanup. Two bounded original-instruction cases verify caller/callee aliasing, inherited uninitialized cells, result placement and both cleanup extents (3,728 instructions, zero differences; report SHA-256 `223e2233816a055e754bcafb2a23818596305a08525dcdf67a92cda0699fe6d1`). The authored context starts with 1,024 cells and preserves native script growth by twice the callee register extent. Checked storage is capped at `1024 + 3 * MaxRegisters`, in addition to the existing aggregate active-register limit; this is an authored safety bound, not a recovered original allocation limit.

Error Execute captures the failing top at `0x140258780`, retires call metadata while restoring frames, and performs one final reverse-inclusive cleanup at `0x140258c94`. A short failing callee can therefore leave a higher caller cell owned after the invocation unwinds. A bounded original Execute/throw case verifies that ownership until actual SetTop growth overwrites the cell (3,076 original/declared-boundary instructions, zero differences; report SHA-256 `4c1726cd5bf26395213ddc11c40f98e2f118adc332737c22cc668bd15055bc62`). The supplied callback only returns one prepared owned string into the real result cell; all call, throw, unwind and overwrite operations execute original instructions. The authored marker scans every allocated physical cell ascending, including cells above logical top. Return and overwrite cleanup use indices so a release callback can safely grow storage through a nested call.

Native stack-object read `0x140223430` returns an existing raw cell without adding a native reference: positive indices use current base plus index minus one through `0x14025a010`, while negative indices use absolute top plus index through `0x14025a020`. Neither path restricts reads to logical top. Fifteen bounded original-instruction cases use actual userdata and scalar cells at two frame bases, including inactive positive cells and negative reads preceding the current frame base. All 410 original/declared allocator-boundary instructions match with zero differences; operand storage, logical base/top and userdata reference count remain unchanged (report SHA-256 `0e0611e8a12c9ab7c0a1abba2e727382d1ff1503a49b8b9c3c960ff433e3041d`). A separate exact Sqrat-wrapper corpus confirms zero-user coordinate calls can read inactive float `19.75` at index three despite logical top two (32 cases, 5,260 instructions, report SHA-256 `6a45a68104364076d177ae4c5f28c7863404cd6159f0e7d23c524c9c5f653fcd`).

`NativeAllocatedStackValue` exposes that allocated-cell inspection only inside an active native callback; it never grows or writes storage, changes top, or resolves a weak wrapper. Its returned portable value owns a safe transient copy, unlike the native API's borrowed raw output cell. Existing `NativeStackValue` retains its logical-extent guard. Index zero and reads beyond actual allocation reject explicitly as authored safety restrictions; unsafe original indices are not dereferenced or claimed as recovered behavior. Eight independent sanitizer groups verify genuine inactive captured values, raw expired weak cells, nested preceding-frame cells, independent child storage, unchanged ownership until actual overwrite, bounds and callback scope. Native `0x140224160` is push-null, not reset-error: the observed missing-property path preserves last error while pushing null and removing the old receiver. This read-only API adds no error-reset operation and does not enable runtime reclamation.

Generator Yield `0x14024ef20` transfers active cells into its saved array. Resume `0x14024ea10` copies them back but repeatedly clears only saved cell zero; saved cells one and higher remain owned while execution is active. Kill `0x14025a450` clears saved cells and the independent generator closure, while the saved call record's closure survives until actual generator destruction. Marker `0x14024e0d0` traces saved cells, saved variable arguments and the independent closure, without marking the saved call-record closure. A 4,526-instruction original-instruction case verifies those distinct references with zero differences (report SHA-256 `99aba7ada3be5db09e538a25bb5b99af9a7a94d722115f365dd36bba0900ce7b`). A second 4,587-instruction case verifies that saved traps retain their original absolute base/top when resumed from a different extent (zero differences; report SHA-256 `b65394a491b17a50c9744761e816a0a4137e819a0849e7fdc6db2da232ceb520`). The authored runtime rejects that differing-extent trap resume before mutating the saved frame; reproducing an unsafe raw-address continuation is not accepted. Script tail calls with active traps still reject pending their reused-record retirement contract. Ordinary script tail calls now retire old variable arguments in reverse before reusing the real call record, matching `0x140255b23` → `0x14025abb0` → `0x14025b3b0`. Two original Execute cases distinguish fully retired arguments from an argument forwarded into the new callee (5,074 original instructions). The independent fixture verifies genuine weak expiration, forwarded ownership, one reused tail frame, native suspension, observed outer/tail frames and resumed return; no active-trap admission follows.

Twelve independent sanitizer fixtures verify physical aliasing, bound native receiver mutation, release during nested storage growth, failed host-call rollback, inactive-cell marking/overwrite, saved-generator ownership, saved call-closure retirement, second-yield callbacks and trap-resume rollback. Fourteen existing execution/lifetime/close/error/context suites pass with the same private core. Native outer-value construction, unverified compatibility callback statuses, temporary/result-cell reuse across failed or nested calls and reentrant generator transitions remain open. The facade's owned invocation-result relation is retained explicitly; it is not evidence of identical original temporary-cell reuse. Runtime reclamation stays disabled.

Native `sq_call` at `0x140222240` owns a local output cell, pushes it only when the return-value flag is true, then releases that local on both success and failure. Twelve bounded original-instruction cases compare both flags for native statuses zero, one, two, failure and raw `-666`, plus ordinary script returns. A transferred native value expires before a no-result call returns; status-zero/failure hidden cells retain their own references until real overwrite. Ordinary script returns remain owned independently by VM `+0x98` after caller operands retire. All 32,951 original/declared-boundary instructions match with zero differences (report SHA-256 `13445134bd1aa9bcdd1c38cfe1dfb76bf00380fc4a0660fdfe476e42f71ce289`). Two additional actual script/base-suspend cases show the temporary still owns the called suspend closure, while the yielded string has one prototype reference plus an optional caller result reference. Both flags preserve the real suspension and active call record (5,797 instructions, zero differences; report SHA-256 `d553e6e2850c45e6b8d8433b6ec112c5b7398d80def5df5ea35ef3af7871423c`). The suspended graph is retained in that native corpus; it does not claim wakeup or shutdown cleanup.

`InvokeWithoutResultInContext` shares the existing invocation checks and execution path, returns completion/suspension/context/instruction metadata with a null `Value`, and drops its facade output while the selected context is still active. It preserves native temporary, last error, independent root and suspended continuation; nested calls restore the existing caller output. Host argument spans remain caller-owned, unlike the original operand-stack API. Ten independent sanitizer groups verify native positive/raw-suspend owner expiry, existing returned-output compatibility, script temporary tracing, a freshly produced yielded owner retiring before child wakeup, typed native/script errors, running-parent/child restoration and 512 discarded initial Generators under a 32-object arena bound. Native generic Call `0x140254080` forwards raise-error only to script Execute; direct native failure does not invoke the script error handler. The new route preserves that distinction and does not reset error or execution state. Native callback status migration, broader cleanup/reentry and collector prerequisites remain open; this additive API does not enable runtime reclamation.

Execution contexts own independent frames, traps, suspension destinations, results, root slots and last errors while sharing the object arena, constants and type delegates. Generation-checked handles reject released/reused contexts. A host scheduler selects contexts explicitly; creating a child does not execute its target synchronously. Native `sqnewthread` (`0x140223bf0`, initializer `0x14025a030`) copies the creator's root and error handler as independent strong storage; its last error starts null. `CreateContext()` copies the executing context, while `CreateContextFrom(creator)` selects a specific creator as the engine's global-VM child setup requires. `RootTable()` returns the executing context's root; `RootTableInContext()` and `SetRootTableInContext()` permit explicit host access. Resetting execution preserves root/handler/error state; releasing a child releases those owned values. Root slots are marked individually and clear before last error/handler during context shutdown. Bounded nested invocation supports same-context and child-to-root native calls, stops at the nested frame boundary, restores caller state and preserves thrown values. Debug-hook copying remains open.

Root getter `0x1402241c0` and load opcode 21 (`0x140256985`) read the executing VM's root, independently of receiver and closure creation. Setter `0x1402251e0` accepts a table or null, installs/AddRefs the incoming value before releasing the old root, then pops its argument. Other types preserve the root and set exact error `ivalid type`. Base `setroottable` (`0x1402a0530`) returns the cleared argument cell, hence null. Thirteen bounded original-instruction cases cover replacement/self-assignment/null transitions, invalid-type rollback, get/set base returns and owned child copies (735 native/boundary instructions, zero differences; report SHA-256 `e14f59c1cfff56eb259d336b9da56bd5cc8ca538f8f129eeff5bf0636520e607`). Null root loads remain valid; implicit table lookup through a null root rejects before the native null-pointer dereference path rather than inventing a result.

Child initialization also runs base registrar `0x1402a50e0`. It pushes the root once, creates fresh native closures in registration order, applies names/type masks, writes four version fields and pops the saved root. Replacing the root during a registration callback therefore does not redirect later registrations. Native newslot API `0x140223a60` discards registration arguments successfully for a null root. Three bounded original-instruction cases verify all 22 native registrations/four version fields, fresh closure identities, unchanged pushed-root target during replacement and discarded null-root temporaries (14,693 native/boundary instructions, zero differences; report SHA-256 `c53296f77f0628d692491723a9773ad358847b9426c41f0e433d28a4ec0778cf`). The authored registrar reinstalls its 15 currently recovered core globals without reinitializing shared type delegates. Existing root properties overwrite directly; table `_newslot` can consume a new registration, while failure or removal of the delegate resumes raw insertion (`0x14025a4d0`, `0x14025a695`/`0x14025a6bd`). A temporary owned VM-stack root preserves the pushed snapshot across boundenv callbacks/collection; initializing contexts cannot retire during that operation. Nine independent sanitizer fixtures verify snapshots, explicit creators, delayed/suspended execution, getter/setter/fallback/error semantics, cycle ownership, close ordering and real registration callbacks.

The additional registered source helpers are `evalstring` (`0x1402a0ce0`, parameter check -1, mask `..o|x|y|t.`) and `template` (`0x1402a0dd0`, -1, `.so|x|y|t`). Their metadata is retained without registering unimplemented execution. Ten core source/debug/printing/collection callbacks remain uninstalled; the separate host-format module already supplies printing on the configured main root. Full source compilation, debugger callbacks, complete core printing setup for replacement roots and runtime collector acceptance remain open.

Class method call preparation (`0x140256297`) retains the current frame's
register-0 receiver when ordinary class-member lookup succeeds. The separate
class-default-delegate fallback (`0x1402561e2`) retains the class receiver.
This distinction permits script-derived classes to invoke a named native base
constructor such as `Object.constructor()`. Independent literal/dynamic call
fixtures and a real decoded utility-script constructor probe verify attachment
to the derived instance without relaxing native Object identity checks.

Metamethod helper `0x1402547fd` invokes its nested call with `raise_error = 0`.
Public get/set/comparison/string meta operations suppress nested notification;
unhandled outer script errors still notify once. Explicit native Object getter
calls retain their separately recovered `raise_error = 1` (`0x140228bcc`).
Builtin suspend thunks `0x1402a0a70` / `0x140225340` push no result; dispatch
`0x140254bf1` exposes the actual stack top, hence the last supplied argument or
the receiver when no value was supplied. Direct native calls lack the execute
frame required by `0x14025b950` and cannot suspend. Fixtures verify these call
boundaries and resume destinations.

## Exception and weak-reference contracts

`setexceptionclass` (`0x1402a05f0` / `0x140224e80`) stores a class in shared
VM state and returns it. Dispatch `0x14025857f` constructs string errors through
that class before trap handling; nonstring thrown values retain their identity.
A failed constructor does not replace the created exception instance.
`seterrorhandler` (`0x1402a0650` / `0x140224e00`) stores a per-context callable or
null; newly created contexts inherit it (`0x14025a030`). Unhandled errors call
it with root and the original value before unwinding. Protected calls suppress
notification; native Thread execution uses `raise_error = 1` (`0x14026789a`).
`LastError()` and `LastErrorInContext()` expose the owned per-VM error value read by native API `0x140223060`, including native Thread failure handling at `0x1402678b5`. New child VMs start with a null error; successful calls do not reset it. Direct native-call failures preserve their raw error value; script dispatch snapshots raw errors for exception construction/traps and restores the wrapped snapshot on unhandled exit (`0x140258cc5`), including after an error handler fails. Last-error objects are collector roots and clear before error handlers during VM shutdown. Native general get replaces failed lookup errors with its fixed diagnostic. Authored budget/depth/shutdown-work failures remain host safety errors and cannot become ignored script-call status or fallback success. Independent sanitizer fixtures cover context isolation, status versus safety, error-object cycle ownership, handler snapshot restoration and close ordering.

The original Execute (`0x140255630`, guard `0x140255675`) and CallNative (`0x140254850`, guard `0x140254882`) share each VM's native-call counter at `+0xf0`. A new depth above 100 sets exact last error `Native stack overflow` and returns failed call status before executing another frame; it is separate from authored work limits. String errors from a script exception constructor can recursively construct exceptions until that native threshold. Generic Class call `0x140254080` stores its new instance before constructor execution, and the outer error dispatcher (`0x1402586b6`) ignores the failed Class-call status while retaining that instance. The authored per-context counter now preserves this status path without weakening instruction, host invocation, callback or allocation safeguards. Five bounded original-instruction cases verify depth 99 acceptance, depth 100 rejection, counter unwind and retained instance after constructor Execute rejection (546 native/boundary instructions, zero differences; report SHA-256 `35efc9719b2f59d10cc12e5e1e3e53b39a206db17371c00d18e6e5097819d891`). Independent sanitizer fixtures verify 99 executions of a recursively string-failing constructor, 100 recursive native callbacks, a lower authored depth limit remaining a host safety error, and migrated Object shutdown continuing native cleanup.

Object registration `0x140226080` installs ordinary member `weakref` at `0x1402261cc`–`0x1402261f0` before derived classes copy its members. The callback `0x140039f50` invokes the native weak-reference API `0x140225dd0` on physical stack index −1 and returns one value. This member takes precedence over `_get`; omitting it incorrectly lets an instance delegate bind the default weakref closure to a different owner. The adapter preserves the final-argument behavior, including extra scalar arguments. An independent bytecode fixture checks actual receiver identity through a delegated picture, inherited ordinary membership, extra arguments and weak expiry; static native evidence does not establish full startup fidelity.

Native release thunk `0x140039f80` invokes script destructor lookup `0x140228e80`, then always calls native deletion and returns zero. `0x140228e80` ignores the status returned by `0x140227810`, which uses `sq_call(params=1, retval=0, raise_error=1)`. A failed script destructor therefore remains a VM error/notification while native cleanup continues; it is not a failed release-hook status. Authored native-owner cleanup failures remain separate release diagnostics. The Object adapter now uses the genuine no-result invocation route. Two independent sanitizer groups return owned userdata with positive or raw `-666` native status and require payload release plus weak expiration before supplied native-owner cleanup; the accepted adapter fails that ordering check. Direct native raw `-666` remains nonsuspending here, while ordinary script RETURN separately retains its real VM temporary. Existing script-failure LastError and native cleanup diagnostics remain unchanged.

`getstackinfos` (`0x1402a06a0` / `0x14025c450`) returns `func`, `src`, `line`, and
in-scope `locals`, including captured names. Native frames retain the source
`NATIVE` and line `-1`. Fixtures cover traps, error inheritance, protected calls,
and exception-constructor snapshots. Debug hooks and notify-all behavior remain open.

Weak creation (`0x14024cfd0` / `0x140225dd0`) caches one real weak identity on
its target; non-reference scalars remain unchanged. `ref` (`0x140223640`)
resolves one layer. Tables, array indexing, ordinary instance/class fields, and
array removal/slicing resolve stored weak values; array top/pop, extension,
argument-array expansion, and static class slots retain raw weak values.
Closure binding (`0x140221f70`) stores the cached weak environment, and `getenv`
(`0x140222dd0`) returns that weak object directly. Owned fixtures verify these
distinctions and clone identity. Optional reclamation and expired object/string weak targets are implemented behind the object ownership gate. Full collector mutation, aggregate heap and native shared-state retirement acceptance remain open.

## Immutable strings and native identity

Factory `0x14024cd90` uses the shared state's string pool at `+0x28` through `0x14024fe30`. The pool compares the complete explicit length and byte extent, including embedded NUL bytes; omitted length uses `strlen`. Its cached 64-bit hash samples sequential signed bytes with the original 16-bit extension. Capacity starts at four and doubles when the count exceeds capacity. Pool buckets hold identities without adding a reference. Last string release `0x14024e9c0` → `0x140253000` removes the identity and destroys it; base destructor `0x14024c850` clears a surviving cached weak wrapper to null. Weak destruction `0x14024e9d0` clears a live string's weak cache. Strings are reference-counted, outside the collectible list and excluded from collector marking.

The bounded original-instruction oracle covers 61 vectors, explicit/omitted lengths, embedded NUL distinctions, high-bit bytes, hash sampling boundaries, growth to 64 buckets, equal-string reuse, cached weak reuse and both release orders. All 53,049 original/synthetic-boundary instructions passed with zero differences; report SHA-256 `66031a652fa93a332f1b0ceb5c06e557b1285b2c709eae5cbe97619da0d458d3`. Allocation and CRT byte operations are declared synthetic boundaries; it does not run the game's entry point or initialized runtime.

`SquirrelString` replaces the runtime variant's mutable `std::string` alternative. `StringText`/`TryStringText` expose immutable borrowed bytes, and `Vm.CreateString` explicitly adopts a host string. Ordinary string arguments remain accepted and acquire a per-VM canonical identity at the VM boundary; copies made before adoption share that ownership relationship. Equal bytes share canonical storage, weak wrappers do not own the target, and copied host-root values acquire ownership while root metadata remains borrowed. The pool has no mutable global state. Foreign VM identities reject; copying bytes explicitly creates a value for another VM. Host byte views remain memory-safe after close, while VM operations reject expired ownership. Runtime strings do not change the decoded bytecode or resource variants, which still use `std::string`.

Shared initializer `0x140250ee0` owns fifteen type names, nineteen metamethod names and `constructor` even before base delegates are installed. Script prototypes own interned literals and metadata through a directed parent-to-child graph; a retained child closure preserves its own prototype without retaining unrelated parent literals. Prototype destruction `0x14024e7f0` releases its value vectors and child prototypes; closure destruction `0x140220a60` releases the prototype while finalization `0x140220f90` only clears captures. Native closure names are retained separately. Shared-state destruction `0x14024f4a0` completes its remaining-list callbacks before retiring the constructor-name slot at `+0x70`. A bounded 517-instruction check observes its cached string weak target alive in the callback, then null after slot replacement, with zero differences (report SHA-256 `7db2fb41006b0ff553f11f954b484733eaa140c3eb3e2c01467e651b4355f09a`). `MaxStringBytes`, `MaxInternedStrings` and `MaxInternedStringBytes` bound individual and unique interned extents; defaults retain the shared metadata within those limits. These bounds do not substitute for whole-heap accounting of host buffers, decoded bytecode, containers or opaque callback captures.

Eleven independent sanitizer fixtures verify identity/extents, immediate expiration, weak-cache retirement, duplicate host roots, ascending array string release, collection of containing cycles, directed prototype ownership, native argument/result/name ownership, foreign/closed safety, pool-budget reuse and shared constructor-name retention through remaining-object finalization. The full VM, lifetime, shutdown, error-state, native-depth and context-root suites also pass. The optional local corpus loads and closes immutable prototype owners for all 72 original scripts without unexplained serialized bytes. None of those checks establishes complete game execution or enables runtime collection.

## Native host roots and collection prerequisites

An original-instruction oracle independently ran 116 AddRef/Release operations across twenty bounded synthetic reference objects and four scalar cases, including table growth from four to thirty-two entries, with zero differences. It executed 13,583 original instructions; only the private allocator/free boundary was hooked. Its compact report hashes to `0fb1cc76a30b77a8a10bd972d70761e938e80ce36d3ca8226515cce4a2d26091`. Release returns one only when the final entry is removed; an intermediate decrement or absent reference returns zero, while scalar release returns one.

Native AddRef `0x140221710` ignores non-reference scalars, including booleans, and retains strings, weak-reference objects, and collectible objects through the shared RefTable at offset `0x30`. `0x140250030` increments the matching entry's external count at `+0x10`; duplicate external references share one strong stored value. Release `0x140224a40` / `0x140252e80` removes that value only when the final external count reaches zero. RefTable identity compares exact type and payload; interned strings distinguish their complete byte extent.

`Vm.Retain(value)` now returns a move-only `SquirrelHostRoot`. Its `Value()` borrows the retained value; moving preserves one reference, destruction releases it, and duplicate explicit retains are counted separately. The VM alone owns the root registry's strong lifetime; scoped roots observe it weakly, safely release after owner destruction, and reject value access once the owner expires. Roots do not extend the VM's lifetime. Ordinary `SquirrelValue` copies retain object storage; explicit host roots separately protect objects from collection. The registry bounds distinct roots by `MaxObjects`, reuses released slots, and rejects count overflow. Equal string roots share their canonical interned identity and full content key.

The authored arena now tracks strong handle copies and moves, immediate last-reference release, cached weak-wrapper unlinking, and recycled indices with new generations. `BorrowObjectIdentity()` returns a non-owning index/generation/owner token backed by a private weak control token; `ResolveObjectIdentity()` cannot retarget a freed slot or a later VM that reuses an owner address. Native assignment `0x1402209a0` installs and retains the incoming value before releasing the previous one. VM value replacement follows that ordering so release callbacks see the replacement.

`SetInstanceReleaseHook()` replaces a single callback, matching API `0x140225160`. Reference release `0x140265100` temporarily protects self during the callback and clears the hook after it returns, including a reentrant replacement. Explicit reference-release resurrection preserves class and fields; collection finalization `0x1402649f0` still clears them. Hook exceptions cannot escape destructors and are retained for explicit `RethrowReleaseFailure()` diagnostics. Iterative release frames preserve ascending array and table key-before-value child release while bounding the C++ call stack. Nested release callbacks now finish child release before their releasing callback resumes. A bounded original-instruction fixture traces root finalization → A hook → assignment of null to B → B hook → A resume and verifies B sees its source slot already null (205 original/synthetic instructions, zero differences; report SHA-256 `b743940360a21c9bf79b7496d4146fd78b116196a9c0c1febf7f1eb7bfe37ed7`). The authored drain pins the outer frame boundary and permits nested drains only while an actual release callback is active; ordinary deep release chains remain iterative. Callback recursion uses `MaxFrames` as an explicit safety limit, records an error at the limit, and finishes storage cleanup. Saved generators keep the independent generator closure, saved call closure, registers and variable arguments without an owning self-cycle. Native finalizer `0x140259940` clears saved registers and the independent closure; the saved call closure and variable arguments remain until actual destruction.

`SquirrelVmLimits::ReclaimObjects` defaults false until adapter ownership migration is complete. With that explicit opt-in, `CollectGarbage()` marks shared tables, explicit external roots, type delegates, owned execution contexts, active native arguments and stored continuations, then counts explicit finalization iterations. It matches the five retained private-native-VM vectors described below; broader ordering, userdata, opaque callback captures, aggregate heap accounting and complete VM-close callback parity remain open. Reentrant collection still rejects explicitly; allocation inside collection callbacks is supported for the bounded cases described below. The recovered base `collectgarbage` callback now invokes this actual collector and returns its signed count, following registrar `0x1402a50e0`, callback `0x1402a04c0` and collector `0x140250120`. The isolated VM default still requires explicit reclamation opt-in; the actual hidden application enables it after configuring the real adapter roots and release hooks. Independent sanitizer fixtures cover immediate weak expiration, generation reuse, 10,000-deep release chains, child/hook/assignment ordering, instance resurrection, callback diagnostics, all five cycle vectors, and live native argument roots. The existing VM suite also passes with reference reclamation enabled.

Native array interop is exposed directly through `AppendArray`, `RemoveArray` and `ClearArray`; host adapters need not install script delegates to retain and mutate their subscriber array. Removal returns the resolved removed value and preserves index bounds. Clear follows resize helper `0x1402215d0`: ascending releases occur while the old logical size and current raw slot remain visible, then size becomes zero. Authored current-slot identities are borrowed and checked during callbacks; expired raw accesses reject instead of dereferencing freed storage. Recursive clearing of the same array rejects explicitly. Broader reentrant mutation cases stay open. Array destruction (`0x1402209f0`/`0x1402208f0`) and collection finalization (`0x140220f60` → `0x1402215d0`) retain the same current raw cell and old logical length throughout each ascending child callback; they clear the vector after release. Two bounded original-instruction cases verify current-cell observation and completed prior-child release (400 instructions, zero differences; report SHA-256 `b34c15b9aeda351e5b8d482d5629a2c3238889f19ac4f6a4383ac002980e6597`). Independent reference-release and collector fixtures preserve current identity and safely reject an expired earlier cell.

Closing differs from collection. API `0x140222450` first invokes root-VM finalizer `0x1402599c0`: root table, last error, error handler, debug hook and temporary value are dropped in that order, then call records and every allocated stack cell are released. Four bounded original-instruction fixtures cover distinct values, aliases, empty state and 32 call records/64 stack cells (2,618 instructions, zero differences; report SHA-256 `0e78f2025e6d929941a6f536c966f6e58cf317955e933a4104248d1d863c8136`). Shared-state destruction `0x14024f4a0` then finalizes remaining collectables in list order before clearing shared tables, type delegates and external references. `Close()` now clears the authored root and context-owned values before remaining collectables, then shared constants, delegates and explicit external roots. It runs native instance hooks while the facade is alive, expires all object identities/context handles/scoped-root access, and is idempotent. The nonthrowing destructor invokes this same shutdown while its implementation is still accessible. Independent sanitizer fixtures verify root-before-cycle ordering, completed context results, callback diagnostics, release of a child context while that same context is being cleared, and migrated Object script-destructor/native-cleanup/completion order. Reentrant close rejects. New host roots remain valid during early root and remaining-instance finalization; retirement later expires callback-created roots along with preexisting roots. Four bounded original-instruction fixtures verify self retention, duplicate self retention, external RefTable growth from four to thirty-two slots during root finalization, and collection finalization despite self retention (7,625 original/callback instructions, zero differences; report SHA-256 `24def17740fde84899488e781329abc76a699d863bf0f00137b6e01fa5cae3ef`). Once explicit external-reference retirement begins, new host roots still reject as a safety restriction pending late-destructor/reentrant RefTable evidence. Cumulative new object allocations during `Close()` are bounded by `MaxObjects`, including immediate arena-slot reuse; exceeding this authored safety bound records a callback diagnostic and still completes shutdown. Native debug hooks, userdata and complete shared-state destructor ordering are still open. Native Class finalizer `0x140264870` really clears its member-index and parent-class references. A forced class-before-derived-instance fixture verifies the parent link is already retired when native owner cleanup runs: constructor-validated owner records remain identified by their borrowed instance identity rather than revalidating the cleared script inheritance graph. Fresh true-reclaimer Thread Close fixtures pass with that native owner distinction. Runtime reclamation remains gated separately; close is not implemented by substituting an ordinary collection run.

Table node layout is now independently tied to successful original insertion `0x14025cb60` and lookup `0x14025ca70`: value is at node `+0`, key at `+0x10`. The earlier table key/value ordering statement used reversed field attribution; that claim is corrected rather than treating its old fixtures as a lookup oracle. Marker `0x14024e2a0` visits delegate, then key/value in each ascending node. Ordinary destructor `0x14025c610` drops delegate first, then releases raw key/value references while preserving their cell tags during child cleanup. Finalizer `0x14025ca50` instead calls `0x14025d2b0` to install null before each key/value release, then drops delegate. Two lookup-backed original-instruction cases use a string key, an actual userdata value and its observer-only native release hook. The key weak target expires before the value hook in both cases; the delegate is expired during ordinary value release and still alive during finalization. All 3,729 original/declared allocator-boundary instructions match with zero differences (report SHA-256 `d26514942ad15773cffc75c8fb4146251ebf25dc5c17f92123712f5d1e2fcb67`). Four independent supported string-key/Instance-release fixtures verify those node/delegate phases and one-iteration collection with an externally strong surviving table; the previous authored path fails the key-expiration check. The authored release cursor distinguishes ordinary raw-reference retirement from collection nulling and keeps only a borrowed delegate identity until its owned child cleanup finishes. Expired raw fields never become new owners or retarget recycled generations. Pointer-key buckets, retired raw-field access, table resurrection and reentrant delegate mutation remain open; runtime reclamation stays disabled.

Explicit table clear is a separate operation from both deletion and collection. Default callback `0x1402a1420` tailcalls API `0x140222320`, which invokes `0x14025c770`: clear each ascending node's key then value by installing null before releasing its old owner, leave the delegate intact, reset used count only after those callbacks, then force rehash `0x14025cf60`. Capacity drops by one halving step with a four-node minimum. Four genuine insertion/lookup-backed original-instruction cases at capacities 4/8/16/32 confirm key expiration, null current cells, old count during the value hook, retained delegate and final capacities 4/4/8/16 (10,480 original/declared-boundary instructions, zero differences; report SHA-256 `9e70c18da220aeebe5612d1fc5457592fd21d83e43454a655abd767c58e9b70f`). Four independent supported string-key/Instance fixtures additionally observe post-clear physical bucket iteration after reinsertion; the prior authored core fails the key-expiration assertion. Table/array clear now use the verified zero-value native status. The authored table-clear cursor rejects callback node-storage replacement before its next access rather than following freed native storage; broader reentrant mutation remains open. These corrections do not enable runtime reclamation.

Userdata is a real collectable arena type rather than a table or opaque C++ callable capture. API `0x140223cb0` allocates size plus `0x57`, with payload at `+0x50`, byte count at `+0x38`, release hook at `+0x40` and borrowed type tag at `+0x48`; `0x140223560` and `0x1402233b0` return the payload and byte extent. `CreateUserData` deliberately accepts fully initialized bytes; uninitialized native allocation contents are not reconstructed. `UserDataBytes` borrows only a live owner, and opaque type tags never imply ownership. Live payload storage is capped by the authored `MaxUserDataBytes` safety limit (8 MiB by default); retired arena storage returns that budget, and a callback allocation still accounts for its currently executing owner's bytes. Marker `0x14024e340` visits only the real delegate, then moves userdata after that descendant. Finalizer `0x140220ff0` releases only the delegate, preserving payload and hook. Last-reference release `0x140221290` calls the payload hook before ordinary delegate destruction, weak-self expiration and freeing; the native hook result is ignored. The nonthrowing authored hook boundary records host cleanup failures while completing deletion. Its opaque C++ callback captures require independently retained host roots when they own VM values; payload bytes are not traced as guessed pointers.

Thirty-five bounded typed original-instruction cases cover creation/payload extent, opaque tag round trips, null-delegate finalization, actual captured userdata/hidden-cell retirement, six genuine userdata/table cycles, direct/default/raw Get, `_get`/`_set`, and retained-self deletion (30,054 original/declared-boundary instructions, zero differences). Compact report hashes: lifetime `fb25153342f65d9962bf2badb3019c0ccc5d70d1ae77e1218fbb88d39110a83f`; tags `86612b8055af69cd62ba433906f96022b68c451ad94bd524faac00a6b9fa5dc7`; captures `90d74ccb1f803f95cf4c7934414238f0b2118d3ca1412b5b46f866ee17865753`; cycles `7b7f0f09a054d0f5c33c8d7a20672abbd55b58b8dea9ae476c141241fa439499`; retained self `b24d7aaf5f31dad37f39eab32b3ca5ed39a690e36a694b3c268778b9d36a0fac`; Get `7602573905e4a56c0a539a322efe5f919718cee21f6e200d4590cd6d1aea9dcc`; metamethods `b7059111f04f26a3a2c147180125891c472ec2584a4e35a4a852a243eacb7768`. Userdata has no type-default table of its own, but general Get `0x140259580` can obtain an ordinary Table default through its actual table delegate before trying its own `_get`. Native setter `0x14025b000` uses `_set` even when that delegate already owns the key; it does not mutate the delegate slot directly. Newslot `0x14025a4d0` rejects userdata without invoking `_newslot`. Nested delegated Table getter continuations remain unsupported and reject explicitly. Twenty-three independent sanitizer groups check these supported lifetime/capture/type/ownership phases, native fixed indices with zero/one/two supplied parameters, callback diagnostics, byte budgets and generation reuse.

A delegated Table's ordinary type default is found during recursive Get before an enclosing Table/userdata `_get` hook. Two genuine constructor/insertion/lookup cases independently verify Table and userdata receivers: a requested default value `73` returns without calling either installed getter (2,798 original/declared-boundary instructions, zero differences; report SHA-256 `6c65ce5d81587ab0065034aac7e12b805d232f7b00b67be20b19c415e981babf`). The authored host and bytecode read paths now select that real default before the enclosing metamethod. Four independent sanitizer groups cover both receiver types and both entry paths, identity of the registered default callback, actual weak-reference output and direct-slot precedence. The accepted baseline calls the Table getter too early. A missing default still follows ordinary Table getter behavior; the later iterative delegated-Get recovery below now covers its bounded nested userdata continuation. This original ordering checkpoint did not establish callback-mutation or delegated-reference retirement; the later delegated-Get comparisons below cover those bounded cases, and does not enable reclamation.

Userdata callback self retention differs from Instance resurrection: the original still deletes userdata with a newly retained reference count of one, leaving a dangling native owner cell. The authored path expires that generation and rejects later alias access rather than following or retargeting freed storage. Hook replacement/reentrant deletion, complex delegated getter continuations, complete userdata behavior during shared-state shutdown with externally surviving aliases, script-visible userpointer/thread values, compatibility adapter statuses and the existing collector gaps remain open. No supplied Windows function-pointer payload is executed by portable code, and this userdata tranche does not enable runtime reclamation.

Native class member tuples at `0x140265370` release attributes before the member value. The member-index table owns the name; field/method tuples hold only value and attributes, and instance storage holds only copied field values. Clone constructor `0x140264360` initializes an empty native user pointer and release hook. A bounded original-instruction fixture clones one genuine native instance field, then executes Class finalizer `0x140264870` and member-index table destruction `0x14025c610`. Name/value/attributes string reference counts become `1/3/1` after cloning; class retirement expires the name and attributes while the two instances retain their field value at count two. The earlier destruction-only fixture stored the two member-index node cells in reversed roles. The corrected fixture uses the encoded integer value at node `+0`, string key at `+0x10`, the actual native hash bucket, and a successful original Class lookup through `0x140221000`/`0x14025ca70` before cloning. All prior release/refcount/weak observations remain unchanged. The extended genuine-lookup case executes 3,336 native and declared-boundary instructions with zero differences (report SHA-256 `f0ea809bdd26ee4883044dc97d5dc14665ebbb25028cff2863eba26ecf269be6`). Only bounded allocation/free/byte operations and a checked one-field constructor iterator are supplied at the host boundary. The authored class/instance slots now borrow names from their actual member index, copy no attributes into instances, release tuple attributes before values, and leave cloned native cleanup empty. Class retirement clears its member-index table before releasing the parent Class; the native parent release observes the derived key already expired. Four independent sanitizer fixtures verify weak name/attribute expiration during class-before-instance Close, tuple callback order, member-index-before-parent retirement, and source-only native cleanup after opcode clone. Retired member metadata no longer resolves an instance field name. Native Instance finalizer `0x1402649f0` reads the current class field count, which becomes zero after Class finalization. Two further bounded original-instruction cases then execute the actual deleting destructor and its inline-field iterator (`0x1402645e0` / `0x14056e344`): finalization leaves those field references intact, deletion releases only the first inline field, and a second field remains retained at count two after both instances are deleted. This 8,236-instruction report has zero differences and SHA-256 `352cc73db6e70346eb1fa03fa0d5da5870b0544651bb57ae5b8b481fe082f95b`. Shared destruction `0x14024f4a0` frees the string table buckets without walking surviving interned strings. The authored runtime safely releases all instance fields; this measured native retention/leak behavior remains an explicit lifetime divergence, without introducing an unbounded portable leak. These corrections do not enable runtime reclamation.

Script closure deletion and collection have separate retirement phases. Ordinary destructor `0x140220a60` releases default arguments, captured values, the function prototype and then the weak environment wrapper. Finalizer `0x140220f90` clears only captures; defaults and environment survive until actual deletion. Two fully typed original-instruction fixtures verify ascending defaults-before-captures deletion and captures-before-defaults collection/deletion, then real function-prototype name/source retirement before environment-wrapper release. StartCall `0x14025b3b0` identifies weak environment at closure offset `0x30` and function prototype at `0x40`; genuine prototype release/destruction `0x14024e7f0` / `0x14024c9b0` distinguishes their final order. The earlier generic-reference fixture placed its weak wrapper in the prototype slot; it proved default/capture/weak-self timing but did not identify that slot, and is superseded by this fully typed oracle. The closure weak target remains alive throughout each child release and expires only in the final base destructor. All 16,123 original/boundary instructions match (report SHA-256 `3ea2e85a337697790b9b9357c4ad82c9dc8cc3fb4f90cc6ad41f1c5c47ab57db`); the only supplied boundaries are bounded allocation/free and byte operations. Authored script closures now preserve these phases, including raw slot identity during child callbacks. Collection and Close schedule last-reference deletion through the iterative release drain before expiring the parent identity. Four independent sanitizer fixtures cover ordinary and bound deletion, a finalized surviving closure with defaults, and immediate cyclic collection deletion; they also verify environment/prototype ownership through callbacks. Non-instance resurrection during deletion, reentrant mutation and additional shared-state roots remain open.

Native closures have a distinct metadata order: destructor `0x140220b10` releases its name and weak environment before outer values, while finalizer `0x140220fc0` clears only outer values. Two bounded original-instruction graphs verify name/environment/capture order and weak-self timing in 10,164 instructions, with zero differences (report SHA-256 `25919ef306ebb803aef16d8015f3ab340b592d83f41f84abe52859460ae50cc9`). The authored native closure retires its name and environment, then its stored VM captures and host callable owner, before expiring self. Two independent sanitizer fixtures distinguish ordinary deletion from a finalized surviving closure and observe the real shared host owner retained by bound/unbound callback clones. This does not model opaque C++ captures as native VM outer values. Original native Call `0x140254850` appends actual VM captures after user arguments and can leave them in allocated cells above restored logical top. Two further bounded original-call/SetTop cases verify those hidden references survive native closure deletion and logical stack shrinking, then expire when later pushes overwrite their cells (5,377 instructions, zero differences; report SHA-256 `edae567fb16e67c1d16656e8be259c6b2bbfa534e3fa8b0355cc396790fbaf5c`). Physical cells, call overlap and overwrite-based inactive ownership are now modeled as described above. `CreateNativeClosureWithCaptures` now accepts bounded VM values in original stack push order and owns their reverse order, matching `sq_newclosure` at `0x140223950` and native constructor `0x140220ec0`. The existing three-argument host API is preserved with no captures; the new span API is a host authoring bridge and does not consume the caller's physical native stack. Captures append to the actual callback operand cells after signature checking of user arguments only; the callback span exposes the same user/capture order. Four bounded original-instruction construction/call cases cover zero, one, two and five mixed integer/string values, vector growth, capture reversal, last-reference closure deletion and inactive capture expiry (6,438 instructions, zero differences; report SHA-256 `f1666d8b9cd05cb2b886251cb29138e98651b39e17a4b1d89df5ba8528b69611`). A separate original Execute/base-suspend case returns captured `23` with original receiver `17` (809 instructions, zero differences; report SHA-256 `afc5dc62633f6e2ca370c81a2d484df8930bb18035fe471ae1d7dd33fae210fb`). Eighteen independent sanitizer fixture groups verify direct and bytecode calls, user-only signatures, zero/positive/failed result ownership, physical hidden cells, reversed release order, bound clone ownership, raw weak/scalar values, captured-container collection, child suspension/wakeup and allocation/owner limits. Native capture append precedes weak receiver replacement. Three bounded original-instruction cases let an overwritten cell's declared last-reference callback observe receiver `17`, change its current cell to `99`, then return to CallNative. An unbound callback sees `99`; live and expired weak environments subsequently replace it with the bound table or null. All 617 original/declared-boundary instructions match with zero differences (report SHA-256 `3e98ebc8f86f4b7ea3f06ad6db5bf87b3ae453f901cdef2f8398b353a6d48063`). The authored path now copies captures into physical cells first, performs weak receiver binding afterward, and snapshots the resulting actual cells for its stable callback span. Three independent instance-release fixtures verify that ordering, receiver mutation and current-cell visibility; the prior snapshot path fails the unbound case. The native corpus uses a declared last-reference output boundary, rather than claiming engine Object destruction parity. Broader capture/receiver callback mutation, shrinking/growth and resurrection remain open. Opaque C++ callback captures remain distinct and do not become traced VM outer values. Native result-count handling is now exposed separately from compatibility value callbacks. Eight bounded original-instruction cases use real push primitive `0x14025ac20` from a finite synthetic callback, then execute CallNative `0x140254850` result replacement, cell clearing and weak expiration. Status zero replaces the output with null without clearing the last operand. Any positive count copies and clears only the current last operand, including when the callback pushes nothing; count two still returns one last value and leaves an earlier pushed cell owned. Negative status other than `-666` fails without replacing the previous output or clearing pushed cells; `-666` copies/clears the last operand and suspends. All 22,860 original/declared-boundary instructions match with zero differences (report SHA-256 `06a6bfdc3f1ea9aa167dd865be680ab72e351fd4b19e163beb3076528236d86a`).

Native Call `0x140254b17` decrements the per-VM callback counter immediately after the callback returns, before replacing the old output or clearing the last result operand. The call record remains active until the later base/top restoration at `0x140254c8e` and record retirement at `0x140254c98`. Eight bounded original-instruction cases observe callback-triggered inactive-cell destruction with counter one, then old-output destruction with counter zero and one still-active call record. All 29,940 original/declared-boundary instructions match with zero differences (report SHA-256 `1ef166ab2ac76bcd10c669b8530ca0d837564988d8cd63134d1776b17b9dc2c7`). The authored counter scope now retires at callback return, including explicit failed-result processing, while physical cells and call metadata retain their separate cleanup phases.

Four independent sanitizer groups create a userdata through a first native getter, retire the caller's register reference, then replace that owned VM temporary through a second getter with status zero, one, two or raw `-666`. The old output's native release hook makes a generic base-suspend call: the outer Execute counter plus the new callback counter equals the separately verified guard value two, and generic dispatch discards the suspension request. The prior delayed counter incorrectly rejects that call. These groups verify exact owner expiry, continued caller execution and null/scalar output, without relaxing any native or authored depth limit. Complete reentrant result/call-record retirement, compatibility callback statuses and existing collector gaps remain open.

`SquirrelNativeResult::NoValues()` selects status zero; `ReturnValue(value)` pushes one value and selects status one; `StackStatus(status)` consumes the actual current stack according to that status. `NativeStackSize`, `NativeStackValue`, `PushNativeValue` and `SetNativeStackSize` provide bounded raw native-frame cells, with checked one-based/negative indices and owner validation. They reject outside an active native frame. Positive return on an empty frame rejects before the original preceding-cell dereference. The explicit path never infers status from value type. Existing `{Value, Suspend}` callbacks retain compatibility behavior until their original status is verified; opaque C++ owners still do not become VM captures. Fifteen independent sanitizer groups verify zero/one-null/existing-top/multiple-value behavior, compatibility separation, typed failure/trap propagation, script suspension/resume, direct-call/getter/constructor suspension-request handling, invalid indices, empty return safety and reuse after failure. Fifteen existing focused VM/lifetime/close/error/context suites pass with the explicit API layout.

GenericCall `0x140254080` discards CallNative's local suspension-request flag: raw status `-666` returns its last value successfully without setting VM suspended state. The script CALL dispatcher interprets the flag; native generic getter and constructor calls do not. Eight original GenericCall cases preserve a preexisting owned VM temporary across every zero/positive/failure/suspend outcome (29,940 original/declared-boundary instructions, zero differences; report SHA-256 `b09cab7cfb44b517ad13337b0b114cf47da7bd30287341756b0def14aef30e37`). Metamethod helper `0x1402547fd` and Class constructor helper `0x140254144` both call this generic path. The authored explicit-status path preserves that distinction; compatibility suspension keeps its prior guards. The recovered base suspension helper below applies the separate guard in `0x14025b950`; other compatibility host callbacks still require their own verified native-status migration.


Base suspend thunk `0x1402a0a70` tailcalls API `0x140225340`, then guard `0x14025b950`. It first rejects an already suspended VM with `cannot suspend an already suspended vm`; otherwise it returns raw `-666` only when that VM's native-call counter equals two, or fails with `cannot suspend through native calls/metamethods`. The guard changes neither the counter nor the suspended flag. Twelve bounded original scalar cases cover counters zero, one, two, three, ninety-nine and one hundred with both suspended states (141 original/declared-boundary instructions, zero differences; report SHA-256 `dc9020516ec44b108303e4187e9c2577a39d0b10c9ffc4215680d6765910ebfb`). The error-format boundary records the actual diagnostic argument without executing CRT formatting.

Five additional original dispatch cases execute the real base thunk from GenericCall, script CALL, native getter, script getter with a trap, and a child VM called from a still-running parent native callback. Direct native GenericCall reaches counter one and fails. Script CALL reaches two, transfers its actual last argument and suspends. A native getter reaches two but GenericCall discards its local suspension flag and returns the key. A script getter contributes another Execute level: counter three rejects suspension and the getter's actual script trap catches the declared scalar error. The nested child reaches its own counter two and suspends while the parent's independent counter remains two; the parent then returns normally. These 5,818 original/declared-boundary instructions have zero differences (report SHA-256 `0fdb9c0257b96339632d1d29c83d151971799f804ed14809cb9ab58fafdbb9ad`). The bounded parent callback invokes original child GenericCall on the same native call stack; it does not call game/OS/CRT entrypoints. The failure observer supplies finite scalar error 91 instead of modeling CRT exception formatting.

The base callback now returns explicit native status, uses the per-context Execute/native/metamethod counter, and leaves suspension to the script CALL dispatcher. Explicit native statuses no longer inherit the compatibility callback's global invocation-depth restriction; native parameter, frame, counter-overflow, stack and instruction safety bounds remain in force. Twelve independent sanitizer fixture executions check receiver/value suspension and real resume destinations, native getter value-only behavior, direct guard failure and last-error retention, generic versus bytecode constructors, one or two child continuations during direct/native-script parents, caught same-VM reentry failure, and a script getter catching its guarded failure. All twenty earlier focused VM suites pass against the same private core. The earlier accepted implementation fails the native-getter fixture by treating the base helper as compatibility suspension. Other native adapters still using compatibility results are not migrated by this tranche, and complete nested/raw-status continuation and collector fidelity remain open.


Native wakeup API `0x140225bd0` checks the target VM's suspended flag, transfers the optional top value into its saved CALL destination, and calls Execute in continuation mode. It does not reject another VM merely because that VM is currently running. Thread execution `0x140267790` is its recovered caller: it pushes the retained wait result and invokes wakeup with input, return value and error reporting enabled, and throw mode disabled (`0x140267848`). Four bounded original cases cover direct and nested-parent wakeup, each with completed or repeated suspension. A finite parent callback calls the actual child wakeup on the same native stack while the parent stays at counter two. The first wake transfers integer 53; a repeated child suspends with 53, then later wakes and returns 59. The 7,340 original/declared-boundary instructions have zero differences (report SHA-256 `1bda13a2d84e129dcf0c2f256220e65509b57d2901ebb358d98756ee6e5eb0d2`). Only bounded allocation/byte operations and that declared parent callback are supplied; no game, OS or CRT entrypoint runs.

`ResumeInContext` now permits an idle suspended target while another context runs, then restores the actual parent context and running state on success, repeated suspension or failure. Nested continuation instructions consume the parent's remaining authored work budget instead of resetting it to the child's larger requested limit. The same global invocation-depth bound used by ordinary invocation applies before modifying the child. A target with its own active invocation/native callback still rejects as an authored safety restriction; full same-VM/raw-status reentry is not claimed. Twelve independent sanitizer fixture executions verify child and root-as-child wakeup from native/script parents, distinct replaced roots, repeated suspend/resume values, child-error ownership and parent recovery, shared work bounds, and depth-limit preflight preserving the original continuation. The accepted baseline fails the independent running-parent wakeup fixture. All twenty-one earlier focused VM suites and current Thread/Scheduler suites pass against the coherent private core. These changes preserve existing public APIs and the separate facade-output ownership relation. Native no-input/throw-mode/error-flag variants, complete reentrant cleanup and collector/root tracing remain open; runtime reclamation stays disabled.

Execute `0x140255630` uses its owned temporary at VM `+0x98` separately from a native callback's local result cell. CALL stores its target at `0x140255bd4`; successful top-level script LeaveFrame (`0x14025aef8`/`0x14025af1c`) replaces it with the returned value, while failed unwind retains the last target. Six fully typed original Execute/closure/prototype cases verify null return, returned target and failed native/script targets: parent and output retirement expire a target only when the VM temporary no longer retains it. The 9,091 original/declared-boundary instructions have zero differences (report SHA-256 `4acff3cd95a165d1ea39f6abab7ed88955b813bc75dae8f320b23844cb85b902`). The initial successful-null vector disproved retaining the last callable after every return. Initial generator entry has another return path: Execute returns the suspended Generator through its separate output while the VM temporary retains the original script closure. Two fully typed factory/Yield cases verify null/integer receivers, saved receiver values and all four closure references in 2,076 original/declared-boundary instructions, with zero differences (report SHA-256 `639b297a26bdb86f28442b975446c585111aee701bb87b9ff40d336442545ac2`). The authored context now owns and traces that persistent temporary independently of its facade output. Script Execute entry and bytecode CALL assign it; ordinary Execute-root return replaces it after call-record retirement, internal script return preserves it, and initial generator creation keeps the original closure. Direct native invocation clears the facade output without replacing the native temporary. Context Close releases the temporary after root/error state and before physical operand cells, matching the separately retained finalizer/marker evidence. Eight independent sanitizer fixture executions verify failed native/script targets, a successful internal script return followed by caller failure, successful null versus retained-target return, initial generator output, the authored ResetExecution control, and root-before-temporary Close callbacks. ResetExecution explicitly clears execution-owned output and temporary state while preserving root/handler/last-error state; that host control is not an original sq_settop fidelity claim. The handler root-order fixture now performs an actual script null return before expecting the previously captured handler to become unreachable: direct native seterrorhandler does not clear the separate temporary. Primary GET/PREPCALL/arithmetic/metamethod output writers, Class factory/constructor temporary replacement, and every nested output/reentry permutation remain open; this narrow call/return lifetime tranche does not establish complete temporary-cell fidelity or enable runtime reclamation.

Collector `0x140250120` marks these external roots, the root VM, shared tables, and type delegates. VM mark `0x14024e3a0` includes all allocated stack cells, variable arguments, active closures, root table, error handler, debug hook, and last error. Weak targets are not marked. Instance finalization `0x1402649f0` invokes its release hook before dropping the class and fields; base destruction `0x14024c850` then clears any surviving weak object's target. Five guarded private-VM measurements establish that immediate last-strong-release expiration occurs before collection (collector returns zero), self and two-array cycles both return one explicit finalize iteration, and externally retained self/pair cycles return zero until root release, then one. Surviving external weak wrappers become null targets in all unrooted cases. The 679-byte report hashes to `b392cd0fd962524d43d8e1d92a88a83a46a544f5d6846682f700783f3ba319c4`. The return count is not the total number of objects freed indirectly through reference release. Runtime reclamation remains disabled until native adapter registries, scheduler objects, and callback-owned values retain their explicit host roots and the remaining finalizer ordering is independently verified. Native marking sets the visited bit before descent, then moves each object to the marked list after its children: arrays visit ascending values (`0x14024de60`), tables visit their delegate then ascending key/value pairs (`0x14024e2a0`), and Class/Instance markers visit their native metadata relations (`0x14024dee0`/`0x14024e190`). The authored marker uses bounded iterative cursors in that order instead of reversing children with a pending-value stack. Four original-instruction array/cycle cases verify the restored list and a later collection's explicit finalizer order in 5,847 instructions, zero differences (report SHA-256 `1adc663a941b39771f5e05aee98576defb4a242bbe469e33e88bb9aae7544916`). A separate 726-instruction Class/attributes/Instance case verifies that a rooted Class whose attributes retain its own instance precedes that instance in the restored list (report SHA-256 `ae2da5f4bdc235c90250403f50c173fa9a1f3f02a39a05c8cb5ceac333c7b5d0`). Independent fixtures preserve the native `A,C,B,D` callback sequence after unrooting, first-visit placement of shared descendants, and marking/releasing a ten-thousand-level graph without recursive C++ calls. The forced Class-before-instance shutdown fixture now uses the same genuine attributes relationship and retains its retired-ancestry cleanup assertions. External RefTable nodes are scanned ascending. Native last-Release `0x140252e80` links the freed node at the free-chain head; insertion `0x14024fda0` consumes that node first. Growth `0x140250b60`/`0x140250070` doubles only a full table and reinserts its occupied nodes ascending, preserving their physical indices. A bounded original-instruction oracle verifies 234 AddRef/Release operations on twenty typed reference identities, duplicate counts, node reuse and capacity growth from four to thirty-two nodes (26,469 instructions, zero differences; report SHA-256 `96ca2d5f93a68134b26fdaafb44ed3553a08c7e8c615e126352ff11ff4003624`). The authored host-root registry now uses a preallocated LIFO free-index chain while scoped tokens keep stable indices. Independent sanitizer fixtures verify observable later-finalizer order, duplicate-root ownership, thirty-two-slot growth/reuse and retaining an owner from its last-Release callback. Native hash collisions affect lookup chains rather than this physical-node sequence, so no synthetic pointer hash is needed for the corrected traversal. Collection follows each current node's next link after finalization and before invoking last-reference destruction (`0x140250690`/`0x1402506a0`); it then installs only the marked chain. Three bounded original-instruction fixtures let a real instance release hook create an array through factory `0x140220d40`, own a temporary value and call original AddRef: the new allocation is excluded from that pass's restored list. A new self-cycle released before a subsequent mark remains alive outside the collector list, while keeping its external root through another mark reattaches it and permits a later one-iteration collection. The 4,027-instruction report has zero differences and SHA-256 `cd29c53dc1f59d6714f0337f22f7338d75463ef7012c842f81cf5c18fd3e221a`. The authored collector models those detached lifetimes with safe arena identities, captures the post-finalizer cursor, and can mark a previously detached collectable again. Cumulative allocation during one collection is bounded by `MaxObjects`, including repeated reuse of a single arena slot; exceeding this authored limit retains a release diagnostic and completes cleanup. Independent sanitizer fixtures cover all three native allocation cases, explicit cycle cleanup, allocation-budget failure and a closure destructor that retires its saved future cursor. Following a freed native next pointer has no safe portable interpretation: the authored path reports `Squirrel collector cursor expired during destructor callbacks`, restores reusable collection state and never retargets a reused generation. Native VM marker `0x14024e3a0` visits last error, error handler, debug hook, root, temporary, all allocated stack cells, variable arguments and active call closures, then moves the VM after those descendants. Two fully typed original-instruction graphs verify that sequence and shared-descendant first visits in 2,942 instructions, zero differences (report SHA-256 `80cc7554841fbc658e9c5d8815b2a9dd9159a3c81ac304c95de221454b0ad29c`). An allocated stack cell above logical top remains marked in both fixtures. The authored collector now starts its global root context regardless of the requesting context, marks its error state before root/temporary/operand values and call closures, then visits external RefTable values before constants. Independent sanitizer fixtures verify the later callback permutation from a child-requested collection and the ordering of an actual script error-handler capture. Physical script/native operand cells are scanned once in ascending allocated order rather than scanning copied logical frame vectors; explicit facade contexts still retain their modeled values until ReleaseContext. Remaining result/temp-cell reuse, native SQThread-as-value placement, debug/userdata tracing, unexposed shared-table/delegate roots, reentrant collection and the remaining mutation/resurrection cases remain acceptance requirements; these narrow traversal corrections do not enable the runtime gate. A registration table is not a substitute for tracing opaque C++ callback captures.

Primary bytecode GET (`0x1402563ad`/`0x1402565e5`), PREPCALL (`0x140256189`), arithmetic (`0x14025675e`), unary-negation metamethod (`0x14025768d`), and Class newmember hook (`0x140257f72`) pass VM `+0x98` as their output cell. Scalar negation writes its destination directly and preserves that temporary. Six bounded original Execute fixtures verify literal/register array reads, both PREPCALL forms, integer addition and scalar negation through a later typed failure: a fetched string survives container/prototype retirement until actual temporary replacement, arithmetic replaces the temporary, and scalar negation preserves its previous identity. The 10,632 original/declared-boundary instructions have zero differences (report SHA-256 `494476cb9dfcfc17322ea7393e982560e4c0cb1cbee9d1089614cf61f56f4fe0`).

Four bounded original general-Get/instance-metamethod fixtures target the actual temporary output pointer with native statuses zero, one, failure and raw `-666`. Zero replaces the old temporary with null while retaining the pushed inactive cell; one and `-666` transfer the actual pushed value into the temporary and clear the physical last cell; failure preserves the old temporary. Generic dispatch discards the suspension flag. These 9,394 original/declared-boundary instructions have zero differences (report SHA-256 `87b09e5eb1539d8fa854ab76b1f30f64dd9c8354dc95c9b6cd900073f2f98f67`). The authored native result path now targets that cell before physical result-cell cleanup; script metamethod continuations read the Execute-root temporary after cleanup. PREPCALL writes its receiver/function destinations only after the get continuation succeeds. Twenty independent sanitizer fixture executions cover the four read/PREPCALL forms, arithmetic replacement versus scalar preservation, and real native/script getter/newmember and native arithmetic/negation continuations. All seventeen prior VM suites pass against the same private source. The subsequent Class factory and encoded-index cases below recover additional temporary writers. Broader reentrant cleanup, the facade's additional result relation, native outer construction, debug/userdata/shared roots and compatibility adapter result statuses still prevent enabling runtime reclamation.

Class factory `0x1402551c0` writes constructor lookup through its supplied output cell. Bytecode CALL passes VM `+0x98`, so absent construction replaces the temporary with null and a constructor keeps the actual method there (`0x140255c97`). A direct generic Class call instead uses a local lookup cell (`0x1402540f5`): absent/native construction preserves the prior VM temporary, while a script constructor invokes Execute and retains its return value on success or its constructor after failure. The inline bytecode script constructor uses ordinary StartCall and does not replace its constructor temporary when it returns. Fourteen bounded original-instruction cases cover these two paths with absent/null-slot, native zero/value and script null/value/failure constructors: 35,058 original/declared-boundary instructions, zero differences (report SHA-256 `953bee7c3117d63d957157a3f07e1d647a01daed78df882422872a0e74737b6f`). A declared external reference keeps each freshly allocated native instance outside unrelated destructor traversal in this output-cell corpus; it does not establish constructor-instance deletion parity. The authored path copies the method into owned local storage before call/reentry, updates the bytecode temporary, skips a null-valued constructor slot as the native path does, and preserves the distinct generic/script entry behavior. Fourteen independent sanitizer fixture executions verify exact weak retirement of Class, constructor and prior/returned string identities, constructor invocation and typed failures; the accepted baseline fails the Class-retirement check. All eighteen prior VM suites pass with the corrected private core. The subsequent encoded-index corpus below covers the earlier Class/Instance lookup phase. Broader reentrant mutation/cleanup ordering and the facade's separate output relation remain open.


Class insertion `0x140264c40` keeps independent field/method tuple arrays and a separate nineteen-entry metamethod array. An existing field updates in place even when the new value is a function or static. A function with a recognized meta name is stored only in the meta array; it does not create an ordinary indexed member, and it can coexist with a same-name ordinary static scalar. Converting an ordinary method to a nonstatic field appends a new field tuple and redirects the member-index entry while retaining the old method tuple. Derived snapshots retain those unreachable tuples. Eight bounded original insertion/lookup steps execute 4,774 instructions with zero differences (report SHA-256 `c917b2cd9eee1a90c6211f201852972fcba46334a41fab61aede732d97166694`). Attribute setter `0x140265160` fails for an unindexed meta-only function; newmember opcode 60 ignores that status, so those attributes acquire no tuple owner. The authored storage preserves these distinctions rather than flattening meta names into ordinary slots.

Class lookup `0x140221000` and Instance lookup `0x140259e20` first write the encoded integer from the member-index table into the supplied output. Field indices carry `0x02000000`, method indices carry `0x01000000`, and the low twenty-four bits select that domain's tuple. Releasing the previous output can run a callback before lookup reads the encoded index again. Four bounded original cases execute 1,296 instructions with zero differences (report SHA-256 `4f28fb7efd6343bbaf169bc2126bd15c53f43687c144edcd66aeeb6d2c037a20`): one declared last-reference callback observes field index zero and can replace it through original assignment with valid method index zero, which redirects the actual lookup. This is an output/reentry boundary corpus, not an engine Object destructor parity claim. Primary GET/PREPCALL and bytecode constructor lookup now preserve that intermediate temporary and read its current index after release. Authored safety checks reject noninteger output or an index outside owned tuples instead of reproducing native raw-payload indexing or out-of-bounds access.

Field lookup resolves one weak target, whereas method lookup returns the weak wrapper itself. Constructor lookup uses that same field/method distinction; an expired weak field constructor becomes null and is skipped. API getsize `0x1402233b0` counts strings, arrays, tables and userdata, and routes Class/Instance to the unexpected-type error. The earlier authored Class-length shortcut is removed. Fourteen bounded original size/weak-lookup cases execute 1,327 instructions with zero differences (report SHA-256 `2af136c0c2c6eea59cc9801148ff6694c05908d49c0a14c7d930df7493f8aff7`). The size corpus observes the actual error branch at a declared status boundary; it does not execute CRT error formatting. Twenty-seven independent sanitizer fixture executions check separate meta storage, retained converted methods and derived copies, unretained meta-only attributes, Class/Instance intermediate release and valid redirection, explicit malformed-index bounds, weak field/method reads and constructors, and Class size rejection. The accepted baseline fails the independent meta-only ordinary-member check; all nineteen earlier focused VM suites pass against the same private core. These measured corrections do not enable runtime reclamation; outer capture construction, complete shared/debug/userdata roots, compatibility adapter statuses, broad reentrant cleanup and the documented retired-Class retention divergence remain open.

## Engine Thread adapter contracts

Class registration `0x1402268a0` derives `Thread` from native `Object` and binds
constructor `0x140225f40`. Constructor `0x140266bc0` starts with elapsed `0`,
priority `10`, timeout `-1`, step mode `0`, and state `NONE`. Native argument
slots are one-based: slot 1 is the instance, slot 2 is retained by Object at
offset `0x38`, slot 3 is the optional target, and later values are target
arguments. `fork` (`0x140268340` / `0x140267240`) constructs
`Thread(root, null, target, arguments...)`. The child's actual invocation
receiver is its VM root (`0x140267790`); the retained Object value does not
replace that receiver.

Target setup `0x1402670c0` clears waits, pending resource, arguments, and old
context. A string selects `LOADING_FILE`; any other value selects
`LOADING_FUNC`, with callable validation occurring during execution. The loader
is polled by `0x140267400`, rather than performing blocking resource I/O inside
a script callback. `Thread.exec` accepts its target in slot 2, resets/reloads
that Thread, registers it, and returns no value. Global `exec` does this to the
current Thread and suspends the old invocation. Global `system` forks and waits
for completion; `0x140268b20` copies the child's stored result into the parent's
resume value. A child's script error is delivered as that value, not rethrown
automatically in its parent.

| Thread method | Target | Result or mutation |
| --- | --- | --- |
| `getCurrentTick` | `0x140225fe0` | Wrapping signed 32-bit elapsed ticks. |
| `getStatus` | `0x140267f10` | `WAIT` if any child/list/timeout wait exists; otherwise stored state. |
| `getExitCode` | `0x140267ed0` | Stored script result/error value at offset `0x110`. |
| `getResult` | `0x140267ef0` | Integer native call status at offset `0x128`. Names deliberately preserve the original registration. |
| `stop` / `run` | `0x140269200` / `0x140269130` | Transition `RUN` to `STOP` / `STOP` to `RUN`. |
| `wait` | `0x1402694d0` | Set waits on the target Thread; this method itself does not suspend. |
| `cancelWait` | `0x140267970` | Remove subscriptions/targets and clear resume value. |
| `getPriority` / `setPriority` | `0x140226000` / `0x1402274f0` | Read/write signed 32-bit priority. |
| `getStepMode` / `setStepMode` | `0x140226010` / `0x140227500` | Read/write integer manual-step flag. |
| `step` | `0x140269150` | Execute only with step mode set, RUN, and no waits. Completed success returns its value; failed completion throws its stored error. |
| `cue` | `0x140267990` | Only for LOADING_FUNC: calls the normal Thread step with delta `0`. |
| `getstackinfos` | `0x140267f60` | Frame metadata table or no value. |

Wait setup `0x1402675c0` first clears existing waits and resume value. It scans
numeric timeouts, notification strings, and native Object instances. Only
nonnegative numeric values qualify; the smallest converted integer timeout
wins and its original numeric value becomes the timeout resume value. Timeout
expiry requires the decremented value to become strictly negative. Notification
matching `0x140269220` removes matching string waits, stores the matched target,
and clears remaining waits. Native Object completion subscriptions are retained
and detached through `0x140228e00` / `0x140229510`.

Registration `0x140268d00` also installs global `getCurrentTick`, `getDiffTick`,
`getCurrentThread`, `getThreadList`, `fork`, `exec`, `exit`, `system`, `wait`,
`notify`, and `execOnBase`, plus default priority `10` and states `NONE=0`,
`LOADING_FILE=1`, `LOADING_FUNC=2`, `STOP=3`, `RUN=4`, `WAIT=5`.
`getCurrentThread` scans the registered instances for a matching VM context and
throws `no thread` when absent. `execOnBase` (`0x140268120`) invokes its callable
on the base VM with root receiver, including nested calls on the same VM.
Its callback returns native `sq_call` status: success `0` produces a null script
result despite the temporary transferred value. Callback dispatcher
`0x140254850` explicitly writes null for native return count zero.

## Object properties and native formatting

Native Object registration `0x140226080` binds `_get`/`get` to `0x140228ac0`
and `_set`/`set` to `0x140228cd0`. A property name selects `get` or `set` plus
the capitalized first byte and unchanged remainder. The accessor is found
through raw receiver lookup and invoked with receiver or receiver/value.
If it fails, normal lookup/assignment falls back to the retained Object
delegate at offset `0x38`, only for tables and instances. Unbound closure
results from instance delegates are cloned with a bound environment through
`0x140221f70`; table delegates preserve the unbound closure. The portable
Object adapter retains base state in a checked Instance registry alongside
independent derived-native identities. `_tostring` accepts its string result;
a failed hook or non-string result recovers the original value's pointer diagnostic through `0x14025b9a0`, rather than throwing the hook's error.

Object getter `0x140228ac0` returns negative native status after a missing
delegate property. General get `0x140222a80` replaces its failed lookup error
with `the index doesn't exist`; CallNative's negative-status branch at
`0x140254b6c` restores the native frame without replacing the output. The
adapter now carries this expected failure through the existing physical
getter continuation instead of repeatedly throwing C++ exceptions. An owning
string-property query preserves a present null, the aliased key/result cell,
selected receiving context, and default-delegate behavior. This complements
the scalar conversion API, whose result must retire before its receiver.
Actual getters still run in order, and the native 100-level limit remains;
there is no cached miss or early cycle suppression. Independent fixtures
verify 99 admitted named getters before overflow, success after ten stateful
visits, delegate mutation, escaped result ownership, and separate callback
safety errors.

An Exit Game prompt observation exposed a three-object delegate cycle for
absent optional properties. The same hidden 1280×720 test reduced sustained
median Update time from 60.58 ms to about 13 ms after the status correction.
Seven focused ASan/UBSan fixture executables pass against both the optimized
public core and a coherent core with the changed Object/VM implementations
sanitized; the remaining core objects are optimized. Original keyboard navigation cancels
the prompt, a second close request reopens it, and confirmation reaches CRT
exit status zero in an owned hidden observer with that termination service
explicitly enabled. The public headless termination guard remains unchanged.
The supplied affirmative button bitmap is preserved. These checks establish
the reported update improvement and original script sequence, not complete
native window, pixel, audio, or whole-game fidelity.

Object subscriber append `0x140228e00`, matching unsubscribe `0x140229510`,
first-consumed notification `0x140229130`, and notify-all/clear
`0x1402292a0` guide the owned completion callbacks. Explicit native release
notifies before removing the record. Native script `destructor`
`0x14004b530` is an actual no-op. Native Object construction `0x140228600` retains a cached weak self, subscriber array and weak delegate wrapper; the adapter now externally roots those wrappers/array and borrows its script identity. Class metadata has an explicit host root. Release thunk `0x140039f80` invokes script destructor lookup `0x140228e80` before derived native destruction and base notification. Notification captures the initial subscriber count and rereads the current numeric index; a consumed callback removes that current index even if the callback has shifted entries. Independent fixtures distinguish that mutation behavior from snapshot iteration and verify delegate expiration, subscriber marking, destructor/native/base order, generation reuse and failure cleanup. Recursive notification, release during active completion callbacks, non-ASCII capitalization and broader reentrant array mutation remain open and reject explicitly. The release preflight protects derived native state before these unsupported callbacks can remove it.

The string-library registration at `0x140968fc0` installs `printf`
(`0x1402657e0`), `errprintf` (`0x140265850`), and `format` (`0x140265780`).
Formatter `0x140265f30` converts every `%s` argument through `sq_tostring`
before numeric conversion; an integer passed to `%s` is intentional.
Parser `0x140265d60` accepts flags `-+ #0`, at most two width/precision digits,
and bounded format fields. The authored formatter supports strings, characters,
signed/unsigned decimal, octal, hexadecimal, width/precision, and `%%`.
Print callbacks receive bounded owned formatting routed to a host log, return
null, add no newline, and stop at embedded NUL. `format` returns the full-length
owned string. Float conversions use promoted float32 and measured CRT
spelling/sign/precision behavior, including canonical negative `-nan(ind)`.
Native pointer fallback `0x14025bb9b` uses `(%s : 0x%p)` with the type name and value's pointer payload. Null uses zero and therefore formats as `(null : 0x0000000000000000)`. A guarded initialized-CRT capture measured eleven synthetic pointer vectors: `%p` emits sixteen uppercase hexadecimal digits without a prefix, while the enclosing format supplies `0x`. All text/count vectors match that recovered contract; the 911-byte report hashes to `f513aa426cfc1e301505a0df9a65937a28f48b9b19bf5c562c1290893019eab1`. Portable `ToString` uses each checked object's stable owned allocation address solely for this diagnostic. Distinct objects retain distinct addresses across arena-vector growth; addresses naturally differ between implementations and runs, so no fixed address or cross-run byte identity is claimed. [Microsoft's CRT format reference](https://learn.microsoft.com/en-us/cpp/c-runtime-library/format-specification-syntax-printf-and-wprintf-functions?view=msvc-140) documents `%p` as a pointer address; the exact game's measured formatter establishes its width and case.

Native scalar `tostring` (`0x14025b9a0`, format at `0x1405f83f8`) uses `%g`
with six significant digits; diagnostic helper `0x14025bd80` instead uses
`%.14g`. A scoped hook in the hidden/silent original game called its initialized
formatter `0x1400770d0` on 414 synthetic vectors. All authored text and byte
counts match exactly on Apple Silicon, including subnormals, extremes, signed
zero, infinities, NaNs, ties-to-even rounding, alternate forms, widths, and
precision through `99`. Native and portable JSONL both hash to
`9ba6fee1bccaca6cb67c71f21d6a59bc2b5ef58fe8891e9c09794a5f6b81d105`.
The hook checked the exact target, function signature, and initialized CRT TLS;
it wrote 34,590 bytes without changing executable bytes, game state, windows,
or audio. This measured corpus does not establish every host standard
library's `to_chars` behavior or universal formatting parity.

Math registrations `0x140969280` / `0x1409692a0` bind `srand` and `rand` to
`0x1402694f0` / `0x140269540`. Exact CRT `rand` `0x14057d7c0` updates the
32-bit seed with `seed * 0x343fd + 0x269ec3` and returns bits 16–30.
`srand` `0x14057d7ec` writes that seed. Thread-local initialization
`construct_ptd_array` at `0x14059262c` stores initial seed `1` at offset
`0x28`, matching the documented [Microsoft CRT contract](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/srand?view=msvc-170).
All game script contexts scheduled on the same original OS thread share it.
The authored math adapter owns that state and installs the recovered RNG,
abs/fabs/iabs, min/max, constants, and finite-domain mathematical callbacks.
`fabs` clears the sign bit; integer absolute value wraps `INT_MIN` unchanged.
Min/max compares each candidate against the retained value through native
ordering, keeps the first on ties, and preserves its original type. Numerical
wrappers convert input to float32, promote to double for the recovered CRT
operation, then narrow the result to float32. A second guarded initialized-game
hook retained 362 records: one environment record, 181 valid unary operations,
32 `atan2` pairs, 20 `pow` pairs, and 128 RNG outputs. All authored result bits
match on Apple Silicon; native and portable reports both hash to
`9297a0e9ee79171ab1fba62d552a69bf3dd2d575afe1c4ade28f026b195705ab`.
The probe restored the original thread's RNG seed, errno, x87 environment,
MXCSR, and Win32 last-error. Errno accessor `0x14058d560` verifies its location
at initialized CRT thread data plus `0x20`. The report occupied 21,944 bytes;
quiet scope and unchanged original saves/settings were checked independently.
This finite corpus does not prove every mathematical input or future host
library. Nonfinite/domain/overflow variants fail explicitly;
they are not marked as recovered. Math registration `0x140269b50` supplies
`RAND_MAX=32767` and float32 `PI` payload `0x40490fdb`.
Its closure catalog at `0x1409690c0` supplies exact reflected names and signed
parameter counts: unary functions, absolute-value variants, and `srand` require
two stack arguments with `.n`; `atan2`/`pow` require three with `.nn`;
`rand` requires one and `min`/`max` require at least two, all without type masks.
The adapter now installs these original metadata fields. Fixtures check reflection,
wildcard receivers, early numeric type rejection, and strict/minimum counts.

Scheduler ticks are units of 1/60 second. `0x1405361c0` converts performance
counter readings to integer milliseconds. Clock update `0x140535f20` computes
milliseconds delta times `60/1000`, adds the residual, rounds with `+0.5`,
clamps the step to at least `1`, and clamps residual below at `-2`.
`0x1404e0540` initializes fixed step to `1`; `0x1404e05d0` selects fixed or
variable mode. Scheduler bridge `0x14023b2c0` converts the selected float step
to integer, calls accumulator `0x1402694c0`, then ticks Thread instances.
Priority ordering and the tick-start snapshot at `0x1402686d0` ensure children
created during a tick first execute on a later tick.

ASan/UBSan execution fixtures pass arithmetic/coercion/wrapping, NaN payloads,
bitwise shifts, branches, sign-bit negation, container lookup and owner checks,
creation-time captures/defaults, native suspension/resumption, exception values
and runtime-error traps, generator yield/resume, constructors/inheritance,
class locking, array/table iteration, delegation/cycle rejection, base-library
operations, and instruction/register/allocation bounds. These fixtures use
independently authored instructions and do not include extracted game content.

A quiet console-only comparison harness maps the unmodified original EXE with
`LoadLibraryExW(..., DONT_RESOLVE_DLL_REFERENCES)` and calls only valid scalar
helpers, without the entry point, game windows, audio, or engine initialization.
On this Apple Silicon host, 196 value-pair equality comparisons, 784 relational
outcomes, and 777 `+`, `-`, `*`, `/` vectors match the portable VM's result types
and payload bits exactly. The corpus includes integer boundaries, mixed
precision, signed zero, infinities, two quiet NaN payloads, and signaling NaN.
The initial comparison exposed ARM versus SSE default-NaN differences; authored
floating operations now preserve SSE operand priority and negative indefinite
NaN `0xffc00000`. This corpus is measured evidence, not a universal floating
point or full-VM fidelity claim.

An expanded `%` helper attempt reached an unavailable dormant-EXE CRT thunk at
`0x1405ac420`; it did not supply a valid modulo oracle. That attempt exited,
and the owned harness now has a process-local error mode and exception filter
that exits directly without crash/debugger UI. Future probes must avoid
unresolved imports. The successful final corpus excludes that path. The
bounded probes and compact results remain under ignored `Files/research/vm/`.

The retained actual startup bytecode executes 24 instructions under sanitizers
with only recovered base callbacks. It creates the requested root array and
closure, then stops precisely at instruction 33 on an unavailable script-defined
helper. Streaming all 72 entries identifies `getSystemLanguage` as a 126-instruction
child of `script/init.nut`; init root instructions 36–38 install it. The native
main stream executes init at instruction 199 before the application includes
startup. The isolated startup probe deliberately omits that earlier phase, so
its stop is a missing script dependency, not evidence of an absent native
`getSystemLanguage` callback. No replacement helper was added to make startup
appear successful.
Deserialization of all 72 script entries, reported in `runtime.md`, is not an
execution-completeness claim.

Delegable metamethod lookup `0x14024cf90` uses actual table lookup `0x14025ca70`, which resolves exactly one weak-reference wrapper before generic call. Six genuine Table/userdata cases cover live, expired and nested weak `_get` slots (9,934 original/declared-boundary instructions, zero differences; report SHA-256 `2af7b93365f8eda11bdc737cd67a85bc00d03991f64260e35326cdd5143015ed`). Only the live single wrapper invokes the getter with the original receiver and key; expired and nested wrappers remain noncallable. The authored lookup now makes that same one-step resolution. Twelve independent sanitizer groups cover both receiver types and host/bytecode GET, raw table lookup's own one-step resolution and preservation of the stored wrapper's reference count. The accepted baseline fails the live case. Class metamethod metadata remains a separate raw-cell path. This original weak-resolution checkpoint did not recover nested userdata continuation or callback mutation; those bounded cases are covered by the later iterative delegated-Get recovery below. Complete collector fidelity remains open.

Native object-error helper `0x140225aa0` pins its supplied object, calls error assignment `0x14025bf90` and returns negative status. The assignment installs the raw incoming tag/payload and increments its strong reference before retiring the former error. String error helper `0x140225a60` reaches this same owned transition after formatting in `0x14025c140`. Six bounded original-helper cases cover scalars, canonical strings, userdata, raw weak wrappers, self alias and callback reentry (2,027 original/declared-boundary instructions, zero differences; report SHA-256 `6b0bb42f15633fd8db71931b7e96e97f4b682cdc4a80c862126afa1c529d5ea7`). The finite release hook observes the installed new error and may replace it with `73`; that later replacement survives. Another isolated VM error remains unchanged. These typed error-cell cases establish assignment and release behavior, not full native VM construction or collection.

The narrow `SetLastError` and `SetLastErrorInContext` operations mirror that owned assignment without executing an artificial call, wrapping an exception or dispatching an error handler. They preserve raw weak wrappers and canonicalize supplied portable string values in the owning VM. Explicit context selection can update the global/root diagnostic while a child remains running with its own independent error and native stack. This is separate from returning a negative callback status and provides no implicit throw/reset operation. Invalid foreign values, retired contexts and closed VMs reject before replacing the prior error. Eleven independent fixture groups cover old-payload release/reentry, self alias, per-context collector ownership, raw negative native status and unchanged invocation state, including caller-context release callbacks during explicit replacement of another context's error. Runtime reclamation remains separately gated.

Failed native getter recovery retires its physical frame before trying an instance default. Two bounded genuine cases use actual native metamethod insertion and instance construction, then general Get with a userdata output owner (3,508 original/declared-boundary instructions, zero differences; report SHA-256 `2ac7c814478371e1be3b9a469401333892c13310bf80f7d84f066d3635491652`). A successful zero-result callback replaces the output while its call record is present, counter is already retired and its receiver/key stack is still selected. A failed callback instead returns through native frame cleanup and argument pop before default lookup retires that output. The previous authored recovery runs too early inside the failed native frame. These direct Get cases use prepared genuine Class metadata plus original insertion/factory operations; they do not claim complete Class or VM initialization.

The authored failed-frame correction is now followed by an iterative delegated Get path. Native fallback `0x140259580` retains each delegated-table local, retires it after recursive failure, then rereads the receiver's current delegate before selecting its outer `_get`. The C++ locals are reference owners but are not independent collector roots. Table defaults are tried at the same level as the original; userdata does not receive a fabricated default delegate. Bytecode GET may perform its separate receiver/root retry, while host general Get never does. Thirty bounded original-instruction cases cover recursive native statuses, genuine script getters, direct slot/default controls, noncallable getters, delegate mutation during release, Class-valued getters, and collection inside a deeper getter: 73,497 original/declared-boundary instructions, zero differences. The retained reports include nested statuses (`9677c85de890fcc08775eb43470d7a054c556a14c2c2dbf95a3358c2cf441c3c`), direct mutation (`69b1e4c3d7599a2815cb6ef8ca57ca42860356b1b623f601957e8cf6fcc09b1e`), release-hook replacement (`bdb70e9e94edaee1415bb308e44ca67af33cbf5dd22ea5e02c9081edba34739d`), real script return/throw (`20b1980eb35e76dacf91402c1deae3add104cc6fb6557e2c5cd130788d142fa4`), and collector-local ownership (`e87d7c055f59e3392f356f893d9470c00cb8b7278a3db2b57d5500492d34d82c`). These are finite SHA-gated synthetic objects with explicit allocation/callback boundaries; full VM initialization and unbounded callback mutation are not claimed.

MetaCall `0x1402547a0` pushes receiver/key before resolving its owned method local. Generic call `0x140254080` rejects a noncallable inner getter without overwriting the previous last error (1,902 instructions, report `b48b8fe5ad7a399e12e62a0793d002653d4496a53066f2149bd879bfb48087f7`). Actual receiver/key arguments have no invented callable prefix. Both arguments retire before the local method and delegated-table owner. Class generic call creates the output instance before resolving its constructor and discards the constructor's separate unused result before popping MetaCall arguments. Five absent/native-constructor cases cover statuses `0`, `1`, `-1`, and `-666` (13,199 instructions, reports `397d68513756307cd157a486a72ee4f1122533aedb27a810aa4f024f965e9680` and `fc3bbb7e65612a36e0027a9b378b08348c8313b66eeec4bee8838084c654a378`). Four genuine script-constructor cases expose an additional alias: separate host output retains its created instance, while output aliased to VM `+0x98` is replaced by the constructor's actual script return or failed-call target (13,106 instructions, report `650b72c1d1e2826e08a30dbfd8e3133688cc7287da23023e04ad1ac420a1fc47`). The authored path preserves that observed alias rather than normalizing it to ordinary class-call behavior. Fifty-three independent sanitizer groups verify these paths, including release-hook stack accessibility, current outer lookup and pending-local collection.

`ReadSlotInContext` selects the supplied generation-checked context for the existing general-Get operation; ordinary `ReadSlot` selects the current context. Native `0x140222a80` passes its supplied VM through lookup and fixed-error replacement, so a scene helper using the base VM must not silently borrow its running child's root or last-error cell. The authored scope restores the caller on success, script failure and safety rejection. A nested script getter shares the caller's remaining instruction work instead of resetting its budget; the selected idle context owns its invocation state while another context remains running. Ten independent sanitizer groups cover both switch directions, direct Table/Instance reads, absent root retry, Class default exclusion, handler policy, old-error release/reentry, output-owner retirement, invalid contexts and preservation of a genuine suspended continuation. The existing Instance/scalar facade and native operand-stack key/output alias phases remain separately bounded recovery gaps; this additive API does not claim identical arbitrary native-stack operations. Getter execution in an already suspended target and newly suspending delegated script getters reject explicitly without consuming an earlier continuation. Class recursion and execution work remain bounded safety restrictions. Runtime reclamation remains disabled until the broader collector acceptance checks pass.

## Open acceptance checks

Remaining default-delegate edge behavior, native thread/userdata relationships,
metamethod fallback edge behavior, userdata/custom iterator relationships, tail-call/native edge paths,
numeric conversion overflow and nonfinite/domain mathematical parity, garbage collection, aggregate
heap accounting, and host engine bindings remain incomplete. The Line opcode
has no native debug-hook equivalent yet. Missing variants fail explicitly.
The recovered language helper queries debug/launch overrides, Steam language,
or OS language/country, then returns the original numeric language selection.
Its required native services are `System.getDebugBuild`, `Steam.GetLanguage`,
`System.getLanguageString`, and `System.getCountryString`; the helper itself
remains authored game bytecode.

Next: complete native adapter ownership and shutdown validation, verify finalizer callback mutation/order, then continue real startup against recovered engine bindings and retained native traces. The VM and the full game rewrite are not
complete.

Opcode SET `0x14025b000` has a distinct implicit-this fallback. After ordinary instance/table/userdata assignment and any `_set` attempt fail, receiver tag and payload must equal register0 before `0x14025b2c1` attempts an existing own key in the current context root through `0x14025d1d0`. It neither creates a missing root key nor follows its delegates. The authored continuation retains receiver/key/assigned value through script or native metamethods and collection; successful `_set`, including a false result, consumes the assignment. Independent fixtures and actual startup KEY_OK/KEY_CANCEL assignments pass this recovered boundary. Context-free host assignment and compound SET preserve their separate contracts.
