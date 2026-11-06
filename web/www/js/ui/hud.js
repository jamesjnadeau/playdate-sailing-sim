// The sailing HUD as DOM over the 3D view: the status bar (as src/pd/hud.c), banners, praise, the
// finish / capsize box, the go-back question, the next-mark marker and the ghost's gap and edge arrow.
const MARK_IND_Z = 4;   // m above the sea where the marker anchors (camera.h)
const EDGE_PAD = 18;

const $ = (root, sel) => root.querySelector(sel);

export class Hud {
  constructor(root) {
    this.root = root;
    root.innerHTML = `
      <div class="hud-top">
        <div class="hud-clock" hidden></div>
        <div class="hud-ghost" hidden></div>
      </div>
      <div class="hud-praise" hidden></div>
      <div class="hud-warning" hidden></div>
      <div class="hud-marker" hidden><div class="tri"></div><div class="lab"></div></div>
      <div class="hud-ghost-arrow" hidden><div class="tri"></div><div class="lab"></div></div>
      <div class="hud-banners"></div>
      <div class="hud-box" hidden><div class="head"></div><div class="line"></div><div class="act"></div></div>
      <div class="hud-bar">
        <div class="cell mode"><span class="word"></span><span class="vert" title="crank raises and lowers the view">&#8597;</span></div>
        <div class="nav">
          <div class="cell"><span class="speed"></span><small>kn</small></div>
          <div class="cell"><small>HDG</small><span class="hdg"></span></div>
          <div class="cell"><small>HEEL</small><span class="heel"></span></div>
        </div>
        <div class="sail"><span class="sail-name"></span><div class="sail-bar"><div class="fill"></div></div></div>
        <div class="spacer"></div>
        <div class="cell icon at" title="Auto trim on">&#9881;</div>
        <div class="cell icon ap" title="Autopilot on"><span class="needle">&#10148;</span><span class="ap-target"></span></div>
        <div class="cell dial-cell">
          <span class="aws"></span><small>kn</small>
          <svg class="dial" viewBox="-12 -12 24 24" aria-label="apparent wind">
            <circle r="10" class="ring"/>
            <line class="bow" x1="0" y1="-8" x2="0" y2="-12"/>
            <line class="wind" x1="0" y1="0" x2="0" y2="-9"/>
          </svg>
        </div>
      </div>`;
    this.el = {
      bar: $(root, '.hud-bar'), word: $(root, '.word'), vert: $(root, '.vert'), nav: $(root, '.nav'),
      speed: $(root, '.speed'), hdg: $(root, '.hdg'), heel: $(root, '.heel'),
      sail: $(root, '.sail'), sailName: $(root, '.sail-name'), sailFill: $(root, '.sail-bar .fill'),
      at: $(root, '.at'), ap: $(root, '.ap'), apTarget: $(root, '.ap-target'),
      aws: $(root, '.aws'), dialWind: $(root, '.dial .wind'), dialBow: $(root, '.dial .bow'),
      banners: $(root, '.hud-banners'), praise: $(root, '.hud-praise'), warning: $(root, '.hud-warning'),
      box: $(root, '.hud-box'), clock: $(root, '.hud-clock'), ghost: $(root, '.hud-ghost'),
      marker: $(root, '.hud-marker'), ghostArrow: $(root, '.hud-ghost-arrow'),
    };
    this.cache = new Map();
  }

  // Only touch the DOM when a value changes.
  set(node, key, value, fn) {
    if (this.cache.get(key) === value) return;
    this.cache.set(key, value);
    fn(node, value);
  }
  text(node, key, value) { this.set(node, key, value, (n, v) => { n.textContent = v; }); }
  show(node, key, on) { this.set(node, key, !!on, (n, v) => { n.hidden = !v; }); }

  hide() { this.show(this.root, 'root', false); }

