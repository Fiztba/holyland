# Fixed game updates and independent rendering

Patches 0057–0058 add an opt-in performance mode. It preserves the original
US game's NTSC cadence, **30000/1001 updates per second** (29.970, conventionally
called 30 Hz), while presenting additional interpolated graphics frames.
No assets or generated game functions are replaced.

## Running

Build the complete patch series with the bounded native VU catalog enabled
as described in [Windows setup](windows.md) and [optional PGO](windows-pgo.md).
The new mode is selected explicitly; the existing launchers retain their settings.

```powershell
./scripts/windows/Run-KFIVPerformance.ps1 -GameDirectory C:\KFIV-PC -RenderFps 120
```

An executable outside that game directory can be selected with `-Executable`.
`-OriginalFrames` retains fixed simulation timing but disables interpolation.
`-RenderFps 0` removes the host presentation limit; it does not accelerate
the game. A requested limit is not a guaranteed achieved frame rate.

On Linux, after building a native runner:

```sh
PS2X_PRESENT_FPS=120 ./scripts/04-run-performance.sh
```

`PS2X_RUNNER` and the existing game-directory configuration still apply.
Use `PS2X_FRAME_INTERPOLATION=0` to retain original graphics frames. A supported
Vulkan GPU is needed for the measured Windows rendering performance; CPU GS
remains available with `PS2X_GS_BACKEND=cpu`.

## What changes

- A game-identity and instruction-signature guarded override replaces the
  original timer/FIELD polling limiter with rational host deadlines. It yields
  to the existing EE scheduler, then resumes the original buffer-swap and
  return sequence. Hardware fields, IOP service and guest callbacks continue
  during that wait. Combat counters, input handling, physics and animation
  updates are still executed by the original game code.
- Bounded VU execution reuses an admitted native descriptor and specializes
  ordinary FMAC flag handling. Boundary/cancellation cases keep the exact
  arithmetic path. MSVC x64 saves/restores the full MXCSR environment directly;
  other platforms retain their portable floating-point environment path.
  Cooperative VIF service uses a bounded 2048-cycle quantum.
- The original graphics backend still receives every game command. Each
  completed frame captures that backend's image and VRAM. One independent
  presentation backend replays owned commands to render intermediate triangle
  positions. Its video memory never feeds back into the game or subsequent
  frames. Capturing the original endpoint requires a synchronous GPU readback;
  the former continuously replayed reference renderer has been removed.
- Only perspective-textured triangles with compatible material/UVs and an
  unambiguous nearby predecessor interpolate. HUD sprites, new/clipped geometry
  and rejected matches retain their original frame positions. Before using a
  frame, its unmodified replay must exactly match the actual game's presentation.
  Mismatches use that original image. Command and frame queues are bounded;
  overflow disables interpolation for that session instead of growing forever.
  Non-game timer resets suspend capture immediately. If the original game
  enters another loop without that signal, a 100 ms missing-boundary timeout
  returns to live presentation. The next gameplay boundary checkpoints fresh
  memory, so inventory, pause and loading do not retain an old game picture.

Interpolation introduces approximately one game update of visual delay. It
does not change simulation timing, but that presentation latency is a real
tradeoff. Matching is conservative and heuristic; not every object is smooth,
and endpoint equality alone cannot prove every intermediate image is correct.
Long stalls discard accumulated deadlines after four periods. This mode cannot
promise 30 updates/s when the host takes longer than a frame to execute the game.

## Native Windows measurements, 2026-10-10

**Local build correction:** the measurements in the following table used
stale generated EE sources that predated patch 0029. The source fix remained
in the PR, but the local delivered executable reintroduced the cave-door
floor fall. These numbers describe that faulty build and must not be treated
as acceptance measurements of a gameplay-preserving build. The same stale
sources were used for the earlier Ubuntu retail captures below. Runtime/VU
source-only tests are independent of that generated output.

