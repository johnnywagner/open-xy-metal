# Metal for the OP-XY

Metal is a two-operator FM exciter that drops onto each note and strikes a MODAL resonator. This
repository ports it to the OP-XY as an [Open-XY](https://github.com/etcetc000/open-xy) synth machine. It is a
fixed-point rewrite of the float reference engine in [port-kit/](port-kit/BRIEF.md): Q31 throughout, with no float,
division or 64-bit arithmetic in the render path, checked against a double-precision model stage by stage. It keeps
the reference's own mode tuning: atonal and metallic.

Other versions were tried and removed; they are in the git history: rings with tonal mappings (a harmonic STRETCH
middle, RATIO magnets; `git show 4669bc6`), a delay-loop resonator (rings3, once `ringsalt`), STRETCH as stops
through resonator shapes (rings4, `129c5a5`), and rings tuned to consonant intervals or semitones (rings5 and rings6,
`61ebe9e`). The machine here was called rings2 until those went, then rings, and is now Metal (ID `metal`).
The port kit keeps the prototype's name: its engine is `rings.c`.

| Folder | What |
| --- | --- |
| [machines/metal](machines/metal) | The machine: `engine.cpp`, `fixmath.h`, `core.h`, `tables.h` (generated), `display.cpp`, artwork, `definition.json`, `machine.json`, and its sound test `tests/sound.cpp` |
| [port-kit](port-kit) | The port kit as handed over: the brief, the float reference engine, the 27 reference renders, `compare.py`, the prototype |
| [tools/gen_tables.py](tools/gen_tables.py) | Generates `tables.h` and `display_tables.h` from the reference's constants and checks the filter designs |
| [tools/gen_panel.py](tools/gen_panel.py) | Generates the screen's artwork, `panel.svg.fragment`: the pieces of the twelve rings and the stroke palette `display.cpp` uses |
| [tools/check_renders.py](tools/check_renders.py) | Renders every reference setting through the machine and runs `compare.py` |
| [tools/probe.c](tools/probe.c) | The float reference with every stage exposed, for levels and debugging |

The SDK lives beside this repository (`../open-xy`, branch `staging`).

## Controls

| Encoder | Control | Readout |
| --- | --- | --- |
| 1 | DROP: the FM starts away from the note and drops onto it. One knob drives the start offset (+9 to -31.5 semitones) and the time (37 to 79 ms) | 0..100 |
| 2 | RATIO: op2 : carrier, 0..1 | 0.00..1.00 |
| 3 | STRETCH: the material's mode ratios in log2 times 2^(2 x - 1): squeezed to ratio^1/2 at -100, the material at 0, spread to ratio^2 at +100 | -100..+100 |
| 4 | MODES: 1..6 modes, continuous; the next mode fades in | 1.0..6.0 |

All four are the reference's mappings, except that MODES runs over the reference's first six modes, not its eight
(for CPU: each mode costs about 520 instructions per voice block). The modes sit at the material's ratios with the reference's small fixed detune
per mode; the first is at 0.54 of the note (0.29 at STRETCH +100). Make-up is the reference's: modes off the FM's
harmonic lattice get up to 18 dB more (they catch less of it), modes stretched up up to 12 dB more. The modes stop at
10.9 kHz, fading from 9.8 kHz.

The presets: **Metal** (STRETCH +100), **Material** (0), **Squeezed** (-50), **All modes** (MODES 6) and **Fall**
(DROP 100).

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

The band after the modes carries the output gain: 12 dB above the brief's level (which was 13 dB under 2op FM), plus
`BOOST_DB` (1 dB). The output is clamped at the SDK's half-scale ceiling (-6 dBFS); there is no limiter.

That 1 dB is all the clean headroom there is. The loud part of a Metal note is its first 50 ms (the strike, BITE and
the modes' early ring), which peaks 11 to 16 dB above the ring's loudness: over a grid of every knob setting from C1
to C7, peaks run from -14.7 to -6.2 dBFS before the boost (median -9.5). With 1 dB, 1.5% of those renders touch the
ceiling, by at most 0.8 dB, for a few samples; with 2 dB, 10%; with 3, 21%. Tried and dropped:

- **A soft knee** (8 dB into it, then 3): it bent the attack and the ring, adding 7 to 15 dB of energy between the
  partials at 8 dB. Heard as grit.
- **Phase rotation** (allpasses at fixed or keytracked frequencies, to lower the peaks without changing the spectrum):
  from -1.7 to +2.7 dB, no gain on balance. The peaks are the attack's, not the waveform's shape.
- **Less attack** (no BITE, or no STRIKE): 1 to 3 dB less peak for the loudness at some settings, none at others, and
  it changes the strike.

For more level, turn the track up on the OP-XY.

## Overflow

A voice struck again while it rings adds each strike to the modes, and repeated strikes in step with a mode build it up
past full scale. The 32-bit sums used to wrap there, which turns an over into a full-scale spike of the other sign: a
pop. At the default knobs, the same voice struck every 131 ms (16ths at about 115 BPM) wrapped on 11 of 85 notes from
C1 to C8; every 65 ms, on 23. Now every place that could wrap saturates instead, and the output is the same sample for
sample wherever nothing wrapped before:

- **The mix.** The modes add into the mix with wrapping sums, which are exact as long as each sample's total is in
  range, whatever the partial sums do on the way. The first pass only notes whether a partial sum wrapped; when one did
  (rarely: a voice struck again and again), the block's modes run again counting each sample's wraps, and a sample whose
  count is not zero saturates.
- **The modes.** A mode at 0.8 of full scale or more (from its state, once a block and after a strike joins) runs the
  block with each output saturated where it would wrap. A mode grows by under 3% of full scale a step and one note
  alone stays below 0.75, so a mode below 0.8 cannot reach full scale before the next look.
- **The band** after the modes saturates where its output would leave the 32-bit range (twice the ceiling): it is worked
  out as a quarter of itself and the rest, and the quarter cannot wrap until four times the range.
- **The FIR** finds its second difference by parts where it would wrap (a full-scale mix at the top of the band), and
  **the half-band's** doubling to the output saturates where it would wrap: its allpasses can overshoot half scale, to
  1.43 times it.
- **The chain** stops following the note at 6.78 kHz. Above that, a sum in its set-up left the 32-bit range (which gave
  the host and the OP-XY different coefficients), and from about 7.8 kHz its band was unstable.

Checked over 36,585 settings (the knobs at three to five positions each, notes from C1 to G9, struck every 4 ms to 260 ms or once, with
bends and knob sweeps) and the 1,875 of the CPU check's grid: the output is bit for bit the old one in every setting
where nothing in the old engine left the 32-bit range (it did in 8,966), and nothing in the new one ever does. The
Blackfin build renders the grid bit for bit as the host does.

## How it was checked

```sh
python ../open-xy/sdk.py test --machine machines/metal --cxx clang++ --host-cxx g++
python tools/check_renders.py          # needs numpy
python tools/gen_tables.py             # after changing a constant
```

**Stage by stage, sample by sample** (`tests/sound.cpp`, run by `sdk.py test`):

- The test compiles the reference into itself twice: as it is, and in double precision with the machine's own
  algorithm (the voice at half rate, the core with op2 stepping four reference samples, the fitted chain, the modes fed
  in pairs or every sample, the strike's rotation, the half-band) built on the reference's state and
  set-up.
- It renders all 27 reference settings through the port and through the double-precision model, and compares the
  FM core, the resonator's drive, the mix after the modes and the output: every stage of every setting at 56 to 68 dB
  SNR (limit 55).
- Against the engine before it ran at half rate: every spectral peak within 50 dB of the loudest matches to 0.3 dB up
  to C6 at the default settings, and to 1 to 3 dB at the extremes (C7, RATIO 1, the top modes near 10 kHz). What
  went is content above 11 kHz, 70 dB down at every setting, and some of the old engine's own images near 11 kHz.

**Fixed point only:**

- Compiled for an ARMv7-A core with soft float (no FPU, no divide instruction), every audio callback's object has
  no external references except the SDK's two Q31 helpers. That means no float, division or 64-bit runtime helpers.
- The source has no `float` or `double`.

## Design notes

The engine is the reference's signal path, simplified for the OP-XY's CPU.

- **The voice at half rate.** Everything up to the output runs at 22.05 kHz (VR): the FM core's output is 70 dB down
  above 11 kHz at every setting, and the modes stop there. A polyphase IIR half-band (two paths of first-order
  allpasses, four coefficients; the Valenzuela-Constantinides elliptic design, as in hiir) takes it to 44.1 kHz: flat
  to 9 kHz, images above 13 kHz 63 dB down.
- **FM core.** The reference runs its core at 2x with a half-band decimator. Here op2 and its one-poles step four
  reference samples per voice sample (op2's difference quartered, the one-poles at their coefficient for four, the
  feedback extrapolated to where the 2x core would read it), and the carrier steps two output samples, its increment
  gliding per output sample. Sines come from a 4096-point table without interpolation.
- **The chain, fitted.** The reference's two keytracked high-passes, presence peak and high shelf (sixth order) are two
  biquads here, fitted in units of the note (`tools/gen_tables.py`, CHAIN): within 1.4 dB from 0.2 times the note, 0.9 dB
  from half the note. A resonant high-pass shapes the drive (where the reference's first high-pass and presence peak
  are), and a band follows the modes. Both are made per note by the bilinear transform warped at the note, at the
  voice rate, stored near their poles.
- **Modes** are the reference's two-pole resonators, run undamped, with their decay (`r^8`) applied every 8 voice
  samples. Below VR/4 (5.5 kHz) they take the drive once per two samples as the pair's sum (gain corrected by
  `1/cos(th/2)`), above it every sample. Each mode runs through the whole block in its own loop: with the Blackfin's
  eight data registers, two modes per loop spilled more than sharing the input saved. Low modes keep
  `2 - 2 cos th` as their coefficient, so a mode at 20 Hz holds its tuning to about 0.05 cents.
- **STRIKE** rings through its own copy of each mode for the note's first 8 voice samples, then joins the mode rotated
  by the filters it skips in the reference (magnitude and phase).
- **Both fixed low-passes** (20.7 kHz, Q 0.2) are one 3-tap linear-phase FIR of shifts at the voice rate, within
  0.5 dB of the pair to 10.9 kHz.
- **Make-up's lattice** (each material ratio folded onto RATIO's FM lattice) is worked out when RATIO moves, not per
  note.
- **Transcendentals and divisions** happen only in set-up, made of table sine, log2 and exp2 (`fixmath.h`). The sine
  table's interpolation puts its curvature back, which keeps modes driven on their own frequency tuned to a few parts
  in 10^9.

## CPU

Blackfin instructions per voice block, counted on the GNU Blackfin simulator running the SDK's own compiler output
(the pinned GCC 8 fork, `-mcpu=bf707 -O2`; the simulator given the Blackfin+ 32-bit multiply it lacked), at 440 Hz:

| Setting | Metal | without the overflow guards | before the final pass | 2op FM |
| --- | --- | --- | --- | --- |
| The defaults (MODES 2), a held note | 7,220 | 5,930 | 8,100 | 4,900 (its defaults) |
| The defaults, while the drop runs (up to the first 90 ms) | 7,770 | 6,490 | 8,850 | |
| MODES 6 | 9,210 | 7,390 | 10,280 | |
| A note-on block, a fresh voice (defaults) | 16,460 | 15,190 | 18,390 | |
| A note-on block, a fresh voice (MODES 6) | 20,530 | 18,590 | 22,690 | |

A block is 1.45 ms, about 580,000 cycles at 400 MHz: eight held voices take about 13% of it at MODES 6, and eight
voices starting in the same block about 28%. A block where a partial sum of the mix wraps (a voice built up by
repeated strikes) runs its modes twice. The Blackfin+ predicts branches, so a count of instructions is the fair
measure; data stalls and cache misses come on top (the voice's per-block code is about 9 KB, the instruction cache
16 KB; the set-up and the mix's second pass add 12 KB that run only on a change or a wrap).

The final pass first changed no output: the port rendered bit for bit what it did before, checked over a grid of 1,876
settings (pitch, knobs, retriggers, knob moves, glides) on the host and on the simulator. What it did:

- The output stage runs as separate loops (the FIR, the band's numerator, its poles, the half-band) instead of one that
  spilled most of its state; the drive's high-pass, two samples at a time, also forms the modes' input.
- The FM core runs op2 and the carrier as separate loops, with op2's feedback carried ahead and the carrier's gliding
  increment kept as one running sum. op2's depth stays below 2, so the clamp after it was dead and went.
- Each mode runs through the whole block at once (above), with no per-step dispatch.
- log2 normalises through a byte table of leading zeros. log2 of each mode's sin th, of the material ratios' lattice
  distances and of the note are kept, not worked out again per note-on, per block or per step, and the set-up
  functions stay out of the per-block code.

Then the overflow guards (above) cost about a fifth of the voice back.

`sdk.py test` estimates 20,000 to 30,000 Blackfin instructions per voice block at the heaviest setting, varying that
much between runs. The estimate times the host, where the SDK's portable Q31 multiply takes a dozen instructions and an
unpredictable branch, so it is mostly a count of multiplies. No device measurement has been made.

## Voices

`voiceCapacity` is 8, as in the prototype. The SDK gives a new note a free voice, or else takes the voice whose note
started longest ago. The prototype preferred released voices over held ones, which avoided clicks when a ringing voice
was retaken. The SDK's rule cannot express that preference, so a taken voice stops where it is. Whether that clicks
audibly needs a listen on the device with more than eight notes ringing.

## Licensing

Metal is clean-room: written from measurement and analysis, with no Elektron code or data. No AGPL code (the SDK's
Timestretch) is used. The repository's LICENSE is the MIT licence.
