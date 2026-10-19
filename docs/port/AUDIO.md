# Sound

How KF4's sound reaches the speakers on the PC. This is PLAN.md's step 4. It takes the "cleaner" of
the plan's two ways:
- the IOP sound driver is rewritten as PC code, as ico-pc did for ICO's;
- no IOP is emulated;
- the IOP modules on the user's disc are not run.

## The PS2's sound, in short

The game (EE) talks to one RPC server on the IOP, `0x1010`: FromSoftware's driver
`FSIOPSND.IRX`. The driver drives:
- Sony's CSL modules:
  - `MODMIDI.IRX`, a MIDI sequencer: four ports, each playing a `.sq` sequence;
  - `MODHSYN.IRX`, a synthesizer: sixteen ports playing MIDI messages on `.hd`/`.bd` sound banks;
- `LIBSD.IRX`, Sony's library for the SPU2.

| what | path |
| --- | --- |
| music | a sequence on a MODMIDI port → that port's message stream → the MODHSYN port of the same number |
| sound effects | each frame the game writes MIDI messages into the driver's buffer (`0x1080`), which appends them to the synthesizer's streams |
| streamed music (`.ADS`, e.g. `TITLE.ADS`) | read from the disc by the driver itself, into a ring that a core's sound data input (AutoDMA) plays |

## The pieces