With freshly regenerated game code, a new 30-second capture measured
**29.997 updates/s** (900 updates / 30.0032 s), with a **0.505 s** original
15-update attack windup, a 120 FPS presentation limit and roughly 75–90
graphics frames/s in the opening view. Animation/cooldown values remained
contiguous; one animation/loop-label check was false under non-atomic reads.
This corrected build was then driven through the cave, opening the door and
walking into the next map without falling through the floor. Floor contact
remained stable after additional movement. This is still a limited-area
measurement, not a whole-game locked-30 guarantee; the older table below is
retained only as explicitly qualified historical evidence.

Same i9-13900KF / RTX 4090 / 64 GiB machine, native Windows, Vulkan, strict
MSVC 19.44 arithmetic, bounded native VU catalog, upstream static lift disabled.
The retained binary uses `/O2 /Ob2 /DNDEBUG /Zi /fp:strict` and MSVC PGO.
Use that `CMAKE_CXX_FLAGS_RELWITHDEBINFO` value when reproducing this candidate;
the older general Windows guide still documents its historical `/Ob1` build.
Its final link reported profile optimization
for 4970/5317 functions and all profiled dynamic instructions, with no mismatched
profile counts. The training profile preceded the final helper-inlining edits;
this is not a new profile trained on every final function. See [PGO](windows-pgo.md).

The opening area's original loop, attack animation and cooldown were observed
through read-only process memory at 5 ms intervals. These are game-update
measurements, not estimates from window FPS or display-buffer flips.

| Candidate | Updates/s | Original 15-update attack windup |
|---|---:|---:|
| Previous PR foundation, locally reproduced PGO build | 26.933 | 0.595 s |
| Fixed deadlines, original graphics frames | 30.000 | 0.500 s |
| Interpolated build before transition recovery, 120 FPS limit | 29.966 | 0.500 s |
| Same build, 300 FPS limit | 29.967 | 0.505 s |
| Final build after inventory and pause/resume, 120 FPS limit | 29.733 | 0.500 s |

The first interpolated 30-second capture observed 899 updates in 30.00037 seconds.
The attack animation and cooldown values advanced through their original
sequences. Graphics presentation was roughly 80–90 frames/s in this scene,
with a requested 120 FPS limit. A prior PR run recorded 28.20 updates/s under
different conditions; the 26.933 row is the local comparison used here, not
a revision of that older measurement.

Raising the limit to 300 retained 899 updates in 30.00000 seconds and the same
contiguous attack/cooldown sequences. Actual graphics stayed around 80–100
frames/s in that scene; the higher limit did not accelerate gameplay. One
animation/loop label alignment check in this second capture was false despite
contiguous animation values, reflecting the non-atomic sampling limitation.

The final build fixes capture suspension/restart across non-game loops. Inventory
and pause were visually checked, and interpolation resumed with zero endpoint
fallbacks. The subsequent 30-second capture observed 892 updates: some frames
still overran the target despite the correct 0.500-second attack windup.
Thus this is a substantial improvement and working independent presentation,
**not an unconditional locked-30 claim**. The final capture also retained
contiguous animation/cooldown values with a non-atomic loop-label mismatch.

Read-only sampling is not an atomic guest snapshot and adds some overhead.
Timing brackets are about 5 ms plus host scheduling jitter. Animation windup
is the original hit-window threshold, not confirmation that an enemy received
damage. An average near 29.97 does not prove every frame met its deadline.
No whole-game performance guarantee follows from this opening-area capture.

## Correctness checks

The local regression was traced to reused generated EE C++, rather than a
reverted patch. Rebuilding the patched generator and regenerating the owned
ELF again reported 28,429 processed / 28,161 recompiled / 268 stubs, 1,352
warnings and zero errors. Exactly 124 generated sources changed; the entire
generated C++ set matches the previously cave-tested output. All 148 patched
square-root sites passed the new staging/build guard. Native Windows execution
of the real translator passed 49 checks, with nine prior-bug control checks.
The five authored guard tests passed on both Windows and Ubuntu.

A separate corrected Windows session verified more than 4500 completed frames
against the actual game backend's complete RGBA output and 4 MiB VRAM, including
turning, attacking and opening/closing inventory. It reported no mismatches or
endpoint fallbacks and exited normally. The cave-route session also exited
normally and kept its test cards separate from the user's saves.

