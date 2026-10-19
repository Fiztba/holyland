# The PC port

holyland is a native PC port of King's Field IV built from the decomp's source. The game code is the
decomp's, pinned in `decomp/` and never edited here; the PC edits to it are kept as patches in
`patches/`. Under it, `port/` stands in for the PlayStation 2. The approach follows
[ico-pc](https://github.com/nathanialf/ico-pc), the PC port of the ICO decomp:
- The game code is compiled for the PC as 64-bit C, with nothing emulated or recompiled.
- Where the code relies on the PS2, it is changed to the PC form, by the rules in
  [RULES.md](RULES.md).
- `port/` provides what the PS2 did, keeping the SDK's names so the game code reads as before:
  - game RAM;
  - the hardware registers;
  - the Sony libraries;
  - later the renderer, the sound driver and the platform layer.

## The decomp and the patches

`decomp/` is a git submodule of [kfiv-decomp](https://github.com/Voicedrew11/kfiv-decomp.git), pinned
to one commit (currently `88b0978`).

> **The decomp is read-only here.** Never edit `decomp/` or the compiled copy in
> `out/<preset>/decomp_src/`; this goes for people and agents alike. Configure fails if `decomp/` has
> local changes, and `decomp_src/` is made read-only (an edit there would be lost at the next
> configure). Edit game code in `work/` and save it into `patches/` (below).

Clone with submodules, but not recursively: `third_party/parallel-gs` (the GS renderer) has a
Granite submodule with twenty more of its own, and the port needs only two of them.

```sh
git clone <this repository> && cd holyland
git submodule update --init
git -C third_party/parallel-gs submodule update --init Granite
git -C third_party/parallel-gs/Granite submodule update --init third_party/volk third_party/khronos/vulkan-headers
```

Without the last two lines the port still builds, but draws nothing.

The PC edits are in `patches/`, as diffs against the decomp with paths relative to its root:
- `patches/game/<folder>.patch`: one per top-level folder of `src/game`.
- `patches/game/_top.patch`: the files directly in `src/game`.
- `patches/nonmatching.patch`: the map draw `src/nonmatching/func_00112BD0.c`, which the PC build
  compiles.

Game code that exists only on the PC, with nothing in the decomp to patch, is ordinary files in
`game/`: `game/data/` holds the data sections that the decomp still has in asm, defined in C. It is
compiled with the patched decomp and edited in place.

The decomp's own tooling and docs (its PS2 build, `build.sh`, `tools/match.py`, the splat config) live
in kfiv-decomp. They do not apply to the patched game code here.

Fixes that are not PC-specific (names, types, structs, matching) go to kfiv-decomp first, and come
here with a pin move (see below).

### Changing game code

1. `python3 tools/patches.py apply`: makes `work/` (git-ignored) a fresh copy of the decomp with the
   patches applied. `work/` is its own git repository, and its `pristine` tag is the unpatched decomp.
   If `work/` has changes that were not saved, `apply` refuses; `apply --force` discards them.
2. Edit the game code in `work/src/game`, by the rules in [RULES.md](RULES.md).
3. `python3 tools/patches.py save`: regenerates `patches/` from `work/`. Commit `patches/`.
4. `cmake --build out/<preset>`: since the patches changed, configure runs again on its own. It
   copies `decomp/src/game` and `decomp/src/nonmatching`, applies `patches/` to the copy, and updates
   `out/<preset>/decomp_src/` with the files that changed, so only those recompile. The build compiles
   `decomp_src/` with `game/` and `port/`. Configure fails if `decomp/` has local changes.

### Moving the pin

`python3 tools/patches.py upgrade <commit>` moves the submodule to a newer kfiv-decomp commit:
- it follows file renames (`git diff -M` between the two pins) and function and global renames (from
  `config/symbol_addrs.txt`, whose addresses are stable);
- it re-applies the patches with `git apply -3` and reports conflicts.

It does not follow renames of struct fields or types: `symbol_addrs.txt` has only functions and
globals. Those come up as conflicts, or as compile errors after a clean upgrade, and are fixed by
hand.

If that is clean, it moves the submodule and rewrites `patches/`. Then commit `decomp` and `patches/`.
If not, it keeps the scratch copy so the conflicts can be resolved there, and then
`python3 tools/patches.py save --from <that dir>` writes the result to `patches/`.

## Building

The game code is GNU C, so build it with clang or gcc (not MSVC's cl). You need CMake 3.21 or newer
and Ninja. The renderer, paraLLEl-GS, is C++ (clang++ or g++, which the presets name). It runs on
desktop Vulkan GPUs (its README lists the features it needs); running it needs a Vulkan driver,
building it does not.

**Windows:** use [llvm-mingw](https://github.com/mstorsjo/llvm-mingw), or LLVM's clang, on the PATH:

```sh
cmake --preset windows-clang -DSDL2_DIR=C:/path/to/SDL2-2.30.9/x86_64-w64-mingw32/lib/cmake/SDL2
cmake --build out/windows-clang
```

SDL2 on Windows comes from its mingw development package (`SDL2-devel-2.x.y-mingw.tar.gz` from
[SDL's releases](https://github.com/libsdl-org/SDL/releases)); `SDL2_DIR` points into it. The build
copies `SDL2.dll` and llvm-mingw's `libc++.dll` and `libunwind.dll` next to `kf4.exe`.

**Linux:**

```sh
cmake --preset linux-gcc        # or linux-clang
cmake --build out/linux-gcc
```

The program is `out/<preset>/kf4`. SDL2 is used for the window and the pad if CMake finds it
(`-DKF4_USE_SDL=OFF` turns it off). paraLLEl-GS is built when it is checked out
(`-DKF4_USE_PARALLEL_GS=OFF` turns it off); without it the game runs and draws nothing.

## Running

```sh
out/linux-gcc/kf4 --disc kfiv_us.iso
```

| Option | What |
|---|---|
| `--disc PATH` | the game's disc image (or set `KF4_DISC`) |
| `--fast` | run as fast as possible instead of at 60 vblanks a second |
| `--headless` | no window, no SDL; the GS draws only for `--dump-frames` |
| `--no-gs` | draw nothing (VIF1 and VU1 still run) |
| `--no-deinterlace` | show each field as the GS scans it out (224 lines), not the two fields woven into one picture |
| `--no-progressive` | read a frame-mode interlaced picture a field at a time |
| `--no-anti-blur` | keep the blur of a game that blends its two read circuits |
| `--no-adapt-width` | scan out at the TV's width, not the game's |
| `--ps2-scanout` | all four of the above: the GS's raw output, for comparing with the PS2 or PCSX2 |
| `--dump-frames N,N,...` | write the picture shown at these vblanks as `frame_NNNNNN.png` |
| `--dump-dir DIR` | where `--dump-frames` writes (the current directory by default) |
| `--frames N` | stop after N vblanks |
| `--input FILE` | play the pad from a script instead of SDL |
| `--log FILE` | the per-frame log (`-` for stdout) |
| `--clock YYYY-MM-DDTHH:MM:SS` | the disc drive's clock at boot, in Japan's time, instead of the host's; for repeatable runs |
| `--host-dir DIR` | the folder `host0:` paths are read from |
| `--controls ps2\|verdite` | the keyboard and mouse layout (see below) |
| `--audio-dump FILE` | write the sound as a 48 kHz WAV file |
| `--no-audio` | open no sound device (the sound is still made) |
| `--stats` | at exit, print where the DMA controller's thread spent its time: VU1 (its runs and instructions too), handing GIF packets to the GS, and vsyncs; and a hash of every packet VU1 sent, which two runs that drew the same share. For A/B timings. It counts from the last save state saved or loaded |
| `--state-save-at VBLANK[:SLOT\|:FILE],...` | save a state at these vblanks, to a slot (0 when left out) or a file (see "Save states") |
| `--state-load [VBLANK:]SLOT\|FILE` | load a state before the game starts, or once the run reaches that vblank |
| `--quick-save-at VBLANK[:SLOT\|:FILE],...` | take a quick save (slot 0 when left out) at these vblanks, or as soon after as the player is in control (see "Quick saves") |
| `--quick-load [VBLANK:]SLOT\|FILE` | load a quick save on the title screen, or in play once the run reaches that vblank |
| `--memcard DIR\|none` | the folder that is the memory card in slot 1 (by default `memcard` in the data folder, beside settings.json); `none` leaves the slot empty, as the tests do |
| `--record-input FILE` | write what the pad and keyboard send as a pad script, to replay with `--input` |
| `--widescreen` | the opt-in 16:9 picture (also on the overlay's Display page; off by default): the game's view is widened, the menus, message boxes and HUD keep their 4:3 shape, and the window shows it at 16:9 (see "Widescreen") |
| `--widescreen-hud-centred` | the same, with the HUD in the 4:3 middle instead of the 16:9 picture's corner |
| `--myus-necklace` | the opt-in mod (also on the overlay's Mods page; off by default): the effect of Myu's Necklace always, worn or not (see "Mods") |
| `--gs-capture-at VBLANK[:N],...` | write a GS frame capture of N vblanks (2 when left out) from each of these vblanks, as F9 does (see "Debugging") |

The window shows the game at 4:3 (16:9 with widescreen). A game controller is the pad; so is the keyboard (the PS2 layout,
the default):

| Keys | Pad |
|---|---|
| arrows | d-pad |
| W A S D, I J K L | left stick, right stick |
| Z or space, X, C, V | cross, circle, square, triangle |
| Q, E, 1, 3 | L1, R1, L2, R2 |
| Enter, Backspace | start, select |

The Verdite layout (`--controls verdite`, or the overlay's Controls page) is that of the KF2/KF3 ports:
W A S D walk and side-step, the mouse turns and looks once captured (Escape), Space attacks
(`port/sdk/controls.c` lists it). In both, F1 opens the settings overlay, F2 takes a quick save and F4
loads it (the saves to play with, which survive updates), F5 saves a save state to the quick slot and F7
loads it (exact, for debugging, same build only), F9, F10 and F12 are the debugging tools (see
"Debugging"), and F11 switches full screen. Every key, mouse button and controller button can be
rebound.

### The settings overlay (F1)

F1 opens a menu bar and the Settings window over the game, drawn with Dear ImGui
(`port/overlay/overlay.cpp`). While it is open the keyboard, the mouse and the controller go to it,
not to the game; the game runs on behind it unless "Pause the game while the overlay is open" is on.
F1 or Escape closes it. What is held as it closes does nothing in the game until it is let go.

| Page | What |
|---|---|
| Display | window size (common heights: 720, 900, 1080, 1440, 2160, or the PS2's 448 and 896 lines, with the width following 4:3 or 16:9; or a monitor's own size: 2560x1440, 2560x1600, 3440x1440, 3840x2160 or 3840x2400, with the picture fitted inside; sizes bigger than the screen are left out), full screen (borderless at the monitor's own resolution; F11), picture shape (4:3 or stretched), widescreen (16:9) and where its HUD goes, live, smooth or sharp scaling, vsync, and the four switches of the PC picture (deinterlace, progressive, anti-blur, adapt width), live, for comparing with the GS's raw output |
| Audio | volume and mute (of what you hear: the game's sound and `--audio-dump` are unchanged) |
| Controls | the layout (PS2 or Verdite), every action's two keys or mouse buttons and its controller button (click a slot and press; Delete or a right click clears it), Verdite's mouse sensitivity, invert Y and the mouse buttons in menus, and Defaults |
| Save states | Quick saves (for playing): the ten slots, which one F2 and F4 use, what each holds (map, level, HP, when), save, load and what the last request did (`port/include/port/quicksave.h`). Save states (for debugging): the ten slots (0 is the quick slot of F5 and F7), save, load, and what the last request did (`port/include/port/savestate.h`) |
| Mods | the opt-in changes to the game, live: Myu's Necklace effect without wearing it (see "Mods") |
| Debug | pictures, vblanks and game frames a second; with `--stats`' counting switched on, the DMA controller's time a second; the map, position, heading, HP, MP, level and gold; a button that copies them for a bug report; and the debugging tools: a button for each, the captures folder and the last file written (see "Debugging") |
| Interface | pause while open, a frame-rate counter, the overlay's size, and where `settings.json` is |

Every way the PC differs from the PS2 is a switch that starts as the port has it.

**settings.json** keeps the settings, in the user's data folder: `%APPDATA%\holyland` on Windows,
`$XDG_CONFIG_HOME/holyland` (or `~/.config/holyland`) elsewhere; `KF4_DATA_DIR` names another folder.
The overlay's window places go beside it in `overlay.ini`. It is written a second after a change, when
the overlay closes and at exit; keys it does not know are kept, and a file that is not valid JSON is
reported and left alone (the defaults are used). The controls keep only the bindings that differ from
the layout's defaults, as `"key:Space"`, `"mouse:left"`, `"pad:a"` or `"trigger:lefttrigger"`.

Options on the command line override it for that run, and are not written back: `--no-deinterlace`,
`--no-progressive`, `--no-anti-blur`, `--no-adapt-width` and `--ps2-scanout` keep their switches off,
`--widescreen` keeps widescreen on, `--myus-necklace` keeps that mod on,
and `--controls` picks the layout.

**Tests see none of it.** A headless run or one that plays a pad script (`--input`) reads no
settings.json and has no overlay, so `boot_test.py` and `frame_test.py` run the same whatever the
player has chosen. Built without SDL there is no overlay at all.

`KF4_OVERLAY_SHOT=page@vblank,...` is for checking the overlay without anyone at the screen: it opens
the overlay on that page at that vblank, even with a pad script (in a hidden window), and writes the
window's whole picture a few vblanks later as `overlay_<page>.png` in `--dump-dir`. The pages are
`display`, `audio`, `controls`, `savestates`, `mods`, `debug` and `interface`, and `closed` is the game alone;
list them in vblank order. Use it with `KF4_DATA_DIR`, as it reads and, after the last picture,
writes back settings.json:

```sh
KF4_DATA_DIR=/tmp/kf4-data KF4_OVERLAY_SHOT=display@1990,debug@2000 \
  out/windows-clang/kf4 --fast --frames 2010 --input tests/newgame.pad --dump-dir /tmp/shots
```

A pad script has one press per line: `<vblank> <buttons> [vblanks held, default 6]`. The buttons are
names such as `CROSS`, `START`, `UP`, joined with `+`, or `-` for none. A line `<vblank> lx=N ...` (or `ly`, `rx`,
`ry`) sets a stick (0x80 is the middle). Text from `#` on is a comment. `tests/newgame.pad` boots to
a new game.

The log has one line per vblank event (`<vblank> event: ...`: files read, movies, the memory card),
and one per game frame with the game's status, the map and the player's position.

`tools/boot_test.py --disc kfiv_us.iso` is step 1's check: it boots to a new game twice and checks
the two logs match and reach the first map. It runs headless with no GS, so it needs no GPU.

`tools/frame_test.py --disc kfiv_us.iso` is step 2's: the same boot with the GS, dumping the logo,
the title menu, the brightness screen, the opening and the first map, and every picture of vblanks 990
to 1010 (`--dense`). It checks two runs (`--runs N` for more) give the same pictures byte for byte and
that none is blank (`--keep DIR` keeps them to look at).

Each folder of the game code is its own target, so `cmake --build out/windows-clang --target game_ui`
compiles just the `ui` folder (`src/game/ui`, with the patches applied).

## Mods

Optional changes to how the game plays. All are off by default; with them off, the game plays as it
does on the PS2. Each is a switch on the command line and on the overlay's Mods page (live), kept in `settings.json`'s `"mods"`, and read by the
game's patches through a function of `port/include/port/host.h`; off, the game code does exactly what
it does without it.

- **Myu's Necklace effect without wearing it** (`--myus-necklace`, `port_myus_necklace()`).
  `MapObject_DrawAll` decides each frame whether the necklace is worn, from the amulet slot; with the
  mod on it counts as worn whatever the slot holds, so the objects it makes glow always glow (their
  pulse starts when the mod is turned on), and the slot is free for another amulet.

## Widescreen

Off by default, so the picture is the PS2's. `--widescreen`, or the overlay's Display page (live),
turns it on:
- **The 3D view is 16:9.** The game's projection (`GameLoop_SetCameraMatrices`, and the King's Map's
  in `MapViewer_RunKingsMap`) is three quarters as wide, so the GS still draws 640x448, now holding a
  16:9 view at the same height (vertical field of view unchanged), squeezed. The window shows it at
  16:9 (and a window that is not full screen is made 16:9). Nothing pops in at the sides: the game
  culls its map records with a half-plane in front of the player, and everything else by where it
  lands on the screen, which the wider projection takes care of.
- **The 2D keeps its 4:3 shape.** The GS front end (`port/platform/gs.c`) squeezes it to three quarters
  of its width as the packets go by: everything drawn by PATH3 (the menus with their item models, the
  title, the memory card notice), and the 2D of the packet chain that the game's patches mark with a
  write to `PORT_GS_REG_WIDESCREEN` (`game/widescreen.c`): the message text, the pause label, the
  splash pictures, the map viewers' pictures, and the HUD, which goes into the 16:9 picture's top-left
  corner or (`--widescreen-hud-centred`, or the overlay) stays where it is at 4:3.
- **Left full width:** pictures of the frame itself (the dimmed world behind the menus, the screen
  wobble, the frame copied at TBP 0x1180 or 0x2BC0, every screen effect that redraws the frame),
  full-screen fades and flashes, and untextured panels that reach the screen's edge.
- **`--dump-frames` writes the GS's picture as it is**: 640x448, squeezed. Stretch it to 16:9 (for
  example to 796x448) to see it as the window shows it. `KF4_OVERLAY_SHOT` writes the window, so its
  pictures are 16:9.
- Off, the game code does exactly what it does without the switch (`port_widescreen()` is 0, nothing is
  marked, and the GS front end does not touch the packets): the frame and boot tests, `--stats`' VU1
  counts and PATH1 hash are main's.
- Not done: the 2D is squeezed into 480 of the 640 columns, so it is a little softer than at 4:3 (a
  higher internal resolution, step 3, would mend that). Anything the game sizes in screen pixels around
  a projected point would come out a third wider; none has been seen yet.

## Saving: quick saves, save states and the memory card

There are three ways to keep a game, for different jobs:

| | Quick saves (F2, F4) | Save states (F5, F7) | Memory card saves |
|---|---|---|---|
| For | playing | debugging and tests | playing, as on the PS2 |
| Taken | anywhere in play (not in menus, messages or events) | at any vblank | at a save point, through the game's menus |
| Loads in | any build of the program | only the build that made it | any build, and on a PS2 or PCSX2 |
| Holds | the game's own save, where you stand | everything, exactly | the game's own save |
| Where | `quicksaves/quick<N>.kf4quick` in the data folder | `states/slot<N>.kf4state` | `memcard/` in the data folder |

Use quick saves to play: the port is rebuilt after each fix, and a quick save survives that, as a save
state does not. A memory card save is still the one to keep for good (it is what the game itself made,
and it loads on a PS2 too).

## Quick saves

A quick save is the game's own save, taken where the player stands, without a save point and without
the memory card. F2 takes one into the slot chosen on the overlay's Save states page (0 at first) and F4
loads it; F4 also works on the title screen. Both only act in play: in a menu, a message, an event, a
map change or a fade, a key's request is refused with a message (the overlay shows it a moment).

How it works (`game/quicksave.c`, `port/platform/quicksave.c`):
- **Saving** calls the game's own writer, `Game_WriteSaveImage` (`core/game.c`), as the save screen
  does before a memory card save (`ui/trade.c`, after `PlayTime_Update`), and writes the image to a file
  instead of the card. The patched main loop asks for it once a frame, at its end (`GameLoop_Run`,
  `Quick_PollGame`), so it is taken between two frames. `save_image` is put back as it was. The image is
  the memory card's, byte for byte: the map (`saved_map_tag`), the player's position and rotation
  (heading and look), HP and MP, level, experience, gold, stats, what is worn and held, the items, magic
  and their levels, key items, the play time, the game's options and button layout, and the world's
  flags (doors and chests, map objects, items picked up and dropped, events, story values). Saving also
  stores the map objects' flags first (`MapObject_SaveAllFlags`), as any save does.
- **Loading** puts the image where a memory card load puts it (`save/save_card.c`: `save_loaded = 1`,
  `game_status.bit9`), after checking it as the game does (`Game_CheckSaveImage`). In play that ends the
  main loop as a card load does, and `main` restarts the session from it: `GameLoop_ResetSystems` (which
  reads the image back with `Game_ReadSaveImage`), then `GameLoop_Run` on the saved map, which puts the
  player where the image says. A player who is dying is saved from it (the game over is not shown);
  once the game over screen is up, go back to the title to load. On
  the title screen, `Title_Run` returns as the title's own Load does after a load (`TitleMenu_Run`'s
  case 5). The first vblank of play then checks where the player is against the file, and the log says
  so (`quick loaded: ... (as saved)`).
- **The file** is a 40-byte header (`KF4QUICK`, the format's version, the save image's version word
  0x15400, sizes, a hash), a PC block (when, the map, position, rotation, HP, MP, level, gold, play
  time: what the overlay lists) and the save image. Every number is written byte by byte, little-endian,
  so nothing in it depends on the build. A file that is not a quick save, is of another format version,
  is cut short, fails its hash or fails the game's own check is refused with a message, and the game goes
  on as it was. Nothing is ever read from or written to the memory card.

What a quick load restores is what loading a memory card save restores, at the place it was taken:
- **Restored:** the map, the exact position, heading and look; HP and MP; everything else in the
  save image (above).
- **Not restored, as after any load of a save:** the map is entered afresh, so enemies are back as when
  you enter a map, and map objects that move (lifts, platforms) are where the map starts them (doors,
  chests and switches keep their flags). What lasts a while starts afresh: the power and magic gauges,
  the timers of ailments and of magic and items in effect, the Power Seed and the Widda Needle. A map's
  own counters that count its loads move on as on any load. The neighbouring map is loaded again as you
  walk.
- A quick save taken on a moving platform or in a hazard puts you back on the same spot, where the
  platform may not be (you fall) or the hazard hurts you again.

For tests, `--quick-save-at 1900:1` takes a quick save into slot 1 at vblank 1900 (it waits while the
player is not in control), and `--quick-load 1` loads slot 1 on the title screen (`--quick-load
2100:1` in play, once the run reaches vblank 2100). Checked on Windows with `tests/newgame.pad` and a
walk:
- a quick save in the first map, away from the start, loaded in a new process from the title and in
  play: the player is back on map 0x63 at the same position, heading and look (`(as saved)`), and the
  picture is the same view;
- a quick save taken again right after that load has the same save image but the play time and one map
  script counter;
- loaded in a build with an unrelated code change: the same (that build refuses the save state taken at
  the same vblank);
- loaded while the player burns to death (HP falling, and after it reached 0, before the game over
  screen): he is back where it was saved, with its HP;
- a damaged, cut short, other-version or foreign file is refused, and the game goes on;
- the memory card folder is the same before and after.

## Save states

A save state is the whole running game in a file: loaded, the game goes on exactly where it was.
They are for testing and debugging (to play, use quick saves, above). The settings
overlay's keys (F5, F7) save and load slot 0; the command line drives them for tests:
- `--state-save-at 900:1` saves slot 1 at vblank 900; `--state-save-at 400:a.kf4state,2000:b.kf4state`
  saves two files in one run.
- `--state-load 1` loads slot 1 before the game's first instruction; `--state-load 300:b.kf4state` loads
  a file once the run reaches vblank 300, while it runs (in a window too).

Slots are `slot<N>.kf4state` in `%APPDATA%\holyland\states` on Windows, and in
`$XDG_DATA_HOME/holyland/states` (or `~/.local/share/holyland/states`) elsewhere; with `KF4_DATA_DIR` set, in its `states` folder, next to settings.json. A file is about 5 to
15 MB (LZ4-compressed; 80 MB in memory).

How it works (`port/platform/savestate.c`):
- A state is taken at a vblank, where every EE thread is parked in a kernel call. It is not taken
  while a GIF packet or an image transfer to the GS is half sent, or a `host0:` file is open: the
  request then waits for the next vblank, and says so.
- It holds:
  - game RAM, the scratchpad, VU0 and the register pages;
  - the globals of the game and of ico-audio's SPU2, which are linked as one block between two
    markers (`port/platform/state_begin.c`, `state_end.c`);
  - the EE threads' fibers and their stacks, and the kernel, DMA controller, VIF1, VU1 and GIF;
  - the GS's VRAM and registers, and what paraLLEl-GS's scanout keeps from the last pictures;
  - the libraries, the IOP's memory, libsd, FSIOPSND, modmidi, modhsyn and the sound's clock.
- Not in a state: what is the host's (the window, the sound device, the log, files, the options) and
  `--stats`' counts.

**Same build only.** The stacks hold return addresses into the program, and the game keeps pointers
in game RAM and its globals. So:
- game RAM and the stacks are at fixed addresses;
- the program is linked without ASLR;
- a state carries a hash of the program's code, and another build refuses it.

A state loads into a run with other options:
- On the device's clock or the game's: the sound goes on from where it was.
- Without the GS (`--headless` with no `--dump-frames`, or `--no-gs`): a state taken without it has no
  VRAM, and one loaded without it skips its VRAM.

The same input gives the same run after a load as without one. Checked on `tests/newgame.pad`, with
states at vblanks 400, 900 and 2000, the GS and the sound on, to vblank 2300. A run that saves
against one that loads the state in a new process:
- the per-frame log after the "state saved" line is the same as after the "state loaded" line;
- the `--audio-dump` WAV is the same from the state's SPU2 frame on;
- `--stats`' VU1 runs, instructions and cycles and the PATH1 hash are the same;
- the frames dumped after the state's vblank are the same, byte for byte;
- the run that saved is the same as one that does not (log, WAV, frames);
- a state saved again right after it is loaded is the same file (but the time in its header).

## Debugging

Tools for finding what is wrong while playing, in every build: they cost nothing until used, and
change nothing the game does. Each writes a file into `captures` in the data folder (beside
settings.json; `KF4_DATA_DIR` moves it), and the overlay's Debug page has a button for each, the
folder and the last file written. The files are read by any build: a save state loads only in the
build that saved it, these do not.

| Key | What it writes |
|---|---|
| F9 | `gs_<vblank>_<time>.kf4gs`: a GS frame capture, for `kf4-gsreplay` (below) |
| F10 | `state_<vblank>_<time>.json`: the game's state for a bug report: the map, the player (position, heading, HP, MP, stats, equipment, ailments and their timers, status bits), the game's status, the live entities (type, spawn, position, HP, AI state, flags), the map objects within 6000 units (kind, state, position), the story's flags that are set (story, visited maps, map script flags, recollections) and the port's last 32 events (files read, the memory card) |
| F12 | `audio_<vblank>_<time>.wav`: the last 20 seconds of the sound, as the game made it (before the volume) |

**A GS frame capture** holds, at the start of a frame, the GS (its 4 MB of VRAM, its registers and
the scanout's last fields), VU1 (micro and data memory, registers and flags) and VIF1, then
everything that reaches them until the frame ends, in order: every GIF packet with its path, every
VIF1 word DMA channel 1 sent, each VU1 program started, the read-backs and the two vsyncs (both fields
of the game's 30 Hz frame). And the vblank, the map and the player's position (the log's line), the
build, and the picture options. It is taken right after a scanout, and waits a vblank if a packet
is half sent. It is LZ4-compressed: 3 to 10 MB. `--gs-capture-at 2290` captures vblanks 2290 and
2291 (`2290:4` four of them), the same pictures `--dump-frames 2290,2291` writes.
`port/include/port/gs_capture_format.h` is the format.

**kf4-gsreplay** (`out/<preset>/kf4-gsreplay`, built with paraLLEl-GS) draws a capture again with
paraLLEl-GS, either from the GIF packets as recorded (`--mode gif`, the default) or from the VIF1
stream run again through the VU1 interpreter (`--mode vif`), which makes PATH1 and PATH2 again and
compares each packet with the recorded one. Both give the pictures `--dump-frames` gave, byte for
byte. `tools/README.md` has its options and examples:

```sh
kf4-gsreplay gs.kf4gs --info                     # what and where: vblank, map, position, build
kf4-gsreplay gs.kf4gs --frames --out pics        # the scanouts again: frame_NNNNNN.png
kf4-gsreplay gs.kf4gs --list > draws.txt         # every draw: path, VU1 program, PRIM, box, Z range, GS state
kf4-gsreplay gs.kf4gs --draws 400-420 --zbuf --out pics   # the frame buffer (and Z buffer) after each draw
kf4-gsreplay gs.kf4gs --pixel 320,200            # the draws that cover a pixel, and its colour and Z after each
kf4-gsreplay gs.kf4gs --vertices 412             # a draw's vertices
kf4-gsreplay gs.kf4gs --mode vif --vu-round nearest   # VU floats rounded to nearest: which vertices move
kf4-gsreplay a.kf4gs --compare b.kf4gs           # two captures, draw by draw
```

Without a GPU, `--info`, `--list`, `--vertices` and `--compare` still work. A draw is a run of vertices
with no GS register written between them (other than the vertex's own colour and texture
coordinates), within one GIF packet; pixel coordinates are the frame buffer's (as `--draws` writes
it). Limits: the GS's CLUT buffer is on the chip and not in a capture, so the first CLUT a capture's
draws compare against (TEX0's CLD 4 or 5) is loaded again from VRAM; with `--widescreen` on,
`--mode vif` makes the packets as the game sent them, before the GS front end squeezed the 2D, so
those differ from the recorded ones.

`KF4_DEBUG_AT=state@2290,audio@2290,gs@2295` presses F10, F12 and F9 at those vblanks, for checking the
tools without anyone at the keyboard (in a run that draws; with `KF4_DATA_DIR`, as they write into its
`captures` folder).

## Status

The steps, in order: [PLAN.md](PLAN.md).

**Step 0** (done): the game code compiles and links as a 64-bit program on Windows and Linux,
and `main` runs until it needs something the stand-ins do not do yet.

- `tools/port_census.py` counts, per folder, the errors and the inline asm still standing between
  each folder and that goal.
- `tools/port_census.py --show ui/menu_draw.c` prints one file's errors.
- `tools/port_census.py --show ui` prints a folder's.

**Step 1** (done): the stand-ins (thread scheduler, files, DMA, IOP, memory card, pad) get the game
from boot to a new game with nothing drawn, deterministically (`tools/boot_test.py`).

**Step 2** (done): the game is drawn, in a window, from boot to the first map, and played with a
pad or the keyboard. The DMA controller reads the VIF1 and GIF chains; VIF1 and an interpreter of
VU1 run the game's microprograms; the GS is paraLLEl-GS (`tools/frame_test.py`).

Next:
1. **Graphics, stage 2:** the VU1 programs become native renderers.
2. **Sound.**

## Layout

| Path | What |
|---|---|
| `decomp/` | submodule: kfiv-decomp at the pinned commit. Never edited here |
| `patches/` | the PC edits to `decomp/`, as diffs (`game/<folder>.patch`, `game/_top.patch`, `nonmatching.patch`) |
| `game/` | PC-only game code, compiled with the patched decomp (`game/data/`: the data sections in C; `state_log.c`: the per-frame log; `quicksave.c`: the game's side of quick saves) |
| `work/` | git-ignored: the decomp with the patches applied, for editing (`tools/patches.py apply`) |
| `port/include/` | the SDK headers the game includes (`libgraph.h`, `eekernel.h` …) for the PC, and `port/host.h` |
| `port/platform/` | the program's entry and options, game RAM, EE addresses, the register pages, the EE kernel (fibers, virtual time), the DMAC, the IPU's input DMA, VIF1, VU1, the GS's front end, the window and frame dumps, the disc, save states, quick saves |
| `port/gs/` | the GS backend: paraLLEl-GS behind a small C interface |
| `port/sdk/` | the Sony libraries' PC versions: files and the IOP over SIF, the sound driver's RPC, libcdvd, libdma, libgraph, libpad (with `controls.c`, the keyboard, mouse and controller bindings), libmc, libsdr, and libmpeg on the port's MPEG-2 decoder (`mpeg2.c`) |
| `port/overlay/` | the settings overlay (`overlay.cpp`, F1) and `settings.json` (`settings.c`) |
| `port/debug/` | the debugging tools: the GS frame capture (`gs_capture.c`, F9), the game's state as JSON (`game_dump.c`, F10), the sound's last 20 seconds and the captures folder (`debug.c`, F12) |
| `port/math/` | libvu0 in C |
| `third_party/parallel-gs/` | submodule: [paraLLEl-GS](https://github.com/Arntzen-Software/parallel-gs) (LGPL-3.0+), the GS in Vulkan compute |
| `third_party/imgui/`, `third_party/cjson/` | [Dear ImGui](https://github.com/ocornut/imgui) 1.92.9b with its SDL2 backends and [cJSON](https://github.com/DaveGamble/cJSON) 1.7.18 (both MIT), copied unchanged: see their `UPSTREAM.md` |
| `tools/` | `patches.py`, `port_census.py`, `port_abi_check.py`, `boot_test.py`, `frame_test.py`, `vu1_disasm.py` (the VU1 programs as text), `gsreplay/` (`kf4-gsreplay`, which draws a GS frame capture again; `tools/README.md`) |
| `tests/` | pad scripts for headless runs (`newgame.pad`); `vu1_test.c`, the VU1 interpreter's self-test (`out/<preset>/vu1_test`) |
| `.github/workflows/ci.yml` | CI: configure (the patches apply), build with paraLLEl-GS, `port_census.py` reports 0 errors |
| `CMakeLists.txt`, `CMakePresets.json` | the build; it compiles the patched game code from `out/<preset>/decomp_src/`, with `game/` |
| `docs/port/` | these notes |
