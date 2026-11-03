// The mouse in the main menu. The menu itself is the C Menu state (src/sim/menu.c), shared with the Playdate,
// so the mouse does not set it directly: it asks for a row (and maybe a button), and each frame this hands the
// game the next button press that gets there, UP or DOWN until the row is selected, then the button.
import { BTN } from '../input.js';

export class MenuPointer {
  constructor() { this.want = null; }

  // Point at a row (hover), or choose it with press = BTN.A (click). row null presses the button at once.
  request(screen, row, press = 0) {
    this.want = { screen, row, press, steps: 0, lastSel: -1 };
  }

  cancel() { this.want = null; }

  // The button to press this frame for menu state m ({ screen, sel, rows }), or 0.
  next(m) {
    const w = this.want;
    if (!w) return 0;
    if (!m || m.screen !== w.screen) { this.want = null; return 0; }   // the screen moved on without us
    const n = m.rows ? m.rows.length : 0;
    if (w.row !== null && w.row !== undefined && m.sel !== w.row) {
      if (w.row < 0 || w.row >= n || !m.rows[w.row][1] || w.steps > 2 * n) { this.want = null; return 0; }
      w.steps++;
      // the shorter way round; the list wraps at both ends
      const down = (w.row - m.sel + n) % n;
      return down <= n - down ? BTN.DOWN : BTN.UP;
    }
    this.want = null;
    return w.press || 0;
  }
}
