// Buoys, the next mark's gate line and turn arrow, rocks and the wake. Sizes follow render.c.
import * as THREE from 'three';
import { seaHeight } from './sea.js';

const BUOY_TOP_M = 2.5, BUOY_HALF_M = 0.8;
const GATE_LEN_M = 40, GATE_HEIGHT_M = 2, GATE_DASH_M = 1;
const ARROW_SWEEP = 1 / 3, ARROW_RADIUS_M = 8, ARROW_HEAD_M = 2.5;
const MAX_DASHES = 160;

function buoyMesh(color) {
  const g = new THREE.Group();
  const body = new THREE.Mesh(new THREE.CylinderGeometry(BUOY_HALF_M * 0.55, BUOY_HALF_M, 1.1, 20),
    new THREE.MeshStandardMaterial({ color, roughness: 0.45 }));
  body.rotation.x = Math.PI / 2;
  body.position.z = 0.3;
  const top = new THREE.Mesh(new THREE.ConeGeometry(BUOY_HALF_M * 0.55, BUOY_TOP_M - 0.85, 20),
    new THREE.MeshStandardMaterial({ color, roughness: 0.45 }));
  top.rotation.x = Math.PI / 2;
  top.position.z = 0.85 + (BUOY_TOP_M - 0.85) / 2;
  const band = new THREE.Mesh(new THREE.CylinderGeometry(BUOY_HALF_M * 0.62, BUOY_HALF_M * 0.72, 0.22, 20),
    new THREE.MeshStandardMaterial({ color: 0xf4f4f0, roughness: 0.5 }));
  band.rotation.x = Math.PI / 2;
  band.position.z = 0.55;
  g.add(body, top, band);
  return g;
}

function rockGeometry(seed) {
  const g = new THREE.IcosahedronGeometry(1, 2);
  const p = g.attributes.position;
  let s = seed * 9301 + 49297;
  const rnd = () => ((s = (s * 9301 + 49297) % 233280) / 233280);
  const bumps = Array.from({ length: 6 }, () => [rnd() * 2 - 1, rnd() * 2 - 1, rnd() * 2 - 1, 0.15 + rnd() * 0.25]);
  for (let i = 0; i < p.count; i++) {
    const v = new THREE.Vector3().fromBufferAttribute(p, i);
    let f = 1;
    for (const [x, y, z, a] of bumps) f += a * Math.max(0, v.dot(new THREE.Vector3(x, y, z).normalize()));
    v.multiplyScalar(f * 0.8);
    p.setXYZ(i, v.x, v.y, v.z);
  }
  g.computeVertexNormals();
  return g;
}

