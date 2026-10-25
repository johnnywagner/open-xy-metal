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

- The test compiles the reference into itself twice: as it is, and in double precision with the machine's 1x FM core.
- It renders all 27 reference settings through the port and through the double-precision model, and compares the
  FM core, the resonator's drive, the mix after the modes and the output.
- Every stage of every setting matches at 59 to 64 dB SNR, including C1, where low modes sit near 20 Hz.
- The float reference itself sits further from its own formulas. Its float32 `2 r cos th` puts low modes a few cents
  off, and its float32 resonators have a noise floor 20 to 30 dB above the port's.

**As sound** (`tools/check_renders.py`, `compare.py`, the port against the float reference as the machine runs it):

- 26 of 27 settings PASS. Level is within 0.04 dB median and 0.31 dB p99, spectrum within 0.22 dB, onset within
  0.15 dB.
- `extreme_b` (C1) shows 1.6 dB in a weak 50 Hz band. That is the float reference's own rounding: the port matches
  the double-precision reference there.

**Fixed point only:**

- Compiled for an ARMv6-M core (no FPU, no divide instruction, no 64-bit multiply), every audio callback's object has
  no external references except the SDK's two Q31 helpers, at -O2 and -O0. That means no float, division or 64-bit
  runtime helpers.
- The source has no `float` or `double`.

## Design notes

- **FM core at 1x.** The reference runs its core at 2x with a half-band decimator. Here it runs at 1x with the 2x
  rate's quantities: op2's sample difference halved, the carrier's phase offset doubled, and the one-poles at their
  1x coefficient. Against the 2x build, `compare.py` passes even at C8 with RATIO 1 (worst band 1.1 dB). Against
  the reference, the 1x port scores as the 2x port did on every setting, the largest change being 0.15 dB of
  spectrum on `extreme_a`. The 2x version is in git history (the first commit).
- **Modes** are the reference's two-pole resonators. Each stores `2 - 2 r cos th` and `1 - r^2` as Q31 coefficients
  rather than `2 r cos th` and `r^2`, so a mode at 20 Hz holds its coefficient to 1 part in 18,000. Their
  coefficients are made per note and per knob change; the make-up gain eases per block.
- **Filters** are RBJ biquads in direct form I, stored the same way near their poles.
- **The second fixed low-pass** runs before the resonator: the chain after the FM core is linear. STRIKE, which the
  reference sends through it after the modes, goes through it in its table instead.
- **Transcendentals and divisions** happen only in set-up, made of table sine, log2 and exp2 (`fixmath.h`).
- **Output:** the loudest reference setting (RATIO 1) peaks at 0.127 of full scale, about 1/8 as 2op FM does
  (gain 143). The declared peak is 3/16 full scale.

## CPU

`sdk.py test` estimates **about 50,000 to 76,000 Blackfin instructions per voice block** at the heaviest setting
(8 modes; about 59,000 typical), against an aim of 15,000 and a risk line of 24,000. The figure varies from run to
run on the host; 2op FM measures 11,600 to 18,900 in the same runs, so rings reads 4 to 4.7 times 2op FM.

Counting instructions with the Q31 multiply as the one instruction it is on the Blackfin (valgrind), rings is
18,000 per block at its defaults and 25,000 with 8 modes. 2op FM is 5,600 by the same count. A pitch bend that re-runs
the note set-up every block adds about 7,000. So a rings voice costs about 3 to 4.5 times a 2op FM voice. The work is
the voice itself: an FM core, six biquads and up to eight resonators per sample. No device measurement has been made.

## Voices

`voiceCapacity` is 8, as in the prototype. The SDK gives a new note a free voice, or else takes the voice whose note
started longest ago. The prototype preferred released voices over held ones, which avoided clicks when a ringing voice
was retaken. The SDK's rule cannot express that preference, so a taken voice stops where it is. Whether that clicks
audibly needs a listen on the device with more than eight notes ringing.

## Licensing

rings is clean-room: written from measurement and analysis, with no Elektron code or data. No AGPL code (the SDK's
Timestretch) is used. The repository's LICENSE is the template's MIT notice; put your own name in it if you want.
