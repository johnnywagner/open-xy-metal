# Instructions for agents

This repository holds the user's own Open-XY machines (instruments and send
effects). It is not the SDK.

- **Set up the SDK beside this repository:** `git clone
  https://github.com/etcetc000/open-xy ../open-xy`. That gives its default branch,
  `staging`; do not switch it to `main`. Run SDK commands from here through it,
  for example `python ../open-xy/sdk.py test --machine machines/<id>`.
- **Do not edit the SDK to make a machine work.** Machine code uses only the
  public `<opxy/...>` headers: no assembly, platform conditionals, OS headers or
  raw addresses. Audio callbacks never allocate, block, log or do I/O. If the
  SDK lacks something a machine needs, tell the user. It becomes a separate
  pull request to the SDK's `staging` branch, not part of the machine.
- **This repository holds rings2** (`machines/rings2`), a fixed-point port of the float reference in `port-kit/`
  with the reference's mode tuning, and two tuned versions of it, rings5 and rings6 (`machines/rings5`,
  `machines/rings6`). rings, rings3 and rings4 were removed; they are in the git history.
  Read README.md and port-kit/BRIEF.md first. `tools/gen_tables.py` generates `tables.h` and `display_tables.h`;
  edit the generator, not the headers. Check sound changes with `tests/sound.cpp` (run by `sdk.py test`) and
  `tools/check_renders.py`. The render path stays free of float, division and 64-bit arithmetic.
- **Each machine keeps its own folder,** ID and `tests/`. Keep build and preview
  output outside this repository.
- **Submitting to the builder** follows [SUBMITTING.md](SUBMITTING.md): tag a
  release here, then open a pull request against the SDK's `staging` branch
  that changes **only** `catalog/machines.json`, with this machine's one entry.
  The SDK's CI refuses a submission that changes anything else.
- **Device operations,** such as copying firmware to an OP-XY, need the user's
  explicit instruction each time. Host checks do not measure device CPU.
