# About Crank Sail

Crank Sail is a small open-sea sailing simulator, written for the [Playdate](https://play.date) and its crank. This page is the same game in your browser: the Playdate build's own simulation and menus, compiled to WebAssembly, drawn in full colour with Three.js.

## What you can do

- **Free Range**: sail anywhere on a wind-shifting sea.
- **Journey**: an endless run of buoys; round each one the way it shows.
- **Racing**: sail a marked course against the clock, and against the ghost of your best run.
- **Controls**: the full list of controls, the same as on the Playdate.

## The boats

A dinghy, a Sunfish, a J/24, a Hobie 16 and a Reynolds 33 catamaran. Each one is a JSON file with a physical model of its hull, sails, keel and rudder, so they sail, heel and capsize differently.

## Playing on the web

- `W` `A` `S` `D` or the arrow keys are the d-pad, `V` is A and `C` is B.
- Click the view, then move the mouse to turn the crank: left turns it the same way as up, right the same way as down. The wheel works too.
- `Backspace` opens the system menu: boat, weather, going back, and mouse settings.
- In the main menu, point at a row to pick it and click to choose it.

On a phone or tablet, hold it sideways:

- The pad bottom left is the d-pad: press it, and slide your thumb round to change direction. `B` and `A` sit bottom right.
- Drag a finger up and down anywhere else on the view to turn the crank; you can keep a thumb on the pad while you do.
- `☰` top right opens the system menu. In the main menu, tap a row to choose it.

Best times and ghosts are saved in this browser.

## Credits

Made by James Nadeau. The source is on [GitHub](https://github.com/jamesjnadeau/playdate-sailing-sim) under the GNU Affero General Public License v3.0.

The web view uses [Three.js](https://threejs.org) (MIT licence). The Playdate build uses the Jersey 10 and Silkscreen fonts (SIL Open Font License).

---

*This page is written in `web/www/about.md`; edit that file to change it.*
