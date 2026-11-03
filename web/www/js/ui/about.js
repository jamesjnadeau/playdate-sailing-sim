// The About page, opened from the main menu: about.md, fetched once and rendered from Markdown.
// Escape, Backspace, the close button or a click outside the panel closes it.
import { renderMarkdown } from './markdown.js';

export class About {
  constructor(root, url, { onClose } = {}) {
    this.root = root;
    this.url = url;
    this.open = false;
    this.loaded = false;
    this.onClose = onClose || (() => {});
    root.innerHTML = `
      <div class="about-panel" role="dialog" aria-modal="true" aria-label="About">
        <button class="about-close" aria-label="Close">✕</button>
        <article class="about-body"></article>
      </div>`;
    this.body = root.querySelector('.about-body');
    root.querySelector('.about-close').addEventListener('click', () => this.close());
    root.addEventListener('mousedown', (e) => { if (e.target === root) this.close(); });
  }

  async show() {
    this.open = true;
    this.root.hidden = false;
    this.root.querySelector('.about-close').focus({ preventScroll: true });
    if (this.loaded) return;
    this.body.textContent = 'Loading…';
    try {
      const res = await fetch(this.url, { cache: 'no-cache' });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      this.body.innerHTML = renderMarkdown(await res.text());
      this.loaded = true;
    } catch (e) {
      this.body.textContent = `Could not load the About page (${e.message || e}).`;
    }
  }

  close() {
    if (!this.open) return;
    this.open = false;
    this.root.hidden = true;
    this.onClose();
  }
}
