// Metal's screen lab (tools/screen_lab.py): a panel of screen-only settings over the SDK's workbench. Browser only.
// - OP-XY look: the OP-XY draws each piece's stroke at its palette entry's width whatever the piece's scale, with flat
//   ends; the workbench shrinks the stroke with the piece. So a short straight piece, which display.cpp shrinks and
//   gives a wide stroke to make up for it, shows on the device as a box: its drawn length long and the palette's
//   width thick. Measured against photos and video of the device (settings 11 77 99 58, 0 0 99 0, a strike at
//   0 65 93 73); not documented by the SDK. Its ends looked flat in those, so the look draws every end flat, unless
//   "OP-XY draws line ends" is on (a guess to try against the device: the artwork's ends, ENDS in tools/gen_panel.py).
// - Colour: strokes every piece in a colour other than the artwork's (COLOUR in tools/gen_panel.py), to try shades
//   before setting one there. '' keeps the artwork's. With the skew on, DROP moves it from colour A (DROP 0) to colour
//   B (DROP 99), mixed channel by channel: browser only, since the OP-XY's 16 palette entries hold one colour for
//   each of the twelve stroke widths.
// - Markers: the node markers' shape (display.cpp, Marker), through the lab's bits 122-123 (bits 29-30 of parameter
//   slot 7), counted on from MARKER there; BUILT is that MARKER. The OP-XY leaves the slot at 0: MARKER.
import { PreviewDisplay, SvgDisplay } from '/sdk/browser/display.mjs';

const KEY = 'metal-screen-lab';
const MARKERS = ['Original', 'Square', 'Diamond', 'Circle'], BUILT = 0;
const SWATCHES = [['#FFFFFF', 'White'], ['#F4F1EA', 'Ivory'], ['#EDE8DC', 'Bone'], ['#E8E6DF', 'Paper'],
                  ['#DCDAD3', 'Stone'], ['#D9D9D6', 'Pale grey'], ['#C4C4C0', 'Silver'], ['#A8A8A4', 'Grey']];
const settings = { device: true, ends: false, colour: '', colourB: '#A8A8A4', skew: false, editing: 'A', marker: BUILT };
try { Object.assign(settings, JSON.parse(localStorage.getItem(KEY)) || {}); } catch {}
const save = () => { try { localStorage.setItem(KEY, JSON.stringify(settings)); } catch {} };

let drop = 0;                                        // DROP, 0 .. 1, as last drawn
const tick = PreviewDisplay.prototype.tick;
PreviewDisplay.prototype.tick = function (input, ...rest) {
  drop = input.slots[0] / 0x7fffffff;
  const slots = [...input.slots];
  slots[7] = (slots[7] & ~(3 << 29)) | (((settings.marker - BUILT) & 3) << 29);
  return tick.call(this, { ...input, slots }, ...rest);
};

// Each node's authored ends, and the stroke width and colours its palette entry last lent it.
const ends = new WeakMap(), widths = new WeakMap(), strokes = new WeakMap(), fills = new WeakMap();
let authored = '';                                   // the artwork's colour, from its first palette entry
const apply = SvgDisplay.prototype.apply;
SvgDisplay.prototype.apply = function (frame) {
  apply.call(this, frame);
  if (!authored) { authored = this.palette[0].stroke.toUpperCase(); showColour(); }
  const colour = drawn();
  for (let i = 0; i < this.nodes.length; i++) {
    const node = this.nodes[i], offset = this.nodesAt + i * 7;
    const scale = frame[offset + 2], shade = frame[offset + 5], paint = frame[offset + 6];
    if (!ends.has(node)) ends.set(node, node.getAttribute('stroke-linecap'));
    if (paint) { widths.set(node, this.palette[shade].width); strokes.set(node, this.palette[shade].stroke); }
    if (paint === 2 && this.fillable[i]) fills.set(node, this.palette[shade].fill);
    node.setAttribute('stroke', colour || strokes.get(node) || node.getAttribute('stroke'));
    if (fills.has(node)) node.setAttribute('fill', colour || fills.get(node));
    const width = widths.get(node) ?? Number(node.getAttribute('stroke-width'));
    if (settings.device) {
      node.setAttribute('stroke-width', String(width * 1024 / scale));   // undoes the node's scale
      node.setAttribute('stroke-linecap', settings.ends && ends.get(node) ? ends.get(node) : 'butt');
    } else {
      node.setAttribute('stroke-width', String(width));
      if (ends.get(node)) node.setAttribute('stroke-linecap', ends.get(node));
    }
  }
};