  update(f, view, keys) {
    this.show(this.root, 'root', true);
    const h = f.hud;
    const e = this.el;
    this.text(e.word, 'word', h.mode);
    this.show(e.vert, 'vert', h.vertical);
    this.show(e.nav, 'nav', h.nav);
    this.show(e.sail, 'sailShow', !h.nav && h.sails > 0);
    this.text(e.speed, 'speed', h.speed);
    this.text(e.hdg, 'hdg', h.hdg);
    this.text(e.heel, 'heel', h.heel + '°');
    this.text(e.sailName, 'sailName', h.sails > 1 ? `${h.sail} (${h.sailIndex + 1}/${h.sails})` : h.sail);
    this.set(e.sailFill, 'sailFrac', Math.round(h.sailFrac * 1000), (n, v) => { n.style.width = `${v / 10}%`; });
    this.show(e.at, 'at', h.autotrim);
    this.show(e.ap, 'ap', h.autopilot);
    this.text(e.apTarget, 'apT', h.apTarget);
    this.text(e.aws, 'aws', h.awsKn);
    const deg = (a) => (a * 180 / Math.PI).toFixed(1);
    this.set(e.dialWind, 'dw', deg(h.windDial), (n, v) => n.setAttribute('transform', `rotate(${v})`));
    this.set(e.dialBow, 'db', deg(h.bowDial), (n, v) => n.setAttribute('transform', `rotate(${v})`));
    this.text(e.banners, 'banners', h.banners.join('\n'));
    this.show(e.banners, 'bannersShow', h.banners.length > 0);
    this.show(e.praise, 'praiseShow', !!h.praise);
    if (h.praise) this.text(e.praise, 'praise', h.praise);
    this.show(e.warning, 'warnShow', !!f.warning);
    if (f.warning) this.text(e.warning, 'warn', f.warning);
    this.show(e.clock, 'clockShow', !!h.clock);
    if (h.clock) this.text(e.clock, 'clock', h.passed !== undefined ? `${h.clock}  ·  ${h.passed} passed` : h.clock);
    this.show(e.ghost, 'ghostShow', !!h.ghostGap);
    if (h.ghostGap) this.text(e.ghost, 'ghost', `Ghost ${h.ghostGap}`);

    // the box: go back? beats finished / capsized
    let box = null;
    if (f.draw === 3) box = { head: 'GO BACK?', line: 'Return to the home screen', act: `${keys.A}: yes    ${keys.B}: no` };
    else if (f.draw === 4) box = { head: 'Thanks for playing the demo', line: 'Back to the home screen…', act: `${keys.A}: continue` };
    else if (h.prompt) box = { head: h.prompt.head, line: h.prompt.line, act: `Press ${keys.A} to ${h.prompt.action}` };
    this.show(e.box, 'box', !!box);
    if (box) {
      this.text(e.box.children[0], 'bh', box.head);
      this.text(e.box.children[1], 'bl', box.line);
      this.text(e.box.children[2], 'ba', box.act);
    }

    const next = (f.marks || []).find((m) => m.next);
    this.indicator(e.marker, 'mk', view, next ? { x: next.x, y: next.y, dist: next.dist, always: true } : null);
    this.indicator(e.ghostArrow, 'gh', view,
      f.ghostAt && h.ghostDist !== undefined ? { x: f.ghostAt.x, y: f.ghostAt.y, dist: h.ghostDist, always: false } : null);
  }

  // Above the target when it is on screen, else an arrow on the edge pointing at it (hud.c mark_indicator).
  indicator(node, key, view, target) {
    if (!target) { this.show(node, key, false); return; }
    const p = view.project(target.x, target.y, MARK_IND_Z);
    const { w, h } = view.size;
    const bottom = h - view.hudPx;
    if (p.onScreen && !target.always) { this.show(node, key, false); return; }
    this.show(node, key, true);
    const label = `${Math.round(target.dist)} m`;
    this.text(node.children[1], key + 'l', label);
    if (p.onScreen) {
      node.classList.add('on');
      node.style.transform = `translate(${p.x.toFixed(1)}px, ${p.y.toFixed(1)}px)`;
      node.children[0].style.transform = '';
      return;
    }
    node.classList.remove('on');
    // clamp along the ray from the view's centre to the point
    const cx = w / 2, cy = bottom / 2;
    let dx = p.x - cx, dy = p.y - cy;
    if (!Number.isFinite(dx) || !Number.isFinite(dy) || (dx === 0 && dy === 0)) { dx = 1; dy = 0; }
    const sx = (w / 2 - EDGE_PAD) / Math.abs(dx || 1e-6), sy = (bottom / 2 - EDGE_PAD) / Math.abs(dy || 1e-6);
    const s = Math.min(sx, sy);
    const x = cx + dx * s, y = cy + dy * s;
    const ang = Math.atan2(dy, dx) * 180 / Math.PI;
    node.style.transform = `translate(${x.toFixed(1)}px, ${y.toFixed(1)}px)`;
    node.children[0].style.transform = `rotate(${ang.toFixed(1)}deg)`;
  }
}
