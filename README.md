# Prism FX, by Prismatic

![Prism FX](screenshot.png)

![Prism FX, cream theme](screenshot-cream.png)

An AU / VST3 effect plug-in built from the CHOMPI firmware's effects and tape
looper. Every control is a knob or button: the hardware's shift functions and
one-knob macros are split out into separate controls.

## Effects

| Section | Controls | Source |
|---|---|---|
| Filter | LP/HP (one knob: low-pass below noon, high-pass above), Resonance | TAPE `DJFilter.h` |
| Drive | Drive (soft clip with loudness compensation) | TAPE `DSPEngine.h` |
| Tape | Warble depth, Warble rate | TAPE `Warble.h` |
| Crush | Sample rate, Bits, Mix | new |
| Delay | Time (ms or synced division), Feedback, Mix, Varispeed | TAPE `DSPEngine.h` |
| Glitch Delay | see below | TEMPO `granularDelay.h` |
| Reverb | Mix, Decay, Tone, Diffusion, Freeze | TAPE/TEMPO `reverb.h` (Mutable Instruments) |
| Tape Looper | see below | TAPE `Sampler.h`, `LooperEngine.h` |
| Squash | Squash (output compressor/limiter) | TAPE `limiter.h` |

Each effect turns on and off by clicking its block in the chain strip, or its
title on its card. Switching crossfades so it never clicks. Input, Dry/Wet, Output and Squash sit in the panel at the top right.
Squash is the output compressor; turn it on with its block at the end of the
chain.

**Layout:** filter, drive, tape and crush on the first row; delay and reverb on
the second; glitch delay and the tape looper side by side at the bottom. Each
of those two stacks its controls in three rows. Switches that belong to a
whole effect (sync, varispeed, freeze, oct/5th, save loop) sit in its card's
header.

**Look:** indie editorial: one ink on a flat ground, hairline rules, Instrument
Serif with DM Mono caps, outlined pills.
- **View** (next to Reset) picks the **Night** or **Cream** theme and the size
  (100 / 125 / 150 / 200%). Both are saved with the project.
- **Pills:** toggles fill with the accent colour when on. Actions (rec, play,
  clear) have a red outline and fill red while pressed; rec stays red while
  recording.
- **The chain** reads as a sentence. Click a name to switch that effect on or
  off (off names are struck through), drag a name to reorder, and **Reset**
  restores the default order. Modules are numbered by their place in the chain.
- **Off effects fold** to a narrow strip with the name struck through. Click
  the strip to bring the effect back.
- **Knobs:** double-click resets to default; shift- or cmd-drag for fine moves;
  the mouse wheel works too.
- **The status line** at the top shows the last control you touched (with a
  small bar for where it's set), and on the right the tempo and what the looper
  is doing. It only changes when something happens.
- **Header notes** spell out what's going on: the glitch delay's division, dice
  roll and pattern, and the looper's speed, direction and whether it's
  recording or overdubbing.
- **The glitch step lane** shows the pattern: filled steps will glitch, with a
  mark for what they'll do (||| retrigger, < reverse, ^ octave up, v octave
  down), and the playing step is red.
- Delay **Sync** turns the Time dial into note divisions of the host tempo.
- Delay **Varispeed** (on by default) treats the delay as a loop of tape whose
  speed is set by Time. Changing the time re-pitches what's already in the
  delay: at 100% feedback, going from 1/4 to 1/2 drops the loop an octave.
  Feedback can reach 100% in this mode and the loop holds. Turn it off for
  TAPE's original delay, which slides to the new time with a quick "whip".
- Each card has a hand-drawn sketch that follows its main controls and shows
  what the effect is doing. It only redraws when a setting changes.
  - Filter: the response curve.
  - Drive: the clean wave against the clipped one.
  - Tape: the reels and the warble.
  - Crush: the steps, set by rate, bits and mix.
  - Squash: peaks before and after.
  - Delay: echoes spaced by the time and fading with the feedback, bouncing
    left and right.
  - Reverb: the tail's length, density and tone, and freeze.
  - Looper: tape direction and speed.

### Signal flow

The default order is filter, drive, tape, crush, delay, glitch, reverb, looper,
then squash. To change it, drag the blocks in the strip under the header by
their grip. **Reset** puts the default order back. Squash always stays last, as the output
stage. The order is saved with the project.

