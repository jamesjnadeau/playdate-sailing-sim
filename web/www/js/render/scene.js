// The 3D view: renderer, the camera posed exactly as the sim's CameraPose (src/sim/camera.c), sky, sea,
// the boat, the ghost and the world. Everything is placed relative to the camera, which sits at the origin.
import * as THREE from 'three';
import { Sea } from './sea.js';
import { Sky } from './sky.js';
import { Spray } from './spray.js';
import { Boat } from './boat.js';
import { World } from './world.js';
import { palette, SUN_DIR } from './weather.js';

const CAM_FOCAL = 220, CAM_SEA_H = 220;   // px focal length over the 220 rows above the Playdate's HUD bar

export class View {
  constructor(canvas) {
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.0;
    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(57, 1, 0.1, 9000);
    this.camera.up.set(0, 0, 1);
    this.sky = new Sky();
    this.sea = new Sea();
    this.spray = new Spray();
    this.scene.add(this.sky.dome, this.sky.rain, this.sea.mesh, this.spray.points);
    this.hemi = new THREE.HemisphereLight(0xdfefff, 0x2a4b5e, 0.9);
    this.hemi.up.set(0, 0, 1);
    this.hemi.position.set(0, 0, 1);
    this.sun = new THREE.DirectionalLight(0xfff4e0, 2.2);
    this.sun.position.copy(SUN_DIR).multiplyScalar(100);
    this.scene.add(this.hemi, this.sun);
    this.scene.fog = new THREE.Fog(0xffffff, 50, 2000);
    this.world = new World(this.scene);
    this.boat = null;
    this.ghost = null;
    this.hudPx = 0;
    this.size = { w: 1, h: 1 };
  }

  setBoat(cfg) {
    for (const b of [this.boat, this.ghost]) if (b) { this.scene.remove(b.root); b.dispose(); }
    this.boat = new Boat(cfg);
    this.ghost = new Boat(cfg, { ghost: true });
    this.scene.add(this.boat.root, this.ghost.root);
  }

  // hudPx: the HUD bar's height; the optical centre sits in the middle of the view above it, as on the
  // Playdate (horizon row 110 of the 220 above its bar), and the focal length scales with that height.
  resize(w, h, hudPx) {
    this.size = { w, h };
    this.hudPx = hudPx;
    this.renderer.setSize(w, h, false);
    const seaH = Math.max(h - hudPx, 1);
    const f = CAM_FOCAL * seaH / CAM_SEA_H;
    const full = h + hudPx;   // the view offset below moves the centre up by hudPx / 2
    this.camera.fov = 2 * Math.atan(full / 2 / f) * 180 / Math.PI;
    this.camera.aspect = w / full;
    this.camera.setViewOffset(w, full, 0, hudPx, w, h);
    this.camera.updateProjectionMatrix();
  }

  poseCamera(c) {
    const cy = Math.cos(c.yaw), sy = Math.sin(c.yaw), cp = Math.cos(c.pitch), sp = Math.sin(c.pitch);
    const cr = Math.cos(c.roll), sr = Math.sin(c.roll);
    // sim frame (x, y starboard, z up) -> Three (x, -y, z)
    const fwd = new THREE.Vector3(cy * cp, -sy * cp, sp);
    const up = new THREE.Vector3(-cy * sp, sy * sp, cp);
    const right = new THREE.Vector3(-sy, -cy, 0);
    const r2 = right.clone().multiplyScalar(cr).addScaledVector(up, -sr);
    const u2 = right.clone().multiplyScalar(sr).addScaledVector(up, cr);
    const m = new THREE.Matrix4().makeBasis(r2, u2, fwd.negate());
    this.camera.position.set(0, 0, c.z);
    this.camera.quaternion.setFromRotationMatrix(m);
    this.camera.updateMatrixWorld(true);
  }

  render(f, stat, dt) {
    const preset = stat.presets[f.preset] || stat.presets[0];
    const pal = palette(preset);
    this.poseCamera(f.cam);
    this.sky.update(pal, preset, f.t, dt, f.cam.z, f.wind);
    this.sea.update(f.sea.k, f.cam.z, pal, preset, f.t, f.cam.x, f.cam.y, f.wind);
    this.spray.update(f.sea.k, f.cam.x, f.cam.y, f.cam.z, f.t, f.wind,
      this.camera.projectionMatrix.elements[5] * this.renderer.domElement.height / 2, pal.fog);
    this.scene.fog.color.copy(pal.fog);
    this.scene.fog.near = Math.min(40, preset.visibility * 0.1);
    this.scene.fog.far = preset.visibility * 1.25;
    this.hemi.intensity = 0.6 + 0.6 * pal.ambient;
    this.hemi.color.copy(pal.skyTop).lerp(new THREE.Color(0xffffff), 0.6);
    this.hemi.groundColor.copy(pal.deep);
    this.sun.intensity = 0.4 + 2.0 * pal.sun + 3 * (this.sky.flashLevel || 0);
    this.renderer.setClearColor(pal.fog);

    if (this.boat) this.boat.update(f.boat);
    if (this.ghost) {
      this.ghost.root.visible = !!f.ghost;
      if (f.ghost) this.ghost.update(f.ghost);
    }
    this.world.update(f, stat, f.t);
    this.renderer.render(this.scene, this.camera);
  }

  // Behind the main menu: the current weather's sea and sky, seen from low down and turning slowly. The sim
  // stands still in the menu, so the waves run on here from each component's omega.
  renderBackdrop(f, stat, tMenu, dt) {
    const preset = stat.presets[f.preset] || stat.presets[0];
    const pal = palette(preset);
    const k = f.sea.k.slice();
    for (let i = 0; 4 * i + 2 < k.length; i++) k[4 * i + 2] -= (f.sea.w[i] || 0) * tMenu;
    this.poseCamera({ z: 2.6, yaw: tMenu * 0.03, pitch: 0.06, roll: 0.02 * Math.sin(tMenu * 0.4) });
    this.sky.update(pal, preset, f.t + tMenu, dt, 2.6, f.wind);
    this.sea.update(k, 2.6, pal, preset, f.t + tMenu);
    this.renderer.setClearColor(pal.fog);
    for (const b of [this.boat, this.ghost]) if (b) b.root.visible = false;
    this.world.group.visible = false;
    this.spray.points.visible = false;
    this.renderer.render(this.scene, this.camera);
    if (this.boat) this.boat.root.visible = true;
    this.world.group.visible = true;
  }

  // Sim-frame point (camera-relative) -> CSS px, or null behind the camera. onScreen: inside the view above the HUD.
  project(x, y, z) {
    const v = new THREE.Vector3(x, -y, z).applyMatrix4(this.camera.matrixWorldInverse);
    const ahead = v.z < -0.05;
    v.applyMatrix4(this.camera.projectionMatrix);
    let px = (v.x * 0.5 + 0.5) * this.size.w, py = (-v.y * 0.5 + 0.5) * this.size.h;
    if (!ahead) { px = this.size.w - px; py = this.size.h - py; }
    const onScreen = ahead && px >= 0 && px < this.size.w && py >= 0 && py < this.size.h - this.hudPx;
    return { x: px, y: py, ahead, onScreen };
  }
}
