# quake.wasm — the whole of Quake as a TRACE module

id Software's GPL WinQuake (software renderer), by way of
[quakegeneric](https://github.com/erysdren/quakegeneric) in `../third_party/quakegeneric`, compiled to WebAssembly and
run by TERMinator's sandbox on the player's own machine. quakegeneric is unmodified; WinQuake's sound mixer
(`snd_dma.c`, `snd_mem.c`, `snd_mix.c`, unmodified from id's release) is in `../third_party/id-winquake-sound`, and
`../third_party/tinf` (zlib licence) unpacks the data archive.

## Build

```
make            # needs wasi-sdk in ~/tools (same as the DOOM module)
```

Headless test (the archive goes in the assets folder named by its SHA-256):

```
gamesandbox_probe gamesandbox quake.wasm -seconds 25 -assets <dir> -data pak=<sha> \
    -keys 1@3000,28@3500,28@4000,328@8000:3000,1@12000,28@12500,336@13000,336@13300,28@13600,28@14000,\
1@16500,328@17000,28@17500,21@18000 -shot q.bmp
```

That opens the menu, starts a new game, walks forward, saves to slot 0, and quits (the save and the config go to the
"door"). Measured 2026-09-18: 1024x768 and 640x400 both at 68 fps (Quake caps itself at 72), sound continuous, a save
34.6 KB.

## What's in here

| File | Replaces | What it does |
|---|---|---|
| `src/qtrace.c` | `quakegeneric.c` | The TRACE entry points. Unzips `id1/pak0.pak` from the door's archive, runs Quake on its own thread (`Host_Frame` in a loop), queues input, and makes quitting close TERMinator's picture after the saves are sent. |
| `src/qfiles.c` | the file system | `fopen`/`fclose` (redirected by `include/qtrace_compat.h`) over memory: the pak, and the player's `config.cfg` and `s0`–`s11.sav`, which go to the BBS when written. A save only its title was sent for is fetched from the door when the game reads past the title (`fopencookie`). |
| `src/sys_trace.c` | `sys_null.c` | Quake's system layer: file handles, the clock (`trace_time_ms`), errors, quitting. |
| `src/vid_trace.c` | `vid_null.c` | The largest of 1024x768, 640x480, 640x400 (shown at 4:3) that TERMinator's frame holds: 1280x1024 from TERMinator 1.1.3, 1280x400 in 1.1.2. |
| | | Also `showfps` (on by default; `showfps 0` at the console turns it off): resolution and FPS, top right, saved with the config. |
| `src/in_trace.c` | quakegeneric's front ends | Keys (WinQuake's own set-1 scancode table) and mouse, for quakegeneric's `in_null.c`. |
| `src/overrides/snd_mix.c` | id's `snd_mix.c` | Identical but for one marked `[qtrace]` line: the volume is scaled by 0.15 before the mix is clipped. Quake runs its effects into the clip; unscaled it measured ~15 dB above DOOM through TERMinator (probe: −14.9 vs −29.9 dBFS RMS); now −25 or so, beside Tyrian's −24.6. |
| `src/snd_trace.c` | `snd_null.c` | The "sound card" for WinQuake's mixer: 16-bit stereo 44.1 kHz ring, sent to TERMinator as its queue drains. |

Quake's one `setjmp`/`longjmp` (Host_Error abandoning a frame) is real: built as WebAssembly exceptions
(`-mllvm -wasm-enable-sjlj`, `-lsetjmp`), which TERMinator's sandbox runs.

## Door messages

Down: `file name=<n> off=<o> total=<t>\n<bytes>` (the config; a save being loaded), `list name=<n>\n<start>` (a save's
first two lines + 1 byte), `none name=<n>`, `pak=<sha256>` (starts the game), `quit` (time's up).
Up: `put name=<n> off=<o> total=<t>\n<bytes>` (3000-byte pieces, `door/files.h` FILES_CHUNK), `get name=<n>`.

## Known gaps

- No CD music (the shareware never had any; Quake's music was CD audio).
- Loading a save fetched from the door is tested by the door test and by compiling `src/qfiles.c` natively against
  stand-ins (the Load menu reads only titles; loading fetches once, byte-identical); not yet over a real call.
- The menus and status bar don't scale with the resolution, as in WinQuake: at 1024x768 they're small.
- Network play is compiled out (`net_none`); single player only.
