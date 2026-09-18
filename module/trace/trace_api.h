/*
 * trace_api.h -- the TRACE (TERMinator Render And Compute Engine) module API.
 *
 * A TRACE module is a WebAssembly file that a BBS door sends to TERMinator. TERMinator caches it by SHA-256 and runs
 * it inside a sandbox (gamesandbox.exe, Wasmtime). This header is everything a module can use: the host functions below,
 * plus plain C (wasi-libc) and pthreads for multicore work. There is no file, network or process access.
 *
 * Build a module with wasi-sdk (see BBSGames/Fractals/trace/Makefile for a working example):
 *   clang --target=wasm32-wasip1-threads -O3 -msimd128 -pthread -mexec-model=reactor \
 *         -Wl,--import-memory,--shared-memory,--max-memory=<bytes> \
 *         -Wl,--export=wasi_thread_start -Wl,--export-dynamic module.c -o module.wasm
 *
 * Lifecycle: the host calls trace_init() once, then for every batch of events it calls trace_on_resize() /
 * trace_on_data() for each event (in order) and trace_update() once after the batch. Do the real work in
 * trace_update(); check trace_input_pending() during long work and return early when it's true, since newer events
 * are waiting (e.g. the player zoomed again).
 *
 * As well as drawing, a module can do what a game needs, so that new games mean new doors and never a new
 * TERMinator: keep a steady tick (trace_set_tick), read a clock (trace_time_ms), make sound (trace_audio_*), talk to
 * the door (trace_send), read assets the door sent such as a WAD (trace_asset_*), and keep a small settings blob
 * between sessions (trace_store_*). Everything here is general: nothing in it knows what kind of module you write.
 *
 * A module that imports something the player's TERMinator doesn't have won't start there, so doors ask for what they
 * need in the Query reply (audio=1, assets=1, send=1, ...) and pick a module to match.
 */

#ifndef TRACE_API_H
#define TRACE_API_H

#include <stdint.h>

#define TRACE_API_VERSION 1

#define TRACE_IMPORT(name) __attribute__((import_module("trace"), import_name(#name)))
#define TRACE_EXPORT(name) __attribute__((export_name(#name)))

/* ---- host functions a module can call ---- */

/* Show a frame: width*height BGRA pixels (0xAARRGGBB little-endian) at pixels. flags: TRACE_PRESENT_ASPECT_4_3 to
 * show it at 4:3 (tall pixels), else square pixels shown 1:1. The host copies the pixels before returning. */
#define TRACE_PRESENT_ASPECT_4_3 1
TRACE_IMPORT(present) void trace_present(const uint32_t *pixels, int32_t width, int32_t height, int32_t flags);

/* 1 when events are waiting (the current work is out of date), else 0. Cheap: call it as often as you like. */
TRACE_IMPORT(input_pending) int32_t trace_input_pending(void);

/* Logical processors on the player's PC, for sizing a thread pool. */
TRACE_IMPORT(cpu_count) int32_t trace_cpu_count(void);

/* The largest frame trace_present accepts. */
TRACE_IMPORT(frame_capacity) void trace_frame_capacity(int32_t *width, int32_t *height);

/* A line for TERMinator's debug output. */
TRACE_IMPORT(log) void trace_log(const char *text, int32_t length);

/* Stop: the module is finished (the player quit the game). TERMinator closes the picture, gives the keyboard back to
 * the terminal and tells the door. It does not return. code is 0 for a normal finish. */
TRACE_IMPORT(quit) void trace_quit(int32_t code);

/* Milliseconds since the module started. Monotonic: it never jumps or goes backwards. */
TRACE_IMPORT(time_ms) int32_t trace_time_ms(void);

/* Ask to be updated hz times a second even when no events arrive, for anything that animates or simulates
 * (a game runs at 35, a smooth zoom at 60). 0, the default, means only when events arrive. The host may run slower
 * if the module can't keep up, so drive real timing from trace_time_ms, never from the number of calls. */
