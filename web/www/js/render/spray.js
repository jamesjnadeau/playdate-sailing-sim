// Spindrift: droplets torn off breaking crests when the wind is up, blowing downwind and settling. Particles live in
// a box that wraps around the camera in sea coordinates, so the sea doesn't pop as the boat moves; each only shows
// while the water under it is a crest. Positions are camera-relative sim-frame (x, y, z up), drawn in the Three frame.
import * as THREE from 'three';
import { seaHeight } from './sea.js';

const COUNT = 4000;
const BOX = 110;        // m, the wrap period
const REACH = 50;       // m, the visible radius (fades out before the wrap seam)
const RATE = 0.7;       // lifetimes per second
const MIN_WIND = 7;     // below this the crests only foam

const vertex = /* glsl */ `
attribute float aAlpha;
uniform float uPx;      // pixels per metre at 1 m distance
varying float vA;
void main() {
  vec4 mv = modelViewMatrix * vec4(position, 1.0);
  vA = aAlpha;
  gl_PointSize = clamp(0.3 * uPx / max(-mv.z, 0.5), 1.0, 14.0);
  gl_Position = projectionMatrix * mv;
}`;

const fragment = /* glsl */ `
uniform vec3 uColor;
varying float vA;
void main() {
  float d = length(gl_PointCoord - 0.5) * 2.0;
  float a = (1.0 - smoothstep(0.35, 1.0, d)) * vA;
  if (a < 0.01) discard;
  gl_FragColor = vec4(uColor, a);
  #include <colorspace_fragment>
}`;

// cheap integer hash -> [0, 1)
function hash(i, c, s) {
  let h = Math.imul(i * 374761393 + c * 668265263 + s * 1274126177, 0x85ebca6b);
  h ^= h >>> 13;
  h = Math.imul(h, 0xc2b2ae35);
  h ^= h >>> 16;
  return (h >>> 0) / 4294967296;
}
const wrap = (v) => v - BOX * Math.floor(v / BOX + 0.5);

export class Spray {
  constructor() {
    this.pos = new Float32Array(COUNT * 3);
    this.alpha = new Float32Array(COUNT);
    this.seed = new Float32Array(COUNT);
    for (let i = 0; i < COUNT; i++) this.seed[i] = hash(i, 0, 7);
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(this.pos, 3));
    g.setAttribute('aAlpha', new THREE.BufferAttribute(this.alpha, 1));
    g.boundingSphere = new THREE.Sphere(new THREE.Vector3(), 1e4);
    this.uniforms = { uPx: { value: 500 }, uColor: { value: new THREE.Color(0xeaf2f6) } };
    this.points = new THREE.Points(g, new THREE.ShaderMaterial({
      vertexShader: vertex, fragmentShader: fragment, uniforms: this.uniforms,
      transparent: true, depthWrite: false,
    }));
    this.points.frustumCulled = false;
    this.points.renderOrder = 2;
    this.points.visible = false;
  }

  // k: the frame's sea components; camX/camY/camZ: camera sim position;
  // wind: { dir, speed }; pxScale: projection scale in pixels (half the drawing-buffer height * proj[1][1]).
  update(k, camX, camY, camZ, t, wind, pxScale, fogColor) {
    const amount = Math.min(1, Math.max(0, (wind.speed - MIN_WIND) / 10));
    this.points.visible = amount > 0;
    if (!amount) return;
    this.uniforms.uPx.value = pxScale;
    this.uniforms.uColor.value.copy(fogColor).lerp(new THREE.Color(1, 1, 1), 0.7);
    let amp = 0;
    for (let i = 3; i < k.length; i += 4) amp += Math.abs(k[i]);
    amp = Math.max(amp, 0.05);
    const wx = Math.cos(wind.dir), wy = Math.sin(wind.dir);
    const drift = 0.25 * wind.speed;                 // m/s the droplets blow along
    const shown = Math.floor(COUNT * amount);
    for (let i = 0; i < COUNT; i++) {
      const o = 3 * i;
      if (i >= shown) { this.alpha[i] = 0; continue; }
      const ph = t * RATE * (0.8 + 0.4 * this.seed[i]) + this.seed[i] * 17;
      const c = Math.floor(ph), u = ph - c;
      const bx = hash(i, c, 1) * BOX, by = hash(i, c, 2) * BOX;
      const life = 1 / RATE;
      const rx = wrap(bx + wx * drift * u * life - camX);
      const ry = wrap(by + wy * drift * u * life - camY);
      const r = Math.hypot(rx, ry);
      const h = seaHeight(k, rx, ry);
      const crest = Math.min(1, Math.max(0, (h / amp - 0.12) / 0.3));
      // up and over: a short arc above the crest, a little faster to rise than to fall
      const z = h + 0.15 + 1.0 * Math.sqrt(this.seed[i] + 0.2) * 4 * u * (1 - u);
      const fade = Math.min(1, u * 6) * (1 - u) * Math.min(1, Math.max(0, (REACH - r) / 15));
      this.pos[o] = rx;
      this.pos[o + 1] = -ry;
      this.pos[o + 2] = z;
      this.alpha[i] = crest * fade;
    }
    const g = this.points.geometry.attributes;
    g.position.needsUpdate = true;
    g.aAlpha.needsUpdate = true;
  }
}
