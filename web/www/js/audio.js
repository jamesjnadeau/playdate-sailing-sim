// The sea sound: the same procedural voices as the Playdate (src/sim/seasound.c), rendered in wasm and
// played through a ScriptProcessorNode. (An AudioWorklet would need shared wasm memory, which needs
// cross-origin isolation headers that GitHub Pages cannot send.) Starts on the first key press or click.
const RATE = 44100, BLOCK = 2048;

export class SeaAudio {
  constructor(sim) {
    this.sim = sim;
    this.ctx = null;
    this.left = sim._web_alloc(BLOCK * 2);
    this.right = sim._web_alloc(BLOCK * 2);
  }

  start() {
    if (this.ctx) {
      if (this.ctx.state === 'suspended') this.ctx.resume();
      return;
    }
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return;
    try {
      this.ctx = new AC({ sampleRate: RATE });
    } catch {
      this.ctx = new AC();   // a browser that will not run at 44.1 kHz resamples a little off pitch
    }
    const node = this.ctx.createScriptProcessor(BLOCK, 0, 2);
    node.onaudioprocess = (e) => {
      const L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
      const n = Math.min(L.length, BLOCK);
      const got = this.sim._web_audio(this.left, this.right, n);
      const h = this.sim.HEAP16;
      const l0 = this.left >> 1, r0 = this.right >> 1;
      for (let i = 0; i < n; i++) {
        L[i] = got ? h[l0 + i] / 32768 : 0;
        R[i] = got ? h[r0 + i] / 32768 : 0;
      }
    };
    node.connect(this.ctx.destination);
    this.node = node;
  }

  set active(on) { this.sim._web_audio_on(on ? 1 : 0); }
}
