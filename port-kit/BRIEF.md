# rings → OP-XY: porting brief

rings is a finished synth voice: a two-operator FM exciter that drops onto each note, feeding an eight-mode MODAL
resonator. It was designed by ear in a browser prototype. The goal is an OP-XY synth machine (open-xy SDK,
https://github.com/etcetc000/open-xy) that sounds the same. The float C engine in this kit is the reference model.
It is the spec, not code to drop in: the OP-XY needs a fixed-point rewrite (see "Platform gaps").

## What's in this kit

| Path | What |
| --- | --- |
| `engine/rings.c`, `engine/rings.h` | The reference engine: one voice, plain C99, float. The sound's constants are at the top of `rings.c` |
| `engine/rings_glue.c` | How the prototype runs 8 voices (allocation rule, mixing). Reference only |
| `reference/render_ref.c` | Command-line renderer over the reference engine: one note at 44.1 kHz, never released, any knob settings, to a float WAV |
| `reference/renders/*.wav`, `reference/manifest.json` | 27 reference renders (3 s, 44.1 kHz, mono float, gain 100) with the exact arguments, peak and RMS of each |
| `reference/compare.py` | Compares a port's render with a reference: 10 ms level envelope, 1/3-octave spectrum and the first 50 ms. Prints PASS/CHECK against suggested tolerances. Reads float or integer PCM (a Q31 render written as 32-bit PCM compares directly). Needs numpy |
| `prototype/rings-v69.html` | The playable prototype (open in Chrome; computer keys play it). The sound to match by ear |

Build the renderer: `cc -std=c99 -O2 -Iengine reference/render_ref.c engine/rings.c -lm -o render_ref`, then
`./render_ref out.wav note=60 drop=0.5 ...` (run it with no arguments for usage).

## The sound

### Controls

The voice has eight controls, each 0..1. The mapping to sound happens inside the engine (`rings_set_knob`).

| Control | Range | Default |
| --- | --- | --- |
| DROP | The FM starts away from the note and drops onto it. One knob drives two lanes with a split at 0.533: start offset from +9 st down to −31.5 st, and time from about 37 ms to about 79 ms. The lane ends, curves and split are fixed constants (`SW_*`, `TM_*`, `DROP_SPLIT`) | 0 |
| RATIO | op2 : carrier, 0..1, linear | 0.0886 |
| STRETCH | Mode spacing: 0 squeezed, 0.5 as the material, 1 spread (ratios ^2) | 1.0 |
| MODES | 1..8 modes, continuous: 1 + 7 × knob; the mode past the whole number fades in | 0.1427 (2 modes) |
| ATTACK | 1 ms .. 4 s, exponential; never shorter than 3 ms | 0 (3 ms) |
| DECAY | 50 ms .. 20 s to −60 dB, exponential | 0.619 (2.04 s) |
| SUSTAIN | level = knob² | 0 |
| RELEASE | 20 ms .. 10 s to −60 dB, exponential | 0.647 (1.12 s) |

Everything else is fixed, with exact values in `rings.c`:
- FM depth (2ENV 44.7) and op2 feedback (2FB 8.1)
- BITE: ×2.8 FM depth at note-on, falling back over 25 ms
- PRESENCE: +10.3 dB peak at 4× the note, before the resonator
- High shelf: +7.7 dB at 6× the note, after the resonator
- MODAL: ring 3.6 s; bright tone; material between bar and bell; mallet strike; 0.5-cent spread
- Mix: 65 % wet
- Ring damp after note-off: about 1.8× RELEASE

### Signal path, per 16-sample block

1. **FM core at 2× rate**
   - op2 is a sine with self-feedback. Its difference signal goes through two one-poles, scaled by the depth, into the carrier's phase.
   - Both operators are 32-bit phase accumulators, with DROP's sweep multiplying the increment.
   - A 4-coefficient polyphase IIR half-band filter brings it back to 1×.
2. **Into the resonator**
   - Envelope stage 1, the drive: `sqrt(Lx)`.
   - Keytracked high-pass (0.45 × note, Q 0.7), low-pass at 20.7 kHz (Q 0.2), then the presence peak.
3. **MODAL**
   - Eight two-pole resonators at note × ratio(k).
   - The material table is morphed by a fixed shape, bent by STRETCH (`ratio^(2^stretch)`) and given a tiny per-mode detune.
   - Each mode has unity peak gain, a weight `ra[k]` and a ring-time multiplier `rt[k]`.
   - A per-mode level make-up compensates for modes moved off the FM's harmonic lattice.
   - Mode k is fed `clamp(MODES − k, 0, 1)`. A mode that's no longer fed keeps running until it's silent.
   - The mallet STRIKE is a raised-cosine pulse, about 0.26 ms long, at note-on.
   - Output: `0.35 × dry + 0.65 × modes`.
4. **After the resonator**
   - Envelope stage 2: `L / sqrt(Lx)`.
   - Keytracked high-pass, the 20.7 kHz low-pass, then the high shelf and the output gain.

**The envelope:** L is the ADSR as set. Lx is the same envelope with a 3 ms attack. It holds at 1 until L's attack finishes, and from then on equals L. So the resonator is always struck at full strength, ATTACK fades the whole result in, and the output follows L exactly.

The reference renders are what one voice outputs from note-on with no note-off. With the defaults (SUSTAIN 0, DECAY 2 s) the sound decays by itself.

## Platform gaps: what the port has to solve

These come from the SDK's `docs/api.md`; read it, plus the bundled `machines/fm2op` (a 2-op FM synth in Q31) and the
machine template, before writing anything.

### 1. No floating point and no math library

- **The problem:** the device is one Blackfin core with no FPU, and the freestanding toolchain has no `cmath`. The reference engine is float throughout and calls `sin`, `cos`, `exp`, `pow` and `exp2`.
- **Fixed-point audio:** the port must be fixed point (Q31, using `opxy/q31.hpp` `multiply` etc.), with transcendentals replaced by const tables (as `fm2op/tables.h`) or short polynomials.
- **No 64-bit helpers:** keep products within 32 bits. The reference's `cyc2u` uses an int64 cast; replace it.
- **Division:** none in the render path. It's tens of cycles on the device.
- **Blind spot in the CPU estimate:** `python sdk.py test` times the render on your computer, where float is fast, so leftover float hides from it. Make sure the render path contains no float at all. One way to check: build for Blackfin and look for soft-float helpers (`__addsf3`, `__mulsf3`, ...) in the link map.

### 2. No note-off reaches the voice

- **What the device provides:** a synth gets `initialize`, `start` (each note), `render` and `destroy` (when the note has finished or the voice is taken). The OP-XY applies its own amp envelope, filters and mixing after `render`.
- **What breaks:** the reference engine's RELEASE stage and its ring damper, which only act after `rings_note_off`, can't be driven.
- **What the renders show:** that's why the reference renders never release.
- **This needs a decision with the user before building.** Two ways:
  - **A. Keep the internal envelope (recommended, closest to the prototype).**
    - Implement ATTACK/DECAY/SUSTAIN internally, with the two-stage drive routing above.
    - Set the stock amp envelope's factory defaults (`machine.json` `presetDefaults.amp`) to an instant attack, full sustain and a release near 1.1 s, so it only provides the release.
    - The internal RELEASE control and the ring damper are dropped. The stock release fades the output instead.
    - Leaves 7 controls: 4 main plus 3 alternates.
  - **B. Use the stock amp envelope for all of ADSR.**
    - The engine drives the resonator at full strength and outputs unshaped.
    - Simpler and cheaper, but the ring no longer decays with the exciter the way it does now. Compare against the references to judge.
- **Free saving:** with no note-off the ring-time factor `rT` is constant (1). The mode coefficients then depend only on the note, STRETCH and MODES, so compute them at note start and when those knobs move, not every block.

### 3. Pitch comes as phase increments, at a fixed rate and block size

- **Fixed format:** the device runs at 44.1 kHz in 64-frame blocks. `render` gets a 64-word buffer of unsigned phase increments (2^32 per cycle), not a MIDI note: `f = inc × 44100 / 2^32`.
- **No pitch at `start()`:** do the note-dependent setup (keytracked filters, mode frequencies, presence peak, high shelf) on the first `render` after `start`. Redo it if the pitch moves noticeably (glide, bend, transpose), compared once per block.
- **DROP** multiplies the device's increments by the sweep factor `2^(sweep × left(t) / 12)`. Use a table for `left(t)` (the shape is fixed) and for `exp2`.
- **Block size:** the reference processes 16-sample sub-blocks and interpolates envelope gains across each. Either run four 16-sample sub-blocks per device block, or move to 64 and check against the references.
- **Tuning:** the reference engine runs 0.79 cents flat (`TUNE_RATIO`). The device's pitch is exact, so ignore it, or feed the same offset in tests.

### 4. The modes in fixed point (the hard part)

- **Why it's hard:** the modes ring for 3.6 s at 44.1 kHz, which puts the pole radius at about 0.99996. A direct-form two-pole in Q31 at low frequencies has poor coefficient resolution and quantization noise.
- **What to use instead:**
  - a coupled-form (Gold–Rader) resonator, or a Chamberlin/Simper state-variable filter, with Q31 coefficients computed at note start
  - possibly 32-bit state with a 64-bit-free double-word trick if needed
- **What to verify:** ring time, tuning (low notes, e.g. `extreme_b` at C1) and noise floor, against the references.
- **The other filters** (high-pass, low-pass, presence, shelf) are ordinary biquads: Q31 coefficients per note, direct form I with care at low cutoffs.

### 5. CPU

- **The budget:** aim for at most 15,000 Blackfin instructions per voice per 64-sample block (2op FM is about 14,000); above 24,000, audio is likely to drop.
- **Where the reference spends it, per block:**
  - The FM core at 2×: 128 iterations of two table sines, two one-poles and a clip, then the half-band.
  - Eight modes × 64 samples.
  - Six biquads × 64.
  - Per-block transcendentals for the envelope, BITE, DROP and the mode coefficients.
- **Easy savings:**
  - Coefficients and multipliers per note or per knob change, not per block. BITE is an exponential decay, so multiply by a constant each block.
  - Envelope decay multipliers computed only when DECAY changes.
  - Skip modes that are neither fed nor ringing. The reference already does this.
- **The fallback:** if the 2× FM core is too expensive, try 1× and compare (aliasing at high RATIO and high notes is the risk).

### 6. Output level and controls

- **Output:** Q31, peaking at most at half scale.
  - The loudest reference render peaks at 0.0876 (−21 dBFS, `ratio_100`) at gain 100, the prototype's level.
  - To match 2op FM's convention (output peak 1/8 full scale, `outputPeakQ31: 268435456`), scale the voice so that render peaks at about 0x10000000, which is gain ≈ 143.
- **Controls:** a synth page has 4 encoder positions plus up to 4 alternates, 8 parameters total.
  - Labels are at most 8 characters, shown in lower case. Values arrive as Q31 words (`word / 2^31` = the 0..1 knob).
  - A natural layout: `drop`, `ratio`, `stretch`, `modes` on the four positions, with `attack`, `decay`, `sustain` as alternates (and `release` if option B).
  - `definition.json` mappings are for display only; the C++ applies the real mapping. MODES' readout should show a fractional count.
- **Voices:** the SDK allocates them: a free voice, else the oldest note. The prototype preferred released voices over held ones (`rings_glue.c`), which mattered for clicks when a ringing voice was retriggered. Note whether that shows up.

### 7. Where the machine lives

- **Repository:** its own, made from the machine template (https://github.com/etcetc000/open-xy-machine-template), with the SDK checked out beside it (`python ../open-xy/sdk.py test --machine <folder>`). Don't add it to the SDK's `machines/` folder.
- **Folder contents:** C++ source, `definition.json`, `machine.json` and artwork only.
- **Identity:** an engine number in 34..62 and a page in 82..127.
- **Display:** a synth also needs a display (`display.cpp`, panel artwork). Follow 2op FM.

## Suggested order of work

1. Read the SDK docs (`README.md`, `docs/api.md`, `docs/first-machine.md`), `machines/fm2op` and the template. Make the envelope decision (A or B) with the user.
2. Start the machine from the template and get a placeholder sound previewing in the browser harness.
3. Port the engine to fixed point as plain C++ in the machine.
   - In `tests/sound.cpp`, render the same settings as `reference/manifest.json` (same note, knob words = round(knob × 2147483647), no note-off, 3 s) and write each as a 32-bit PCM WAV.
   - Compare with `python reference/compare.py reference/renders/<name>.wav <yours>.wav --gain-match`.
   - Port stage by stage, checking as you go: FM core, then the into-resonator filters, then the modes, then the output stage.
4. Check the CPU estimate with `sdk.py test`, confirm there's no float in the render path, and preview the machine.
5. Firmware build and device test only when the user asks (`docs/build.md`).

## Licensing

rings is clean-room: written from measurement and analysis, with no Elektron code or data, and the user's own work.
The SDK is MIT. Keep AGPL code (the SDK's Timestretch vendor code) out of the machine.
