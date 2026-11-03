// Colours for a weather preset. The presets give sky shades as Playdate dither levels (0 white .. 15 near
// black); here they set how far a clear sky darkens toward storm grey.
import * as THREE from 'three';

const c = (hex) => new THREE.Color(hex);
const CLEAR_TOP = c(0x2f78c8), CLEAR_HORIZON = c(0xcfe6f6);
const GREY_TOP = c(0x7d8a96), GREY_HORIZON = c(0xc9d0d6);
const DARK_TOP = c(0x1a1f26), DARK_HORIZON = c(0x3c434b);
const DEEP_CLEAR = c(0x0b4a6e), DEEP_DARK = c(0x0d1a22);
const MIST = c(0xdfe4e8);

export function palette(p) {
  const top = Math.min(Math.max(p.skyTop / 15, 0), 1);
  const hor = Math.min(Math.max(p.skyHorizon / 15, 0), 1);
  const overcast = Math.min(1, p.cloudCover * (p.sun > 0 ? 0.6 : 1.1));
  const skyTop = CLEAR_TOP.clone().lerp(GREY_TOP, overcast).lerp(DARK_TOP, top);
  const skyHorizon = CLEAR_HORIZON.clone().lerp(GREY_HORIZON, overcast).lerp(DARK_HORIZON, hor);
  // mist: with little visibility the whole sky is the fog, pale on a light day, dark in a storm
  const mist = Math.min(Math.max((700 - p.visibility) / 550, 0), 1) * (1 - 0.6 * top);
  const fog = skyHorizon.clone().lerp(MIST.clone().lerp(DARK_HORIZON, hor), mist);
  skyTop.lerp(fog, mist * 0.85);
  skyHorizon.lerp(fog, mist);
  const deep = DEEP_CLEAR.clone().lerp(DEEP_DARK, Math.max(top, overcast * 0.5)).lerp(c(0x4f6470), mist * 0.5);
  const sunStrength = p.sun > 0 ? 1.0 : 0.25 * (1 - top) * (1 - mist);
  return {
    skyTop, skyHorizon, deep,
    fog,
    sun: sunStrength,
    ambient: 0.55 + 0.35 * (1 - top),
    cloudColor: c(0xffffff).lerp(c(0x2b3036), Math.min(Math.max(p.cloudShade / 12, 0), 1)),
  };
}

// The sun's direction (Three frame: x, -y, z up), fixed so shadows and glints read the same every run.
export const SUN_DIR = new THREE.Vector3(-0.45, 0.55, 0.7).normalize();
