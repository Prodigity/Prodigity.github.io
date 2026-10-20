# BatteryCheck in the browser

**Play: https://prodigity.github.io**

*BatteryCheck* (1998), the Batteryman game, running in a web browser. It is a port of the original
game's code: its controller, physics, camera, objects, particles, menus and level flow were rebuilt
routine by routine from the shipped program, and run on the
[Jazz² Resurrection](https://github.com/deathkiller/jazz2-native) engine, compiled to WebAssembly.

## Controls

| Key | Action |
|---|---|
| ← → | walk |
| ↑ ↓ | look up, crouch |
| Ctrl | jump |
| Space | put a battery in, feed Freddy, menus |
| Esc | menu (save, load, quit) |
| Pause/Break | pause |

The saved game (SPEL BEWAREN) stays in your browser's storage.

## What is here

- `index.html`, `jazz2.js`, `jazz2.wasm`, `jazz2.data` - the game as GitHub Pages serves it. `jazz2.data`
  holds the engine's content and the game's levels, tilesets, sprites, sounds and music, converted from
  the original files.
- The [`source`](https://github.com/Prodigity/Prodigity.github.io/tree/source) branch - the complete
  source of this build (the engine with the BatteryCheck port), as the GPL-3.0 requires.

## Licence

The engine and the port are GPL-3.0: see [LICENSE](LICENSE) and the `source` branch. The libraries in the
build and their licences are listed in [THIRD-PARTY.md](THIRD-PARTY.md). BatteryCheck's own levels, graphics,
sounds and music are © 1998 Taxus Interactive and Orangegames BV, published here for preservation; see
[THIRD-PARTY.md](THIRD-PARTY.md#batterychecks-own-content).
