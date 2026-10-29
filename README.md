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
| [tools/screen_lab.py](tools/screen_lab.py) | The SDK's workbench with a screen lab panel (browser only): the OP-XY look, which draws strokes as the device does (a shrunk piece keeps its full stroke width); the node markers' shape and corners; the colour |
| [tools/check_renders.py](tools/check_renders.py) | Renders every reference setting through the machine and runs `compare.py` |
| [tools/probe.c](tools/probe.c) | The float reference with every stage exposed, for levels and debugging |

The SDK lives beside this repository (`../open-xy`, branch `staging`), which builds for OP-XY OS 1.2.1.

## Controls

| Encoder | Control | Readout |
| --- | --- | --- |
| 1 | DROP: the FM starts away from the note and drops onto it. One knob drives the start offset (+9 to -31.5 semitones) and the time (37 to 79 ms); no offset at 25 (0..25 covers the reference's knob up to its zero, 0.17; 25..99 the rest) | 0..99 |
| 2 | RATIO: op2 : carrier, the readout / 100 (0.00..0.98), and 1.00 at 99 | 0..99 |
| 3 | STRETCH: the material's mode ratios in log2 times 2^(2 x - 1): squeezed to ratio^1/2 at 0, the material at 49.5, spread to ratio^2 at 99 | 0..99 |
| 4 | MODES: 1..6 modes, continuous (1 at 0, 6 at 99); the next mode fades in | 0..99 |

Every readout is 0..99, as the OP-XY's own synths show theirs. Under them are the reference's mappings (DROP skewed and RATIO stepped as above), except that MODES runs over the reference's first six modes, not its eight
(for CPU: each mode costs about 840 instructions per voice block). The modes sit at the material's ratios with the reference's small fixed detune
per mode; the first is at 0.54 of the note (0.29 at STRETCH +100). Make-up is the reference's: modes off the FM's
harmonic lattice get up to 18 dB more (they catch less of it), modes stretched up up to 12 dB more. The modes stop at
10.9 kHz, fading from 9.8 kHz.

The presets: **Metal** (STRETCH 99), **Material** (50), **Squeezed** (25), **All modes** (MODES 99) and **Fall**
(DROP 99).

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

The band after the modes carries the output gain: the brief's gain 143, which puts the loudest reference setting
(RATIO 1) at 1/8 of full scale (the peak 2op FM declares), and 12 dB more (`LEVEL_DB` in `tools/gen_tables.py`). There
the loudest notes would pass the SDK's half-scale ceiling (-6 dBFS), so a **limiter** holds each voice 0.3 dB under it
(`limit` in `engine.cpp`), by gain alone: nothing shapes the wave, and there is no per-setting trim. Then the output is
**doubled**, to just under full scale (`outputPeakQ31` 2147483647): 18 dB over the brief in all.

**Why full scale.** A recording on the OP-XY (build 016, before the doubling: Metal struck four times, then the stock
Wavetable, the same ADSR and track volume, no filter) put Metal 13.9 dB under Wavetable in K-weighted loudness: its
peaks 9.9 dB lower (-29.3 against -19.4 dBFS in the recording, the first 250 ms of each note) and 4 dB more above its
average (9.5 dB against 5.5). A typical Metal note then sat about 4 dB under half scale, so Wavetable's peak is about
full scale per voice: the stock engines do not keep to the SDK's half scale. The doubling takes 6 dB of the gap; the
rest is Metal's crest (the strike and the inharmonic modes) and the headroom its typical note leaves under the
limiter. Whether the OP-XY's filter and mixer take Metal at full scale cleanly, at high resonance and with many
voices, is for the device to show.

**The limiter.** The voice sounds a block (1.45 ms) late, so the limiter knows each block's peak before the block
sounds. A block ramps linearly from the last block's gain to the lowest of: what holds it under the threshold, what
holds the next block under it, and the last gain brought back by 0.0145 dB (1 dB per 100 ms). Both ends of the ramp,
and every sample between, are at or under what the block needs, so no sample passes the threshold, and the gain
falls over at least a block. Where nothing passes the threshold the gain stays at 1, and the voice is the same sample
for sample as without the limiter, a block late. Over 800 random settings at notes C1 to C7 every half octave, it
turns down 2% of notes, by 0.5 dB typically and 2.4 dB at most. On the six notes it turns down most (by 1.5 to 2.2 dB),
the floor between the partials rises 0 to 2.6 dB in the windows where it acts, and not at all elsewhere (the soft knee
below: 7 to 15 dB). It costs 264 bytes a voice (`voiceBytes` 936 then, 908 now) and, counted from the compiled Blackfin code (not the simulator; the CPU
table below is from before it), about 610 instructions a voice block, 950 while it turns a note down: three hardware
loops (the block's peak, 4 instructions a sample; the held block out and the new one in, 5; or with the gain, 9) and
its log2 and exp2. It runs in a function of its own: inlined into `render()` its loops lost their hardware loops (12
instructions a sample). Every note
starts 1.45 ms after its key.

**Why this level.** Measured on the host against 2op FM (`open-xy-core-machines`, `machines/fm2op`), 800 random
settings at notes C1 to C7 every half octave, and eight voices with each new note taking the one whose note started
longest ago, the taken note fading over 23 ms (as with [etcetc000/open-xy#183](https://github.com/etcetc000/open-xy/pull/183)),
random notes C1 to C7 10 to 60 ms apart, 24 settings:

| | Metal at 143 | +6 dB | +12 dB, limiter | Metal now (doubled) | 2op FM |
| --- | --- | --- | --- | --- | --- |
| One voice: peak, median / loudest | -22.4 / -16.1 dBFS | -16.4 / -10.1 | -10.4 / -6.3 | -4.3 / -0.3 | -18.1 / -18.1 |
| One voice: K-weighted loudness (400 ms, loudest), median | -33.2 LUFS | -27.1 | -21.1 | -15.1 | -21.6 |
| Eight voices, keys held: sum's peak, loudest setting | -7.4 dBFS | -1.4 | +4.6 | +10.6 | +0.5 |
| Eight voices, a 2 s decay: sum's peak, loudest setting | -10.4 dBFS | -4.4 | +1.5 | +6.9 | -2.3 |

A Metal note's peaks sit 8 to 14 dB above its loudness (2op FM's: 4). That is mostly the waveform, not the strike:
six inharmonic modes stack into peaks, and the first 50 ms is only about 1 dB louder than the ring. At 143 Metal was
12 dB quieter than 2op FM, at +6 dB still too quiet on the OP-XY, and at +12 dB as loud as 2op FM but still well
under the stock engines (above). Eight voices played fast sum past full scale on the host, 10 dB more than 2op FM's. The machine once ran 13 dB over the brief with a table of
trims over the four knobs (`trim.h`, 57,800 bytes) and clipping was heard on the front of the strikes when lots of keys
were played in quick succession, but its recording (peak -0.1 dBFS) has no flattened tops and the same crest factor
as one at 143: what was heard was most likely the taken voices' cut ([Voices](#voices)), since faded. Whether the
OP-XY's mix takes eight such voices cleanly is for the device to show.

Tried and dropped: **a soft knee** (8 dB into it, then 3: it bent the attack and the ring, adding 7 to 15 dB of energy
between the partials, heard as grit), **phase rotation** (allpasses to lower the peaks without changing the
spectrum: -1.7 to +2.7 dB, no gain on balance), **less attack** (without BITE and STRIKE the voice is a steady tone
for the stock envelope to shape, but that gains only 1 to 2.5 dB and changes the strike, which the stock envelope
cannot put back), and **+6 dB without a limiter** (the most that fits under the ceiling: still too quiet on the OP-XY).

**Every strike from silence.** A voice that has sounded and gets a new note plays the old note's held block (the
limiter's) fading out to zero (1.45 ms, linear), then starts from silence, as a voice taken from another note does.
Before, the strike was added to the ring: at the old level, with DROP 0, RATIO 0, STRETCH 0, MODES 99 and the eight
voices above, 19% of 2,000 strikes passed the ceiling (at most by 11 dB), and repeated strikes in step with a mode
could build it up past full scale (Overflow, below). A restruck note starts 1.45 ms later and no longer carries the
previous ring into it; a single strike is unchanged. `tests/sound.cpp` (restrike_check) plays the eight voices at four
settings and fails on any sample at the ceiling; limiter_check renders every reference setting through the limiter
and without it, and fails on a sample over the threshold or any change where nothing passes it.

## Overflow

(Since a restruck voice now starts from silence (Level, above), strikes no longer build up; the guards stay.)
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
- **The band** after the modes saturates where its output would leave the 32-bit range (full scale): it is worked
  out as a quarter of itself and the rest, and the quarter cannot wrap until four times the range.
- **The FIR** finds its second difference by parts where it would wrap (a full-scale mix at the top of the band), and
  **the half-band** runs on a quarter of the band's output, and its quadrupling back saturates at full scale where it
  would wrap: its allpasses can overshoot their input's range, to 1.43 times it. The limiter then holds the voice
  under half scale, and the doubling after it to full scale is exact.
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
  and damped every sample, the strike's rotation, the half-band) built on the reference's state and
  set-up.
- It renders all 27 reference settings through the port and through the double-precision model, and compares the
  FM core, the resonator's drive, the mix after the modes and the output: every stage of every setting at 62 to 79 dB
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
  gliding per output sample. Sines are interpolated from the 2048-point Q30 table the set-up uses (within 1.2e-6;
  [Fidelity](#fidelity)).
- **The chain, fitted.** The reference's two keytracked high-passes, presence peak and high shelf (sixth order) are two
  biquads here, fitted in units of the note (`tools/gen_tables.py`, CHAIN): within 1.4 dB from 0.2 times the note, 0.9 dB
  from half the note. A resonant high-pass shapes the drive (where the reference's first high-pass and presence peak
  are), and a band follows the modes. Both are made per note by the bilinear transform warped at the note, at the
  voice rate, stored near their poles.
- **Modes** are the reference's two-pole resonators, fed the drive and damped every voice sample
  ([Fidelity](#fidelity)). Each mode runs through the whole block in its own loop: with the Blackfin's eight data
  registers, two modes per loop spilled more than sharing the input saved. Low modes keep `2 - 2 r cos th` as their
  coefficient (`r (2 - 2 cos th) + 2 - 2 r`), so a mode at 20 Hz holds its tuning to about 0.05 cents; `-r^2 y2` is
  `-y2 + (1 - r^2) y2`.
- **STRIKE** rings through its own copy of each mode for the note's first 8 voice samples, then joins the mode rotated
  by the filters it skips in the reference (magnitude and phase).
- **Both fixed low-passes** (20.7 kHz, Q 0.2) are one 3-tap linear-phase FIR of shifts at the voice rate, within
  0.5 dB of the pair to 10.9 kHz.
- **Make-up's lattice** (each material ratio folded onto RATIO's FM lattice) is worked out when RATIO moves, not per
  note.
- **Transcendentals and divisions** happen only in set-up, made of table sine, log2 and exp2 (`fixmath.h`). The sine
  table's interpolation puts its curvature back, which keeps modes driven on their own frequency tuned to a few parts
  in 10^9.

### Fidelity

Three shortcuts once taken for CPU made every note sound a little lo-fi on the OP-XY: a buzz, and a shimmer at the
top. Each is gone; measured on the host against the float reference (`tools/check_renders.py`'s renders, third-octave
bands in the ring), the port now sits at or under the reference's floor in every band to 11 kHz, 125 to 137 dB down.

**The FM sine.** The FM core once read its sines from a 4096-point Q15 table without interpolation. The phase error that leaves made a
flat floor about 75 dB down in every third-octave from 1 kHz to 11 kHz, on every note, C2 included, where the float
reference is 100 to 140 dB down: heard as a lo-fi buzz. (The double-precision model in `tests/sound.cpp` read the
same table, so its stage check did not see it.) Interpolated from the 2048-point Q30 table, the floor in the ring is
125 to 137 dB down, at or under the reference's; the Q15 table was removed (8 KB). It costs about 420 instructions a
voice block (op2 26 to 32 a sample, the carrier 13 to 20; counted from the compiled code).

**The modes' feed** (the shimmer). Modes below VR/4 (5.5 kHz) took the drive once per two voice samples, as the pair's
sum. A mode's output carries its input directly, so that input, zero on every other sample, put an image of the note
mirrored about 11,025 Hz into the output: a pair of tones at 11,025 Hz +- the loudest partial, which move with the
note. The upper modes get the most make-up, so it grew with MODES: about 88 dB down at the defaults, 55 dB at MODES 99,
as a recording from the OP-XY (build 020) showed. Fed every sample, as the reference does, the pair is 147 dB down.

**The modes' decay.** The modes ran undamped, their decay (`r^8`) applied once per 8-sample step: a step in level every
step, which put tones 2,756 Hz either side of each mode (and at twice and three times that), about 83 dB under the
peak (67 dB in that recording). Damped every sample, they are 134 dB down.

The two cost about 320 instructions a mode per voice block: the mode loop runs about 23 instructions a sample
against about 13 a sample before (counted from the compiled code; unrolled by two, or a function of its own, it was no
better), and the drive's high-pass, with no pair sums to form, about 110 fewer. In all about 8,900 a voice block at
the defaults and 12,200 at MODES 6, against 8,400 and 10,400 with the shortcuts (the table below is from before the
three). The voice is 28 bytes smaller (`voiceBytes` 908).

**The half rate is not the cause** of any of it: the reference has almost nothing above 5 kHz at most settings
(120 dB down above 11 kHz at the defaults, 100 dB at C6 with RATIO 1). A full-rate voice would need the same two fixes
and cost about 13,000 a block at the defaults and 18,300 at MODES 6, over the budget, for what is left: the half-band's
images near 19 to 22 kHz on high notes, 68 to 90 dB down before these changes, and now about 167 dB down at the
settings measured.

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

A later pass, again the same output bit for bit (7,500 renders of the overflow check's kinds of settings on the host,
and the Blackfin code against the host at four settings), took 857 instructions off a held voice block at the CPU
benchmark's setting (knobs at midpoint, note 60): 8,956 to 8,099, and 10,058 to 9,201 at MODES 6 (440 Hz). These
counts are from an interpreter of the pinned compiler's Blackfin output, which reads about 8% above the simulator's
figures in the table; the differences are what to compare. What it did:

- The low-pass FIR and the half-band each run in a function of their own: the compiler then gives the rest of the
  voice's code better registers, and the FIR becomes a hardware loop.
- The band's range check compares `e >> 2` with q, and the output saturates from `|u| >= 2^29` (the half-band's
  bound; now `full4`, at full scale), each in fewer instructions and registers.
- Make-up's easing loop stops running once every mode has reached its target (`Voice::easing`).

Most of that saving is register allocation in one large function, so later changes to `process()` can move it either
way. Tried and not kept (each more instructions): per-function optimisation attributes (and clang rejects them), the
half-band's paths in separate loops, the numerator or the carrier unrolled, the high-pass, the core or the modes in
functions of their own.

`sdk.py test` estimates 20,000 to 30,000 Blackfin instructions per voice block at the heaviest setting, varying that
much between runs. The estimate times the host, where the SDK's portable Q31 multiply takes a dozen instructions and an
unpredictable branch, so it is mostly a count of multiplies. No device measurement has been made.

## Voices

`voiceCapacity` is 8, as in the prototype. The OP-XY gives every note its own voice index; with more notes ringing
than voices, the SDK takes the voice whose note started longest ago. In the SDK as released, that voice stops where
it is (the old note's index renders silence from the next sample), and with lots of keys played in quick succession
the cut clicks on the front of the new strike: on the host, 290 steps a minute at DROP 0, RATIO 0, STRETCH 0,
MODES 99; on the device, steps at the strikes that took a voice. A Metal voice rings at full level until the stock amp
envelope ends it, so the cut is loud; a pluck (Karplus, also 8 voices) is nearly silent by then. A machine cannot fade
a taken voice itself: its old index goes silent, and a tail rendered through the new note's index would get the new
note's envelope.

The fix belongs in the SDK's voice adapter: [etcetc000/open-xy#183](https://github.com/etcetc000/open-xy/pull/183)
starts the new note at once in one of four spare voices and fades the old note over 23 ms (a raised cosine): Metal's
lowest mode is at 0.54 of the note (35 Hz at C2), and a one-block fade (1.45 ms) of it still clicked on the device
(on the host, 11 to 36 dB of the cut's 23 to 44 dB of click left at C1 to C4; 23 ms leaves about none). Until it is
merged, a build from the released SDK clicks where a voice is taken. Tried on the device:
16 voices made the clicks rare and 256 (as 2op FM has) removed them, but a machine should not need a voice per stock
index to sound clean.

## Licensing

Metal is clean-room: written from measurement and analysis, with no Elektron code or data. No AGPL code (the SDK's
Timestretch) is used. The repository's LICENSE is the MIT licence.
