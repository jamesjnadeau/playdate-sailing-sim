// The sea: a camera-centred polar grid displaced in the vertex shader by the same sum of cosines the
// physics uses (src/sim/sea.c sea_view_height), so hulls and buoys sit exactly on the water they feel.
// Three frame: X = sim x, Y = -sim y, Z up; the camera is at the origin.
import * as THREE from 'three';
import { SUN_DIR } from './weather.js';

export const MAX_COMPONENTS = 6;

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

const vertex = /* glsl */ `
uniform vec4 uK[${MAX_COMPONENTS}];
uniform int uN;
varying vec3 vWorld;
varying vec2 vSlope;
varying float vHeight;
void main() {
  vec3 p = position;
  float h = 0.0;
  vec2 d = vec2(0.0);
  for (int i = 0; i < ${MAX_COMPONENTS}; i++) {
    if (i >= uN) break;
    vec4 k = uK[i];
    // sim (x, y) = (X, -Y)
    float a = k.x * p.x - k.y * p.y + k.z;
    h += k.w * cos(a);
    float s = -k.w * sin(a);
    d += vec2(s * k.x, -s * k.y);
  }
  p.z = h;
  vWorld = p;
  vSlope = d;
  vHeight = h;
  gl_Position = projectionMatrix * viewMatrix * vec4(p, 1.0);
}`;

const fragment = /* glsl */ `
uniform vec3 uSkyTop, uSkyHorizon, uDeep, uFog;
uniform vec3 uCamPos;
uniform vec2 uOrigin;
uniform float uVis, uSun, uTime, uAmp, uFoam;
varying vec3 vWorld;
varying vec2 vSlope;
varying float vHeight;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
// small ripples on top of the swell, only for the shading
vec2 ripple(vec2 p, float t) {
  float e = 0.15;
  float n0 = noise(p * 0.9 + vec2(t * 0.35, t * 0.2)) + 0.5 * noise(p * 2.3 - vec2(t * 0.5, -t * 0.3));
  float nx = noise((p + vec2(e, 0)) * 0.9 + vec2(t * 0.35, t * 0.2)) + 0.5 * noise((p + vec2(e, 0)) * 2.3 - vec2(t * 0.5, -t * 0.3));
  float ny = noise((p + vec2(0, e)) * 0.9 + vec2(t * 0.35, t * 0.2)) + 0.5 * noise((p + vec2(0, e)) * 2.3 - vec2(t * 0.5, -t * 0.3));
  return vec2(nx - n0, ny - n0) / e;
}
void main() {
  vec3 toEye = uCamPos - vWorld;
  float dist = length(toEye);
  vec3 v = toEye / dist;
  float near = clamp(1.0 - dist / 120.0, 0.0, 1.0);
  // the camera sits at the origin, so anchor the noise to the real sea position: it slides past as the boat moves
  vec2 wp = vWorld.xy + uOrigin;
  vec2 rp = ripple(wp, uTime) * 0.08 * near;
  vec3 n = normalize(vec3(-vSlope.x - rp.x, -vSlope.y - rp.y, 1.0));
  float fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
  vec3 r = reflect(-v, n);
  vec3 sky = mix(uSkyHorizon, uSkyTop, pow(clamp(r.z, 0.0, 1.0), 0.6));
  vec3 sun = normalize(vec3(${SUN_DIR.x.toFixed(4)}, ${SUN_DIR.y.toFixed(4)}, ${SUN_DIR.z.toFixed(4)}));
  float diffuse = 0.55 + 0.45 * max(dot(n, sun), 0.0);
  // light through the crests: thin water facing the sun glows green-blue
  float sss = clamp(vHeight / max(uAmp, 0.05), 0.0, 1.0) * 0.35;
  vec3 body = uDeep * diffuse + vec3(0.02, 0.12, 0.12) * sss;
  vec3 col = mix(body, sky, fres);
  float spec = pow(max(dot(r, sun), 0.0), 420.0) * 6.0 * uSun;
  col += vec3(1.0, 0.95, 0.85) * spec;
  // whitecaps on steep crests, broken up by noise
  float steep = length(vSlope);
  float crest = smoothstep(0.55, 1.0, vHeight / max(uAmp, 0.05));
  float foamN = noise(wp * 0.7 + uTime * 0.25) * noise(wp * 2.1 - uTime * 0.3);
  float foam = clamp((crest * 0.8 + smoothstep(0.25, 0.6, steep)) * uFoam * smoothstep(0.15, 0.45, foamN), 0.0, 0.85);
  col = mix(col, vec3(0.93, 0.96, 0.98) * diffuse, foam);
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
  update(k, camZ, pal, preset, t, camX = 0, camY = 0) {
    const u = this.uniforms;
    u.uOrigin.value.set(camX, -camY);   // Three Y = -sim y
    const n = Math.min(MAX_COMPONENTS, Math.floor(k.length / 4));
    let amp = 0;
    for (let i = 0; i < n; i++) {
      u.uK.value[i].set(k[4 * i], k[4 * i + 1], k[4 * i + 2], k[4 * i + 3]);
      amp += Math.abs(k[4 * i + 3]);
    }
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
