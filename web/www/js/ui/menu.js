// The main menu, drawn from the C Menu state (src/sim/menu.c) and driven by the same buttons; the layout
// follows src/pd/menu_draw.c: a title block on the left, the list on the right, a hint bar along the bottom.
// The mouse works it too: pointing at a row selects it, clicking chooses it, and the hints are buttons. Those
// go back to the game as button presses (see menupointer.js), so the C menu stays the one source of truth.
import { BTN } from '../input.js';

export const SCREEN = { MAIN: 0, TRACKS: 1, PREVIEW: 2, BOATS: 3, WEATHER: 4, HELP: 5, TRAINING: 6 };
const LIST_VIS = 5, HELP_VIS = 14;
const PV_LOOP = 240, PV_HOLD = 45;

const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const cap = (s) => (s ? s[0].toUpperCase() + s.slice(1) : s);

export class MenuView {
  // on.point(screen, row, press): select a row, then press (0 for none); on.about(): open the About page.
  constructor(root, on = {}) {
    this.root = root;
    this.on = { point: () => {}, about: () => {}, ...on };
    this.screen = -1;
    root.innerHTML = `
      <div class="menu-card">
        <div class="menu-title"><div class="eyebrow"></div><h1></h1><div class="rule"></div><div class="sub"></div><div class="extra"></div></div>
        <div class="menu-list"></div>
        <div class="menu-preview" hidden><div class="pv-head"><h2></h2><span class="pv-best"></span></div><canvas></canvas><div class="pv-info"></div></div>
        <div class="menu-help" hidden><h2>Controls</h2><div class="help-keys"></div><div class="help-body"></div></div>
      </div>
      <div class="menu-hints"></div>`;
    this.title = root.querySelector('.menu-title');
    this.list = root.querySelector('.menu-list');
    this.preview = root.querySelector('.menu-preview');
    this.canvas = this.preview.querySelector('canvas');
    this.help = root.querySelector('.menu-help');
    this.hints = root.querySelector('.menu-hints');
    this.last = '';

    const press = (bit) => this.on.point(this.screen, null, bit);
    // pointermove rather than pointerover: a list redrawn under a still mouse must not grab the selection back
    // from the keyboard
    this.list.addEventListener('pointermove', (e) => {
      const it = e.target.closest('.item');
      if (it && !it.classList.contains('off') && !it.classList.contains('sel')) this.on.point(this.screen, Number(it.dataset.row), 0);
    });
    this.list.addEventListener('click', (e) => {
      const it = e.target.closest('.item');
      if (it && !it.classList.contains('off')) this.on.point(this.screen, Number(it.dataset.row), BTN.A);
      else if (e.target.closest('.more.up')) press(BTN.UP);
      else if (e.target.closest('.more.down')) press(BTN.DOWN);
    });
    this.help.addEventListener('click', (e) => {
      if (e.target.closest('.more.up')) press(BTN.UP);
      else if (e.target.closest('.more.down')) press(BTN.DOWN);
    });
    this.canvas.addEventListener('click', () => press(BTN.A));
    this.hints.addEventListener('click', (e) => {
      const b = e.target.closest('button');
      if (!b) return;
      if (b.dataset.about !== undefined) this.on.about();
      else if (b.dataset.press) press(Number(b.dataset.press));
    });
  }

  hide() { this.root.hidden = true; }

