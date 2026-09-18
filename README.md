# QuakeDoor

Shareware Quake as a BBS door for callers using [TERMinator](https://deadmodemsociety.com/terminator/). The whole game
runs on the caller's own PC inside TERMinator's TRACE sandbox, as WebAssembly, with sound, at up to 1024x768; the BBS
sends it once per caller and keeps each player's saves.

The engine is id Software's GPL WinQuake (software renderer) by way of
[quakegeneric](https://github.com/erysdren/quakegeneric), with WinQuake's own sound mixer from
[id's release](https://github.com/id-Software/Quake). Both are included unmodified in `third_party/`; the one change
to id's code (the mixer's level, before clipping) is a marked copy in `module/src/overrides/`.

## Running it on your BBS

On a Linux BBS box with `gcc`, `make`, `python3`, `curl`, `unzip` and `lhasa` (`apt install lhasa`):

```
git clone https://github.com/omniphil/QuakeDoor
cd QuakeDoor/door
make              # builds the door, quakedoor
make install      # downloads shareware Quake 1.06 and packs it, with id's licence, into quake-shareware.zip
```

Then add a door entry that runs `quakedoor <folder holding door32.sys>`. The details, including Mystic's settings
and where each player's saves are kept, are in [`door/INSTALL.md`](door/INSTALL.md). Callers need TERMinator 1.1.2
or newer (1024x768 needs a TERMinator whose TRACE frame holds it; older ones get 640x400); anyone else is told so and
sent back to the BBS.

`door/quake.wasm` is prebuilt from `module/` at the same commit, so no WebAssembly toolchain is needed to run the door.
(Quake stamps its build time into itself, so a rebuild differs from it in that line alone.)

| Folder | What |
|---|---|
| `door/` | the BBS side: detects TERMinator, sends the game and data by hash (once per caller), keeps saves per player |
| `module/` | the game side: TRACE's screen, keys, sound and files for Quake (see `module/README.md`) |
| `tools/` | `get_shareware.sh` fetches shareware Quake 1.06; `mkzip.py` packs it with id's licence |
| `third_party/` | quakegeneric, id's WinQuake sound files, tinf (inflate): all unmodified |

## Building the module

Only needed if you change it. Needs [wasi-sdk](https://github.com/WebAssembly/wasi-sdk) 34
(`~/tools/wasi-sdk-34.0-x86_64-linux` by default):

```
cd module
make                      # or: make WASI_SDK=/path/to/wasi-sdk
cp quake.wasm ../door/
```

If you change it, point `QUAKE_SOURCE_URL` in `door/main.c` at your own copy of the source, since that's where the
door tells players to find it.

## Game data

Not included. The shareware episode is (c) id Software; its licence (`slicnse.txt`) allows passing it on free of
charge, as a whole, compressed, with the licence. `make install` downloads it and the door sends it only in that form.
The registered game must never be sent.

## Licence

GPL-2 (`LICENSE`), as id's Quake source. tinf is under the zlib licence (`third_party/tinf/LICENSE`).
