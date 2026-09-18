/* trace_quake.h -- sending Quake to the player's terminal and starting it; see trace_quake.c. */

#ifndef TRACE_QUAKE_H
#define TRACE_QUAKE_H

#include <stdbool.h>
#include <stddef.h>

/* Reads quake.wasm and quake-shareware.zip from beside the door binary. False means the door isn't installed properly. */
bool trace_quake_load_files(void);

/* Does this terminal have TRACE, with everything Quake needs (its own module, assets, sound, sending back)? */
bool trace_quake_detect(void);

/* Makes sure the terminal has the game data, uploading it if this is the player's first game. */
bool trace_quake_send_data(void (*progress)(int));

/* Starts the game on the player's machine (uploading it the first time), with the player's own files. */
bool trace_quake_open(void);

/* Waits until the player quits, or their time runs out, keeping the files the game sends back. */
void trace_quake_wait(void);

/* Sends a message to the game: a line of text, and optionally a payload after it. */
void trace_quake_send(const char *head, const void *payload, size_t len);

/* Stops the game if it's still running. */
void trace_quake_close(void);

#endif
