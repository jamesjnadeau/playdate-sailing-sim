// A boat built from the boat config (hull lofted from look.hull_width, rig, foils) and posed each frame
// from the snapshot. Geometry is made in the boat frame converted to Three's axes: X forward, Y to port
// (sim y is starboard, so Y = -y), Z up from the mean waterline.
import * as THREE from 'three';

function tableEval(t, x) {
  if (!t || t.length < 2) return 1;
  const n = t.length / 2;
  if (x <= t[0]) return t[1];
  if (x >= t[2 * (n - 1)]) return t[2 * n - 1];
  for (let i = 0; i < n - 1; i++) {
    const x0 = t[2 * i], x1 = t[2 * i + 2];
    if (x <= x1) {
      const f = (x - x0) / Math.max(x1 - x0, 1e-6);
      return t[2 * i + 1] + (t[2 * i + 3] - t[2 * i + 1]) * f;
    }
  }
  return t[2 * n - 1];
}

const STATIONS = 28;
const SIDE = 7;   // points down each side of a section

// One hull's topsides and deck: centre yc (sim starboard), gunwale half-width hb.
function hullGeometry(cfg, yc, hb, keel) {
  const L = cfg.length, fb = cfg.freeboard, tuck = cfg.tuck;
  const pos = [], idx = [];
  const deckPos = [], deckIdx = [];
  const ring = 2 * SIDE + 1;   // port gunwale .. keel centre .. starboard gunwale
  for (let i = 0; i <= STATIONS; i++) {
    const u = i / STATIONS;
    const x = L / 2 - u * L;
    const w = Math.max(hb * tableEval(cfg.width, u), 0.002);
    // the forefoot rises toward the stem so the bow is not a flat-bottomed box
    const kz = keel + (fb - keel) * Math.pow(Math.max(0, 1 - u / 0.12), 2) * 0.85;
    for (let k = 0; k < ring; k++) {
      const s = k <= SIDE ? k / SIDE : (ring - 1 - k) / SIDE;   // 0 at the gunwale, 1 at the keel
      const sgn = k < SIDE ? -1 : k > SIDE ? 1 : 0;
      const y = yc + sgn * w * (1 - (1 - tuck) * Math.pow(s, 1.7));
      const z = fb + (kz - fb) * Math.pow(s, 0.9) - (k === SIDE ? 0.04 * w : 0);
      pos.push(x, -y, z);
    }
    deckPos.push(x, -(yc - w), fb, x, -(yc + w), fb);
  }
  for (let i = 0; i < STATIONS; i++) {
    for (let k = 0; k < ring - 1; k++) {
      const a = i * ring + k, b = a + ring;
      idx.push(a, b, a + 1, a + 1, b, b + 1);
    }
    const d = i * 2;
    deckIdx.push(d, d + 1, d + 2, d + 1, d + 3, d + 2);
  }
  // transom: a fan from the stern section's centre
  const base = STATIONS * ring;
  const cz = (fb + keel) * 0.5;
  pos.push(-L / 2, -yc, cz);
  const c = pos.length / 3 - 1;
  for (let k = 0; k < ring - 1; k++) idx.push(c, base + k + 1, base + k);
  idx.push(c, base, base + ring - 1);

  const hull = new THREE.BufferGeometry();
  hull.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  hull.setIndex(idx);
  hull.computeVertexNormals();
  const deck = new THREE.BufferGeometry();
  deck.setAttribute('position', new THREE.Float32BufferAttribute(deckPos, 3));
  deck.setIndex(deckIdx);
  deck.computeVertexNormals();
  return { hull, deck };
}

// A unit cylinder along +Y, stretched between two points each frame.
function strut(radius, material) {
  const g = new THREE.CylinderGeometry(radius, radius, 1, 8);
  g.translate(0, 0.5, 0);
  return new THREE.Mesh(g, material);
}
const UP = new THREE.Vector3(0, 1, 0);
function placeStrut(m, a, b) {
  const d = new THREE.Vector3().subVectors(b, a);
  const len = d.length();
  m.visible = len > 1e-4;
  if (!m.visible) return;
  m.position.copy(a);
  m.quaternion.setFromUnitVectors(UP, d.divideScalar(len));
  m.scale.set(1, len, 1);
}

function sailColor(s) {
  const shade = Math.min(Math.max(s.shade, 0), 1);
  if (s.shape === 2) return new THREE.Color(0xd8343c).lerp(new THREE.Color(0xf6efe2), 0.15);   // spinnaker
  if (s.shape === 3) return new THREE.Color(0xc9a46a).multiplyScalar(0.6 + 0.4 * shade);    // a square sail: canvas
  // shade is a dither level on the Playdate, where a dark sail reads against a white sky; in colour the
  // sky is already darker than cloth, so keep every sail light and let shade only tint it
  return new THREE.Color(0xf5f2ea).multiplyScalar(0.78 + 0.22 * shade);
}

