// hud-bar.mjs: loads the real Hud class and style.css in headless Chromium (no wasm needed) and checks that the
// status bar shows every instrument in TRIM mode with autotrim and autopilot on, and that nothing overflows.
// Run: node web/test/hud-bar.mjs
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const www = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../www');
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css' };
const PAGE = `<!doctype html><link rel="stylesheet" href="/style.css"><div id="hud"></div>
<script type="module">
  import { Hud } from '/js/ui/hud.js';
  const hud = new Hud(document.getElementById('hud'));
  window.show = (h) => hud.update({ hud: h, warning: null, draw: 1, marks: [] }, { size: { w: 800, h: 400 }, hudPx: 40, project: () => ({ onScreen: false, x: 0, y: 0 }) }, { A: 'A', B: 'B' });
</script>`;

const server = http.createServer((req, res) => {
  const url = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  if (url === '/') { res.writeHead(200, { 'content-type': 'text/html' }); res.end(PAGE); return; }
  const file = path.join(www, url);
  if (!file.startsWith(www) || !fs.existsSync(file)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'content-type': TYPES[path.extname(file)] || 'application/octet-stream' });
  res.end(fs.readFileSync(file));
});
await new Promise((r) => server.listen(0, r));
const url = `http://localhost:${server.address().port}/`;

const base = { mode: 'TRIM', vertical: false, speed: '6.2', hdg: '231', heel: '+14', awsKn: '27', sail: 'Main', sailFrac: 0.6,
  sailIndex: 0, sails: 1, sailMode: true, autotrim: true, autopilot: true, apTarget: '231', windDial: 0.9, bowDial: 0,
  banners: [], praise: null };

let failed = 0;
const check = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed++; };

const browser = await chromium.launch();
for (const [w, h] of [[1280, 720], [667, 375], [390, 844]]) {
  const page = await browser.newPage({ viewport: { width: w, height: h } });
  const errors = [];
  page.on('pageerror', (e) => errors.push(e.message));
  await page.goto(url);
  await page.waitForFunction(() => window.show);
  for (const [name, patch] of [['TRIM, AT+AP on', {}], ['HELM', { mode: 'HELM', sailMode: false, autotrim: false, autopilot: false }],
                               ['HOIST, 3-digit wind, negative heel', { mode: 'HOIST', awsKn: '100', heel: '-14' }]]) {
    await page.evaluate((hh) => window.show(hh), { ...base, ...patch });
    const r = await page.evaluate(() => {
      const vis = (sel) => { const e = document.querySelector(sel); if (!e) return null; const b = e.getBoundingClientRect(); return { l: b.left, r: b.right, w: b.width, h: b.height }; };
      const bar = document.querySelector('.hud-bar');
      return { speed: vis('.speed'), hdg: vis('.hdg'), heel: vis('.heel'), aws: vis('.aws'), dial: vis('.dial'),
               hdgIcon: vis('.hdg-icon'), heelIcon: vis('.heel-icon'), sail: vis('.sail'),
               overflow: bar.scrollWidth - bar.clientWidth, sailHidden: document.querySelector('.sail').hidden };
    });
    const tag = `${w}px ${name}`;
    for (const k of ['speed', 'hdg', 'heel', 'aws', 'dial', 'hdgIcon', 'heelIcon']) check(r[k] && r[k].w > 0 && r[k].h > 0, `${tag}: ${k} is visible`);
    check(r.hdgIcon && r.heelIcon && r.hdgIcon.r <= r.hdg.l + 1 && r.heelIcon.r <= r.heel.l + 1, `${tag}: icons sit left of their numbers`);
    check(r.speed && r.hdg && r.heel && r.aws && r.speed.r <= r.hdgIcon.l + 1 && r.hdg.r <= r.heelIcon.l + 1 && r.heel.r <= r.aws.l + 1 && r.aws.r <= r.dial.l + 1,
          `${tag}: order is speed, heading, heel, wind, dial`);
    check(r.overflow <= 0, `${tag}: the bar does not overflow (${r.overflow}px)`);
    check(patch.mode === 'HELM' ? r.sailHidden : !r.sailHidden, `${tag}: sail block ${patch.mode === 'HELM' ? 'hidden' : 'shown'}`);
    if (patch.mode !== 'HELM') check(r.sail && r.sail.w >= 40, `${tag}: the sail bar keeps room (${r.sail && Math.round(r.sail.w)}px)`);
  }
  check(errors.length === 0, `${w}px: no page errors ${errors.join('; ')}`);
  await page.close();
}
await browser.close();
server.close();
if (failed) { console.log(`${failed} check(s) failed`); process.exit(1); }
console.log('all checks passed');