export class World {
  constructor(scene) {
    this.group = new THREE.Group();
    scene.add(this.group);
    this.buoys = [];
    this.buoyNext = buoyMesh(0xffc21a);
    this.group.add(this.buoyNext);
    this.flag = new THREE.Mesh(new THREE.PlaneGeometry(0.9, 0.55),
      new THREE.MeshBasicMaterial({ color: 0xff3b30, side: THREE.DoubleSide }));
    this.flagPole = new THREE.Mesh(new THREE.CylinderGeometry(0.03, 0.03, 1.4, 6), new THREE.MeshStandardMaterial({ color: 0x333333 }));
    this.flagPole.rotation.x = Math.PI / 2;
    this.flagPole.position.z = BUOY_TOP_M + 0.6;
    this.flag.position.set(0.45, 0, BUOY_TOP_M + 1.0);
    this.flag.rotation.x = Math.PI / 2;
    this.buoyNext.add(this.flagPole, this.flag);

    this.dashes = new THREE.InstancedMesh(new THREE.BoxGeometry(1, 0.14, 0.14),
      new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.9 }), MAX_DASHES);
    this.dashes.frustumCulled = false;
    this.group.add(this.dashes);

    this.rockGeos = Array.from({ length: 5 }, (_, i) => rockGeometry(i + 1));
    this.rockMat = new THREE.MeshStandardMaterial({ color: 0x4a4744, roughness: 0.95, flatShading: true });
    this.foamMat = new THREE.MeshBasicMaterial({ color: 0xf2f6f8, transparent: true, opacity: 0.75, depthWrite: false });
    this.rocks = [];

    const wg = new THREE.BufferGeometry();
    this.wakeMax = 128;
    wg.setAttribute('position', new THREE.BufferAttribute(new Float32Array(this.wakeMax * 2 * 3), 3));
    wg.setAttribute('color', new THREE.BufferAttribute(new Float32Array(this.wakeMax * 2 * 4), 4));
    const idx = [];
    for (let i = 0; i < this.wakeMax - 1; i++) idx.push(2 * i, 2 * i + 1, 2 * i + 2, 2 * i + 1, 2 * i + 3, 2 * i + 2);
    wg.setIndex(idx);
    this.wake = new THREE.Mesh(wg, new THREE.MeshBasicMaterial({ vertexColors: true, transparent: true, depthWrite: false, side: THREE.DoubleSide }));
    this.wake.frustumCulled = false;
    this.wake.renderOrder = 1;
    this.group.add(this.wake);
    this._m = new THREE.Matrix4();
    this._q = new THREE.Quaternion();
    this._v = new THREE.Vector3();
  }

  buoy(i) {
    while (this.buoys.length <= i) {
      const b = buoyMesh(0xff6a1a);
      this.group.add(b);
      this.buoys.push(b);
    }
    return this.buoys[i];
  }

  update(f, stat, t) {
    const k = f.sea.k;
    let nb = 0, nd = 0;
    this.buoyNext.visible = false;
    for (const m of f.marks || []) {
      const b = m.next ? this.buoyNext : this.buoy(nb++);
      b.visible = true;
      b.position.set(m.x, -m.y, m.z);
      // a little roll with the swell: tilt along the sea's slope there
      const e = 0.5;
      const sx = (seaHeight(k, m.x + e, m.y) - seaHeight(k, m.x - e, m.y)) / (2 * e);
      const sy = (seaHeight(k, m.x, m.y + e) - seaHeight(k, m.x, m.y - e)) / (2 * e);
      b.rotation.set(-Math.atan(sy) * 0.8, -Math.atan(sx) * 0.8, 0);
      if (m.next) {
        this.flag.rotation.z = Math.sin(t * 7) * 0.15;
        nd = this.gate(k, m, nd);
      }
    }
    for (let i = nb; i < this.buoys.length; i++) this.buoys[i].visible = false;
    this.dashes.count = nd;
    this.dashes.instanceMatrix.needsUpdate = true;

    this.updateRocks(f.rocks || [], k);
    this.updateWake(f.wake || [], k, stat.boat);
  }

  // One dash from sim-frame (x0, y0, z0) to (x1, y1, z1).
  dash(n, x0, y0, z0, x1, y1, z1) {
    if (n >= MAX_DASHES) return n;
    const a = new THREE.Vector3(x0, -y0, z0), b = new THREE.Vector3(x1, -y1, z1);
    const d = this._v.subVectors(b, a);
    const len = d.length();
    if (len < 1e-4) return n;
    this._q.setFromUnitVectors(new THREE.Vector3(1, 0, 0), d.clone().divideScalar(len));
    this._m.compose(a.add(b).multiplyScalar(0.5), this._q, new THREE.Vector3(len, 1, 1));
    this.dashes.setMatrixAt(n, this._m);
    return n + 1;
  }

  // The gate: a dashed line raised above the sea along the ray to cross, and the turn arrow (render.c draw_gate).
  gate(k, m, n) {
    const dx = Math.cos(m.gate), dy = Math.sin(m.gate);
    const at = (x, y, h) => seaHeight(k, x, y) + h;
    for (let s = 0; s < GATE_LEN_M; s += 2 * GATE_DASH_M) {
      const x0 = m.x + dx * s, y0 = m.y + dy * s, x1 = m.x + dx * (s + GATE_DASH_M), y1 = m.y + dy * (s + GATE_DASH_M);
      n = this.dash(n, x0, y0, at(x0, y0, GATE_HEIGHT_M), x1, y1, at(x1, y1, GATE_HEIGHT_M));
    }
    const sweep = ARROW_SWEEP * 2 * Math.PI, arc = sweep * ARROW_RADIUS_M, turn = m.round;
    const a0 = m.gate - turn * sweep;
    const pt = (u) => {
      const a = a0 + turn * sweep * Math.min(u, 1);
      const x = m.x + Math.cos(a) * ARROW_RADIUS_M, y = m.y + Math.sin(a) * ARROW_RADIUS_M;
      return [x, y, at(x, y, GATE_HEIGHT_M * (2 - u * u))];
    };
    for (let s = 0; s < arc; s += 2 * GATE_DASH_M) {
      const p0 = pt(s / arc), p1 = pt((s + GATE_DASH_M) / arc);
      n = this.dash(n, ...p0, ...p1);
    }
    const head = (a, h) => {
      const px = m.x + Math.cos(a) * ARROW_RADIUS_M, py = m.y + Math.sin(a) * ARROW_RADIUS_M;
      const ux = -turn * Math.sin(a), uy = turn * Math.cos(a);
      const tz = at(px, py, h);
      for (const sgn of [1, -1]) {
        const bx = px - ux * ARROW_HEAD_M, by = py - uy * ARROW_HEAD_M;
        n = this.dash(n, px, py, tz, bx, by, at(bx, by, h + sgn * ARROW_HEAD_M * 0.6));
      }
    };
    head(m.gate, GATE_HEIGHT_M);
    head(a0, GATE_HEIGHT_M * 2);
    return n;
  }

  updateRocks(rocks, k) {
    while (this.rocks.length < rocks.length) {
      const i = this.rocks.length;
      const g = new THREE.Group();
      const m = new THREE.Mesh(this.rockGeos[i % this.rockGeos.length], this.rockMat);
      const foam = new THREE.Mesh(new THREE.RingGeometry(1.0, 1.35, 28), this.foamMat);
      g.add(m, foam);
      g.userData = { rock: m, foam };
      this.group.add(g);
      this.rocks.push(g);
    }
    rocks.forEach(([x, y, r, awash], i) => {
      const g = this.rocks[i];
      g.visible = true;
      const z = seaHeight(k, x, y);
      g.position.set(x, -y, 0);
      g.userData.rock.scale.set(r, r, awash ? r * 0.35 : r * 0.75);
      g.userData.rock.position.z = awash ? -0.25 * r : -0.1 * r;
      g.userData.foam.scale.setScalar(r * 0.95);
      g.userData.foam.position.z = z + 0.05;
      g.userData.foam.material.opacity = awash ? 0.85 : 0.5;
    });
    for (let i = rocks.length; i < this.rocks.length; i++) this.rocks[i].visible = false;
  }

  // A foam ribbon along the boat's recent track, widening and fading astern.
  updateWake(w, k, boat) {
    const n = Math.min(this.wakeMax, Math.floor(w.length / 3));
    const pos = this.wake.geometry.attributes.position.array;
    const col = this.wake.geometry.attributes.color.array;
    const hb = 0.5 * boat.beam;
    const reach = 6 + boat.length * 3;
    let s = 0;
    for (let i = 0; i < this.wakeMax; i++) {
      const o = 6 * i, c = 8 * i;
      if (i >= n || n < 2) { pos.fill(0, o, o + 6); col.fill(0, c, c + 8); continue; }
      const x = w[3 * i], y = w[3 * i + 1], u = w[3 * i + 2];
      const j = Math.min(i + 1, n - 1), jp = Math.max(i - 1, 0);
      let tx = w[3 * jp] - w[3 * j], ty = w[3 * jp + 1] - w[3 * j + 1];
      const tl = Math.hypot(tx, ty) || 1;
      tx /= tl; ty /= tl;
      if (i > 0) s += Math.hypot(x - w[3 * (i - 1)], y - w[3 * (i - 1) + 1]);
      const half = hb * 0.7 + s * 0.16;
      const fade = Math.max(0, 1 - s / reach) * Math.min(1, u / 1.5);
      const z = seaHeight(k, x, y) + 0.04;
      const nx = -ty, ny = tx;
      pos[o] = x + nx * half; pos[o + 1] = -(y + ny * half); pos[o + 2] = z;
      pos[o + 3] = x - nx * half; pos[o + 4] = -(y - ny * half); pos[o + 5] = z;
      const a = 0.55 * fade;
      col[c] = col[c + 1] = col[c + 2] = 0.95; col[c + 3] = a * (i === 0 ? 0 : 1);
      col[c + 4] = col[c + 5] = col[c + 6] = 0.95; col[c + 7] = a * (i === 0 ? 0 : 1);
    }
    this.wake.geometry.attributes.position.needsUpdate = true;
    this.wake.geometry.attributes.color.needsUpdate = true;
  }
}
