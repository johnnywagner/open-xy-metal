#!/usr/bin/env python3
"""Generates machines/metal/presets/: the presets the builder installs on the OP-XY, one <name>.json for each preset
in definition.json (the browser preview's), named in lower case with - for a space. Run from the repository root:

    python3 tools/gen_presets.py

Each is a version 4 OP-XY preset in the structure the SDK's builder gives the init preset it makes (builder/core.mjs,
defaultPreset): the controls as Q15 words, as the catalog stores the defaults (tooling/adapters.py, catalog_defaults),
and the amp envelope with machine.json's presetDefaults.amp over the builder's own. Change a preset in definition.json
or the envelope in machine.json, then rerun this.
"""
import json
import math
from pathlib import Path

MACHINE = Path(__file__).resolve().parent.parent / 'machines' / 'metal'
# The builder's init envelope (defaultPreset), which presetDefaults.amp overrides.
AMP = {'attack': 128, 'decay': 8192, 'release': 32767, 'sustain': 32767}

definition = json.loads((MACHINE / 'definition.json').read_text())
machine = json.loads((MACHINE / 'machine.json').read_text())
amp = {**AMP, **machine.get('presetDefaults', {}).get('amp', {})}
ids = [p['id'] for p in definition['parameters']]


def preset(values):
    params = [math.floor(values[i] * 32767 + 0.5) for i in ids]
    return {
        'engine': {
            'bendrange': 8191, 'highpass': 0,
            'modulation': {s: {'amount': 16384, 'target': 0} for s in ('aftertouch', 'modwheel', 'pitchbend', 'velocity')},
            'params': params + [0] * (8 - len(params)), 'playmode': 'poly',
            'portamento.amount': 0, 'portamento.type': 0, 'transpose': 0,
            'tuning.root': 0, 'tuning.scale': 0, 'velocity.sensitivity': 0, 'volume': 16384, 'width': 0,
        },
        'envelope': {'amp': amp, 'filter': {'attack': 0, 'decay': 0, 'release': 0, 'sustain': 32767}},
        'fx': {'active': False, 'params': [0, 0, 0, 0, 0, 32767, 0, 0], 'type': 'svf'},
        'lfo': {'active': False, 'params': [0, 0, 0, 0, 0, 0, 0, 0], 'type': 'value'},
        'octave': 0, 'platform': 'OP-XY', 'regions': [], 'type': definition['id'], 'version': 4,
    }


folder = MACHINE / machine['presets']
folder.mkdir(exist_ok=True)
for old in folder.glob('*.json'):
    old.unlink()
for name, values in definition['presets'].items():
    (folder / (name.lower().replace(' ', '-') + '.json')).write_text(json.dumps(preset(values), indent=2) + '\n')
