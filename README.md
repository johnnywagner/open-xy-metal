# rings for the OP-XY

rings is a two-operator FM exciter that drops onto each note and strikes a MODAL resonator. This
repository ports it to the OP-XY as an [Open-XY](https://github.com/etcetc000/open-xy) synth machine. It is a
fixed-point rewrite of the float reference engine in [port-kit/](port-kit/BRIEF.md): Q31 throughout, with no float,
division or 64-bit arithmetic in the render path. Its algorithm is checked against a double-precision model stage by
stage. Since the port, its knob mappings have been made more tonal (Controls), so it no longer matches the reference
note for note.

The machines here now are **rings2** and two tuned versions of it. rings itself (the tonal mappings), the
delay-loop version (rings3, once `ringsalt`) and the STRETCH-stops version (rings4) were tried and removed; they are in
the git history (`git show 4669bc6` and `129c5a5`). The engine sections below (Controls to Design notes) were written
for rings and describe the engine all three share; rings2's own section says where its knobs differ.

- **rings2** ([machines/rings2](machines/rings2)) is the port with the reference's own mode tuning: the modes at the
  material's ratios (the first below the note), STRETCH as ratio^(2^(2 x - 1)), RATIO free, and the reference's
  make-up. Atonal and metallic (rings2, below).
- **rings5** ([machines/rings5](machines/rings5)) is rings2 with its four lowest modes tuned onto consonant intervals
  of the note, the four above left free (rings5 and rings6, below).
- **rings6** ([machines/rings6](machines/rings6)) is rings2 with every mode tuned onto the nearest semitone of the
  note.

| Folder | What |
| --- | --- |
| [machines/rings2](machines/rings2) | The machine: `engine.cpp`, `fixmath.h`, `core.h`, `tables.h` (generated), `display.cpp`, artwork, `definition.json`, `machine.json`, and its sound test `tests/sound.cpp` |
| [machines/rings5](machines/rings5), [machines/rings6](machines/rings6) | rings2 tuned: the same files with `tune` and `stretch_map` in `engine.cpp`, `TUNE_MASK` in `tables.h`, and the tuned map in the page and test |
| [port-kit](port-kit) | The port kit as handed over: the brief, the float reference engine, the 27 reference renders, `compare.py`, the prototype |
| [tools/gen_tables.py](tools/gen_tables.py) | Generates the three machines' `tables.h` and `display_tables.h` from the reference's constants and checks the filter designs |
| [tools/check_renders.py](tools/check_renders.py) | Renders every reference setting through the machine and runs `compare.py` |
| [tools/probe.c](tools/probe.c) | The float reference with every stage exposed, for levels and debugging |

The SDK lives beside this repository (`../open-xy`, branch `staging`).

## Controls

| Encoder | Control | Readout |
| --- | --- | --- |
| 1 | DROP: the FM starts away from the note and drops onto it. One knob drives the start offset (+9 to -31.5 semitones) and the time (37 to 79 ms) | 0..100 |
| 2 | RATIO: op2 : carrier, 0..1, pulled onto simple ratios | 0.00..1.00 |
| 3 | STRETCH: the harmonic series in the middle, the material at -50 and +50, squeezed (-100) or spread (+100) | -100..+100 |
| 4 | MODES: 1..8 modes, continuous; the next mode fades in | 1.0..8.0 |

DROP and MODES are the reference's mappings. The others were changed to make rings more tonal:

- **The first mode is always on the note.** The material counts from it, so the loudest, lowest mode is the pitch you
  play (in the reference it sat at 0.54 of the note, and STRETCH moved it further).
- **STRETCH has a harmonic middle.** Within 6 of 0 the modes are the harmonic series (1, 2, 3 ... 8 times the note),
  exactly on their multiples. Out to +-50 they morph to the material's ratios; beyond, as before, the material is
  squeezed to ratio^1/2 or spread to ratio^2.
- **RATIO has magnets** at 1/8, 1/6, 1/5, 1/4, 1/3, 2/5, 1/2, 3/5, 2/3, 3/4 and 1: within 0.07 octave of one, the
  ratio is pulled onto it (flat at the magnet, continuous at the edge). Between them it is free.
- **No lattice make-up.** The reference boosted modes that sat off the FM's harmonics by up to 18 dB so they caught as
  much of it as on them. That is gone: off-harmonic modes now ring quieter. Modes stretched up still get up to 12 dB.
- **The modes stop at 11 kHz** (fs/4), fading from 9.9 kHz.

The presets: **Rings** (the dialed-in sound, STRETCH +100), **Harmonic** (STRETCH 0), **Material** (STRETCH +50),
**All modes** (MODES 8) and **Fall** (DROP 100).

### Envelope: option B

The OP-XY gives a voice no note-off, so the **stock amp envelope shapes the whole sound** (BRIEF.md, Platform gaps,
section 2). The voice runs as the reference does at ATTACK 0 and SUSTAIN 1, struck at full strength. It keeps only
the reference's fixed 3 ms eased onset, which stops the strike clicking. There are no envelope alternates, because a
machine cannot drive the stock envelope.

`machine.json` sets the factory amp envelope's attack to 0 and sustain to 0. The SDK does not document how its
0..32767 envelope words map to seconds. For the reference's defaults (DECAY about 2 s, RELEASE about 1.1 s), set
decay and release on the device, then copy the saved words into `presetDefaults.amp`.

What option B changes, measured by `tools/check_renders.py` (the stock envelope emulated as one gain on the output,
against the kit's renders):

- **Long DECAY:** the same as the reference (level within 0.14 dB median, spectrum within 0.17 dB).
- **The default 2 s DECAY:** the tail is 1 to 2 dB quieter from 0.5 to 2 s. The mode bands are 4 to 8 dB lower over
  the render, so the ring loses some weight against the strike.
- **Short DECAY:** the ring dies with the exciter. In the reference the resonator is driven by sqrt(L) and the output
  is shaped by sqrt(L), so the ring rings on after the strike has faded. With DECAY at 0.3 the B version is 40 to
  70 dB below the reference from 0.5 to 1.5 s.

## Level

The output is 12 dB above the brief's level, which was 13 dB under 2op FM. With the first mode on the note the modes
catch more of the FM, so their drive is trimmed 8 dB (`RES_TRIM_DB`). Held at C2 to C5 (no envelope), the first second
measures about -17 to -19.5 dBFS RMS against 2op FM's -20. The strike peaks at -7 to -12 dBFS, under the SDK's
half-scale ceiling (-6 dBFS), where the output is clamped: the loudest setting (RATIO 1) touches it. To match the stock
wavetable synth exactly, record both at the same note and settings and change `LEVEL_DB` in `tools/gen_tables.py`.

## How it was checked

```sh
python ../open-xy/sdk.py test --machine machines/rings --cxx clang++ --host-cxx g++
python tools/check_renders.py          # needs numpy
python tools/gen_tables.py             # after changing a constant
```

**Stage by stage, sample by sample** (`tests/sound.cpp`, run by `sdk.py test`):

- The test compiles the reference into itself twice: as it is, and in double precision with the machine's own
  algorithm (the 1x core, the fitted chain, the half-rate modes and their interpolation, the strike's rotation) built
  on the reference's state and set-up.
- It renders all 27 reference settings through the port and through the double-precision model, and compares the
  FM core, the resonator's drive, the mix after the modes and the output.
- The model takes the tonal mappings from the engine (`stretch_map`, `ratio_magnet`).
- Every stage of every setting matches that model at 59 to 70 dB SNR, except one: C1 at extreme settings reaches
  53 dB at the output. There the first mode sits on the note, driven at its own frequency, with a 0.6 Hz bandwidth,
  and its Q30 coefficient's tuning (about 1e-5, 0.02 cents) shows. The output's limit is 50 dB, the others 55.

**As sound:** `tools/check_renders.py` still compares the port with the float reference, but the tonal mappings move
the modes and the ratio, so most settings now differ from it on purpose. Before them, 8 of the 27 settings passed
compare.py's tolerances, and every setting's level was within 0.21 dB median.

**Fixed point only:**

- Compiled for an ARMv6-M core (no FPU, no divide instruction, no 64-bit multiply), every audio callback's object has
  no external references except the SDK's two Q31 helpers, at -O2 and -O0. That means no float, division or 64-bit
  runtime helpers.
- The source has no `float` or `double`.

## Design notes

The engine is the reference's signal path, simplified for the OP-XY's CPU.

- **FM core at 1x.** The reference runs its core at 2x with a half-band decimator. Here it runs at 1x with the 2x
  rate's quantities: op2's sample difference halved, the carrier's phase offset doubled, and the one-poles at their
  1x coefficient. While op2 is below 260 Hz (RATIO low, as in the default sound), op2 and its one-poles run at half
  rate, with its feedback extrapolated to where the 2x core would read it. Sines come from a 4096-point table without
  interpolation.
- **The chain, fitted.** The reference's two keytracked high-passes, presence peak and high shelf (sixth order) are two
  biquads here, fitted in units of the note (`tools/gen_tables.py`, CHAIN): within 1.4 dB from 0.2 times the note, 0.9 dB
  from half the note. A resonant high-pass shapes the drive (where the reference's first high-pass and presence peak
  are), and a band follows the modes. Both are made per note by the bilinear transform, stored near their poles.
- **Modes** are the reference's two-pole resonators, run undamped, with their decay (`r^16`) applied every 16 samples.
- **Low modes at half rate.** A mode below fs/8 (chosen at note-on) steps by twice its angle and takes the drive's
  sum by four once per four samples, which at the even samples is exactly the full-rate mode fed by four. The
  half-rate modes are summed and interpolated back with an 8-tap half-band filter (images 53 dB down at 4.4 kHz, 39 dB at
  5.5 kHz). The mix runs 8 samples (0.2 ms) late for it.
- **Other modes** take their input once per two samples as the pair's sum (gain corrected by `1/cos(th/2)`), two
  modes per loop sharing the input and the accumulator. Low modes keep `2 - 2 cos th` as their coefficient, so a mode
  at 20 Hz holds its tuning to about 0.05 cents.
- **STRIKE** rings through its own copy of each mode for the note's first 16 samples, then joins the mode rotated by
  the filters it skips in the reference (magnitude and phase).
- **Both fixed low-passes** (20.7 kHz, Q 0.2) are one 3-tap linear-phase FIR made of shifts, within 0.7 dB to 8 kHz and
  1.3 dB to 16 kHz of the pair.
- **Transcendentals and divisions** happen only in set-up, made of table sine, log2 and exp2 (`fixmath.h`). The sine
  table's interpolation puts its curvature back, which keeps modes driven on their own frequency tuned to a few parts
  in 10^9.

The 2x-core version, the first 1x version, the 4-mode version and the 8-mode version before the tonal mappings are
earlier commits.

## rings2

rings as it was before the tonal mappings, on the current engine (eight modes, half-rate low modes, the fitted chain,
the modes stopping at 11 kHz, +12 dB). Its knobs read as rings':

- **STRETCH** scales the material's ratios in log2 by 2^(2 x - 1): ratio^1/2 at -100, the material at 0, ratio^2 at
  +100, with the reference's small fixed detune per mode. The first mode sits at 0.54 times the note (an octave below
  at +100), so there is no harmonic middle.
- **RATIO** is free: no magnets.
- **Make-up** is the reference's: modes off the FM's harmonic lattice get up to 18 dB more (they catch less of it),
  modes moved up up to 12 dB more. No drive trim: held notes sit 3 to 4 dB below rings', the strike about level.
- **Presets:** **Rings 2** (STRETCH +100), **Material** (0), **Squeezed** (-50), **All modes** and **Fall**.
- Its test is rings' `tests/sound.cpp` with the reference's tuning and make-up in the model: every stage at least
  55 dB against it.

## rings5 and rings6

rings2 tuned. STRETCH places the modes exactly as rings2 does; then modes move onto pitches of the note, in 12-TET
counted from the note, and the small fixed detune goes back on. The rest is rings2: engine, make-up, level and CPU.

- **rings5, Tuned:** the four lowest modes go to the nearest consonant interval of the note: unison and octaves,
  minor and major thirds, fourth, fifth, minor and major sixths (never a second, tritone or seventh). The four above
  stay where rings2 puts them. A tuned body under rings2's metal, as tuned percussion is made: the low partials tuned,
  the rest left to the material. At the default MODES (2) every mode that sounds is tuned.
- **rings6, Chromatic:** every mode goes to the nearest semitone of the note. Every partial is in tune with 12-TET,
  including the dissonant ones (seconds, tritones, sevenths): rings2's metal, quantized.
- **STRETCH** steps the tuned modes semitone by semitone as it turns, where rings2 glides. At STRETCH 0 (the
  material), rings2's modes sit at -10.7, +1.9, +6.0 and +10.4 semitones from the note; rings5 puts them at -12, +3,
  +7 and +9, rings6 at -11, +2, +6 and +10.
- Tuning moves no mode more than half a semitone (rings6) or a semitone and a half (rings5), so levels stay with
  rings2's at every STRETCH: no trim.
- Each test is rings2's with the tuned map in the model (`stretch_map`).

## CPU

Counting instructions per voice block with the Q31 multiply as the one instruction it is on the Blackfin (valgrind,
the most repeatable figure here), at 440 Hz as the SDK times it:

| Setting | rings2 | rings5, rings6 | rings (removed) | rings3 (removed) | 2op FM |
| --- | --- | --- | --- | --- | --- |
| The defaults | 10,900 | 10,800 | 10,900 | 9,700 | 5,600 |
| The heaviest knob of the SDK's sweep | 14,200 (MODES 8) | 14,100 (MODES 8) | 13,900 (MODES 8) | 10,600 (RATIO up) | |

Stopping the modes at 11 kHz saves most at high notes (about 15 to 20% at C6 and C7). A note-on block costs about
31,700 once in rings (the set-up of eight modes and the strike).

`sdk.py test` estimates **about 22,000 to 30,000 Blackfin instructions per voice block for rings2, rings5 and
rings6** at their heaviest settings; 2op FM reads about 14,000. The estimate times the host, where the SDK's
portable Q31 multiply takes a dozen instructions and an unpredictable branch, so it overstates multiply-heavy engines
such as these, and it varies by a fifth between runs here. The aim is 15,000, and above 24,000 audio may drop with
several voices. No device measurement has been made.

## Voices

`voiceCapacity` is 8, as in the prototype. The SDK gives a new note a free voice, or else takes the voice whose note
started longest ago. The prototype preferred released voices over held ones, which avoided clicks when a ringing voice
was retaken. The SDK's rule cannot express that preference, so a taken voice stops where it is. Whether that clicks
audibly needs a listen on the device with more than eight notes ringing.

## Licensing

rings is clean-room: written from measurement and analysis, with no Elektron code or data. No AGPL code (the SDK's
Timestretch) is used. The repository's LICENSE is the template's MIT notice; put your own name in it if you want.
