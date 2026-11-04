// Crank'n Sail on the web: the Playdate game's own simulation and app flow (src/sim, src/app) compiled to
// wasm, with a Three.js view. One sim step per animation frame with the real elapsed time.
import createSim from '../sim.js';
import { Input, loadSettings, saveSettings } from './input.js';
import { View } from './render/scene.js';
import { Hud } from './ui/hud.js';
import { MenuView } from './ui/menu.js';
import { SysMenu } from './ui/sysmenu.js';
import { MenuPointer } from './ui/menupointer.js';
import { About } from './ui/about.js';
import { SeaAudio } from './audio.js';
import { TouchControls } from './touch.js';
import { setupFullscreen } from './fullscreen.js';

const DRAW = { NONE: 0, MENU: 1, PLAY: 2, CONFIRM: 3, DEMO_END: 4 };
const KEYS = { A: 'V', B: 'C', updown: 'W / S',
  legend: '<span class="key">W</span><span class="key">A</span><span class="key">S</span><span class="key">D</span> d-pad, ' +
    '<span class="key">V</span> A, <span class="key">C</span> B, move the mouse to turn the crank (left works as up, right as down) ' +
    '(click the view first), <span class="key">Backspace</span> the system menu.' };
const TOUCH_KEYS = { A: 'A', B: 'B', updown: '▲ / ▼', wheel: 'Drag',
  legend: 'hold your phone sideways. The pad bottom left is the d-pad, <span class="key">B</span> and ' +
    '<span class="key">A</span> are bottom right, drag up and down anywhere else on the view to turn the crank, ' +
    'and <span class="key">☰</span> top right opens the system menu.' };
const HUD_PX = 46;

const $ = (s) => document.querySelector(s);

// Elements the page needs that are made here rather than in index.html: the host may cache index.html for a few
// minutes, and a page from before an element existed must still boot with these scripts.
function ensure(id, tag, html, attrs = {}) {
  let el = document.getElementById(id);
  if (el) return el;
  el = document.createElement(tag);
  el.id = id;
  el.innerHTML = html;
  for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, v);
  document.body.insertBefore(el, document.getElementById('about'));
  return el;
}
const status = $('#status');
const say = (s) => { status.textContent = s; status.hidden = !s; };

async function mountSaves(FS) {
  const IDBFS = FS.filesystems && FS.filesystems.IDBFS;
  try { FS.mkdir('/save'); } catch { /* exists */ }
  if (!IDBFS) return false;
  try {
    FS.mount(IDBFS, {}, '/save');
    await new Promise((res) => FS.syncfs(true, () => res()));
    return true;
  } catch {
    return false;   // private window or blocked storage: saves last for this visit
  }
}