const panel = document.createElement('section');
panel.className = 'side-section lab';
panel.innerHTML = `<p class="eyebrow">METAL SCREEN LAB</p><h3>Screen</h3>
  <label class="lab-row"><input type="checkbox" id="lab-device"> OP-XY look</label>
  <p class="hint">Draws strokes as the OP-XY does: a shrunk piece keeps its full stroke width, so short pieces show as
  boxes. Off: as the workbench draws them.</p>
  <label class="lab-row"><input type="checkbox" id="lab-ends"> OP-XY draws line ends</label>
  <p class="hint">A guess to check on the device: with the look on, draw the artwork's line ends (round or square)
  instead of flat ones.</p>
  <p class="lab-label">Node markers</p>
  <div class="lab-markers segmented"></div>
  <p class="hint" id="lab-marker-note"></p>
  <p class="lab-label">Colour</p>
  <label class="lab-row"><input type="checkbox" id="lab-skew"> DROP skews colour A to B</label>
  <div class="lab-targets segmented"><button type="button" data-target="A">A</button><button type="button" data-target="B">B</button></div>
  <div class="lab-swatches"></div>
  <div class="lab-colour"><input type="color" id="lab-colour" aria-label="Ring colour"><code id="lab-hex"></code>
  <button type="button" id="lab-reset" class="text-button">Use the artwork's</button></div>
  <p class="hint" id="lab-note"></p>`;
document.querySelector('aside').prepend(panel);
const device = panel.querySelector('#lab-device');
device.checked = settings.device;
device.addEventListener('change', () => { settings.device = device.checked; save(); });
const ends_ = panel.querySelector('#lab-ends');
ends_.checked = settings.ends;
ends_.addEventListener('change', () => { settings.ends = ends_.checked; save(); });

const markers = panel.querySelector('.lab-markers');
function showMarker() {
  for (const button of markers.children) button.setAttribute('aria-pressed', String(+button.dataset.marker === settings.marker));
  panel.querySelector('#lab-marker-note').textContent = settings.marker === BUILT
    ? `${MARKERS[BUILT]}: as the OP-XY draws it now.`
    : `Near-square boxes become ${MARKERS[settings.marker].toLowerCase()}s; long bars and stubs stay. To keep it, set ` +
      `MARKER = ${MARKERS[settings.marker].toUpperCase()} in display.cpp.`;
}
MARKERS.forEach((name, i) => {
  const button = document.createElement('button');
  button.type = 'button';
  button.textContent = name;
  button.dataset.marker = i;
  button.addEventListener('click', () => { settings.marker = i; save(); showMarker(); });
  markers.append(button);
});
showMarker();

// The colour drawn now: '' for the artwork's own, A, or with the skew on A mixed into B by DROP.
const hex = colour => [1, 3, 5].map(i => parseInt(colour.slice(i, i + 2), 16));
function drawn() {
  if (!settings.skew) return settings.colour;
  const a = hex(settings.colour || authored || '#FFFFFF'), b = hex(settings.colourB || authored || '#FFFFFF');
  return '#' + a.map((v, i) => Math.round(v + (b[i] - v) * drop).toString(16).padStart(2, '0')).join('').toUpperCase();
}

const picker = panel.querySelector('#lab-colour'), swatches = panel.querySelector('.lab-swatches');
const skew = panel.querySelector('#lab-skew'), targets = panel.querySelector('.lab-targets');
const editing = () => settings.skew ? settings.editing : 'A';
function showColour() {
  const b = editing() === 'B', colour = (b ? settings.colourB : settings.colour) || authored || '#FFFFFF';
  picker.value = colour.toLowerCase();
  panel.querySelector('#lab-hex').textContent = `${settings.skew ? editing() + ' ' : ''}${colour}`;
  panel.querySelector('#lab-reset').hidden = b;
  targets.hidden = !settings.skew;
  for (const button of targets.children) button.setAttribute('aria-pressed', String(button.dataset.target === editing()));
  const a = settings.colour || authored || '#FFFFFF';
  panel.querySelector('#lab-note').textContent = settings.skew
    ? `DROP 0 draws ${a}, DROP 99 ${settings.colourB || a}. Browser only: the OP-XY's palette has no room for a ` +
      `colour per DROP step at every stroke width.`
    : settings.colour && settings.colour !== authored
    ? `Trying ${settings.colour}. To keep it, set COLOUR = '${settings.colour}' in tools/gen_panel.py.`
    : `The artwork's colour${authored ? ` (${authored}, COLOUR in tools/gen_panel.py)` : ''}.`;
  for (const button of swatches.children) button.setAttribute('aria-pressed', String(button.dataset.colour === colour));
}
const choose = colour => {
  const value = colour ? colour.toUpperCase() : '';
  if (editing() === 'B') settings.colourB = value || settings.colourB; else settings.colour = value;
  save(); showColour();
};
for (const [colour, name] of SWATCHES) {
  const button = document.createElement('button');
  button.type = 'button';
  button.dataset.colour = colour;
  button.title = `${name} ${colour}`;
  button.setAttribute('aria-label', `${name} ${colour}`);
  button.style.background = colour;
  button.addEventListener('click', () => choose(colour));
  swatches.append(button);
}
picker.addEventListener('input', () => choose(picker.value));
panel.querySelector('#lab-reset').addEventListener('click', () => choose(''));
skew.checked = settings.skew;
skew.addEventListener('change', () => { settings.skew = skew.checked; save(); showColour(); });
for (const button of targets.children) {
  button.addEventListener('click', () => { settings.editing = button.dataset.target; save(); showColour(); });
}
showColour();
