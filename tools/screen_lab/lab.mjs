// Metal's screen lab (tools/screen_lab.py): a panel of screen-only settings over the SDK's workbench. Browser only.
// - OP-XY look: the OP-XY draws each piece's stroke at its palette entry's width whatever the piece's scale, with flat
//   ends; the workbench shrinks the stroke with the piece. So a short straight piece, which display.cpp shrinks and
//   gives a wide stroke to make up for it, shows on the device as a box: its drawn length long and the palette's
//   width thick. Measured against photos and video of the device (settings 11 77 99 58, 0 0 99 0, a strike at
//   0 65 93 73); not documented by the SDK. It ignores line ends (a build with round ones drew the same), so the look
//   draws every end flat.
// - Colour: strokes every piece in a colour other than the artwork's (COLOUR in tools/gen_panel.py), to try shades
//   before setting one there. '' keeps the artwork's.
// - Markers: the node markers' shape (display.cpp, Marker), through the lab's bits 122-123 (bits 29-30 of parameter
//   slot 7), counted on from MARKER there; BUILT is that MARKER. The OP-XY leaves the slot at 0: MARKER.
// - Corners: the rounded squares' corner radius, redrawn in the browser (CORNER in tools/gen_panel.py, mirrored by
//   rounded() here; CORNER_BUILT is it). null keeps the artwork's.
import { PreviewDisplay, SvgDisplay } from '/sdk/browser/display.mjs';

const KEY = 'metal-screen-lab';
const MARKERS = ['Original', 'Rounded', 'Square', 'Diamond'], BUILT = 0;
const CORNER_BUILT = 4, ROUNDS = 120;
const SWATCHES = [['#FFFFFF', 'White'], ['#F4F1EA', 'Ivory'], ['#EDE8DC', 'Bone'], ['#E8E6DF', 'Paper'],
                  ['#DCDAD3', 'Stone'], ['#CDCDCD', 'Stock grey'], ['#C4C4C0', 'Silver'], ['#A8A8A4', 'Grey']];
const settings = { device: true, corner: null, colour: '', marker: BUILT };
try { Object.assign(settings, JSON.parse(localStorage.getItem(KEY)) || {}); } catch {}
const save = () => { try { localStorage.setItem(KEY, JSON.stringify(settings)); } catch {} };

const tick = PreviewDisplay.prototype.tick;
PreviewDisplay.prototype.tick = function (input, ...rest) {
  const slots = [...input.slots];
  slots[7] = (slots[7] & ~(3 << 29)) | (((settings.marker - BUILT) & 3) << 29);
  return tick.call(this, { ...input, slots }, ...rest);
};

// gen_panel.py's rounded(): a square 16 px wide with corners of `radius`, each four straight steps.
function rounded(radius) {
  const c = 8 - radius, pts = [];
  for (const [cx, cy, start] of [[c, -c, -90], [c, c, 0], [-c, c, 90], [-c, -c, 180]]) {
    for (let k = 0; k < 5; k++) {
      const t = (start + k * 22.5) * Math.PI / 180;
      const pt = [+(cx + radius * Math.cos(t)).toFixed(2), +(cy + radius * Math.sin(t)).toFixed(2)];
      const last = pts[pts.length - 1];
      if (!last || last[0] !== pt[0] || last[1] !== pt[1]) pts.push(pt);
    }
  }
  if (pts.length > 1 && pts[0][0] === pts[pts.length - 1][0] && pts[0][1] === pts[pts.length - 1][1]) pts.pop();
  return 'M' + pts.map(([x, y]) => `${x},${y}`).join(' L') + ' Z';
}

