# The QUAKE door. It doesn't run the game: it sends quake.wasm and quake-shareware.zip to TERMinator, which plays them
# on the player's own machine (see ../module). Built for the BBS box; the same C builds anywhere POSIX.
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
LDFLAGS =

OBJ = main.o door.o trace_quake.o trace_door.o files.o sha256.o

quakedoor: $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Everything the sysop needs in one folder: the door, the game and its data.
#   quake.wasm            ships in this folder; ../module rebuilds it (needs wasi-sdk) if it's missing
#   quake-shareware.zip   id's shareware Quake 1.06 (about 9 MB), downloaded and repacked the first time, with id's
#                         licence inside it; never shipped here
install: quakedoor
	@if [ -f quake.wasm ]; then echo "quake.wasm: already here"; \
	elif [ -f ../module/quake.wasm ]; then cp ../module/quake.wasm . && echo "quake.wasm: copied"; \
	else echo "quake.wasm: MISSING -- run make in ../module (needs wasi-sdk), then make install again"; fi
	@if [ -f quake-shareware.zip ]; then echo "quake-shareware.zip: already here"; \
	else bash ../tools/get_shareware.sh ../data && python3 ../tools/mkzip.py ../data/q106 quake-shareware.zip; fi

clean:
	rm -f $(OBJ) quakedoor

.PHONY: clean install
