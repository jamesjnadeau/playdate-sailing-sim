// smoke.mjs DIST [SHOTDIR]: serves the built page, drives it in headless Chromium and checks that it boots
// without errors, that the keys start a run, that the crank (mouse wheel here; headless has no pointer
// lock) turns the boat, that the system menu goes back home, and that a race and a journey start; then, on a
// phone-sized touch screen held sideways, that the touch d-pad, buttons, menu and drag-to-crank work.
// With SHOTDIR, saves a screenshot of each stage there.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
let chromium;
try {
  ({ chromium } = require('playwright'));
} catch {
  ({ chromium } = await import('playwright'));
}

const dist = path.resolve(process.argv[2] || 'dist');
const shots = process.argv[3] ? path.resolve(process.argv[3]) : null;
if (shots) fs.mkdirSync(shots, { recursive: true });
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.wasm': 'application/wasm', '.data': 'application/octet-stream' };

const server = http.createServer((req, res) => {
  const url = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  const file = path.join(dist, url === '/' ? 'index.html' : url);
  if (!file.startsWith(dist) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
    res.writeHead(404);
    res.end();
    return;
  }
  res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream' });
  fs.createReadStream(file).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}/`;

const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
const errors = [];
page.on('pageerror', (e) => errors.push(`pageerror: ${e.message}`));
page.on('console', (m) => { if (m.type() === 'error') errors.push(`console: ${m.text()}`); });

let failed = false;
const check = (ok, what) => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`);
  if (!ok) failed = true;
};
const state = () => page.evaluate(() => window.__cranksail && { draw: window.__cranksail.draw, f: window.__cranksail.frame });
const waitDraw = (d, ms = 8000) => page.waitForFunction((want) => window.__cranksail && window.__cranksail.draw === want, d, { timeout: ms }).then(() => true, () => false);
const tap = async (key) => { await page.keyboard.press(key); await page.waitForTimeout(80); };
const hudMode = (want) => page.waitForFunction((w) => {
  const e = document.querySelector('.hud-bar .word');
  return e && e.textContent === w;
}, want, { timeout: 8000 }).then(() => true, () => false);
const shot = async (name) => { if (shots) await page.screenshot({ path: path.join(shots, `${name}.png`) }); };