// Each node's authored ends and outline, and the stroke width and colours its palette entry last lent it.
const outlines = new WeakMap(), ends = new WeakMap(), widths = new WeakMap(), strokes = new WeakMap(), fills = new WeakMap();
let authored = '';                                   // the artwork's colour, from its first palette entry
const apply = SvgDisplay.prototype.apply;
SvgDisplay.prototype.apply = function (frame) {
  apply.call(this, frame);
  if (!authored) { authored = this.palette[0].stroke.toUpperCase(); showColour(); }
  const colour = settings.colour;
  const corner = settings.corner ?? CORNER_BUILT;
  if (this.corner !== corner) {
    for (const node of this.nodes.slice(ROUNDS)) {
      if (!outlines.has(node)) outlines.set(node, node.getAttribute('d'));
      node.setAttribute('d', settings.corner === null ? outlines.get(node) : rounded(corner));
    }
    this.corner = corner;
  }
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
      node.setAttribute('stroke-linecap', 'butt');
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
  <p class="lab-label">Node markers</p>
  <div class="lab-markers segmented"></div>
  <p class="hint" id="lab-marker-note"></p>
  <label class="lab-row lab-corner">Corners <input type="range" id="lab-corner" min="0" max="8" step="0.5"
  aria-label="Corner radius"><code id="lab-corner-out"></code></label>
  <p class="hint" id="lab-corner-note"></p>
  <p class="lab-label">Colour</p>
  <div class="lab-swatches"></div>
  <div class="lab-colour"><input type="color" id="lab-colour" aria-label="Ring colour"><code id="lab-hex"></code>
  <button type="button" id="lab-reset" class="text-button">Use the artwork's</button></div>
  <p class="hint" id="lab-note"></p>`;
document.querySelector('aside').prepend(panel);
const device = panel.querySelector('#lab-device');
device.checked = settings.device;
device.addEventListener('change', () => { settings.device = device.checked; save(); });
const corner = panel.querySelector('#lab-corner');
function showCorner() {
  const value = settings.corner ?? CORNER_BUILT;
  corner.value = value;
  panel.querySelector('#lab-corner-out').textContent = `${value} px`;
  panel.querySelector('#lab-corner-note').textContent = value === CORNER_BUILT
    ? `Of the rounded squares' 16 px (0 square, 8 round): the artwork's, CORNER in tools/gen_panel.py.`
    : `Trying ${value} of 16 px. To keep it, set CORNER = ${value} in tools/gen_panel.py.`;
}
corner.addEventListener('input', () => { settings.corner = +corner.value; save(); showCorner(); });
showCorner();

const markers = panel.querySelector('.lab-markers');
function showMarker() {
  for (const button of markers.children) button.setAttribute('aria-pressed', String(+button.dataset.marker === settings.marker));
  panel.querySelector('#lab-marker-note').textContent = settings.marker === BUILT
    ? `${MARKERS[BUILT]}: as the OP-XY draws it now.`
    : (settings.marker === 1 ? 'The boxes keep their size and motion, with rounded corners; thin lines and stubs stay.'
      : `Near-square boxes become ${MARKERS[settings.marker].toLowerCase()}s; long bars and stubs stay.`) +
      ` To keep it, set MARKER = ${MARKERS[settings.marker].toUpperCase()} in display.cpp.`;
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

const picker = panel.querySelector('#lab-colour'), swatches = panel.querySelector('.lab-swatches');
function showColour() {
  const colour = settings.colour || authored || '#FFFFFF';
  picker.value = colour.toLowerCase();
  panel.querySelector('#lab-hex').textContent = colour;
  panel.querySelector('#lab-note').textContent = settings.colour && settings.colour !== authored
    ? `Trying ${settings.colour}. To keep it, set COLOUR = '${settings.colour}' in tools/gen_panel.py.`
    : `The artwork's colour${authored ? ` (${authored}, COLOUR in tools/gen_panel.py)` : ''}.`;
  for (const button of swatches.children) button.setAttribute('aria-pressed', String(button.dataset.colour === colour));
}
const choose = colour => { settings.colour = colour ? colour.toUpperCase() : ''; save(); showColour(); };
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
showColour();
