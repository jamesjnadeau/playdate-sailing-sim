import test from 'node:test';
import assert from 'node:assert/strict';
import { Input, BTN, loadSettings, DEFAULTS } from '../www/js/input.js';

test('WASD and arrows are the d-pad, C is B and V is A', () => {
  const i = new Input();
  for (const [code, bit] of [['KeyW', BTN.UP], ['KeyA', BTN.LEFT], ['KeyS', BTN.DOWN], ['KeyD', BTN.RIGHT],
    ['ArrowUp', BTN.UP], ['KeyC', BTN.B], ['KeyV', BTN.A]]) {
    assert.equal(i.keyDown(code), true);
    assert.equal(i.take().pressed, bit, code);
    i.keyUp(code);
    i.take();
  }
  assert.equal(i.keyDown('KeyQ'), false);
});

test('a press shows once; holding keeps it in held only', () => {
  const i = new Input();
  i.keyDown('KeyC');
  let f = i.take();
  assert.equal(f.pressed, BTN.B);
  assert.equal(f.held, BTN.B);
  i.keyDown('KeyC', true);   // auto-repeat
  f = i.take();
  assert.equal(f.pressed, 0);
  assert.equal(f.held, BTN.B);
  i.keyUp('KeyC');
  assert.equal(i.take().held, 0);
});

test('a tap between two frames still counts as a press and as held that frame', () => {
  const i = new Input();
  i.keyDown('KeyV');
  i.keyUp('KeyV');
  const f = i.take();
  assert.equal(f.pressed, BTN.A);
  assert.equal(f.held, BTN.A);
  assert.equal(i.take().pressed, 0);
});

test('taps before a frame are handed out one per frame, in order', () => {
  const i = new Input();
  for (let k = 0; k < 3; k++) { i.keyDown('KeyV'); i.keyUp('KeyV'); }
  i.keyDown('KeyS'); i.keyUp('KeyS');
  assert.deepEqual([1, 2, 3, 4, 5].map(() => i.take().pressed), [BTN.A, BTN.A, BTN.A, BTN.DOWN, 0]);
});

test('a held key stays held while later presses queue', () => {
  const i = new Input();
  i.keyDown('KeyC');
  i.keyDown('KeyW'); i.keyUp('KeyW');
  assert.deepEqual(i.take(), { pressed: BTN.B, held: BTN.B, crank: 0 });
  assert.deepEqual(i.take(), { pressed: BTN.UP, held: BTN.B | BTN.UP, crank: 0 });
  assert.deepEqual(i.take(), { pressed: 0, held: BTN.B, crank: 0 });
  i.releaseAll();
  assert.deepEqual(i.take(), { pressed: 0, held: 0, crank: 0 });
});

test('mouse down turns the crank clockwise, scaled and invertible', () => {
  const i = new Input({ degPerPx: 0.5 });
  i.mouseMove(10);
  i.mouseMove(-4);
  assert.equal(i.take().crank, 3);
  assert.equal(i.take().crank, 0);
  const inv = new Input({ degPerPx: 1, invert: true });
  inv.mouseMove(5);
  assert.equal(inv.take().crank, -5);
  inv.mouseMove(NaN);
  assert.equal(inv.take().crank, 0);
});

test('mouse left turns the crank as up does, right as down', () => {
  const i = new Input({ degPerPx: 1 });
  i.mouseMove(0, -6);
  assert.equal(i.take().crank, -6);
  i.mouseMove(-6, 0);
  assert.equal(i.take().crank, -6);
  i.mouseMove(2, 5);
  assert.equal(i.take().crank, 7);
  const inv = new Input({ degPerPx: 1, invert: true });
  inv.mouseMove(0, -3);
  assert.equal(inv.take().crank, 3);
  inv.mouseMove(NaN, 4);
  assert.equal(inv.take().crank, -4);
});

test('wheel notches step the crank', () => {
  const i = new Input({ degPerWheel: 10 });
  i.wheel(120);
  i.wheel(-3);
  i.wheel(0);
  assert.equal(i.take().crank, 0);
  i.wheel(53);
  assert.equal(i.take().crank, 10);
});

test('settings fall back to defaults on bad or missing storage', () => {
  assert.deepEqual(loadSettings(null), DEFAULTS);
  assert.deepEqual(loadSettings({ getItem: () => '{bad' }), DEFAULTS);
  assert.deepEqual(loadSettings({ getItem: () => { throw new Error('blocked'); } }), DEFAULTS);
  assert.deepEqual(loadSettings({ getItem: () => '{"degPerPx":2,"invert":true,"degPerWheel":-1}' }),
    { degPerPx: 2, degPerWheel: DEFAULTS.degPerWheel, invert: true });
});