try {
  await page.goto(base);
  check(await waitDraw(1, 30000), 'boots to the main menu');
  check(await page.locator('.menu-list .item.sel').count() === 1, 'menu shows a selected row');
  check(await page.evaluate(() => !document.body.classList.contains('touch') && document.querySelector('#touch').hidden), 'no touch controls with a mouse');
  await page.waitForTimeout(600);
  await shot('menu');

  await tap('KeyV');   // Free Range
  check(await waitDraw(2), 'V starts Free Range');
  await page.waitForTimeout(1500);
  const s0 = await state();
  check(s0.f.boat && s0.f.boat.sails.length > 0, 'the frame carries the boat and its sails');
  check(await hudMode('HELM'), 'HUD shows the helm mode');
  await shot('free_start');

  await page.mouse.move(640, 360);
  for (let i = 0; i < 12; i++) { await page.mouse.wheel(0, 120); await page.waitForTimeout(60); }
  await page.waitForTimeout(2500);
  const s1 = await state();
  check(Math.abs(s1.f.boat.heading - s0.f.boat.heading) > 0.05, `the crank turns the boat (heading ${s0.f.boat.heading.toFixed(3)} -> ${s1.f.boat.heading.toFixed(3)})`);
  check(s1.f.hud.speed !== undefined, 'HUD has a speed');
  await shot('free_turned');

  await tap('KeyD');   // trim mode
  check(await hudMode('TRIM'), 'D selects trim');
  await tap('KeyW');

  await tap('Backspace');
  check(await page.locator('#sysmenu').isVisible(), 'Backspace opens the system menu');
  await shot('sysmenu');
  await page.locator('.sys-back').click();
  await page.waitForTimeout(200);
  check(await waitDraw(3), 'go back asks first');
  await tap('KeyV');
  check(await waitDraw(1), 'V answers yes and goes home');

  // Racing: down twice, then A through course, preview, boat and weather
  await tap('KeyS'); await tap('KeyS');
  await tap('KeyV');
  await tap('KeyV');
  await page.waitForTimeout(500);
  await shot('preview');
  await tap('KeyV'); await tap('KeyV'); await tap('KeyV');
  check(await waitDraw(2), 'a race starts');
  await page.waitForTimeout(1500);
  const r = await state();
  check(r.f.mode === 1 && r.f.marks.length > 0, `race has marks (${r.f.marks.length})`);
  // the marker is a zero-size anchor for its arrow and label, so check it is not hidden rather than "visible"
  check(await page.evaluate(() => !document.querySelector('.hud-marker').hidden), 'next-mark marker shows');
  await shot('race');

  await tap('Backspace');
  await page.locator('.sys-back').click();
  await tap('KeyV');
  check(await waitDraw(1), 'back home from the race');
  await tap('KeyS');
  await tap('KeyV'); await tap('KeyV'); await tap('KeyV');
  check(await waitDraw(2), 'a journey starts');
  await page.waitForTimeout(1500);
  const j = await state();
  check(j.f.mode === 2 && j.f.marks.length > 0, `journey has buoys (${j.f.marks.length})`);
  await shot('journey');

  // the mouse in the main menu: pointing selects, clicking chooses, the hints are buttons, About opens
  await tap('Backspace');
  await page.locator('.sys-back').click();
  await tap('KeyV');
  check(await waitDraw(1), 'back home from the journey');
  const menuIs = (screen, sel) => page.waitForFunction(([s, k]) => {
    const m = window.__cranksail && window.__cranksail.frame.menu;
    return m && m.screen === s && (k === null || m.sel === k);
  }, [screen, sel], { timeout: 4000 }).then(() => true, () => false);
  await page.locator('.menu-list .item[data-row="3"]').hover();
  await page.mouse.move(700, 400, { steps: 2 });   // off the list: the selection stays
  await page.locator('.menu-list .item[data-row="3"]').hover({ position: { x: 30, y: 10 } });
  check(await menuIs(0, 3), 'pointing at a row selects it');
  await page.locator('.menu-list .item[data-row="3"]').click();
  check(await menuIs(6, null), 'clicking Training opens the training screen');
  await page.locator('.menu-list .item[data-row="0"]').click();
  check(await menuIs(5, null), 'clicking Controls opens the controls screen');
  await page.locator('.menu-hints button[data-press="16"]').click();
  check(await menuIs(6, null), 'the BACK hint is a button');
  await page.locator('.menu-hints button[data-press="16"]').click();
  check(await menuIs(0, 3), 'BACK leaves Training');
  await page.locator('.menu-hints button.about').click();
  check(await page.locator('#about .about-body h1').textContent({ timeout: 4000 }).then((t) => t === 'About Crank Sail', () => false),
    'About shows the page rendered from about.md');
  await shot('about');
  await tap('KeyS');
  check(await menuIs(0, 3), 'keys do not reach the menu while About is open');
  await tap('Escape');
  check(!(await page.locator('#about').isVisible()), 'Escape closes About');
  await page.locator('.menu-list .item[data-row="0"]').click();
  check(await waitDraw(2), 'clicking Free Range starts it');
} catch (e) {
  check(false, `exception: ${e.message}`);
}

// A WebGL canvas cannot be read back after its frame, so judge the screenshot: a blank or flat view
// compresses to a few kilobytes, a drawn sea and sky to hundreds.
const png = await page.screenshot().catch(() => Buffer.alloc(0));
check(png.length > 100000, `the 3D view is drawn (screenshot ${Math.round(png.length / 1024)} KB)`);
check(errors.length === 0, `no console errors${errors.length ? ':\n  ' + errors.join('\n  ') : ''}`);

