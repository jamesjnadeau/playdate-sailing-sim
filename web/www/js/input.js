// Keyboard and mouse -> the Playdate's buttons and crank.
//   W A S D (or the arrow keys) = d-pad, C = B, V = A (C sits left of V as B sits left of A),
//   moving the mouse = the crank (the canvas takes pointer lock on click): left turns it as up does, right as down
//   does; the wheel turns it too.
// Codes are physical keys (KeyboardEvent.code), so other keyboard layouts get the same positions.

export const BTN = { LEFT: 1, RIGHT: 2, UP: 4, DOWN: 8, B: 16, A: 32 };

export const KEYMAP = {
  KeyW: BTN.UP, KeyA: BTN.LEFT, KeyS: BTN.DOWN, KeyD: BTN.RIGHT,
  ArrowUp: BTN.UP, ArrowLeft: BTN.LEFT, ArrowDown: BTN.DOWN, ArrowRight: BTN.RIGHT,
  KeyC: BTN.B, KeyV: BTN.A,
};

export const DEFAULTS = { degPerPx: 0.75, degPerWheel: 15, invert: false };

// Button and crank state, read once per frame with take().
export class Input {
  constructor(settings = {}) {
    this.settings = { ...DEFAULTS, ...settings };
    this.held = 0;      // buttons down now
    this.queue = [];    // presses not yet handed out, oldest first: one per frame, so their order holds
    this.crank = 0;     // degrees since the last take()
  }

  keyDown(code, repeat = false) {
    const bit = KEYMAP[code];
    if (!bit) return false;
    if (!repeat && !(this.held & bit)) {
      // taps that land between two frames (a slow frame, a fast typist) reach the game on successive
      // frames, in order, rather than merging into one
      if (this.queue.length < 16) this.queue.push(bit);
    }
    this.held |= bit;
    return true;
  }

  keyUp(code) {
    const bit = KEYMAP[code];
    if (!bit) return false;
    this.held &= ~bit;
    return true;
  }

  // Mouse moved dx, dy pixels (right and down are positive). Down, and right, turn the crank clockwise (+);
  // up and left turn it back. Inverted, the other way round.
  mouseMove(dy, dx = 0) {
    const d = (Number.isFinite(dy) ? dy : 0) + (Number.isFinite(dx) ? dx : 0);
    this.crank += d * this.settings.degPerPx * (this.settings.invert ? -1 : 1);
  }

  // A wheel event's deltaY (down is positive), in any delta mode: one notch is one step.
  wheel(deltaY) {
    if (!Number.isFinite(deltaY) || deltaY === 0) return;
    this.crank += Math.sign(deltaY) * this.settings.degPerWheel * (this.settings.invert ? -1 : 1);
  }

  // A button press from somewhere other than the keyboard (the mouse in the menu); bit 0 does nothing.
  press(bit) {
    if (bit && this.queue.length < 16) this.queue.push(bit);
  }

  // Focus lost or the system menu opened: nothing stays held or queued.
  releaseAll() {
    this.held = 0;
    this.queue.length = 0;
    this.crank = 0;
  }

  // This frame's input. A key pressed and released between two frames still counts as one press, and is
  // held for that frame.
  take() {
    const pressed = this.queue.length ? this.queue.shift() : 0;
    const out = { pressed, held: this.held | pressed, crank: this.crank };
    this.crank = 0;
    return out;
  }
}

export function loadSettings(storage) {
  try {
    const raw = storage && storage.getItem('cranksail.input');
    if (!raw) return { ...DEFAULTS };
    const s = JSON.parse(raw);
    return {
      degPerPx: Number.isFinite(s.degPerPx) && s.degPerPx > 0 ? s.degPerPx : DEFAULTS.degPerPx,
      degPerWheel: Number.isFinite(s.degPerWheel) && s.degPerWheel > 0 ? s.degPerWheel : DEFAULTS.degPerWheel,
      invert: s.invert === true,
    };
  } catch {
    return { ...DEFAULTS };
  }
}

export function saveSettings(storage, s) {
  try {
    storage && storage.setItem('cranksail.input', JSON.stringify(s));
  } catch {
    // private window or blocked storage: the settings last for this visit only
  }
}
