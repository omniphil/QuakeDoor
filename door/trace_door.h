/*
 * trace_door.h -- the door side of TRACE (TERMinator Render And Compute Engine), shared by every TRACE door.
 * Everything here is tdoor_*, so it never clashes with the module API (trace_api.h: trace_send...), which a door
 * that also runs its game natively for an ANSI mode links in too.
 *
 * One copy of everything a door needs to hand its game to TERMinator: find out whether the terminal has TRACE, send
 * the module and its data (once: TERMinator caches both by SHA-256), start it, talk to it while it runs, ask for the
 * player's own copy of a file, and stop it. The protocol is BBSGames/Doom/ENGINE.md section 3a.
 *
 * THE SOURCE IS HERE, in BBSGames/TraceDoor. Each door folder carries a copy so it still builds on the BBS box on its
 * own; ./sync.sh refreshes those copies. Edit this one, never a copy.
 *
 * Binary frames: when the Query reply has bin=1, uploads and messages go as binary frames
 * (ESC _ TERMinator:TRACE;Bin;<escaped bytes> ESC \) instead of base64, about 3.5% over the raw size instead of 33%,
 * and in 60 KB pieces instead of 3 KB. Older TERMinators get base64 as before. Nothing else changes for the door.
 *
 * Needs the door's door.h (door_write, door_write_raw, door_read_char_timeout, door_time_remaining) and sha256.h.
 */

#ifndef TRACE_DOOR_H
#define TRACE_DOOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A file the door sends: the module, or its data. */
typedef struct {
    uint8_t *data;
    size_t   size;
    char     hash[65];   /* SHA-256, lower-case hex */
} tdoor_blob_t;

/* Replies tdoor_wait_reply() returns */
enum {
    TDOOR_REPLY_NONE      = 0,    /* nothing in time */
    TDOOR_REPLY_READY     = 'R',  /* the module is running */
    TDOOR_REPLY_NEED      = 'N',  /* send the module */
    TDOOR_REPLY_HAVE      = 'H',  /* the asset (or the player's imported file) is there */
    TDOOR_REPLY_NEEDASSET = 'A',  /* send the asset */
    TDOOR_REPLY_CLOSED    = 'C',  /* the module ended, or couldn't start */
    TDOOR_REPLY_NOIMPORT  = 'I',  /* the player didn't give us their file */
};

/* A message the module sent the door (its "network": saves, scores, moves...). */
typedef void (*tdoor_message_fn)(const unsigned char *data, size_t len);

/* The module id every command names (e.g. "doom"). Call once, before anything else. */
void tdoor_init(const char *module_id, tdoor_message_fn on_message);

/* Loads a file that ships next to the door binary (else from the current directory), up to max_size bytes. */
bool tdoor_load_blob(const char *filename, tdoor_blob_t *blob, size_t max_size);

/*
 * Asks the terminal (half a second at most; terminals without TRACE never answer) and keeps the reply. True when it
 * has TRACE with a module runner (wasm=1) and every flag in need, a NULL-terminated list like
 * { "audio=1", "assets=1", NULL }. Binary frames are used from here on when the reply offers bin=1.
 */
bool tdoor_detect(const char *const *need);

/* Whether the last Query reply had this flag ("mouse=1", "pad=1", "import=1"...). */
bool tdoor_has(const char *flag);

/* Waits for TERMinator's next reply about our module (TDOOR_REPLY_*), passing module messages on meanwhile. */
int tdoor_wait_reply(int timeout_ms);

/* The hash in the last Have reply: which of an Import's hashes the player's file matched. */
const char *tdoor_last_have_hash(void);

/* Why the module last closed: the "error=" field of a Closed reply, or "" if it
 * closed normally. A module that fails to start reports its reason here. */
const char *tdoor_last_close_reason(void);

/* The raw Query reply, for diagnostics -- which capabilities this terminal has. */
const char *tdoor_info(void);

/* Offers an asset by hash and uploads it only if the terminal doesn't have it yet. progress gets 0-100. */
bool tdoor_send_asset(const tdoor_blob_t *asset, void (*progress)(int));

/*
 * Opens the module: options are extra Open fields without the leading ';' ("exclusive=1", "rect=1,1,22,80"), or NULL.
 * Uploads it the first time. True once it runs.
 */
bool tdoor_open(const tdoor_blob_t *module, const char *options, void (*progress)(int));

/* A short text message for the module ("wad=<hash>"): sent as it is, no encoding. No ';' ESC or control bytes. */
void tdoor_send_text(const char *text);

/* A message for the module: a header line and, optionally, a payload after a newline (binary-safe). */
void tdoor_send(const char *head, const void *payload, size_t len);

/*
 * Asks for the player's OWN copy of a file (a game they bought, a ROM): the door never sends it and never sees it.
 * hashes is a comma-separated list of the SHA-256s the door accepts (every release it knows), name what the player is
 * asked for. True when TERMinator has it (tdoor_last_have_hash() says which); the module then reads it by that hash
 * like any asset. Needs import=1. The player may take a while to answer: this waits up to timeout_ms.
 */
bool tdoor_import(const char *hashes, const char *name, int timeout_ms);

/* Waits until the module ends (TDOOR_REPLY_CLOSED) or the caller's time runs out. */
void tdoor_wait_closed(void);

/* Whether the module is running (it was opened, and hasn't been seen to close). */
bool tdoor_is_open(void);

/*
 * Stops the module. With quit_message, the game is first asked to quit by itself (so it can save and send its files
 * on the way out) and given up to grace_ms; Close is the fallback.
 */
void tdoor_close(const char *quit_message, int grace_ms);

#endif /* TRACE_DOOR_H */