Native Windows and Ubuntu both passed:

- Three new fixtures using the rational pacer, actual runtime scheduler and
  actual CPU renderer. They check continued physical fields and callbacks,
  original continuation/register handling, re-registration, teardown, a real
  intermediate image different from both endpoints, and unchanged guest VRAM.
  They also check non-game suspension, missing-boundary fallback and fresh
  capture after returning from another scene, on both platforms.
- Seven compiled authored VU differential CTests and seven bounded native VU
  stepping CTests. The additional stepping run covered 334,058 checks over
  12,208 calls. Comparisons include flags, registers, cycles, memory and packets.

Native Windows retail verification additionally ran more than 2400 completed
frames with camera turning and attack input using `PS2X_INTERPOLATION_VERIFY=1`.
Canonical replay matched the actual game backend's complete RGBA image and
all 4 MiB of VRAM on every checked frame, without fallback. That expensive
verification mode is disabled in performance measurements. Subsequent retained
runs also reported zero endpoint fallbacks.
The final transition-aware binary then completed another verification session
with inventory, pause/resume, turning and attack input: more than 2000 completed
frames matched complete RGBA and VRAM, with zero verification failures or
endpoint fallbacks and a normal exit.

The older foundation's broader tests and gameplay observations remain recorded
in [Windows validation](windows-validation.md); they were not all rerun for this
change. A full playthrough, every later scene and real save/load remain unverified.

## Ubuntu and the owner's merged work

