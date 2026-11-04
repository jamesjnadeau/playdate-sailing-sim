// Touch controls for phones and tablets held in landscape: the Playdate's d-pad bottom left, B and A bottom right,
// the system menu top right, and the crank anywhere else: drag a finger up and down (or left and right) the view to
// turn it, as the mouse does. Every finger is its own pointer, so one thumb can hold the d-pad while the other turns the crank.
import { BTN } from './input.js';

// The d-pad direction for a finger at (dx, dy) from the pad's centre, for a pad of radius r: the nearest of the
// four arrows, or 0 inside the dead zone in the middle.
export const DEAD = 0.22;
export function padDirection(dx, dy, r) {
  if (!Number.isFinite(dx) || !Number.isFinite(dy) || Math.hypot(dx, dy) < DEAD * r) return 0;
  if (Math.abs(dx) > Math.abs(dy)) return dx > 0 ? BTN.RIGHT : BTN.LEFT;
  return dy > 0 ? BTN.DOWN : BTN.UP;
}

const ARROW = (cls, label) => `<span class="tc-arrow ${cls}" aria-hidden="true">${label}</span>`;

export class TouchControls {
  // root: an empty element over the view; canvas: the view; input: an Input; on.menu(): open the system menu;
  // on.touch(): a finger came down anywhere (first use switches the page to touch mode).
  constructor(root, canvas, input, on = {}) {
    this.root = root;
    this.input = input;
    this.on = { menu: () => {}, touch: () => {}, ...on };
    this.enabled = false;
    this.dragging = new Map();   // crank fingers: pointerId -> last [clientX, clientY]
    root.innerHTML = `
      <div class="tc-pad" role="group" aria-label="D-pad">
        ${ARROW('up', '▲')}${ARROW('right', '▶')}${ARROW('down', '▼')}${ARROW('left', '◀')}
      </div>
      <div class="tc-buttons">
        <button class="tc-btn b" aria-label="B">B</button>
        <button class="tc-btn a" aria-label="A">A</button>
      </div>
      <button class="tc-menu" aria-label="Menu">☰</button>`;
    this.pad = root.querySelector('.tc-pad');
    this.padBit = 0;
    this.padId = null;

    const buzz = () => { try { navigator.vibrate && navigator.vibrate(8); } catch { /* not allowed */ } };
    const setPad = (bit) => {
      if (bit === this.padBit) return;
      if (this.padBit) this.input.buttonUp(this.padBit);
      this.padBit = bit;
      if (bit) { this.input.buttonDown(bit); buzz(); }
      this.pad.dataset.dir = bit ? String(bit) : '';
    };
    const padAt = (e) => {
      const b = this.pad.getBoundingClientRect();
      const r = b.width / 2;
      setPad(padDirection(e.clientX - (b.left + r), e.clientY - (b.top + b.height / 2), r));
    };
    // the finger may slide off the pad: capture keeps its moves, and sliding round switches arrows
    this.pad.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      if (this.padId !== null) return;
      this.padId = e.pointerId;
      try { this.pad.setPointerCapture(e.pointerId); } catch { /* already gone */ }
      padAt(e);
    });
    this.pad.addEventListener('pointermove', (e) => { if (e.pointerId === this.padId) padAt(e); });
    const padEnd = (e) => {
      if (e.pointerId !== this.padId) return;
      this.padId = null;
      setPad(0);
    };
    this.pad.addEventListener('pointerup', padEnd);
    this.pad.addEventListener('pointercancel', padEnd);
    // only the pad's own: taking capture from the arrow under the finger fires one on the arrow, which bubbles
    this.pad.addEventListener('lostpointercapture', (e) => { if (e.target === this.pad) padEnd(e); });

    for (const [sel, bit] of [['.tc-btn.a', BTN.A], ['.tc-btn.b', BTN.B]]) {
      const el = root.querySelector(sel);
      let id = null;
      el.addEventListener('pointerdown', (e) => {
        e.preventDefault();
        if (id !== null) return;
        id = e.pointerId;
        try { el.setPointerCapture(e.pointerId); } catch { /* already gone */ }
        el.classList.add('down');
        this.input.buttonDown(bit);
        buzz();
      });
      const up = (e) => {
        if (e.pointerId !== id) return;
        id = null;
        el.classList.remove('down');
        this.input.buttonUp(bit);
      };
      el.addEventListener('pointerup', up);
      el.addEventListener('pointercancel', up);
      el.addEventListener('lostpointercapture', (e) => { if (e.target === el) up(e); });
      el.addEventListener('contextmenu', (e) => e.preventDefault());   // a long press (autopilot) is not a menu
    }
    root.querySelector('.tc-menu').addEventListener('click', () => this.on.menu());

    // the crank: fingers on the view itself
    canvas.addEventListener('pointerdown', (e) => {
      if (e.pointerType !== 'touch') return;
      this.on.touch();
      if (!this.enabled) return;
      this.dragging.set(e.pointerId, [e.clientX, e.clientY]);
      try { canvas.setPointerCapture(e.pointerId); } catch { /* already gone */ }
    });
    canvas.addEventListener('pointermove', (e) => {
      const p0 = this.dragging.get(e.pointerId);
      if (p0 === undefined) return;
      this.dragging.set(e.pointerId, [e.clientX, e.clientY]);
      this.input.mouseMove(e.clientY - p0[1], e.clientX - p0[0]);   // as the mouse: up or left one way, down or right the other
      document.body.classList.add('dragged');   // the hint has done its job
    });
    const crankEnd = (e) => this.dragging.delete(e.pointerId);
    canvas.addEventListener('pointerup', crankEnd);
    canvas.addEventListener('pointercancel', crankEnd);
    canvas.addEventListener('lostpointercapture', crankEnd);
  }

  // Shown while sailing (and for the go-back question); the menu screens take taps directly.
  show(play) {
    if (play === this.enabled) return;
    this.enabled = play;
    this.root.dataset.play = play ? '1' : '';
    if (!play) this.release();
  }

  // Focus lost or a panel opened over the game: nothing stays held.
  release() {
    if (this.padBit) this.input.buttonUp(this.padBit);
    this.padBit = 0;
    this.padId = null;
    this.pad.dataset.dir = '';
    this.dragging.clear();
    for (const el of this.root.querySelectorAll('.tc-btn.down')) el.classList.remove('down');
  }
}
