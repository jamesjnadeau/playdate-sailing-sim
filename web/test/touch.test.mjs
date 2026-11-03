import test from 'node:test';
import assert from 'node:assert/strict';
import { padDirection, DEAD } from '../www/js/touch.js';
import { Input, BTN } from '../www/js/input.js';

test('the pad picks the nearest arrow, with a dead zone in the middle', () => {
  const r = 100;
  assert.equal(padDirection(80, 0, r), BTN.RIGHT);
  assert.equal(padDirection(-80, 10, r), BTN.LEFT);
  assert.equal(padDirection(10, -80, r), BTN.UP);
  assert.equal(padDirection(-20, 90, r), BTN.DOWN);
  assert.equal(padDirection(DEAD * r - 1, 0, r), 0);
  assert.equal(padDirection(150, 0, r), BTN.RIGHT);   // slid off the pad, still pointing right
  assert.equal(padDirection(NaN, 0, r), 0);
});

test('on-screen buttons press once and hold until lifted', () => {
  const i = new Input();
  i.buttonDown(BTN.A);
  i.buttonDown(BTN.A);   // a second finger on the same button is not a second press
  assert.deepEqual(i.take(), { pressed: BTN.A, held: BTN.A, crank: 0 });
  assert.deepEqual(i.take(), { pressed: 0, held: BTN.A, crank: 0 });
  i.buttonUp(BTN.A);
  i.buttonDown(BTN.RIGHT);
  i.buttonUp(BTN.RIGHT);
  i.buttonDown(BTN.UP);   // sliding round the pad: each arrow is a press of its own
  assert.deepEqual(i.take(), { pressed: BTN.RIGHT, held: BTN.RIGHT | BTN.UP, crank: 0 });
  assert.deepEqual(i.take(), { pressed: BTN.UP, held: BTN.UP, crank: 0 });
  i.buttonDown(0);
  assert.deepEqual(i.take(), { pressed: 0, held: BTN.UP, crank: 0 });
});
