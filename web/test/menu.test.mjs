import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { renderMarkdown, inline } from '../www/js/ui/markdown.js';
import { MenuPointer } from '../www/js/ui/menupointer.js';
import { BTN } from '../www/js/input.js';

test('markdown: headings, paragraphs, lists, code, rules and quotes', () => {
  const html = renderMarkdown('# Title\n\nOne\nline.\n\n## Sub\n- a\n- b\n  wrapped\n\n1. x\n2. y\n\n```\n<b>\n```\n\n---\n\n> said');
  assert.equal(html, [
    '<h1>Title</h1>', '<p>One line.</p>', '<h2>Sub</h2>', '<ul><li>a</li><li>b wrapped</li></ul>',
    '<ol><li>x</li><li>y</li></ol>', '<pre><code>&lt;b&gt;</code></pre>', '<hr>', '<blockquote><p>said</p></blockquote>',
  ].join('\n'));
});

test('markdown: inline spans', () => {
  assert.equal(inline('**bold** and *it* and _it_ and `a*b*`'), '<strong>bold</strong> and <em>it</em> and <em>it</em> and <code>a*b*</code>');
  assert.equal(inline('snake_case_name stays'), 'snake_case_name stays');
  assert.equal(inline('[site](https://x.org/?a=1&b=2)'), '<a href="https://x.org/?a=1&amp;b=2" target="_blank" rel="noopener noreferrer">site</a>');
  assert.equal(inline('[local](help.md)'), '<a href="help.md">local</a>');
});

test('markdown: escapes HTML and drops script links', () => {
  assert.equal(inline('<img src=x onerror=alert(1)>'), '&lt;img src=x onerror=alert(1)&gt;');
  assert.equal(inline('[x](javascript:alert(1))'), 'x)');
  assert.equal(inline('[x](" onclick="y)'), '[x](&quot; onclick=&quot;y)');
});

test('the About page renders', () => {
  const html = renderMarkdown(readFileSync(new URL('../www/about.md', import.meta.url), 'utf8'));
  assert.match(html, /^<h1>About Crank Sail<\/h1>/);
  assert.doesNotMatch(html, /<script/i);
});

const rows = (n, off = []) => Array.from({ length: n }, (_, i) => [`r${i}`, !off.includes(i)]);

test('pointer: steps the shorter way to the row, then presses', () => {
  const p = new MenuPointer();
  const m = { screen: 0, sel: 0, rows: rows(5) };
  p.request(0, 4, BTN.A);
  assert.equal(p.next(m), BTN.UP);   // 0 -> 4 is one step up, wrapping
  m.sel = 4;
  assert.equal(p.next(m), BTN.A);
  assert.equal(p.next(m), 0);
  p.request(0, 2, 0);
  assert.equal(p.next(m), BTN.UP);
  m.sel = 3;
  assert.equal(p.next(m), BTN.UP);
  m.sel = 2;
  assert.equal(p.next(m), 0);
  m.sel = 0;
  assert.equal(p.next(m), 0, 'done: does not chase the row again');
  p.request(0, 2, 0);
  assert.equal(p.next(m), BTN.DOWN);
});

test('pointer: gives up on a new screen, a disabled row or a row it cannot reach', () => {
  const p = new MenuPointer();
  p.request(0, 1, BTN.A);
  assert.equal(p.next({ screen: 1, sel: 0, rows: rows(3) }), 0);
  p.request(3, 1, BTN.A);
  assert.equal(p.next({ screen: 3, sel: 0, rows: rows(3, [1]) }), 0);
  p.request(3, 2, 0);
  const stuck = { screen: 3, sel: 0, rows: rows(3) };
  let n = 0;
  while (p.next(stuck) && n < 50) n++;
  assert.ok(n <= 7, `stopped after ${n} steps`);
});

test('pointer: a bare press goes out at once', () => {
  const p = new MenuPointer();
  p.request(2, null, BTN.B);
  assert.equal(p.next({ screen: 2, sel: 0, rows: [] }), BTN.B);
});
