# rings for the OP-XY

rings is a two-operator FM exciter that drops onto each note and strikes an eight-mode MODAL resonator. This
repository ports it to the OP-XY as an [Open-XY](https://github.com/etcetc000/open-xy) synth machine. It is a
fixed-point rewrite of the float reference engine in [port-kit/](port-kit/BRIEF.md): Q31 throughout, with no float,
division or 64-bit arithmetic in the render path. Its sound is checked against the reference stage by stage.

| Folder | What |
| --- | --- |
| [machines/rings](machines/rings) | The machine: `engine.cpp`, `fixmath.h`, `core.h`, `tables.h` (generated), `display.cpp`, artwork, `definition.json`, `machine.json`, and its sound test `tests/sound.cpp` |
| [port-kit](port-kit) | The port kit as handed over: the brief, the float reference engine, the 27 reference renders, `compare.py`, the prototype |
| [tools/gen_tables.py](tools/gen_tables.py) | Generates `tables.h` and `display_tables.h` from the reference's constants and checks the filter designs |
| [tools/check_renders.py](tools/check_renders.py) | Renders every reference setting through the machine and runs `compare.py` |
| [tools/probe.c](tools/probe.c) | The float reference with every stage exposed, for levels and debugging |

The SDK lives beside this repository (`../open-xy`, branch `staging`).

## Controls

| Encoder | Control | Readout |
| --- | --- | --- |
| 1 | DROP: the FM starts away from the note and drops onto it. One knob drives the start offset (+9 to -31.5 semitones) and the time (37 to 79 ms) | 0..100 |
| 2 | RATIO: op2 : carrier, 0..1 | 0.00..1.00 |
| 3 | STRETCH: mode spacing, squeezed (-100), as the material (0), spread (+100) | -100..+100 |
| 4 | MODES: 1..8 modes, continuous; the next mode fades in | 1.0..8.0 |

The mappings are the reference's, applied in `engine.cpp`. The defaults are the dialed-in sound, saved as the
**Rings** preset. **Material**, **All modes** and **Fall** are the same sound with STRETCH at 0, MODES at 8 and DROP
at 100.

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

## How it was checked

```sh
python ../open-xy/sdk.py test --machine machines/rings --cxx clang++ --host-cxx g++
python tools/check_renders.py          # needs numpy
python tools/gen_tables.py             # after changing a constant
```

**Stage by stage, sample by sample** (`tests/sound.cpp`, run by `sdk.py test`):

- The test compiles the reference into itself twice: as it is, and in double precision with the machine's own
  algorithm (the 1x core, the FIR, the undamped modes fed in pairs) built on the reference's state and set-up.
- It renders all 27 reference settings through the port and through the double-precision model, and compares the
  FM core, the resonator's drive, the mix after the modes and the output.
- Every stage of every setting matches that model at 58 to 72 dB SNR, including C1, where low modes sit near 20 Hz.
- The float reference itself sits further from its own formulas. Its float32 `2 r cos th` puts low modes a few cents
  off, and its float32 resonators have a noise floor 20 to 30 dB above the port's.

**As sound** (`tools/check_renders.py`, `compare.py`, the port against the float reference as the machine runs it):

- 26 of 27 settings PASS. Level is within 0.03 dB median and 0.26 dB p99, spectrum within 0.58 dB, onset within
  0.61 dB.
- `extreme_b` (C1) shows 1.6 dB in a weak 50 Hz band. That is the float reference's own rounding: the port matches
  the double-precision reference there.

**Fixed point only:**

- Compiled for an ARMv6-M core (no FPU, no divide instruction, no 64-bit multiply), every audio callback's object has
  no external references except the SDK's two Q31 helpers, at -O2 and -O0. That means no float, division or 64-bit
  runtime helpers.
- The source has no `float` or `double`.

## Design notes

The engine is the reference's signal path, simplified for the OP-XY's CPU. Each simplification was kept only if
`compare.py` still passed against the float reference on every setting.

- **FM core at 1x.** The reference runs its core at 2x with a half-band decimator. Here it runs at 1x with the 2x
  rate's quantities: op2's sample difference halved, the carrier's phase offset doubled, and the one-poles at their
  1x coefficient. While op2 is below 260 Hz (RATIO low, as in the default sound), op2 and its one-poles run at half
  rate, with its feedback extrapolated to where the 2x core would read it. Above that, half rate cost up to 3 dB at
  the onset of high notes, so it isn't used there.
- **Modes** are the reference's two-pole resonators, run undamped with their decay (`r^16`) applied every 16
  samples. Each takes its input once per two samples as the pair's sum, with its gain corrected by `1/cos(th/2)`. That
  is 1.5 multiplies per mode-sample instead of 3. Low modes keep `2 - 2 cos th` as a Q31 coefficient, so a mode at
  20 Hz holds its tuning to about 0.05 cents. Modes run in pairs, sharing the input and the accumulator.
- **Both fixed low-passes** (20.7 kHz, Q 0.2) are one 3-tap linear-phase FIR (1 multiply), within 0.7 dB to 8 kHz
  and 1.3 dB to 16 kHz of the pair. It runs before the resonator: the chain after the FM core is linear. STRIKE, which
  the reference sends through the second low-pass after the modes, goes through the exact low-pass in its table.
- **Filters** are RBJ biquads in direct form I, stored near their poles as `2 + a1` and `1 - a2`. The high-passes run
  unnormalised (2 multiplies each). The presence peak runs as x plus a band-pass of Q·A, which is exactly the RBJ peak
  (3 multiplies). Their gains, the dry share and the output gain all live in the shelf's coefficients.
- **Not simplified:** both keytracked high-passes. Folding the second one into the modes' gains failed `compare.py`
  (onsets of the drop, mid ratios): it shapes the FM's content below the note, which is not modal.
- **Transcendentals and divisions** happen only in set-up, made of table sine, log2 and exp2 (`fixmath.h`).
- **Output:** the loudest reference setting (RATIO 1) peaks at 0.127 of full scale, about 1/8 as 2op FM does
  (gain 143). The declared peak is 3/16 full scale.

The 2x-core version and the first 1x version are earlier commits.

## CPU

`sdk.py test` and the same sweep run directly give, in the SDK's estimated Blackfin instructions per voice block:

| Setting | Now | Before simplifying |
| --- | --- | --- |
| The defaults (2 modes) | about 18,000 | about 45,000 |
| Heaviest: MODES at 8 | about 33,000 | about 59,000 |

The aim is 15,000, and above 24,000 audio may drop with several voices; 2op FM reads 11,000 to 15,000 in the same
runs. Each mode adds about 2,400, and the rest of the voice is about 13,000. Counting instructions with the Q31
multiply as the one instruction it is on the Blackfin (valgrind), rings is 14,200 per block at its defaults and
19,500 with 8 modes; 2op FM is 5,600 by that count. A pitch bend that re-runs the note set-up every block adds about
7,000. No device measurement has been made.

## Voices

`voiceCapacity` is 8, as in the prototype. The SDK gives a new note a free voice, or else takes the voice whose note
started longest ago. The prototype preferred released voices over held ones, which avoided clicks when a ringing voice
was retaken. The SDK's rule cannot express that preference, so a taken voice stops where it is. Whether that clicks
audibly needs a listen on the device with more than eight notes ringing.

## Licensing

rings is clean-room: written from measurement and analysis, with no Elektron code or data. No AGPL code (the SDK's
Timestretch) is used. The repository's LICENSE is the template's MIT notice; put your own name in it if you want.