async function boot() {
  say('Loading…');
  const sim = await createSim({ print: (s) => console.log(s), printErr: (s) => console.warn(s) });
  const persistent = await mountSaves(sim.FS);
  sim._web_init((Date.now() >>> 0) ^ 0x5a17);

  const canvas = $('#view');
  const view = new View(canvas);
  const hud = new Hud($('#hud'));
  const settings = loadSettings(window.localStorage);
  const input = new Input(settings);
  const pointer = new MenuPointer();
  const about = new About($('#about'), 'about.md');
  const menu = new MenuView($('#menu'), {
    point: (screen, row, press) => { if (press) audio.start(); pointer.request(screen, row, press); },
    about: () => { pointer.cancel(); input.releaseAll(); about.show(); },
  });
  const audio = new SeaAudio(sim);

  // touch mode: on from the start on a touch-first device, and as soon as a finger lands on the view; a key
  // on the keyboard turns it off again
  ensure('touch', 'div', '', { hidden: '' });
  ensure('rotate', 'div', '<div>↻</div>Turn your phone sideways to sail');
  ensure('fullscreen', 'button', `<svg viewBox="0 0 24 24" aria-hidden="true">
      <path class="fs-enter" d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/>
      <path class="fs-leave" d="M9 4v5H4M20 9h-5V4M15 20v-5h5M4 15h5v5"/></svg>`,
  { hidden: '', 'aria-label': 'Full screen', title: 'Full screen' });
  const touch = new TouchControls($('#touch'), $('#view'), input, {
    menu: () => { audio.start(); if (sys.open) sys.close(); else openSys(); },
    touch: () => { audio.start(); setTouch(true); },
  });
  let touchMode = false;
  const setTouch = (on) => {
    if (on === touchMode) return;
    touchMode = on;
    document.body.classList.toggle('touch', on);
    $('#touch').hidden = !on;
    menu.last = '';   // redraw the hints with the touch labels
  };
  try { setTouch(window.matchMedia('(pointer: coarse)').matches); } catch { /* no matchMedia */ }
  setupFullscreen($('#fullscreen'), () => audio.start());

  let stat = null, statGen = -1, frame = null, paused = false;
  const readStatic = () => {
    stat = JSON.parse(sim.UTF8ToString(sim._web_static_json()));
    statGen = stat.gen;
    view.setBoat(stat.boat);
  };

  const sys = new SysMenu($('#sysmenu'), {
    onBoat: (i) => sim._web_sys_boat(i),
    onWeather: (i) => sim._web_sys_weather(i),
    onBack: () => sim._web_sys_back(),
    onSettings: (s) => { Object.assign(input.settings, s); saveSettings(window.localStorage, input.settings); },
    onClose: () => {
      paused = false;
      audio.active = !!sim._web_playing();
      last = performance.now();
    },
  });
  const openSys = () => {
    if (!frame || sys.open) return;
    paused = true;
    audio.active = false;
    input.releaseAll();
    touch.release();
    if (document.pointerLockElement) document.exitPointerLock();
    sys.show(frame, stat, input.settings);
  };

  const hudPx = () => parseFloat(getComputedStyle(document.documentElement).getPropertyValue('--hud-h')) || HUD_PX;
  const resize = () => view.resize(window.innerWidth, window.innerHeight, hudPx());
  window.addEventListener('resize', resize);
  resize();

  window.addEventListener('keydown', (e) => {
    audio.start();
    if (about.open) {
      if (e.code === 'Escape' || e.code === 'Backspace') { e.preventDefault(); about.close(); }
      return;
    }
    if (e.code === 'Backspace' || e.code === 'KeyP') {
      e.preventDefault();
      if (sys.open) sys.close(); else openSys();
      return;
    }
    if (sys.open) return;
    if (input.keyDown(e.code, e.repeat)) { pointer.cancel(); setTouch(false); e.preventDefault(); }
  });
  window.addEventListener('keyup', (e) => { if (input.keyUp(e.code)) e.preventDefault(); });
  window.addEventListener('blur', () => { input.releaseAll(); touch.release(); });
  canvas.addEventListener('click', () => {
    audio.start();
    if (!touchMode && !sys.open && !document.pointerLockElement && canvas.requestPointerLock) {
      try { const p = canvas.requestPointerLock(); if (p && p.catch) p.catch(() => {}); } catch { /* unsupported */ }
    }
  });
  document.addEventListener('pointerlockchange', () => {
    document.body.classList.toggle('locked', document.pointerLockElement === canvas);
  });
  document.addEventListener('mousemove', (e) => {
    if (document.pointerLockElement === canvas) input.mouseMove(e.movementY, e.movementX);
  });
  window.addEventListener('wheel', (e) => {
    if (sys.open || about.open) return;   // let the panel scroll
    input.wheel(e.deltaY);
    e.preventDefault();
  }, { passive: false });

  say('');
  let last = performance.now();
  let saving = false;
  let tMenu = 0;
  let d0 = DRAW.NONE;   // what the last frame drew
  const tick = (now) => {
    requestAnimationFrame(tick);
    const dt = Math.min(Math.max((now - last) / 1000, 0), 0.1);
    last = now;
    if (paused) return;
    if (d0 === DRAW.MENU && frame && !input.queue.length && !about.open) input.press(pointer.next(frame.menu));
    else if (d0 !== DRAW.MENU) pointer.cancel();
    const inp = input.take();
    const d = sim._web_step(inp.pressed, inp.held, inp.crank, dt || 1 / 60);
    d0 = d;
    frame = JSON.parse(sim.UTF8ToString(sim._web_frame_json()));
    if (!stat || frame.static !== statGen) readStatic();
    if (persistent && !saving && sim._web_take_dirty()) {
      saving = true;
      sim.FS.syncfs(false, () => { saving = false; });
    }
    document.body.dataset.draw = String(d);
    const keys = touchMode ? TOUCH_KEYS : KEYS;
    touch.show(d === DRAW.PLAY || d === DRAW.CONFIRM);
    if (d === DRAW.MENU) {
      hud.hide();
      tMenu += dt;
      view.renderBackdrop(frame, stat, tMenu, dt);
      menu.update(frame.menu, stat, keys);
      canvas.classList.add('dim');
    } else if (d === DRAW.PLAY || d === DRAW.CONFIRM || d === DRAW.DEMO_END) {
      menu.hide();
      canvas.classList.remove('dim');
      view.render(frame, stat, dt);
      hud.update(frame, view, keys);
    }
    window.__cranksail = { frame, stat, draw: d };
  };
  requestAnimationFrame(tick);
}

boot().catch((e) => {
  console.error(e);
  say(`Could not start: ${e.message || e}`);
});