| path | what it is |
| --- | --- |
| `port/audio/fsiopsnd.c` | the driver: every RPC command, the 240 Hz tick, ADS streaming, the sound-effect flush |
| `port/audio/modmidi.c` | the sequencer (Sony's API names, `csl.h`) |
| `port/audio/modhsyn.c` | the synthesizer: voice allocation, pitch, volume, pan, envelopes, LFOs, reverb |
| `port/audio/sd.c`, `sd.h` | libsd: its calls and encodings, on the SPU2 below; `sceSdNote2Pitch`; the sound data input |
| `port/audio/libsd_irx.c` | reads libsd's reverb presets and idle voice block from the disc's `LIBSD.IRX` at start-up |
| `port/audio/iop.c` | the IOP's 2 MB of memory: where SIF DMA, `sceSifLoadIopHeap` and the IOP heap put the sound data |
| `port/audio/output.c` | renders the SPU2 on EE time; the WAV dump and the SDL2 sound device |
| `third_party/ico-audio/` | ico-pc's software SPU2 (MIT), unchanged: see its `UPSTREAM.md` |

`port/sdk/sif.c` sends server `0x1010` to `port_fsiopsnd_call`. `port/sdk/fssnd.c` used to answer
it without making a sound; it is gone.

## Where the code comes from

The driver and the two CSL modules were written from the code of the user's own disc's
`FSIOPSND.IRX`, `MODMIDI.IRX` and `MODHSYN.IRX`:
- `FSIOPSND.IRX` kept its symbol table, so its functions and data keep their own names.
- The CSL modules have no symbols. Their functions are named from the public CSL API
  (`csl.h`, `modmidi.h`, `modhsyn.h`): the export ordinals match those of Sony's import libraries.

No code or data of Sony's, FromSoftware's or an emulator's is in this repository.

**Read from the disc.** libsd's reverb presets, their work area sizes and the block idle voices
play are its own data. `port/audio/libsd_irx.c` reads them from the disc's `LIBSD.IRX` at
start-up, as ico-pc does, and logs one line. Without the file, ico-audio's built-in presets
(psx-spx's) are used.

**Written out in the code.** The only numeric tables there are mathematical:
- `sceSdNote2Pitch`'s: 0x8000·2^(i/12) and 0x8000·2^(i/1536), as LIBSD.IRX rounds them;
- MODHSYN's sine wave: trunc(32767·sin(2πi/200)).

MODHSYN's velocity curves are formulas, written as code.

## Threads become steps on EE time

On the PS2 the driver is four IOP threads. Here each becomes the code it ran between two waits, run
at the EE time the PS2 would have woken it:

| IOP thread | woken by | here |
| --- | --- | --- |
| RPC server (prio 25) | an `sceSifCallRpc` from the EE | `port_fsiopsnd_call`, at the EE time of the call |
| tick (prio 23) | a hardware timer every 4167 µs, while the tick state is RUN | `fsiopsnd_tick`. The kernel runs it as an event of its own (`port_audio_next_event`, in `advance_to`), every 153612 × 4 EE cycles: `USec2SysClock(4167)` at the IOP's 36.864 MHz |
| ADS stream (prio 82) | the sound data input finishing half of a ring | `ads_proc`, from the input's callback inside the SPU2's render |
| ADS set (prio 84) | each `0x30B0` poll | `ads_set_proc`, one pass per poll, as on the PS2 |

Before each RPC call, at each tick and at each vblank, the SPU2 is rendered up to the current EE
time (`port_audio_sync`). So what the driver reads back from the SPU2 is as current as on the PS2:
- envelope levels and the voices' end flags (`ENDX`/`ENVX`, which MODHSYN uses to free voices);
- the half the sound data input is on.

Every libsd write is stamped with its EE time, at 3072 EE cycles per 48 kHz frame.

**What this means.** The same input gives the same run, and the same audio to the byte:
- the replies the game reads;
- when the game's loads finish (they wait on the driver);
- the WAV dump (`--audio-dump`).

`tools/boot_test.py` still passes, and its state log is unchanged.

## Matching the PS2

The driver's replies are FSIOPSND.IRX's, where the canned server's were not:
- `0x90` rounds the transfer size up to 64 bytes.
- `0x3090` answers 1 while a stream plays, so the title starts its music once, not twice.
- `0x30B0` answers 0 until the set thread has opened and preloaded the stream.
- `0x10` and `0xA0` answer the IOP addresses of FSIOPSND's own buffers.

Retail bugs that the game reaches are kept. Two of them:
- `adsSet` tests the previous stream's header format before reading the new one.
- `0x2070` indexes the four sequence ports' states with a synthesizer port.

Retail bugs that the game does not reach are logged instead:
- `adsPreset` never returns on the PS2. It is reached only by commands KF4 never sends.

### How it was checked

The checks compare against `FSIOPSND.IRX`, `MODMIDI.IRX`, `MODHSYN.IRX` and `LIBSD.IRX` from the
disc, run on an IOP emulator.

The reference is a development harness. It is not in this repository: the IOP emulator it uses is
GPL. It replays a recording of what the game sent the IOP, made by `KF4_SIF_TRACE`, then logs:
- every libsd call;
- every reply;
- the synthesizer's input streams at each tick;
- the sound.

The port writes the same logs (`KF4_SD_TRACE`, `KF4_STREAM_DUMP`, `--audio-dump`). Against it:
- **FSIOPSND's libsd calls:** the same, call for call.
- **The title music (`TITLE.ADS`):** sample-identical.
- **MODMIDI's message streams:** byte-identical.
- **MODHSYN's libsd writes:**
  - Up to the start of the first map's music (vblank 749), identical, sound effects included.
  - Once music plays, the two drift apart. Two causes were found, neither in modhsyn:
    1. **The SPU2's exponential release.** The reference rounds each step toward zero with a
       minimum of 1, ico-audio's rounds down. So here a released voice reaches 0 a little sooner,
       and modhsyn frees it sooner. With the reference's SPU2 values fed in, modhsyn's first 2334
       writes are the reference's, read for read. Which rounding the PS2's SPU2 uses has not been
       checked against a PS2: both are emulator models.
    2. **The tick and the game's calls never overlap here.** On the PS2 an RPC can preempt the tick
       thread, and a sound effect sent then is played in that tick.

### Known differences

- **The reverb change (`0x40070`).** The PS2 driver sweeps core 1's master volume down and polls
  it until it is 0 while the EE waits. An RPC takes no EE time here, so it is set to 0 at once.
- **The tick's phase.** The phase of the tick against the vblank is the port's own: a tick falls
  4167 µs after the driver's init. On the PS2 it depends on when the timer was started. Which tick
  first sees a message can differ by one.
- **`0x30C0` (`adsSetWithLsn`, streams by sector number).** It is not implemented: KF4 never sends it.

## Output, and the two clocks

**The game's clock.** This mode is used when any of these holds:
- the run is headless;
- `--audio-dump`, `--input` (a pad script), `--fast` or `--no-audio` is given;
- there is no sound device.

The SPU2 is rendered up to the current EE time at each vblank, at each tick and before each RPC
call, as described above. The same input gives the same run and the same sound, to the byte.
- `--audio-dump FILE` writes the sound as a 48 kHz 16-bit stereo WAV.
- With a window, the frames are queued for the sound device. The device starts once 100 ms are
  queued, and again after running dry. With `--fast` the queue is dropped past half a second.

**The sound device's clock.** This mode is used when there is a window and a device, and none of
the above. On the PS2 the IOP runs on its own clock: the music and the streams play on while the EE
loads a map. Here the same happens:
- SDL's audio thread renders the SPU2 when the device asks for sound, and runs the driver's tick
  between the frames it renders, every 200 + 1/64 frames (4167 µs).
- The game's RPC calls take the same lock (`port_audio_lock`) and happen at the frame the device has
  reached. libsd's calls are stamped with that frame (`sd_set_clock`).
- So a hitch on the game's thread no longer stops the sound. A hitch is a load, a shader paraLLEl-GS
  builds on first sight, or an area where the port runs below the PS2's speed.
- What the game reads back now depends on how far the device has played: a stream's state
  (`0x3090`), a voice's envelope (modhsyn). Such a run is not repeatable to the frame, which is why
  the tests use the game's clock.
- If the device stops asking for sound for half a second, the game's clock takes over for the rest
  of the run, with no device.
- `port_disc_read` takes a lock of its own, because the driver's stream refills read the disc on the
  audio thread.

`--no-audio` opens no device, and the sound is still made: the game reads the driver's state back.

**The player's volume.** The overlay's Audio page (and settings.json) scales what the device plays
(`port_audio_set_volume`), on either clock. The SPU2's output, what the game reads back and
`--audio-dump` are not touched, and at full volume the samples go to the device as before.

Music and sound effects have no volumes of their own yet. Nothing in the driver keeps them apart where
the sound is mixed:
- The sequences play through MODHSYN ports 0 to 3, but the game's sound-effect messages (`0x1080`)
  name their own synthesizer streams, and which ones each effect uses has not been surveyed; both
  kinds share the SPU2's voices, and a voice does not know which port started it once it plays.
- Scaling the volumes MODHSYN writes per port would change the libsd writes that are compared with
  the PS2 driver's, and scaling per voice needs a gain in the SPU2's mixer, in ico-audio, which is
  kept as upstream has it.
- Only the streamed music (`.ADS`, the sound data input) is apart from the rest.

A clean split would be a per-voice gain in the SPU2's mixer, set by modhsyn from the port a note
came from, once the effects' streams are known.

## Debugging

| variable | writes |
| --- | --- |
| `KF4_SIF_TRACE=path` | what the EE asks of the IOP: module loads, heap, SIF DMA payloads, RPC calls and replies |
| `KF4_SD_TRACE=path` | every libsd call, by module, with the driver's RPC calls and ticks between |
| `KF4_STREAM_DUMP=path` | the synthesizer's input streams at each tick |
| `KF4_AUDIO_FIRST_TICK=cycle` | puts the driver's ticks on that EE cycle's phase, to line them up with a reference (the game's clock) |
| `KF4_AUDIO_STATS=1` | once a second on stderr: on the game's clock, the share of real time made and the device queue's low point and underruns; on the device's clock, the SPU2's time against the game's |
| `KF4_AUDIO_DEVICE_CLOCK=1` | the device's clock even with a pad script, `--fast` or `--audio-dump`, to test it on a scripted run |
