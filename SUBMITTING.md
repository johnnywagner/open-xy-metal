# Submitting a machine to Open-XY

The Open-XY builder ships the machines listed in its catalog,
[`catalog/machines.json`](https://github.com/etcetc000/open-xy/blob/staging/catalog/machines.json)
in the Open-XY repository. Your machine stays in your repository; the catalog
pins one tagged commit of it. You never copy code into Open-XY.

## 1. Reserve the ID

Your machine's ID, the `definition.json` `id`, is saved in presets and is held
for good: a lowercase letter, then up to 14 lowercase letters, digits or
underscores, unused in the catalog. Open a pull request against the catalog
that reserves it:

```json
{"id": "<id>", "kind": "synth", "maintainer": "<your GitHub user>", "status": "reserved"}
```

Engine, effect and page numbers are not reserved: each firmware's layout gives
them out and keeps them for its projects. The `identity` in `machine.json` is
only a preference, which `sdk.py new` fills in; a build uses it when free,
otherwise the next free numbers, so you can leave it as generated.
`python sdk.py catalog check` in the Open-XY repository validates the catalog
before you open the pull request. Your own builds need no reservation.

## 2. Meet the requirements

| Requirement | How it is checked |
| --- | --- |
| The machine targets a released SDK (`"sdk"` in `machine.json`) | `sdk.py test` |
| C++ uses only the public headers (`<opxy/...>`): no assembly, platform conditionals or OS headers | `sdk.py test`, then review |
| Audio callbacks never allocate, block, log or do I/O | Review |
| Your own checks in `tests/*.cpp` pass: sound bounds, tuning, decay, whatever the machine promises | `sdk.py test` |
| CI is green, including the firmware compile | Your repository's CI |
| It fits beside the bundled machines in the shared 2044 KiB module | The firmware compile prints each machine's share |
| A licence that allows redistribution in firmware: `LICENSE` at the root, and `vendor/LICENSE` for vendored code | Review |
| A short `description` in `machine.json`; a builder `icon` is optional | `sdk.py test` |
| Its CPU estimate stays under the aim, or you have measured it on a device | `sdk.py test` prints it |

## 3. Release and submit

1. Tag the commit to publish, for example `v1.0.0`, and push the tag.
2. Open a pull request against the catalog that changes your entry to:

   ```json
   {"id": "<id>", "kind": "synth", "maintainer": "<your GitHub user>", "status": "published",
    "repository": "https://github.com/<you>/<repo>", "tag": "v1.0.0",
    "commit": "<the tag's full commit SHA>", "path": "machines/<id>"}
   ```

   `path` is the machine's folder in your repository (`"."` at the root).
3. The catalog's CI fetches exactly that commit and runs the same checks as your
   CI (`python sdk.py catalog test` runs them locally). A maintainer then
   reviews the code once, in full, and listens to it. See the
   [catalog](https://github.com/etcetc000/open-xy/blob/staging/docs/catalog.md)
   for every field and what review covers.

Merged machines reach the beta site (beta.open-xy.com) with the next catalog
release, then the main site with the next Open-XY release.

## 4. Updates

Tag a new version and open a pull request that moves your entry's `tag` and
`commit`. It is checked the same way and reviewed as a diff. Projects save each
control's value by its position, so keep every published control at its
position with its id and range: removing, reordering or remapping one fails the
catalog check unless a maintainer approves it. Add new controls at the end. A
change of ID or kind is refused: release a new machine instead.

## Removal

A maintainer can remove a machine that is abandoned or harmful, or you can ask
for removal. Its entry stays as `"status": "retired"`, so its ID is never
reused.