TRACE_IMPORT(set_tick) void trace_set_tick(int32_t hz);

/* ---- talking to the door ----
 * The door is the only thing a module can reach: no sockets, no addresses, no other machines. For a multiplayer game
 * the door is the server, so "send to the door" is the whole network API. Bytes arrive at the door in order, and what
 * it sends back comes in through trace_on_data(). Payloads are binary-safe. */

/* Send bytes to the door. Returns the number accepted: 0 when the link is backed up, so a game can drop an update
 * instead of queueing it. At most TRACE_SEND_MAX bytes per call. */
#define TRACE_SEND_MAX 4096
TRACE_IMPORT(send) int32_t trace_send(const void *data, int32_t length);

/* How many bytes trace_send would accept right now. Check it before building an update to skip the work entirely. */
TRACE_IMPORT(send_room) int32_t trace_send_room(void);

/* ---- sound ----
 * S16 stereo frames at 44100 Hz (TE_AUDIO_RATE), mixed into TERMinator's own output, so the player's volume control
 * covers it. Write a little at a time from the same place you present frames, and keep roughly 50-100 ms queued. */

/* Queue frames (2 shorts each). Returns the frames accepted, which can be fewer than asked, or 0 when full. */
TRACE_IMPORT(audio_write) int32_t trace_audio_write(const int16_t *frames, int32_t frame_count);

/* Frames the queue can still take. Below about a tenth of a second's worth, write more or the sound will break up. */
TRACE_IMPORT(audio_room) int32_t trace_audio_room(void);

/* ---- assets the door sent ----
 * Big read-only files (a WAD, a tileset, music) that the door uploads once and TERMinator caches by SHA-256, the same
 * way it caches the module itself. The door tells the module which hash to use; a module can only read an asset that
 * the door gave it, and can't write, list or delete anything. sha256 is 64 lower-case hex characters. */

/* The asset's size in bytes, or 0 when the module has no such asset. */
TRACE_IMPORT(asset_size) int32_t trace_asset_size(const char *sha256);

/* Read from an asset. Returns bytes read, or 0 at the end or on a bad hash. */
TRACE_IMPORT(asset_read) int32_t trace_asset_read(const char *sha256, int32_t offset, void *buffer, int32_t length);

/* ---- settings that survive between sessions ----
 * One small private blob per module (up to TE_STORE_MAX_BYTES), kept on the player's PC: what they'd expect a game to
 * remember, like resolution or key bindings. It is not shared with the door, other modules or other boards. */

/* Read the blob into buffer. Returns the bytes read (0 when nothing is stored yet). */
TRACE_IMPORT(store_read) int32_t trace_store_read(void *buffer, int32_t length);

/* Replace the blob (length 0 clears it). Returns 0 on success. */
TRACE_IMPORT(store_write) int32_t trace_store_write(const void *data, int32_t length);

/* ---- functions a module must export ---- */

/* Called once, before anything else. Return 0 on success. */
TRACE_EXPORT(trace_init) int32_t trace_init(void);

/* Memory for the host to pass data in; trace_free is called when the host is done with it. */
TRACE_EXPORT(trace_alloc) void *trace_alloc(int32_t size);
TRACE_EXPORT(trace_free) void trace_free(void *ptr);

/* The size (device pixels) the module is shown at. */
TRACE_EXPORT(trace_on_resize) void trace_on_resize(int32_t width, int32_t height);

/* Data the door sent (e.g. a viewport); only valid during the call, copy what you keep. */
TRACE_EXPORT(trace_on_data) void trace_on_data(const char *data, int32_t length);

/* Keyboard/mouse (engine contract TE_IN_* records), for modules that take input; others can ignore it. */
TRACE_EXPORT(trace_on_input) void trace_on_input(int32_t type, int32_t flags, int32_t a, int32_t b, int32_t c);

/* After each batch of events: do the work (render, simulate) and present. */
TRACE_EXPORT(trace_update) void trace_update(void);

#endif /* TRACE_API_H */
