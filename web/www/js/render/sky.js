// Sky dome with sun and drifting clouds, lightning flashes and rain streaks, all from the weather preset.
import * as THREE from 'three';
import { SUN_DIR } from './weather.js';

const vertex = /* glsl */ `
varying vec3 vDir;
void main() {
  vDir = normalize(position);
  vec4 p = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
  gl_Position = p.xyww;   // on the far plane
}`;

const fragment = /* glsl */ `
uniform vec3 uTop, uHorizon, uCloud, uFog;
uniform float uSun, uCover, uSize, uTime, uSpeed, uFlash, uVis;
varying vec3 vDir;
float hash(vec2 p) { return fract(sin(dot(p, vec2(41.3, 289.1))) * 43758.5453); }
float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) {
  float a = 0.5, s = 0.0;
  for (int i = 0; i < 5; i++) { s += a * noise(p); p = p * 2.03 + vec2(1.7, 9.2); a *= 0.5; }
  return s;
}
void main() {
  vec3 d = normalize(vDir);
  float up = clamp(d.z, 0.0, 1.0);
  vec3 col = mix(uHorizon, uTop, pow(up, 0.55));
  vec3 sun = normalize(vec3(${SUN_DIR.x.toFixed(4)}, ${SUN_DIR.y.toFixed(4)}, ${SUN_DIR.z.toFixed(4)}));
  float sd = max(dot(d, sun), 0.0);
  col += vec3(1.0, 0.92, 0.75) * (pow(sd, 900.0) * 4.0 + pow(sd, 12.0) * 0.25) * uSun;
  // clouds on a flat layer above the sea, seen in perspective
  if (d.z > 0.01) {
    vec2 uv = d.xy / (d.z + 0.08) * (0.9 / max(uSize, 0.2)) + vec2(uTime * 0.004 * uSpeed, uTime * 0.0015 * uSpeed);
    float n = fbm(uv * 1.6);
    float cover = smoothstep(1.0 - uCover * 0.9 - 0.1, 1.05 - uCover * 0.45, n);
    float shade = 0.75 + 0.25 * fbm(uv * 3.1 + 4.0);
    vec3 cloud = uCloud * shade + vec3(1.0, 0.95, 0.85) * pow(sd, 4.0) * 0.25 * uSun;
    col = mix(col, cloud, cover * smoothstep(0.0, 0.12, d.z));
  }
  col = mix(col, uFog, smoothstep(0.12, 0.0, d.z) * clamp(600.0 / max(uVis, 50.0), 0.0, 1.0));
  col += vec3(0.75, 0.8, 1.0) * uFlash;
  gl_FragColor = vec4(col, 1.0);
  #include <colorspace_fragment>
}`;

const RAIN = 1400;

export class Sky {
  constructor() {
    this.uniforms = {
      uTop: { value: new THREE.Color() }, uHorizon: { value: new THREE.Color() },
      uCloud: { value: new THREE.Color() }, uFog: { value: new THREE.Color() },
      uSun: { value: 1 }, uCover: { value: 0.3 }, uSize: { value: 1 }, uTime: { value: 0 },
      uSpeed: { value: 1 }, uFlash: { value: 0 }, uVis: { value: 1000 },
    };
    this.dome = new THREE.Mesh(
      new THREE.SphereGeometry(5000, 48, 24),
      new THREE.ShaderMaterial({ vertexShader: vertex, fragmentShader: fragment, uniforms: this.uniforms,
        side: THREE.BackSide, depthWrite: false }),
    );
    this.dome.frustumCulled = false;
    this.dome.renderOrder = -2;

    // rain: short streaks in a box around the camera, falling and leaning with the wind
    const pos = new Float32Array(RAIN * 6);
    this.rainSeed = new Float32Array(RAIN * 3);
    for (let i = 0; i < RAIN * 3; i++) this.rainSeed[i] = Math.random();
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    this.rain = new THREE.LineSegments(g, new THREE.LineBasicMaterial({ color: 0xc8d2dc, transparent: true, opacity: 0.45 }));
    this.rain.frustumCulled = false;
    this.flash = 0;
    this.nextFlash = 5;
  }

  // wind: { dir (rad, blowing toward, sim frame), speed }; dt for the lightning and rain.
  update(pal, preset, t, dt, camZ, wind) {
    const u = this.uniforms;
    u.uTop.value.copy(pal.skyTop);
    u.uHorizon.value.copy(pal.skyHorizon);
    u.uCloud.value.copy(pal.cloudColor);
    u.uFog.value.copy(pal.fog);
    u.uSun.value = pal.sun;
    u.uCover.value = preset.cloudCover;
    u.uSize.value = preset.cloudSize || 1;
    u.uSpeed.value = preset.cloudSpeed || 1;
    u.uTime.value = t;
    u.uVis.value = preset.visibility;

    // lightning: a quick double flash every lightning_interval seconds on average
    this.flash = Math.max(0, this.flash - dt * 4);
    if (preset.lightning > 0) {
      this.nextFlash -= dt;
      if (this.nextFlash <= 0) {
        this.flash = 1;
        this.nextFlash = preset.lightning * (0.3 + 1.4 * Math.random());
      }
    }
    const flicker = this.flash > 0.5 ? this.flash : this.flash * (0.5 + 0.5 * Math.sin(t * 60));
    u.uFlash.value = flicker * 0.6;
    this.flashLevel = flicker;

    const n = Math.floor(RAIN * Math.min(1, Math.max(0, preset.rain)));
    this.rain.visible = n > 0;
    if (!n) return;
    const p = this.rain.geometry.attributes.position.array;
    const wx = Math.cos(wind.dir) * wind.speed * 0.04, wy = -Math.sin(wind.dir) * wind.speed * 0.04;
    const fall = 9;
    for (let i = 0; i < RAIN; i++) {
      const o = i * 6;
      if (i >= n) { p.fill(0, o, o + 6); continue; }
      const s = this.rainSeed;
      const x = (s[3 * i] - 0.5) * 60, y = (s[3 * i + 1] - 0.5) * 60;
      const z = camZ + 25 - ((s[3 * i + 2] * 30 + t * fall) % 30);
      p[o] = x; p[o + 1] = y; p[o + 2] = z;
      p[o + 3] = x + wx; p[o + 4] = y + wy; p[o + 5] = z - 0.9;
    }
    this.rain.geometry.attributes.position.needsUpdate = true;
  }
}