export class Boat {
  constructor(cfg, { ghost = false } = {}) {
    this.cfg = cfg;
    this.ghost = ghost;
    const op = ghost ? { transparent: true, opacity: 0.38, depthWrite: false } : {};
    const shade = Math.min(Math.max(cfg.hullShade, 0), 1);
    const hullColor = new THREE.Color(0xf7f7f2).lerp(new THREE.Color(0x15304f), shade);
    this.mat = {
      hull: new THREE.MeshStandardMaterial({ color: hullColor, roughness: 0.35, metalness: 0.05, side: THREE.DoubleSide, ...op }),
      deck: new THREE.MeshStandardMaterial({ color: shade > 0.5 ? 0xd9d3c4 : 0xe4ddcc, roughness: 0.8, side: THREE.DoubleSide, ...op }),
      dark: new THREE.MeshStandardMaterial({ color: 0x2a2d31, roughness: 0.6, ...op }),
      well: new THREE.MeshStandardMaterial({ color: 0x8a7a63, roughness: 0.9, ...op }),
      spar: new THREE.MeshStandardMaterial({ color: 0xb9bec4, roughness: 0.3, metalness: 0.6, ...op }),
      net: new THREE.MeshStandardMaterial({ color: 0x1f2326, roughness: 0.9, side: THREE.DoubleSide, ...op }),
      wire: new THREE.LineBasicMaterial({ color: 0x30343a, transparent: true, opacity: ghost ? 0.3 : 0.8 }),
    };
    if (ghost) for (const m of Object.values(this.mat)) m.color && m.color.lerp(new THREE.Color(0xdfe9f2), 0.5);

    this.root = new THREE.Group();
    this.pitchG = new THREE.Group();
    this.heelPivot = new THREE.Group();
    this.heelG = new THREE.Group();
    this.content = new THREE.Group();
    this.root.add(this.pitchG);
    this.pitchG.add(this.heelPivot);
    this.heelPivot.add(this.heelG);
    this.heelG.add(this.content);

    const cat = cfg.hulls === 2;
    const hulls = cat ? [-0.5 * cfg.spacing, 0.5 * cfg.spacing] : [0];
    const hb = cat ? 0.5 * (cfg.beam - cfg.spacing) : 0.5 * cfg.beam;
    const keel = cat ? -cfg.draft : cfg.keel;
    this.rudders = [];
    for (const yc of hulls) {
      const g = hullGeometry(cfg, yc, hb, keel);
      this.content.add(new THREE.Mesh(g.hull, this.mat.hull));
      this.content.add(new THREE.Mesh(g.deck, this.mat.deck));
      this.rudders.push(this.makeRudder(yc));
    }
    if (cat) this.makeTrampoline(hb);
    else this.makeKeel(keel);
    if (!cat && cfg.cockpitDepth > 0) this.makeCockpit();
    this.makeRig();

    this.sails = cfg.sails.map((s) => {
      const geo = new THREE.BufferGeometry();
      // sailcloth lets light through: a share of its own colour as emissive keeps the shaded side from going grey
      const col = sailColor(s);
      const m = new THREE.Mesh(geo, new THREE.MeshStandardMaterial({
        color: col, emissive: col.clone().multiplyScalar(0.32), roughness: 0.8, side: THREE.DoubleSide, ...op,
      }));
      m.visible = false;
      this.content.add(m);
      return { mesh: m, w: -1, h: -1, boom: s.shape === 0 ? this.addStrut(0.035 * Math.max(1, cfg.mastHeight / 8), this.mat.spar) : null };
    });
  }

  addStrut(r, mat) {
    const m = strut(r, mat);
    this.content.add(m);
    return m;
  }

  makeKeel(keel) {
    const c = this.cfg;
    const chord = Math.min(Math.max(Math.sqrt(Math.max(c.boardArea, 0.05)) * 0.9, 0.25), 2.6);
    const depth = Math.min(Math.max(c.boardArea / chord, 0.3), 3.2);
    const g = new THREE.BoxGeometry(chord, 0.06 + 0.01 * c.length, depth);
    const m = new THREE.Mesh(g, this.mat.dark);
    m.position.set(c.boardX, 0, keel - depth / 2 + 0.02);
    this.content.add(m);
  }

  makeRudder(yc) {
    const c = this.cfg;
    const chord = 0.55 * Math.max(1, Math.sqrt(c.length / 4));
    const depth = Math.min(Math.max(c.rudderArea / chord, 0.4), 2.5);
    // the blade below the waterline, a slim stock up to the deck
    const blade = new THREE.BoxGeometry(chord, 0.04, depth);
    blade.translate(-chord / 2, 0, 0.05 - depth / 2);
    const stock = new THREE.CylinderGeometry(0.035, 0.035, c.freeboard + 0.15, 8);
    stock.rotateX(Math.PI / 2);
    stock.translate(-0.03, 0, (c.freeboard + 0.15) / 2);
    const pivot = new THREE.Group();
    pivot.position.set(c.rudderX, -yc, 0);
    pivot.add(new THREE.Mesh(blade, this.mat.dark), new THREE.Mesh(stock, this.mat.dark));
    this.content.add(pivot);
    return pivot;
  }

