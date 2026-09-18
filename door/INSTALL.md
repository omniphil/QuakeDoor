# Installing the QUAKE door on the BBS

The door doesn't run the game. It sends it: TERMinator runs the whole of Quake on the player's own machine, so the
BBS only pays for one upload per player, ever. A player on any other terminal gets a polite screen explaining that.

## What the BBS needs

A Linux BBS box (the door finds its own folder the Linux way) with `gcc`, `make`, `python3`, `curl`, `unzip` and
`lhasa` (`apt install lhasa`: the shareware episode comes in an LHA archive), and BBS software that writes a
`door32.sys` drop file (tested with Mystic).

| File | Where it comes from | Size |
|---|---|---|
| `quakedoor` | built here with `make` | ~40 KB |
| `quake.wasm` | ships in this folder (or `make` in `../module`, which needs wasi-sdk) | ~470 KB |
| `quake-shareware.zip` | id's shareware Quake 1.06, downloaded and repacked by `make install` | 8.5 MB |

The door looks for `quake.wasm` and `quake-shareware.zip` beside its own binary.

## Steps

1. **Get the whole project onto the BBS box**, not just this folder: `make install` uses `../tools` to fetch and pack
   the shareware episode.
2. In `door/`:

   ```
   make            # builds quakedoor
   make install    # checks quake.wasm is here; downloads shareware Quake and packs it into quake-shareware.zip
   ```

   After that, `door/` holds everything the door needs and can be moved into your BBS's doors directory on its own.
3. **Add a door entry** that runs `quakedoor` with the folder holding `door32.sys` as its one argument. In Mystic:
   `(D3) Exec DOOR32 program` with Data

   ```
   ./doors/quake/quakedoor /path/to/mystic/temp%3
   ```

   (`%3` is the node number.) Without a drop file (Mystic's `(DD) Exec external program`, for one) the door falls
   back to local mode and **every caller shares one `saves/player/` folder**. The door prints "Saved games for:
   <name>" as it starts; `player` there means the drop file isn't reaching it.

Callers need [TERMinator](https://deadmodemsociety.com/terminator/) 1.1.2 or newer with TRACE graphics.

## Checking it works

```
python3 test_door.py            # the full exchange: game and data arrive intact, saves go both ways
python3 test_door.py --plain    # a terminal with no TRACE: the door should bow out politely
rm -rf saves                    # the test leaves a saves/player folder behind
```

## Saved games

Kept **per player** in `saves/<handle>-<user number>/` beside the door: `config.cfg` (keys and settings, written when
the player quits) and `s0.sav` .. `s11.sav`, the twelve slots of Quake's Save and Load menus. The config goes to the
game whole at the start; of each save only its title goes, which is all the Load menu shows, and the whole save only
when the player loads it. The game sends a file back whenever it writes it. A file only replaces the old one once it
has arrived whole, and the old one is kept as `<name>.bak`. If the player's time runs out mid-game, the door asks the
game to quit, which writes the config on the way out, before it closes it. (Quake has no autosave: a game that isn't
saved isn't kept, as in the original.)

## Licences

- The engine is **id Software's WinQuake**, GPL-2 (`COPYING-quake`), by way of quakegeneric, with WinQuake's own
  sound mixer. The door tells players where the source is as they leave.
- The data is the **shareware episode**, (c) id Software. Its licence (`slicnse.txt`) allows it to be passed on free
  of charge, "as a whole", "only in a compressed format", and with the licence accompanying it. So it travels as
  `quake-shareware.zip`: `pak0.pak` compressed, together with id's `slicnse.txt` and `licinfo.txt`, unchanged. The
  registered game (`pak1.pak`) must never be sent.
