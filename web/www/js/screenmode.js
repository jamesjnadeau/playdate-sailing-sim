// The full-screen button, top left: the whole page fills the screen, with no browser bars, until pressed again
// (or Esc). On a phone it also asks for landscape, where the browser allows it. Browsers without the Fullscreen
// API for pages (Safari on iPhone) get no button: there, Add to Home Screen opens the game without bars.
const doc = document;
const element = () => doc.fullscreenElement || doc.webkitFullscreenElement || null;

export function fullscreenSupported(d = doc) {
  const el = d.documentElement;
  return !!(d.fullscreenEnabled || d.webkitFullscreenEnabled) && !!(el.requestFullscreen || el.webkitRequestFullscreen);
}

async function enter() {
  const el = doc.documentElement;
  try {
    if (el.requestFullscreen) await el.requestFullscreen({ navigationUI: 'hide' });
    else el.webkitRequestFullscreen();
  } catch {
    return;   // refused (not from a tap, or blocked by the browser)
  }
  try {
    if (screen.orientation && screen.orientation.lock && matchMedia('(pointer: coarse)').matches) {
      await screen.orientation.lock('landscape');
    }
  } catch { /* the browser keeps the phone's own orientation */ }
}

function leave() {
  try {
    if (doc.exitFullscreen) { const p = doc.exitFullscreen(); if (p && p.catch) p.catch(() => {}); } else if (doc.webkitExitFullscreen) doc.webkitExitFullscreen();
  } catch { /* already out */ }
}

export function setupFullscreen(button, onToggle = () => {}) {
  if (!fullscreenSupported()) return false;
  button.hidden = false;
  doc.body.classList.add('can-fs');
  const sync = () => {
    const on = !!element();
    button.classList.toggle('on', on);
    button.setAttribute('aria-label', on ? 'Leave full screen' : 'Full screen');
    button.title = on ? 'Leave full screen' : 'Full screen';
  };
  button.addEventListener('click', (e) => {
    e.stopPropagation();
    onToggle();
    if (element()) leave(); else enter();
    button.blur();   // so Space or Enter go to the game, not back to this button
  });
  doc.addEventListener('fullscreenchange', sync);
  doc.addEventListener('webkitfullscreenchange', sync);
  sync();
  return true;
}