Where the looper sits decides what gets recorded:
- **Looper last** (the default, like TAPE's default): the loop records the
  effected sound.
- **Looper first** (like TAPE's "FX after looper" setting): the loop records
  the dry sound, and the effects stay live on its playback.

### Glitch Delay

A stereo delay locked to the host tempo. On each "dice roll" (every 1/4 to
1/32 note), Chaos sets the chance that the next repeat becomes a variation:

- **Mode**: Glitch picks from retrigger, reverse, octave up and octave down.
  Shimmer always uses octave up.
- **Event switches**: Retrig / Reverse / Oct Up / Oct Down pick which
  variations Glitch mode can choose.
- **Spread**: how far each variation is panned at random.
- **Pattern** and **Seed** make the glitches repeatable. With a pattern length
  set, the rolls repeat every 1, 2 or 4 bars. Because they're counted from the
  song position, the same glitches land in the same places on every playback.
  Change the Seed for a different pattern.
- **Freeze**: captures the repeats and loops them in time.

### Tape Looper

The loop behaves like a tape loop:
- **Speed** changes pitch and tempo together. **Oct / 5th** turns the Speed
  dial into steps of octaves and fifths.
- **Stop** slows the tape to a halt, and **Play** spins it back up.
  **Reverse** runs the tape down through zero and back up the other way.
  **Glide** sets how long these speed changes take.
- **Overdubs** are written at the tape's current speed, so a part overdubbed at
  half speed plays back an octave up at normal speed.
- **Dub Keep** sets how much of the existing loop survives each overdub pass.
- **Scrub**: while stopped, click or drag the waveform to move the tape.
- **Start / End** set which part of the tape loops. Set them with the knobs or
  by dragging the handles on the waveform. Playback and overdubs stay inside
  that window, and the audio outside it is kept.
- **Snap points** (1/16, 1/8, 1/4, 1 bar) snap Start and End to note values,
  counted from where the recording began. A faint grid on the waveform shows
  the snap positions. Free lets them move anywhere.

| Button | Does |
|---|---|
| REC | Empty: start recording. Recording: close the loop and keep overdubbing. Playing: overdub on/off. |
| PLAY | Recording: close the loop and play. Playing: stop. Stopped: play. |
| CLEAR | Erase the loop. |

- **Quantize** (Beat / Bar) holds a button press until the next beat or bar.
  The REC button blinks while a press is waiting.
- **Length** (1 to 8 bars) stops the first recording automatically at exactly
  that length, so the loop stays in time with the song.
- **Save Loop** stores the loop audio in the project, as 24-bit FLAC.
- **Length limit**: loops can be up to 2 minutes long.
- **MIDI mapping**: REC, PLAY and CLEAR are momentary parameters, so they can be
  MIDI-mapped in the DAW.

### Classic Macros

Classic Macros brings back the hardware's one-knob behaviour:
- Delay Feedback also sets the delay's dry/wet.
- Delay Time also sets the reverb decay.
- Reverb Mix also sets tone and diffusion.
- Warble sets both depth and rate.
- The glitch delay's panning and event choice follow TEMPO.

The controls this mode takes over are greyed out.

## Build (macOS)

You need CMake 3.22+ and Xcode or the Command Line Tools. JUCE is downloaded on
first configure.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

The AU and VST3 are copied to `~/Library/Audio/Plug-Ins/` after each build. To
turn that off, configure with `-DPRISM_INSTALL_PLUGINS=OFF`. A standalone app
is in `build/PrismFX_artefacts/Release/Standalone/`.

By default the build targets this Mac's chip only and skips link-time
optimisation, so rebuilds are quick. For a release build:

```bash
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DPRISM_LTO=ON
```

To check the AU passes validation (Logic runs the same check):

```bash
auval -v aufx Pfx1 Prsm
```

### Dev tools

Configure with `-DPRISM_SNAPSHOT=ON` to build two dev tools:
- `PrismSnapshot` renders the editor to a PNG without a DAW. You can set
  parameters on the command line.
- `PrismStateTest` records a loop, saves the plug-in state, restores it into a
  new instance, and checks the loop and routing came back.

```bash
cmake -B build -DPRISM_SNAPSHOT=ON && cmake --build build --target PrismSnapshot PrismStateTest
./build/PrismSnapshot_artefacts/Release/PrismSnapshot ui.png glitch_on=1 reverb_mix=0.4
./build/PrismStateTest_artefacts/Release/PrismStateTest
```

## Porting notes

- The DSP in `Source/dsp/` is plain C++ with no JUCE or Daisy dependencies.
- The firmware runs at a fixed 48 kHz. Smoothing coefficients, delay lengths and
  filter coefficients are rescaled for the host rate (`RateScale` in `DspUtil.h`).
- The reverb's delay lengths are fixed at compile time, so at 88.2 kHz and above
  the reverb runs at half or quarter rate to keep its sound.
- The tape delay and reverb keep the firmware's 16-bit storage and clamping,
  which are part of their sound.

## Name

The plug-in name is set in one place: `PLUGIN_NAME` in `CMakeLists.txt`. The
AU/VST3 identity (manufacturer `Prsm`, plug-in `Pfx1`) is separate. Don't
change it after people have saved projects with the plug-in, or the DAW won't
find it in those projects.