// A phone held sideways: no keyboard or mouse, only fingers. The touch controls show, a finger dragging the
// view turns the crank while a thumb holds the d-pad, and the on-screen buttons and ☰ work.
const phone = await browser.newContext({ viewport: { width: 844, height: 390 }, hasTouch: true, isMobile: true, deviceScaleFactor: 1 });
const mob = await phone.newPage();
const merrors = [];
mob.on('pageerror', (e) => merrors.push(`pageerror: ${e.message}`));
mob.on('console', (m) => { if (m.type() === 'error') merrors.push(`console: ${m.text()}`); });
const cdp = await phone.newCDPSession(mob);
const touches = (type, points) => cdp.send('Input.dispatchTouchEvent', {
  type, touchPoints: points.map(([id, x, y]) => ({ id, x, y, radiusX: 4, radiusY: 4, force: 1 })),
});
const mstate = () => mob.evaluate(() => window.__cranksail && { draw: window.__cranksail.draw, f: window.__cranksail.frame });
const centre = async (sel) => {
  const b = await mob.locator(sel).boundingBox();
  return [b.x + b.width / 2, b.y + b.height / 2, b];
};
try {
  await mob.goto(base);
  check(await mob.waitForFunction(() => window.__cranksail && window.__cranksail.draw === 1, null, { timeout: 30000 }).then(() => true, () => false),
    'phone: boots to the main menu');
  check(await mob.evaluate(() => document.body.classList.contains('touch')), 'phone: starts in touch mode');
  const hint = await mob.locator('.menu-hints button[data-press="32"]').textContent();
  check(/^A\b/.test(hint.trim()), `phone: the hints name the on-screen buttons (${hint.trim()})`);
  if (shots) await mob.screenshot({ path: path.join(shots, 'phone_menu.png') });
  await mob.locator('.menu-list .item[data-row="0"]').tap();
  // the first sailing frame compiles the sea's shaders, which takes seconds in software-rendered headless Chromium
  check(await mob.waitForFunction(() => window.__cranksail.draw === 2, null, { timeout: 30000 }).then(() => true, () => false),
    'phone: tapping Free Range starts it');
  check(await mob.locator('.tc-pad').isVisible() && await mob.locator('.tc-btn.a').isVisible(), 'phone: d-pad and buttons show');
  await mob.waitForTimeout(1200);
  if (shots) await mob.screenshot({ path: path.join(shots, 'phone_play.png') });

  // pad: press and slide from right (trim) round to up (helm)
  const [px, py, pb] = await centre('.tc-pad');
  const r = pb.width / 2;
  // headless Chromium draws few frames while a finger is down, so wait for the HUD rather than a fixed time
  const mobMode = (want) => mob.waitForFunction((w) => document.querySelector('.hud-bar .word').textContent === w, want,
    { timeout: 8000 }).then(() => true, () => false);
  await touches('touchStart', [[1, px + r * 0.7, py]]);
  check(await mobMode('TRIM'), 'phone: right on the pad selects trim');
  await touches('touchMove', [[1, px + r * 0.3, py - r * 0.3]]);
  await touches('touchMove', [[1, px, py - r * 0.7]]);
  check(await mobMode('HELM'), 'phone: sliding round to up selects helm');

  // keep the thumb on the pad and drag another finger down the view: the crank turns the boat
  const s0 = await mstate();
  await touches('touchStart', [[1, px, py - r * 0.7], [2, 520, 60]]);
  for (let k = 1; k <= 14; k++) {
    await touches('touchMove', [[1, px, py - r * 0.7], [2, 520, 60 + k * 18]]);
    await mob.waitForTimeout(60);
  }
  await touches('touchEnd', []);
  const turned = await mob.waitForFunction((h0) => Math.abs(window.__cranksail.frame.boat.heading - h0) > 0.05,
    s0.f.boat.heading, { timeout: 8000 }).then(() => true, () => false);
  const s1 = await mstate();
  check(turned, `phone: dragging the view turns the boat (heading ${s0.f.boat.heading.toFixed(3)} -> ${s1.f.boat.heading.toFixed(3)})`);
  check(await mob.evaluate(() => window.__cranksail && document.querySelector('.tc-pad').dataset.dir === ''), 'phone: lifting the thumb releases the pad');
  if (shots) await mob.screenshot({ path: path.join(shots, 'phone_turned.png') });

  // ☰ opens the system menu; go back asks, and A on screen answers yes
  await mob.locator('.tc-menu').tap();
  check(await mob.locator('#sysmenu').isVisible(), 'phone: ☰ opens the system menu');
  if (shots) await mob.screenshot({ path: path.join(shots, 'phone_sysmenu.png') });
  await mob.locator('.sys-back').tap();
  check(await mob.waitForFunction(() => window.__cranksail.draw === 3, null, { timeout: 8000 }).then(() => true, () => false), 'phone: go back asks first');
  check((await mob.locator('.hud-box .act').textContent()).includes('A: yes'), 'phone: the question names the on-screen buttons');
  const [ax, ay] = await centre('.tc-btn.a');
  await touches('touchStart', [[3, ax, ay]]);
  await mob.waitForTimeout(120);
  await touches('touchEnd', []);
  check(await mob.waitForFunction(() => window.__cranksail.draw === 1, null, { timeout: 8000 }).then(() => true, () => false), 'phone: A answers yes and goes home');

  await mob.setViewportSize({ width: 390, height: 844 });
  await mob.waitForTimeout(200);
  check(await mob.locator('#rotate').isVisible(), 'phone: held upright, it asks to be turned sideways');
} catch (e) {
  check(false, `phone: exception: ${e.message}`);
}
check(merrors.length === 0, `phone: no console errors${merrors.length ? ':\n  ' + merrors.join('\n  ') : ''}`);
await phone.close();

await browser.close();
server.close();
process.exit(failed ? 1 : 0);