The comparison point is upstream `8719d72d3f258cfc194ac23a5af66889bcc38ef7`,
the merge of [owner PR #11](https://github.com/Voicedrew11/holyland/pull/11).
It was already an ancestor of this branch before these edits. Its VU census,
disassembler, opt-in static lift, lifted-source staging and handoff notes remain
intact. Patches 0040–0041 are unchanged.

The bounded native catalog in patch 0047 is a separate implementation from the
owner's `PS2X_VU1_LIFT` experiment. These tests keep `PS2X_VU1_LIFT=0` and do not
claim to validate that experimental path. The new Ubuntu changes address the
newer bounded-catalog implementation and its tests:

- Give generated catalog includes an explicit source include directory; GCC
  does not use MSVC's enclosing-include search behavior.
- Explicitly capture the constexpr FMAC descriptor in generated lambdas.
- Narrow Mersenne Twister results to `uint32_t` before `bit_cast<float>` in
  fixtures; Linux's `uint_fast32_t` result type can be 64 bits.
- Link the new fixture to native Linux libraries and use portable environment
  setters. No Windows object or PGO profile is used by the Linux build.

The stale-source guard is shared by Windows and Linux; it does not modify
the owner's runtime work. Shell scripts are now explicitly checked out with
LF endings so WSL can execute a Windows checkout with `core.autocrlf=true`.

Ubuntu 26.04 under WSL2 used GCC 15.2 and CMake 4.2.3. The complete native Linux
runner was compiled on ext4 from the same runtime and privately generated game
sources, with a freshly generated native VU catalog. Owned disc files were
read through the Windows filesystem mount.

WSL's available hardware graphics route was Mesa Dozen 26.0.3 over Direct3D 12
on the RTX 4090, not the native Linux NVIDIA Vulkan driver. The interpolated
game run failed before gameplay with `Failed to end command buffer`,
`D3D12: Removing Device`, compute-pipeline errors and process signal 11.
Passing Linux source tests therefore does not establish a working Vulkan
game session or the Windows performance figures on Linux.

A control run with interpolation disabled failed at the same initial shaders
and device removal. The failure is therefore not specific to the additional
presentation renderers; native Linux Vulkan remains unverified here.

The CPU-renderer fallback completed actual Ubuntu game sessions and
30-second, 6001-sample captures with normal exits. Before transition recovery
it measured **23.765 updates/s** and a **0.650 s** original 15-animation-frame
windup. The final build measured **21.700 updates/s** and **0.680 s**.
Animation and cooldown value sequences were contiguous. Animation/loop label
alignment checks were false (the final windup had 16 observed loop labels);
those reads are not atomic, so it is not evidence of a skipped frame. CPU
replay could not keep up and correctly hit the bounded queue fallback to
original presentation. This is functional Linux execution, **not a successful
Linux 30 Hz / high-FPS result**. The final transition-aware build and all three
new fixtures were rebuilt and rerun on Ubuntu. No system driver or security
policy was changed.

After regenerating the corrected game code, the Ubuntu runner was rebuilt
and the three fixed-frame CTests passed again. A fresh CPU-renderer session
completed a 30-second / 6001-sample capture at **21.933 updates/s**, with a
**0.700 s** 15-update attack windup. Animation/cooldown sequences and their
observed loop alignment were contiguous. This replaces the stale-source
CPU measurements as the corrected build's result; it still falls short of
the 30 Hz target. The unchanged WSL Vulkan failure was not retested for this
generated-code-only correction, and cave-door progression was not tested on
the Linux CPU renderer.

The final 58-patch series was applied to a fresh pinned checkout, first in
tools-only mode and then in full. Repeated application verified all 58 patches
and 83 managed files without changing them.

## Menu vibration and character loading follow-up (0059–0060)

The vibrating title/menu image was alternating between two half-height
fields. Patch 0059 reconstructs stationary complementary rows using the
field offset of the actual draw, rather than the host presentation tick.
Changed pixels keep the current field; repeated presentations cannot make
stale opposite-field pixels valid again. Mode/offset changes and overlapping
VRAM transfers invalidate history. Texture uploads outside the framebuffer
do not discard an otherwise valid field. This only changes host scanout;
guest VRAM, original draw commands and simulation are untouched.

An isolated native Windows title/menu run exercised Start and selection
changes. The static logo region had **zero changed pixels** across four
settled intervals containing 131, 67, 59 and 184 captures. The previous build
visibly alternated that region. The ten hardware Vulkan CTests passed,
including scanout/viewport controls, and the new source-only reconstruction
fixture passed on both Windows and Ubuntu. That fixture covers moving
pixels, repeated/skipped fields and stale-history resets.

A separate new-game investigation reproduced character loading stuck
alternating between sound-bank transfer stages 3 and 4, leaving the NPC model
uninitialized. A synchronous IOP RPC had no kernel thread to suspend, so
WaitEventFlag returned the polling error before DMA completion. The original
sound driver ignored that error and reported the transfer ready. Patch 0060
services IOP hardware and workers during these synchronous waits. It also
allows DMA hardware to finish while interrupts are masked, defers interrupt
callbacks until delivery is allowed, and reports interrupt context correctly.
The synchronous bridge retains a one-second emulated-time timeout with an
explicit diagnostic; this is not a complete asynchronous IOP RPC scheduler.

The 118-check synthetic IOP suite passes on native Windows and Ubuntu; the
pre-change runtime fails nine assertions. Sound-DMA cancellation (including
its guard-disabled negative control), CD audio and the 607-check SPU2 suite
also pass on both platforms. The private Windows new-game run loaded the
previously failing character banks, traversed the cave door without falling
through the floor, and rendered the first NPC seated on his rock. This is
targeted regression coverage, not a full playthrough or proof that every
character-loading path is correct.

Ubuntu's rebuilt CPU-renderer runner completed another 30-second,
6001-sample gameplay capture at **18.699 updates/s**, with contiguous animation
and cooldown values and matching loop alignment. This run overlapped native
validation on the same machine and is a functional check, not a controlled
performance comparison. The WSL Dozen Vulkan limitation described above
remains; Linux hardware presentation and the NPC route were not verified.
An isolated Windows capture beside the now-visible NPC measured **13.733
game updates/s** over 15 seconds / 3001 samples (206 original loop updates),
with 120 FPS requested and interpolation verification disabled. Thus the
opening-area result does not extend to this scene. These fixes do **not**
establish the requested locked-30-Hz result throughout the game.

The owner's merged PR #11 remains the latest merged upstream PR at this
check. Patches 0040–0041 are byte-for-byte unchanged. The earlier Linux build
additions retain that work and add native-VU configuration and stale-source
validation; 0059–0060 introduce no Linux-only overrides. Both follow-ups were
exported from persistent runtime commits, and tools/full/repeat application
of the **60-patch series** succeeded on a fresh pinned checkout, verifying
**87 managed files**.

## Fading title text follow-up (0061)

The stationary-logo checks above missed a remaining defect in the fading
"PUSH START BUTTON" prompt. Patch 0059 treated every RGB change as motion,
so fading letters fell back to duplicated half-height rows. Their shape
alternated with the field even while the logo stayed perfectly still.

Patch 0061 keeps complementary rows when local RGB change is at most 16/255,
then smoothly transitions to spatial interpolation by 32/255. Changes
accumulate until the opposite field is refreshed; repeated small changes
cannot indefinitely reuse stale pixels. Spatial interpolation uses the
neighbours on the correct sides of each missing row for either field phase.
The original field's known rows, fade brightness values, guest VRAM, draw
commands and game timing are unchanged. This is bounded display filtering,
not perfect motion estimation; low-contrast motion can blend briefly.

Native Windows: all ten hardware Vulkan/reconstruction CTests pass. The
expanded synthetic fixture covers both directions of a fade, cuts, both
field phases, repeated presentations, accumulated change and reset. An
isolated rebuilt-game run captured 346 title frames spanning multiple fade
cycles, then exercised Start, Down and Up. The prompt retains its outline at
dim, intermediate and bright levels. The static logo has zero changed pixels
across that title interval and three settled menu intervals (44, 44 and 48
captures). This check includes the fading prompt, unlike the prior logo-only
comparison. Original assets and timing are retained; no full playthrough is
claimed for this presentation-only change.

Ubuntu 26.04 / WSL2: the same reconstruction fixture passes with GCC 15.2
(`-O2 -Wall -Wextra -Werror`), the actual Linux runner rebuilds, and an isolated
CPU-renderer run reaches the title and exits normally at tick 1200. CPU
presentation does not execute the Vulkan reconstruction stage; its portable
algorithm is covered by the fixture. Linux hardware visual verification is
still blocked by the previously documented Dozen driver failure.

The owner's merged PR #11 is still the latest upstream merge and its patches
remain unchanged. This fix is shared by Windows/Linux and adds no platform
overrides. All 61 source-exported patches apply in tools/full phases and
repeat verification on a fresh pinned checkout, with 87 managed files matching
the development source. The overall locked-30-Hz goal remains unfinished.


## Equipment and sky corruption follow-up (0062-0064)

The blank Equipment contents and corrupted sky after closing menus were
reproduced with Vulkan and the CPU renderer, with interpolation disabled and
with original pacing. They were not caused solely by extra graphics frames.
The retail menu routine starts GIF source-chain DMA with QWC/MADR already
pointing at a scratchpad packet and CHCR.TAG=REFE. The runtime ignored that
payload and fetched an unrelated old TADR, feeding texture/other data to the
GIF command parser. A live trace caught a 524304-byte incorrect submission
and repeated bogus PRIM writes while Equipment was blank.

Patch 0064 consumes the active payload first, honors REFE/END and IRQ+TIE
termination, advances CNT to the following tag, and makes the native GIF
shortcuts decline transfers with active QWC. This follows PCSX2's
[`dmaGIF`](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Gif.cpp) and
[`hwDmacSrcTadrInc`](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Hw.cpp)
behavior. No game code, inventory, assets or gameplay rules are changed.

Two independently verified corrections are retained. Patch 0062 compares
interpolation against the actual rendered endpoint and reseeds its private
VRAM every frame, preventing replayed state from accumulating. A retained
CLUT regression fails six assertions with the previous implementation; all
four fixed-frame tests pass on Windows and Ubuntu. This requires an actual
endpoint readback and is not presented as a measured performance win.
Patch 0063 preserves 32-bit PACKED vertex depth without rounding through
float. It passes 77 packet/raster assertions on each platform; the prior
frontend fails 30. Neither change alone fixed the retail Equipment failure.

The DMA package passes 245 existing chain checks and 267 new payload checks
on native Windows and Ubuntu. The actual pre-0064 source fails 127 new
assertions on each platform. Both GIF shortcuts are tested with valid
fresh-chain positive controls as well as preloaded-payload rejection.


Native Windows retail validation used the new PGO binary with fixed updates,
120 FPS presentation requested, interpolation and full endpoint/VRAM checks.
A fresh new game crossed both cave doors, reached the seated NPC, opened
Equipment, returned to an intact cloudy sky, picked up the wooden club, and
selected/equipped it through original inputs. The preview showed the club in
hand, its E marker and 4.4 carried weight. Reopening the main menu while facing
the sky worked. The isolated run exited normally after 1122.457 seconds; its
last interpolation report had 6480 captured frames, 6472 exact endpoints and
zero fallbacks. This was correctness testing, not a controlled benchmark.

Ubuntu's rebuilt CPU-renderer binary completed new-game, turning, Journal,
Equipment and menu return checks in the opening area and exited with status
zero. Equipment's category/name labels and character silhouette rendered;
closing it restored the scene. The initial route helper failed to discover
RAM within its limited scan prefixes and never started its route. A separate
read-only controller validated the ELF/archive and full mapping, then drove
the actual menu checks. Thus the NPC route and weapon pickup/equipping are
**Windows-only** results. No guest state or inventory was patched during the
accepted tests. Linux hardware Vulkan presentation remains unverified because
of the previously recorded Dozen driver failure. The portable fixtures and
actual CPU game execution are separate evidence from Windows Vulkan.

All 64 source-exported patches apply in tools/full phases and repeat
verification on a fresh pinned checkout; all 87 managed source files match
the persistent development checkout. Owner PR #11 is still the latest merged
upstream PR, and patches 0040/0041 are unchanged. Existing Linux script
additions only enable the bounded native catalog and reject stale generated
sources; this follow-up adds no Linux-specific runtime behavior. Its test
package now supplies the required GCC SSE4.1 flag. The overall locked-30-Hz
performance goal remains unfinished.

## Manual mouse engagement (0065)

The user's installed Verdite 2 and 3 sources use Escape for mouse capture
and Tab for Menu/Back. Their remaining keyboard bindings already match
Holyland: WASD, arrow keys, Space, F, Q, Enter and Right Shift. Patch 0065
removes click-to-capture and the extra Escape menu binding. It separates
pointer engagement from the gameplay camera gate: pickups, dialogue, menus
and pause keep the chosen engagement state. Camera motion is still discarded
while the game blocks looking, and held mouse buttons cannot change meaning
across a context transition until released. Focus loss releases the pointer;
a fresh Escape press engages it after returning.

The actual pad mapper passes 496 checks under native Windows MSVC and Ubuntu
GCC, covering every keyboard binding, Escape press/hold/release, clicks while
disengaged, focus return with Escape already held, both gameplay transitions,
blocked/stale camera displacement, mixed controllers, the indicator and
concurrent snapshots. Device functions are simulated in this fixture.

Both rebuilt retail runners completed isolated scripted host-input sequences
through new-game, menu/back and pause/resume, exiting normally at tick 7400.
Their traces contain exactly the five requested Escape capture changes and
no pad command from Escape. Engagement survives menu and pause transitions;
a gameplay click after disengaging does not recapture. Windows used Vulkan;
Ubuntu WSL2 used the CPU renderer. These runs exercise the actual mapper and
guest input paths with synthetic host events; they do not test physical OS
cursor confinement. The item-pickup gate is covered by the source fixture;
the prior cave/NPC/weapon route was not repeated for this input-only change.
Linux hardware Vulkan remains limited by the previously recorded Dozen issue.

All 65 exported patches pass tools/full/repeat application; the 87 managed
files match the development checkout. Owner patches 0040/0041 are unchanged.
This change has no Linux-specific behavior and claims no performance gain.

## Native block compilation and stamina timing (0066)

The slow stamina report reproduces beside the first NPC with the corrected
game sources and patches through 0065. A read-only 30-second observation
measured 11.800 original gameplay updates/s. One unarmed attack required
77 updates to return to full power, taking 6.505 seconds instead of about
2.57 seconds at the intended NTSC update rate. The observed animation frames
and cooldown values were contiguous, with one step per observed retail loop.
This establishes simulation slowdown in this case; it does not establish
that every weapon or stamina condition is correct.

Windows stack samples put most execution time in the compiled VU blocks.
Patch 0066 forces their upper/lower instruction and instruction-pair templates
inline, retaining the existing non-inline 16-pair group boundary. This removes
repeated calls and lets the compiler retain constants and temporary state
within each group. Strict arithmetic, operand normalization, pipeline commits,
code-generation checks, callbacks and cooperative budgets are unchanged.
Neither recovery constants nor original game code/assets change.

Native Windows comparison, i9-13900KF / RTX 4090, Vulkan, fixed updates,
interpolation enabled, 120 FPS requested, upstream static lift disabled:

| Observation | Through 0065 | With 0066 |
|---|---:|---:|
| Original game updates/s | 11.800 | 14.433 |
| Attack to full stamina | 6.505 s / 77 updates | 5.390 s / 77 updates |
| Attack to hit-window entry | 1.290 s / 15 updates | 1.065 s / 15 updates |
| Mean rendered frames/s during the measurement | 83.85 | 75.41 |

These are single 30-second runs from new games in closely matched stationary
NPC views, within five game-coordinate units and 0.008 radians of each other.
No compiler, stack sampler or second game was active during either measurement.
Read-only polling and ordinary host scheduling still introduce uncertainty.
The roughly 22% gain is in **simulation throughput**, not graphics FPS:
presentation throughput decreased about 10% in this comparison while remaining
above the simulation rate. The heavy scene is still less than half real time;
this is not a locked-30-Hz result or a complete stamina fix.

Both Windows binaries use strict MSVC 19.44 and the same existing PGO profile.
The new link reports 4969/5317 functions optimized from profile data, with no
mismatched call sites. This is reuse of the earlier profile, not new training
on this final generator. The measured executable SHA256 is
`3fc53fef3b50235d8df83bda02f485aafe9406b77934184a1e84c751c8a3371c`.

Native Windows and Ubuntu 26.04 / WSL2 each pass all seven authored-native
tests and all seven cooperative-native tests. Their positive runs perform
672585 and 1116253 differential checks respectively; negative controls verify
that register, flag, cycle, memory and packet corruption is detected. These
fixtures compile the actual generator output and compare against the frozen
reference with substituted GS/VIF boundaries; they are not a full-game proof.

The rebuilt Windows PGO game crossed both cave doors, retained floor contact
and the seated NPC, completed the measured attack, displayed Equipment, and
returned through the main menu to an intact sky. It exited normally after
729.68 seconds. Eight explicitly requested captures are retained privately;
the harness's automatic-final-image postcheck failed because control-file
quit does not produce `final.png`. The game exit code was zero. This run did
not repeat weapon pickup/equipping or a full playthrough.

Ubuntu rebuilt the actual game with GCC 15.2. Three isolated CPU-renderer
new-game runs with attack/menu inputs exited normally around tick 7400.
The first captured the main menu; later runs returned to gameplay. The
scripted Equipment sequence did not reach Equipment, including a repeat
with longer button holds, so Linux menu coverage is partial. The input trace
records Circle reaching the game and later inventory entry; the reason the
earlier menu request did not open the menu remains unverified. A control
rebuilt through patch 0065 matched the previous Linux executable byte for byte
(`e3333dc5e956fe5f8934015584cc86516eacb696239a76d3b4c4db1db5e8f117`)
and also failed to reach Equipment with the same longer-input sequence.
Thus that automated sequence's failure predates this optimization. The final
0066 Linux binary was restored and compared byte for byte before the control
run. Its SHA256 is
`2916a5b9bd88b0d6139b791d11cd30dfc832d29ac5c3ae7d48648b4dfbfd6216`.
Equipment,
the NPC route and the performance/stamina comparison above are Windows-only.
Linux hardware Vulkan remains unverified due to the previously documented
Dozen failure. No new Linux-specific behavior is introduced, and the owner's
merged patches 0040/0041 remain unchanged.

All 66 exported patches pass fresh tools/full/repeat application; the 87
managed source files match the persistent development checkout.