  makeTrampoline(hb) {
    const c = this.cfg;
    const x0 = -0.32 * c.length, x1 = Math.min(c.mastX + 0.15, 0.3 * c.length);
    const span = c.spacing - 2 * hb * 0.6;
    const g = new THREE.PlaneGeometry(x1 - x0, Math.max(span, 0.3));
    const m = new THREE.Mesh(g, this.mat.net);
    m.position.set((x0 + x1) / 2, 0, c.freeboard + 0.02);
    this.content.add(m);
    for (const x of [x0, x1]) {
      const beam = new THREE.Mesh(new THREE.BoxGeometry(0.12, c.spacing, 0.1), this.mat.spar);
      beam.position.set(x, 0, c.freeboard + 0.06);
      this.content.add(beam);
    }
  }

  makeCockpit() {
    const c = this.cfg;
    const fb = c.freeboard + 0.006;
    const shape = new THREE.Shape();
    shape.moveTo(c.cockpitX1, -c.cockpitW1);
    shape.lineTo(c.cockpitX1, c.cockpitW1);
    shape.lineTo(c.cockpitX0, c.cockpitW0);
    shape.lineTo(c.cockpitX0, -c.cockpitW0);
    const m = new THREE.Mesh(new THREE.ShapeGeometry(shape), this.mat.well);
    m.position.z = fb;
    this.content.add(m);
  }

  makeRig() {
    const c = this.cfg;
    const fb = c.freeboard;
    const r = 0.045 * Math.max(1, c.mastHeight / 8);
    const mast = this.addStrut(r, this.mat.spar);
    const foot = new THREE.Vector3(c.mastX, 0, fb), head = new THREE.Vector3(c.mastX, 0, c.mastHeight);
    placeStrut(mast, foot, head);
    const pts = [head, new THREE.Vector3(c.forestayX, 0, fb)];
    for (const sd of [-1, 1]) pts.push(head, new THREE.Vector3(c.mastX, sd * c.shroudWidth, fb));
    const lines = new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(pts), this.mat.wire);
    this.content.add(lines);
  }

  // pose: the frame's boat (or ghost) object; seaZ is unused by the hull, which the water clips.
  update(pose) {
    this.root.position.set(pose.x, -pose.y, pose.heave);
    this.root.rotation.set(0, 0, -pose.heading);
    this.pitchG.rotation.set(0, -pose.pitch, 0);
    this.heelPivot.position.set(0, -pose.pivot, 0);
    this.heelG.rotation.set(pose.heel, 0, 0);
    this.content.position.set(0, pose.pivot, 0);
    const d = pose.wheel * this.cfg.rudderMax;
    for (const r of this.rudders) r.rotation.set(0, 0, d);

    const byIndex = new Map(pose.sails.map((s) => [s.i, s]));
    this.sails.forEach((sl, i) => {
      const s = byIndex.get(i);
      if (!s || !s.p) {
        sl.mesh.visible = false;
        if (sl.boom) sl.boom.visible = false;
        return;
      }
      if (sl.w !== s.w || sl.h !== s.h) this.resizeSail(sl, s.w, s.h);
      const a = sl.mesh.geometry.attributes.position.array;
      for (let k = 0, n = s.p.length / 3; k < n; k++) {
        a[3 * k] = s.p[3 * k];
        a[3 * k + 1] = -s.p[3 * k + 1];
        a[3 * k + 2] = s.p[3 * k + 2];
      }
      sl.mesh.geometry.attributes.position.needsUpdate = true;
      sl.mesh.geometry.computeVertexNormals();
      sl.mesh.geometry.computeBoundingSphere();
      sl.mesh.visible = true;
      if (sl.boom) {
        // the boom runs along the foot, from the tack to the clew, just under the sail
        const tack = new THREE.Vector3(a[0], a[1], a[2] - 0.04);
        const j = 3 * s.w;
        placeStrut(sl.boom, tack, new THREE.Vector3(a[j], a[j + 1], a[j + 2] - 0.04));
      }
    });
  }

  resizeSail(sl, w, h) {
    const n = (w + 1) * (h + 1);
    const g = sl.mesh.geometry;
    g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(n * 3), 3));
    const idx = [];
    for (let a = 0; a < h; a++)
      for (let q = 0; q < w; q++) {
        const i0 = a * (w + 1) + q, i1 = i0 + 1, i2 = i0 + w + 1, i3 = i2 + 1;
        idx.push(i0, i1, i2, i1, i3, i2);
      }
    g.setIndex(idx);
    sl.w = w;
    sl.h = h;
  }

  dispose() {
    this.root.traverse((o) => {
      if (o.geometry) o.geometry.dispose();
      if (o.material && !Object.values(this.mat).includes(o.material)) o.material.dispose();
    });
    for (const m of Object.values(this.mat)) m.dispose();
  }
}
