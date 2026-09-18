/* files.h -- the player's own Quake files, kept on the BBS, one set per player; see files.c. */

#ifndef FILES_H
#define FILES_H

#include <stddef.h>

/* Bytes of file per message. Base64 plus the TRACE wrapper has to stay under the terminal's 8 KB command limit.
 * The module sends pieces of the same size (module/src/qfiles.c CHUNK): the two must agree. */
#define FILES_CHUNK 3000

/* Picks the folder for this player (their handle and BBS user number, from the drop file) and makes sure it exists. */
void files_init(const char *player, int user_number);

/* The folder name this player's files are kept under, for the door to show them. */
const char *files_player(void);

/* Sends the game this player's config whole, and for each saved game just its first lines (what the Load menu shows). */
void files_send_all(void (*send)(const char *head, const void *payload, size_t len));

/* Sends one whole saved game the player is loading, in pieces, or "none" if it isn't there. */
void files_send_one(const char *name, void (*send)(const char *head, const void *payload, size_t len));

/* Takes a piece of a file from the game. Returns 1 when the last piece completes the file and it has been kept. */
int files_receive_chunk(const char *name, size_t offset, size_t total, const unsigned char *data, size_t size);

#endif
