# Native Screen targets

Logical target: `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256
`75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`.
All addresses below identify read-only executable analysis.

`0x140245410` registers `Screen` as an Object subclass. It is separate from
LayerBase: a Screen owns an offscreen target and a root layer manager. Its type
tag is `0x14098ef78`, consumed by folder construction/owner selection. Folder
ancestors contribute camera transforms; a Screen terminates that transform chain.

Constructor `0x140244d50` accepts width, height, and an optional integer insertion
flag through native integer conversion. Driver creation `0x14023acf0` constructs
LayerManager with reset-transform flag one and MVirtualScreenDriver
`0x1404f9fa0`. A zero insertion flag appends the screen task; nonzero inserts
before existing tasks. The target extent is stored as two uint32 values at
driver offsets `+0x30/+0x34`. Target format/depth come from the active driver;
exact format/depth and screen projection sampling remain open.

The portable registry requires an actual owned renderer allocation, validates its
returned extent, retains it for the screen lifetime, and preserves front/tail
task order. It preallocates bounded screen and layer-manager slots. The macOS
factory currently supplies the existing offscreen Metal sprite renderer.
Registry iteration/state mutation performs no target allocation or compilation.
Queue scheduling, complete scene traversal, and projection import are separate
unfinished integration work.

`getBounds` at `0x1402451f0` returns a fresh table with left/top/right/bottom and
width/height. It converts actual integer dimensions to float32, multiplies each
by one half, and subtracts the centered endpoints to recover dimensions.
Visibility `0x140245dd0`/`0x1402453e0` stores/tests the target task's inverted
hidden byte. `setBackgroundColor` at `0x140245d30` forwards four raw integer
channels to `0x1404c5d90`, which stores them at `+0x58` through `+0x64`.
The native thunk `0x14005ff70` initializes all four channels to zero and ignores
failed integer conversions. Clear-color conversion at frame submission remains
under recovery; stored values are retained without guessed clamping.

Independent offscreen Metal checks verify three owned extents, odd-dimension
bounds, insertion order, native Object properties, color conversion/defaults,
Screen-versus-LayerBase inheritance, folder Screen ownership, explicit lifetime,
and actual RGBA8 clear output. No window, activation, or audio output is used.
Dimension/capacity/pixel budgets and stale-handle rejection are authored safety
limits, not claims about native validation. The borrowed script registry and externally rooted class metadata now connect to the single Object release bridge. Opt-in cycle collection releases the actual Metal target and detaches a separately rooted folder, preserving its scene state while clearing the weak Screen owner. Both immediate folder cleanup and Screen target reclamation pass sanitizer fixtures. Broader finalizer ordering, VM-close integration and full ScreenProjection behavior remain open.
