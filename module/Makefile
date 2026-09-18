# Builds quake.wasm: the whole of Quake (game, software renderer and sound) as a TRACE module the door sends to
# TERMinator.
#
# quakegeneric (id's GPL WinQuake, cut down to a few platform hooks) comes from ../third_party unmodified. Its null
# system, video and sound drivers are replaced by src/*.c, and WinQuake's own sound mixer is put back from
# ../third_party/id-winquake-sound (unmodified, from id's GPL release). The data archive is unpacked with tinf.
# Run in WSL/Linux with wasi-sdk.
WASI_SDK ?= $(HOME)/tools/wasi-sdk-34.0-x86_64-linux
CC = $(WASI_SDK)/bin/clang

QG    = ../third_party/quakegeneric/source
SOUND = ../third_party/id-winquake-sound
TINF  = ../third_party/tinf
# trace_api.h: the client's own tree when building here, or the copy bundled with the published source
TRACE_INCLUDE = $(firstword $(wildcard ../../../TERMinator-Windows/trace/include) trace)

# Quake's setjmp/longjmp (Host_Error drops the rest of a frame) are WebAssembly exceptions: TERMinator's sandbox runs
# them (Wasmtime 48, and it turns them on explicitly).
SJLJ = -mllvm -wasm-enable-sjlj -mllvm -wasm-use-legacy-eh=false

CFLAGS = --target=wasm32-wasip1-threads -O2 -pthread -std=gnu99 $(SJLJ) \
         -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function -Wno-missing-braces \
         -Wno-pointer-sign -Wno-dangling-else -Wno-parentheses -Wno-deprecated-non-prototype \
         -Wno-incompatible-pointer-types-discards-qualifiers -Wno-unused-value -Wno-sometimes-uninitialized \
         -Wno-unused-label -Wno-tautological-constant-out-of-range-compare -Wno-format-security \
         -D_WASI_EMULATED_SIGNAL -D_GNU_SOURCE \
         -include qtrace_compat.h -I include -I src -I $(TRACE_INCLUDE) -I $(QG) -I $(TINF)
LDFLAGS = -mexec-model=reactor -Wl,--import-memory,--export-memory,--shared-memory,--max-memory=268435456 \
          -Wl,--export=wasi_thread_start -Wl,--strip-all -Wl,-z,stack-size=1048576 -lm -lsetjmp

# quakegeneric's engine, less its front ends and the null drivers replaced here
REPLACED = quakegeneric.c quakegeneric_dos.c quakegeneric_null.c quakegeneric_sdl2.c quakegeneric_w32.c \
           snd_null.c sys_null.c vid_null.c
GAME_SRC = $(filter-out $(addprefix $(QG)/, $(REPLACED)), $(wildcard $(QG)/*.c))
# snd_mix.c comes from src/overrides: id's, with the volume scaled before clipping (see the top of that file)
SOUND_SRC = $(SOUND)/snd_dma.c $(SOUND)/snd_mem.c src/overrides/snd_mix.c
TRACE_SRC = $(wildcard src/*.c) $(TINF)/tinflate.c

SRC = $(GAME_SRC) $(SOUND_SRC) $(TRACE_SRC)
OBJ = $(patsubst %.c, obj/%.o, $(notdir $(SRC)))
VPATH = src:src/overrides:$(QG):$(SOUND):$(TINF)

quake.wasm: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) $(LDFLAGS) -o $@

obj/%.o: %.c | obj
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

obj:
	mkdir -p obj

clean:
	rm -rf obj quake.wasm

.PHONY: clean
