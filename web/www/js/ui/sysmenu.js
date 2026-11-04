// Backspace: the Playdate's system menu. The run is paused while it is open. Boat and weather are offered in free
// range only (as on the Playdate); "go back" asks the question in the game; crank settings live here too.
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

export class SysMenu {
  constructor(root, { onBoat, onWeather, onBack, onSettings, onClose }) {
    this.root = root;
    this.open = false;
    this.cb = { onBoat, onWeather, onBack, onSettings, onClose };
    root.innerHTML = `
      <div class="sys-panel" role="dialog" aria-label="Paused">
        <h2>Paused</h2>
        <div class="sys-rows"></div>
        <div class="sys-picks">
          <label>Boat <select class="sys-boat"></select></label>
          <label>Weather <select class="sys-weather"></select></label>
        </div>
        <button class="sys-back">Go back to the home screen</button>
        <h3>Crank (mouse or drag)</h3>
        <label class="range">Sensitivity <input class="sys-sens" type="range" min="0.1" max="3" step="0.05"><span class="sys-sens-v"></span></label>
        <label class="check"><input class="sys-inv" type="checkbox"> Up (or left) turns the crank clockwise</label>
        <button class="sys-resume">Resume <span class="key mouse-only">Backspace</span></button>
      </div>`;
    this.q = (s) => root.querySelector(s);
    this.q('.sys-boat').addEventListener('change', (e) => this.cb.onBoat(Number(e.target.value)));
    this.q('.sys-weather').addEventListener('change', (e) => this.cb.onWeather(Number(e.target.value)));
    this.q('.sys-back').addEventListener('click', () => { this.cb.onBack(); this.close(); });
    this.q('.sys-resume').addEventListener('click', () => this.close());
    const sens = this.q('.sys-sens'), inv = this.q('.sys-inv');
    const changed = () => {
      this.q('.sys-sens-v').textContent = `${Number(sens.value).toFixed(2)}°/px`;
      this.cb.onSettings({ degPerPx: Number(sens.value), invert: inv.checked });
    };
    sens.addEventListener('input', changed);
    inv.addEventListener('change', changed);
    root.addEventListener('mousedown', (e) => { if (e.target === root) this.close(); });
  }

  show(frame, stat, settings) {
    this.open = true;
    this.root.hidden = false;
    const racing = frame.racingMenu;
    this.q('.sys-picks').hidden = racing;
    const fill = (sel, names, value) => {
      sel.innerHTML = names.map((n, i) => `<option value="${i}">${esc(n)}</option>`).join('');
      sel.value = String(value);
    };
    fill(this.q('.sys-boat'), stat.boats, frame.boatIndex);
    fill(this.q('.sys-weather'), stat.weathers, frame.preset);
    const rows = (frame.hud && frame.hud.pause) || [];
    this.q('.sys-rows').innerHTML = rows.map((r) => `<div>${esc(r)}</div>`).join('');
    this.q('.sys-rows').hidden = rows.length === 0;
    this.q('.sys-back').hidden = !(frame.draw === 2 || frame.draw === 3);
    this.q('.sys-sens').value = String(settings.degPerPx);
    this.q('.sys-sens-v').textContent = `${settings.degPerPx.toFixed(2)}°/px`;
    this.q('.sys-inv').checked = settings.invert;
  }

  close() {
    if (!this.open) return;
    this.open = false;
    this.root.hidden = true;
    this.cb.onClose();
  }
}