  update(m, stat, keys) {
    this.root.hidden = false;
    const key = JSON.stringify([m.screen, m.sel, m.top, m.rows, m.helpTop, m.best, m.boatBest, m.boatGhost, m.journey, stat.gen]);
    if (m.screen === SCREEN.PREVIEW) this.drawPreview(m, stat);
    if (key === this.last) return;
    this.last = key;
    this.screen = m.screen;
    const mode = m.journey ? 'Journey' : 'Racing';
    const go = m.journey ? 'SAIL' : 'RACE';
    const isList = m.screen !== SCREEN.PREVIEW && m.screen !== SCREEN.HELP;
    this.title.hidden = !isList;
    this.list.hidden = !isList;
    this.preview.hidden = m.screen !== SCREEN.PREVIEW;
    this.help.hidden = m.screen !== SCREEN.HELP;
    let hint;
    const track = stat.tracks[m.track] || { name: '' };
    if (m.screen === SCREEN.MAIN) {
      this.titleBlock(m.demo ? 'DEMO' : null, "Crank'n Sail", 'A sailing simulation');
      hint = ['SELECT', null, 'CHOOSE'];
    } else if (m.screen === SCREEN.TRACKS) {
      this.titleBlock('Menu › Racing', 'Racing', 'Choose a course');
      hint = ['SELECT', 'BACK', 'CHOOSE'];
    } else if (m.screen === SCREEN.BOATS) {
      if (m.journey) this.titleBlock('Menu › Journey', 'Journey', 'Choose a boat');
      else this.titleBlock('Racing › Course', track.name, 'Choose a boat',
        m.boatBest ? `Best ${m.boatBest}<br>Ghost: ${m.boatGhost ? 'yes' : 'none'}` : '');
      hint = [m.nweather > 0 ? 'SELECT' : go, 'BACK', 'CHOOSE'];
    } else if (m.screen === SCREEN.WEATHER) {
      this.titleBlock(`${mode} › Boat`, cap(stat.boats[m.boat] || ''), 'Choose weather');
      hint = [go, 'BACK', 'CHOOSE'];
    } else if (m.screen === SCREEN.TRAINING) {
      this.titleBlock('Menu', 'Training', '');
      hint = ['SELECT', 'BACK', 'CHOOSE'];
    } else if (m.screen === SCREEN.PREVIEW) {
      this.preview.querySelector('h2').textContent = track.name;
      this.preview.querySelector('.pv-best').textContent = `Best ${m.best || '--:--.-'}`;
      this.preview.querySelector('.pv-info').textContent = track.info || '';
      hint = ['CONFIRM', 'BACK', null];
    } else {
      this.drawHelp(m, stat, keys);
      hint = [null, 'BACK', 'SCROLL'];
    }
    if (isList) this.drawList(m, m.screen === SCREEN.WEATHER || m.screen === SCREEN.BOATS ? cap : (s) => s);
    this.drawHints(hint, keys, m.screen === SCREEN.HELP);
  }

  titleBlock(eyebrow, title, sub, extra = '') {
    this.title.querySelector('.eyebrow').textContent = eyebrow || '';
    this.title.querySelector('h1').textContent = title;
    this.title.querySelector('.sub').textContent = sub;
    this.title.querySelector('.extra').innerHTML = extra;
  }

  drawList(m, fmt) {
    const n = m.rows.length, vis = Math.min(n, LIST_VIS);
    let html = `<div class="more up"${m.top > 0 ? '' : ' hidden'}>▲</div>`;
    for (let i = 0; i < vis; i++) {
      const k = m.top + i;
      const [label, on] = m.rows[k];
      html += `<div class="item${k === m.sel ? ' sel' : ''}${on ? '' : ' off'}" data-row="${k}">${esc(fmt(label))}</div>`;
    }
    html += `<div class="more down"${m.top + vis < n ? '' : ' hidden'}>▼</div>`;
    this.list.innerHTML = html;
  }

  drawHints([a, b, dpad], keys, crank) {
    const k = (cap, cls) => `<span class="key ${cls}">${esc(cap)}</span>`;
    let html = '';
    if (a) html += `<button class="hint" data-press="${BTN.A}">${k(keys.A, 'a')} ${a}</button>`;
    if (b) html += `<button class="hint" data-press="${BTN.B}">${k(keys.B, 'b')} ${b}</button>`;
    html += '<span class="grow"></span>';
    if (dpad) html += `<span class="hint">${k(crank ? (keys.wheel || 'Wheel') : keys.updown, 'd')} ${dpad}</span>`;
    html += '<button class="hint about" data-about>About</button>';
    this.hints.innerHTML = html;
  }

  drawHelp(m, stat, keys) {
    this.help.querySelector('.help-keys').innerHTML = `
      <div><b>On the web</b>: ${keys.legend}</div>`;
    const rows = stat.help.slice(m.helpTop, m.helpTop + HELP_VIS);
    let html = '';
    for (const [text, style, first] of rows) {
      if (style === 1) html += `<div class="h-head">${esc(text)}</div>`;
      else if (style === 2) html += `<div class="h-bullet${first ? ' first' : ''}">${esc(text)}</div>`;
      else html += `<div class="h-body">${esc(text) || '&nbsp;'}</div>`;
    }
    if (m.helpTop > 0) html = '<div class="more up">▲</div>' + html;
    if (m.helpTop + HELP_VIS < m.helpRows) html += '<div class="more down">▼</div>';
    this.help.querySelector('.help-body').innerHTML = html;
  }

