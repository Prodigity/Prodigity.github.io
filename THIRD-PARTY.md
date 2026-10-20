# Third-party notices

The game served by this site (`jazz2.js`, `jazz2.wasm`, `jazz2.data`) is a WebAssembly build of a fork of
Jazz² Resurrection with a port of BatteryCheck. Its complete source is the
[`source`](https://github.com/Prodigity/Prodigity.github.io/tree/source) branch of this repository.

## The program

| Component | Licence | Notes |
|---|---|---|
| [Jazz² Resurrection](https://github.com/deathkiller/jazz2-native), © Dan R. (deathkiller) and contributors | GPL-3.0 ([LICENSE](LICENSE)) | the engine; this build and the BatteryCheck port in it are distributed under the same licence |
| [nCine](https://github.com/nCine/nCine) | MIT | the game framework Jazz² Resurrection is built on (extensively modified there) |

## Libraries compiled into `jazz2.wasm` / `jazz2.js`

| Library | Licence | Copyright |
|---|---|---|
| [libopenmpt](https://lib.openmpt.org/) 0.7.1 (module music) | BSD-3-Clause | © 2004-2023 OpenMPT Project Developers and Contributors, © 1997-2003 Olivier Lapicque |
| [mpg123](https://www.mpg123.de/) 1.26.2 (used by libopenmpt) | LGPL-2.1 | © the mpg123 project; compiled from the unmodified Emscripten port, see below |
| [libogg](https://xiph.org/ogg/) | BSD-3-Clause | © 2002 Xiph.org Foundation |
| [libvorbis](https://xiph.org/vorbis/) | BSD-3-Clause | © 2002-2015 Xiph.org Foundation |
| [zlib](https://zlib.net/) 1.2.13 | zlib | © 1995-2022 Jean-loup Gailly and Mark Adler |
| [emscripten-glfw](https://github.com/pongasoft/emscripten-glfw) (Emscripten port `contrib.glfw3`, the window and input backend) | Apache-2.0 | © pongasoft |
| [Emscripten](https://emscripten.org/) 3.1.64 runtime, incl. its OpenAL (Web Audio), IndexedDB file system and file packager | MIT / University of Illinois NCSA | © Emscripten authors |
| MiniLZ4 (Emscripten `third_party/mini-lz4.js`, unpacks `jazz2.data`), based on [node-lz4](https://github.com/pierrec/node-lz4) | MIT | © 2012 Pierre Curto |
| [JsonCpp](https://github.com/open-source-parsers/jsoncpp) | MIT (or public domain) | © 2007-2010 Baptiste Lepilleur and the JsonCpp Authors |
| [parallel-hashmap](https://github.com/greg7mdp/parallel-hashmap) | Apache-2.0 | © 2019 Gregory Popovitch |
| [pdqsort](https://github.com/orlp/pdqsort) | zlib | © 2021 Orson Peters |

The audio is Emscripten's own OpenAL implementation on Web Audio, not OpenAL Soft, and the window backend is GLFW, not
SDL2. Libraries that appear in the source tree only for other platforms or features (SDL2, OpenAL Soft, lz4, zstd,
libxm, ulc, vadpcm, ENet, IXWebSocket and others) are not part of this build.

**mpg123 (LGPL-2.1):** it is linked statically into `jazz2.wasm`. To relink the program with a modified mpg123, build
the `source` branch with the Emscripten SDK 3.1.64 as described in the message of its first commit, with your mpg123 in place of
the Emscripten port (`-s USE_MPG123=1`); the complete corresponding source of everything else is in that branch. The
mpg123 source used is the one of Emscripten's port, version 1.26.2, available from https://www.mpg123.de/.

## BatteryCheck's own content

*BatteryCheck* (1998) is © 1998 Taxus Interactive and Orangegames BV, made for Stibat. It was built on a licensed
beta of the Jazz Jackrabbit 2 engine by Epic MegaGames. Its levels, graphics, sounds and music, converted from the
original game files into `jazz2.data`, are not covered by the GPL. They are published here for preservation, so the
game can be played again; they will be removed on request of the rights holders (open an issue in this repository).
