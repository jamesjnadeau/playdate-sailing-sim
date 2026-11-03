// The sea: a camera-centred polar grid displaced in the vertex shader by the same sum of cosines the
// physics uses (src/sim/sea.c sea_view_height), so hulls and buoys sit exactly on the water they feel.
// Three frame: X = sim x, Y = -sim y, Z up; the camera is at the origin.
import * as THREE from 'three';
import { SUN_DIR } from './weather.js';

export const MAX_COMPONENTS = 6;
const GERSTNER_Q = 0.5;   // how far crests pinch: 0 is the plain cosine sum the physics uses, 1 a cusp

// Sea height (m) at sim-frame (x, y) relative to the camera, from the frame's components [kx, ky, ph, amp]*.
export function seaHeight(k, x, y) {
  let h = 0;
  for (let i = 0; i + 3 < k.length; i += 4) h += k[i + 3] * Math.cos(k[i] * x + k[i + 1] * y + k[i + 2]);
  return h;
}

function polarGrid(rings, segments, r0, r1) {
  const pos = [];
  const idx = [];
  pos.push(0, 0, 0);
  for (let i = 0; i < rings; i++) {
    const r = r0 * Math.pow(r1 / r0, i / (rings - 1));
    for (let j = 0; j < segments; j++) {
      const a = (j / segments) * Math.PI * 2;
      pos.push(r * Math.cos(a), r * Math.sin(a), 0);
    }
  }
  for (let j = 0; j < segments; j++) idx.push(0, 1 + j, 1 + ((j + 1) % segments));
  for (let i = 0; i < rings - 1; i++) {
    const a = 1 + i * segments, b = a + segments;
    for (let j = 0; j < segments; j++) {
      const j1 = (j + 1) % segments;
      idx.push(a + j, b + j, b + j1, a + j, b + j1, a + j1);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setIndex(idx);
  g.boundingSphere = new THREE.Sphere(new THREE.Vector3(), r1 * 2);
  return g;
}

// Shared GLSL: the swell at a rest point p0 (Three frame), summed over the components. Each component is
// weighted by how well the grid (vertex pass) or the pixel (fragment pass) can resolve its wavelength, so the
// short chop fades out with distance instead of aliasing. Besides the height it gives the gradient, the
// Gerstner displacement (crests pinch, troughs flatten), its Jacobian and a foam trail behind steep crests.
const swell = /* glsl */ `
uniform vec4 uK[${MAX_COMPONENTS}];
uniform int uN;
uniform float uQ;
const float PI = 3.14159265, TAU = 6.2831853;
struct Swell { float h; vec2 grad; vec2 disp; vec4 jac; float trail; };
// res: smallest wavelength weighted fully is hi, none below lo (metres)
Swell evalSwell(vec2 p0, float lo, float hi) {
  Swell s = Swell(0.0, vec2(0.0), vec2(0.0), vec4(0.0), 0.0);
  for (int i = 0; i < ${MAX_COMPONENTS}; i++) {
    if (i >= uN) break;
    vec4 k = uK[i];
    vec2 kv = vec2(k.x, -k.y);   // sim (x, y) = (X, -Y)
    float kl = length(kv);
    float w = smoothstep(lo, hi, TAU / max(kl, 1e-4));
    float a = dot(kv, p0) + k.z;
    float sa = sin(a), ca = cos(a);
    float A = k.w * w;
    s.h += A * ca;
    s.grad += -A * sa * kv;
    vec2 dir = kv / max(kl, 1e-4);
    float q = uQ * A;
    s.disp += -q * sa * dir;
    s.jac += q * ca * vec4(dir.x * kv.x, dir.x * kv.y, dir.y * kv.x, dir.y * kv.y);
    // foam trails behind the crest of a component steep enough to break: the point has just seen the crest
    // when the phase has wrapped to just below 0, and the foam fades over about half a wavelength
    float brk = smoothstep(0.05, 0.13, A * kl);
    float wph = mod(a + PI, TAU) - PI;
    float tr = wph <= 0.0 ? exp(wph * 0.9) : smoothstep(0.5, 0.0, wph);
    s.trail += brk * tr;
  }
  return s;
}`;

const vertex = /* glsl */ `
${swell}
varying vec2 vRest;
varying vec3 vWorld;
void main() {
  vec2 p0 = position.xy;
  float sp = max(length(p0) * 0.0449, 0.05);   // the grid's radial spacing here
  Swell s = evalSwell(p0, 3.0 * sp, 8.0 * sp);
  vec3 p = vec3(p0 + s.disp, s.h);
  vRest = p0;
  vWorld = p;
  gl_Position = projectionMatrix * viewMatrix * vec4(p, 1.0);
}`;

const fragment = /* glsl */ `
${swell}
uniform vec3 uSkyTop, uSkyHorizon, uDeep, uFog;
uniform vec3 uCamPos;
uniform vec2 uOrigin, uWind;       // uWind: unit direction the wind blows toward, Three frame
uniform float uVis, uSun, uTime, uAmp, uFoam, uWindSpeed;
varying vec2 vRest;
varying vec3 vWorld;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
// fraction of a noise of this spatial frequency (cycles per metre) that a pixel of footprint fp still resolves
float resolved(float freq, float fp) { return 1.0 - smoothstep(0.3 / freq, 0.9 / freq, fp); }
// gradient of a wind-stretched noise layer: long streaks along the wind, short across it
vec2 streakGrad(vec2 u, vec2 stretch, vec2 drift) {
  float e = 0.12;
  vec2 q = u * stretch + drift;
  float n0 = noise(q);
  return vec2(noise(q + vec2(e * stretch.x, 0.0)) - n0, noise(q + vec2(0.0, e * stretch.y)) - n0) / e;
}
void main() {
  vec3 toEye = uCamPos - vWorld;
  float dist = length(toEye);
  vec3 v = toEye / dist;
  vec2 dx = dFdx(vRest), dy = dFdy(vRest);
  float fp = max(length(dx), length(dy));          // metres of sea one pixel covers
  Swell s = evalSwell(vRest, 2.0 * fp, 5.0 * fp);
  // slope in the displaced surface's frame: solve J^T slope = grad
  float det = max(1.0 - s.jac.x - s.jac.w + s.jac.x * s.jac.w - s.jac.y * s.jac.z, 0.2);
  vec2 g = s.grad;
  vec2 slope = vec2((1.0 - s.jac.w) * g.x + s.jac.z * g.y, s.jac.y * g.x + (1.0 - s.jac.x) * g.y) / det;
  float height = s.h;
  // the camera sits at the origin, so anchor the noise to the real sea position: it slides past as the boat moves
  vec2 wp = vRest + uOrigin;
  vec2 wd = uWind, wn = vec2(-uWind.y, uWind.x);
  vec2 u = vec2(dot(wp, wd), dot(wp, wn));
  float windy = clamp(uWindSpeed / 12.0, 0.15, 1.5);
  // capillary ripples: a wind-aligned streaky layer plus a fine isotropic one, each faded when a pixel can't resolve it
  vec2 rp = streakGrad(u, vec2(0.35, 1.7), vec2(-uTime * 0.25 * windy, 0.0)) * 0.05 * resolved(1.0, fp)
          + streakGrad(u, vec2(1.2, 3.4), vec2(-uTime * 0.6 * windy, uTime * 0.15)) * 0.03 * resolved(2.5, fp);
  rp *= windy;
  vec3 n = normalize(vec3(-slope.x - rp.x, -slope.y - rp.y, 1.0));
  float fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
  vec3 r = reflect(-v, n);
  vec3 sky = mix(uSkyHorizon, uSkyTop, pow(clamp(r.z, 0.0, 1.0), 0.6));
  vec3 sun = normalize(vec3(${SUN_DIR.x.toFixed(4)}, ${SUN_DIR.y.toFixed(4)}, ${SUN_DIR.z.toFixed(4)}));
  float diffuse = 0.55 + 0.45 * max(dot(n, sun), 0.0);
  // light through the crests: thin water facing the sun glows green-blue
  float sss = clamp(height / max(uAmp, 0.05), 0.0, 1.0) * 0.35;
  vec3 body = uDeep * diffuse + vec3(0.02, 0.12, 0.12) * sss;
  vec3 col = mix(body, sky, fres);

  // sun: a glitter field of tiny glints that twinkle while the facets are resolved, and a broad lobe standing
  // in for the glints once they are too small to see (so the highlight doesn't vanish with distance)
  float glintRes = resolved(4.0, fp);
  vec2 gp = wp * 4.0;
  vec2 tilt = vec2(noise(gp + uTime * vec2(0.9, 0.5)) - 0.5, noise(gp.yx * 1.13 - uTime * vec2(0.7, 0.8)) - 0.5);
  vec3 ng = normalize(n + vec3(tilt, 0.0) * 0.9 * windy * glintRes);
  vec3 rg = reflect(-v, ng);
  float glint = pow(max(dot(rg, sun), 0.0), 900.0) * 14.0 * glintRes;
  float lobe = pow(max(dot(r, sun), 0.0), 220.0) * 2.5 + pow(max(dot(r, sun), 0.0), 40.0) * 0.35 * (1.0 - glintRes);
  col += vec3(1.0, 0.95, 0.85) * (glint + lobe) * uSun;

  // whitecaps: foam trails behind steep crests, drifting downwind in streaks, broken up by noise
  float steep = length(slope);
  float crest = smoothstep(0.55, 1.0, height / max(uAmp, 0.05));
  float fn1 = noise(vec2(u.x * 0.22 - uTime * 0.05 * windy, u.y * 1.1));
  float fn2 = noise(vec2(u.x * 0.45, u.y * 2.6) + uTime * vec2(-0.12, 0.05));
  float fn3 = noise(vec2(u.x * 1.4, u.y * 5.0) - uTime * vec2(0.2, 0.0));
  float res2 = resolved(1.0, fp);
  float foamN = mix(0.4, fn1 * (0.45 + 0.9 * fn2), res2) * mix(1.0, 0.55 + 0.9 * fn3, resolved(3.0, fp));
  float amount = (0.35 * crest + smoothstep(0.3, 0.7, steep) + 0.6 * s.trail) * uFoam;
  float foam = smoothstep(0.22, 0.65, amount * (0.25 + 1.5 * foamN)) * 0.88;
  col = mix(col, vec3(0.97, 0.985, 1.0) * (0.78 + 0.22 * diffuse), foam);

  // haze: fog to the horizon colour over the preset's visibility
  float fog = 1.0 - exp(-pow(dist / max(uVis, 1.0) * 1.6, 1.4));
  col = mix(col, uFog, clamp(fog, 0.0, 1.0));
  gl_FragColor = vec4(col, 1.0);
  #include <colorspace_fragment>
}`;

export class Sea {
  constructor() {
    this.uniforms = {
      uK: { value: Array.from({ length: MAX_COMPONENTS }, () => new THREE.Vector4()) },
      uN: { value: 0 },
      uSkyTop: { value: new THREE.Color() },
      uSkyHorizon: { value: new THREE.Color() },
      uDeep: { value: new THREE.Color() },
      uFog: { value: new THREE.Color() },
      uCamPos: { value: new THREE.Vector3() },
      uOrigin: { value: new THREE.Vector2() },
      uWind: { value: new THREE.Vector2(1, 0) },
      uWindSpeed: { value: 5 },
      uQ: { value: 0 },
      uVis: { value: 1000 },
      uSun: { value: 1 },
      uTime: { value: 0 },
      uAmp: { value: 1 },
      uFoam: { value: 0.5 },
    };
    this.material = new THREE.ShaderMaterial({ vertexShader: vertex, fragmentShader: fragment, uniforms: this.uniforms });
    this.mesh = new THREE.Mesh(polarGrid(220, 384, 0.4, 6000), this.material);
    this.mesh.frustumCulled = false;
    this.mesh.renderOrder = -1;
  }

  // k: the frame's [kx, ky, ph, amp]* list; pal: weather.palette(); preset: the weather preset.
  // camX, camY: the camera's sim-frame world position (the mesh itself stays centred on the camera).
  // wind: { dir (rad, blowing toward, sim frame), speed }
  update(k, camZ, pal, preset, t, camX = 0, camY = 0, wind = null) {
    const u = this.uniforms;
    const wd = wind ? wind.dir : 0;
    u.uWind.value.set(Math.cos(wd), -Math.sin(wd));   // Three Y = -sim y
    u.uWindSpeed.value = wind ? wind.speed : preset.wind;
    u.uOrigin.value.set(camX, -camY);   // Three Y = -sim y
    const n = Math.min(MAX_COMPONENTS, Math.floor(k.length / 4));
    let amp = 0, steep = 0;
    for (let i = 0; i < n; i++) {
      u.uK.value[i].set(k[4 * i], k[4 * i + 1], k[4 * i + 2], k[4 * i + 3]);
      amp += Math.abs(k[4 * i + 3]);
      steep += Math.abs(k[4 * i + 3]) * Math.hypot(k[4 * i], k[4 * i + 1]);
    }
    u.uQ.value = Math.min(GERSTNER_Q, 0.8 / Math.max(steep, 1e-3));   // never let the surface fold over itself
    u.uN.value = n;
    u.uAmp.value = amp;
    u.uCamPos.value.set(0, 0, camZ);
    u.uSkyTop.value.copy(pal.skyTop);
    u.uSkyHorizon.value.copy(pal.skyHorizon);
    u.uDeep.value.copy(pal.deep);
    u.uFog.value.copy(pal.fog);
    u.uVis.value = preset.visibility;
    u.uSun.value = pal.sun;
    u.uTime.value = t;
    u.uFoam.value = Math.min(1, Math.max(0, (preset.wind - 4) / 14));
  }
}