  // A north-up plan of the course with the route dotted and a line that sails it (menu_draw.c draw_preview).
  drawPreview(m, stat) {
    const t = stat.tracks[m.track];
    if (!t) return;
    const cv = this.canvas;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const W = cv.clientWidth || 600, H = cv.clientHeight || 300;
    if (cv.width !== Math.round(W * dpr) || cv.height !== Math.round(H * dpr)) {
      cv.width = Math.round(W * dpr);
      cv.height = Math.round(H * dpr);
    }
    const g = cv.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, W, H);
    const pts = [[t.sx, t.sy], ...t.marks.map((k) => [k.x, k.y])];
    for (let i = 0; i + 1 < t.route.length; i += 2) pts.push([t.route[i], t.route[i + 1]]);
    let lx = Infinity, hx = -Infinity, ly = Infinity, hy = -Infinity;
    for (const [x, y] of pts) { lx = Math.min(lx, x); hx = Math.max(hx, x); ly = Math.min(ly, y); hy = Math.max(hy, y); }
    const pad = 28;
    const scale = Math.min((W - 2 * pad) / Math.max(hy - ly, 20), (H - 2 * pad) / Math.max(hx - lx, 20));
    const x0 = (hx + lx) / 2, y0 = (hy + ly) / 2;
    const sx = (wy) => W / 2 + (wy - y0) * scale, sy = (wx) => H / 2 - (wx - x0) * scale;

    // route as points along its polyline
    const route = [];
    for (let i = 0; i + 1 < t.route.length; i += 2) route.push([t.route[i], t.route[i + 1]]);
    const segs = [];
    let len = 0;
    for (let i = 0; i + 1 < route.length; i++) {
      const l = Math.hypot(route[i + 1][0] - route[i][0], route[i + 1][1] - route[i][1]);
      segs.push(l);
      len += l;
    }
    const at = (d) => {
      d = Math.max(0, Math.min(d, len));
      for (let i = 0; i < segs.length; i++) {
        if (d <= segs[i] || i === segs.length - 1) {
          const f = segs[i] > 0 ? d / segs[i] : 0;
          return [route[i][0] + (route[i + 1][0] - route[i][0]) * f, route[i][1] + (route[i + 1][1] - route[i][1]) * f];
        }
        d -= segs[i];
      }
      return route[0] || [0, 0];
    };
    g.fillStyle = 'rgba(255,255,255,0.35)';
    for (let d = 0; d < len; d += 6 / scale) {
      const [x, y] = at(d);
      g.fillRect(sx(y) - 1, sy(x) - 1, 2, 2);
    }
    const f = m.tick % (PV_LOOP + PV_HOLD);
    const run = f < PV_LOOP ? (len * f) / PV_LOOP : len;
    g.strokeStyle = '#ffc21a';
    g.lineWidth = 3;
    g.lineCap = 'round';
    g.beginPath();
    for (let d = 0; d <= run; d += 2 / scale) {
      const [x, y] = at(d);
      if (d === 0) g.moveTo(sx(y), sy(x)); else g.lineTo(sx(y), sy(x));
    }
    g.stroke();
    const [hx2, hy2] = at(run);
    g.fillStyle = '#ffc21a';
    g.beginPath(); g.arc(sx(hy2), sy(hx2), 5, 0, Math.PI * 2); g.fill();

    g.font = '600 13px system-ui, sans-serif';
    for (const k of t.marks) {
      const px = sx(k.y), py = sy(k.x);
      if (k.name === 'FIN') {
        g.strokeStyle = '#fff'; g.lineWidth = 2;
        g.beginPath(); g.moveTo(px, py - 7); g.lineTo(px, py + 7); g.moveTo(px - 7, py); g.lineTo(px + 7, py); g.stroke();
      } else {
        g.fillStyle = '#ff6a1a';
        g.beginPath(); g.arc(px, py, 6, 0, Math.PI * 2); g.fill();
        g.strokeStyle = '#fff'; g.lineWidth = 1.5; g.stroke();
      }
      g.fillStyle = '#fff';
      g.fillText(k.name, px + 9, py - 8);
    }
    g.fillStyle = '#fff';
    g.fillRect(sx(t.sy) - 4, sy(t.sx) - 4, 8, 8);
    g.fillText('S', sx(t.sy) + 8, sy(t.sx) + 4);

    // wind arrow, pointing the way the wind blows
    const wd = t.windFrom + Math.PI, ax = W - 46, ay = 46;
    const tx = ax + 18 * Math.sin(wd), ty = ay - 18 * Math.cos(wd);
    g.strokeStyle = '#cfe6f6'; g.fillStyle = '#cfe6f6'; g.lineWidth = 2;
    g.beginPath(); g.moveTo(ax - 18 * Math.sin(wd), ay + 18 * Math.cos(wd)); g.lineTo(tx, ty); g.stroke();
    g.beginPath();
    g.moveTo(tx, ty);
    g.lineTo(tx - 7 * Math.sin(wd) - 5 * Math.cos(wd), ty + 7 * Math.cos(wd) - 5 * Math.sin(wd));
    g.lineTo(tx - 7 * Math.sin(wd) + 5 * Math.cos(wd), ty + 7 * Math.cos(wd) + 5 * Math.sin(wd));
    g.fill();
    g.fillText('WIND', ax - 17, ay + 34);
  }
}
