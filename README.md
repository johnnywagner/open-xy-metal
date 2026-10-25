# Open-XY machine template

Start an [Open-XY](https://github.com/etcetc000/open-xy) machine in a repository of
your own. A machine is portable C++ against the Open-XY SDK: a synth, with its
own sound and screen, or a send effect. Your machines live here, you release
them by tagging, and the Open-XY catalog pins the version it ships.

| Folder | Kind |
| --- | --- |
| [machines/mysynth](machines/mysynth) | Synth: voices, a 64-sample render and a drawing display |
| [machines/myeffect](machines/myeffect) | Send effect: in-place stereo processing on the FX page |

Each folder is one machine: `definition.json` (controls, presets, resources),
`machine.json` (kind, SDK version, the numbers it asks for), its C++ and, for a
synth, its artwork. `tests/` holds the machine's own checks.

These are minimal starting points. For worked examples, published through the
catalog, see [Karplus](https://github.com/etcetc000/open-xy-karplus), a
plucked-string synth with an animated screen, and
[Ping Pong](https://github.com/etcetc000/open-xy-pingpong), a send effect.

## Start

1. Use this template on GitHub, and clone the Open-XY SDK next to it:
   `git clone https://github.com/etcetc000/open-xy` (its default branch,
   `staging`, is the current SDK). Follow its
   [first machine](https://github.com/etcetc000/open-xy/blob/staging/docs/first-machine.md)
   guide to install Python, Node and Clang.
2. Replace the examples with your own machines, each with an ID of its own:

   ```sh
   python ../open-xy/sdk.py new --kind synth --id <id> --name "<Name>" --out machines/<id>
   python ../open-xy/sdk.py new --kind effect --id <id> --name "<Name>" --out machines/<id>
   ```

   Keep one machine, or several. A single machine may also sit at the
   repository root.
3. Hear and see a synth while you work on it:

   ```sh
   python ../open-xy/sdk.py preview --machine machines/<id> --out ../previews/<id>-001
   python ../open-xy/tools/serve_harness.py --preview ../previews/<id>-001
   ```

4. Check everything as CI does: records, sources, previews and your tests.

   ```sh
   python ../open-xy/sdk.py test --machine machines/<id> --cxx clang++ --host-cxx g++
   ```

   It also prints an estimate of the machine's CPU cost in Blackfin
   instructions per block, with the limits to stay under
   ([CPU](https://github.com/etcetc000/open-xy/blob/staging/docs/api.md#cpu)).

   Add `--firmware` once the pinned Blackfin tools are installed
   (`python ../open-xy/sdk.py toolchain fetch`) to compile the firmware module
   with your machines.
5. Build firmware for your own OP-XY with
   `python ../open-xy/sdk.py build --machine machines/<id> --stock-os <os.ldr from your OP-XY> --out ...`
   ([building firmware](https://github.com/etcetc000/open-xy/blob/staging/docs/build.md)).
   Pass the previous build's `layout.json` with `--layout` so your machines
   keep their numbers, and your projects their instruments.

Keep build and preview output outside this repository: the SDK refuses output
inside a machine folder.

## CI

[.github/workflows/machines.yml](.github/workflows/machines.yml) checks every
machine against the SDK named by `OPEN_XY_REF`: the machine checks on Linux,
and a firmware compile with the bundled machines on Windows. Point
`OPEN_XY_REF` at the SDK release your machines' `"sdk"` targets.

## Sharing your machine

To have a machine in the Open-XY builder, follow [SUBMITTING.md](SUBMITTING.md).
