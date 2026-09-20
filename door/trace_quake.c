/*
 * trace_quake.c -- the TRACE side of the Quake door: sending the game to the player's terminal and starting it.
 *
 * The door carries two files next to its binary:
 *   quake.wasm             the whole game, built from ../module (renderer, sound and all)
 *   quake-shareware.zip    id's shareware episode (pak0.pak) with id's licence, as id allows it to be passed on:
 *                          whole, compressed, licence included. Made by ../tools/mkzip.py.
 *
 * Both go to TERMinator once, which caches them by SHA-256; every call after that starts straight away. Then the
 * module is opened over the whole screen with the keyboard, the player's files are sent, and the door waits until
 * the player quits, keeping whatever the game sends back. The talking itself (detection, uploads, messages,
 * binary frames) is the shared door library, trace_door.c.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "trace_quake.h"
#include "trace_door.h"
#include "door.h"
#include "files.h"

#define MODULE_FILE  "quake.wasm"
#define PAK_FILE     "quake-shareware.zip"

static tdoor_blob_t module_blob, pak_blob;

static void handle_module_message(const unsigned char *data, size_t len);

bool trace_quake_load_files(void)
{
    tdoor_init("quake", handle_module_message);
    return tdoor_load_blob(MODULE_FILE, &module_blob, 16 * 1024 * 1024)
        && tdoor_load_blob(PAK_FILE, &pak_blob, 64 * 1024 * 1024);
}

bool trace_quake_detect(void)
{
    /* Quake needs all four: a module of its own, the data as an asset, sound, and a way to send saves back */
    static const char *const need[] = { "assets=1", "audio=1", "send=1", NULL };
    tdoor_init("quake", handle_module_message);
    return tdoor_detect(need);
}

/*
 * A message from the game itself (it can only talk to us):
 *   put name=<n> off=<o> total=<t>\n...   a piece of one of the player's files to keep for them
 *   get name=<n>                          send the whole of this saved game: the player is loading it
 */
static void handle_module_message(const unsigned char *data, size_t len)
{
    char head[128], name[32] = "";
    const unsigned char *payload = NULL;
    size_t payload_len = 0, head_len;
    const unsigned char *newline = memchr(data, '\n', len);
    long off = 0, total = 0;

    head_len = newline != NULL ? (size_t)(newline - data) : len;
    if (head_len >= sizeof(head))
        head_len = sizeof(head) - 1;
    memcpy(head, data, head_len);
    head[head_len] = '\0';
    if (newline != NULL)
    {
        payload = newline + 1;
        payload_len = len - (size_t)(payload - data);
    }

    if (strstr(head, "name=")) sscanf(strstr(head, "name="), "name=%31s", name);
    if (strstr(head, "off=")) sscanf(strstr(head, "off="), "off=%ld", &off);
    if (strstr(head, "total=")) sscanf(strstr(head, "total="), "total=%ld", &total);

    if (!strncmp(head, "put", 3) && payload != NULL && off >= 0 && total >= 0)
        files_receive_chunk(name, (size_t)off, (size_t)total, payload, payload_len);
    else if (!strncmp(head, "get", 3))
        files_send_one(name, trace_quake_send);
}

/*
 * Sends a message to the module: a line of text, and optionally a payload after it. TERMinator hands the module
 * exactly these bytes, so a saved game travels as itself rather than as text.
 */
void trace_quake_send(const char *head, const void *payload, size_t len)
{
    tdoor_send(head, payload, len);
}

/* The game data: offered by hash, uploaded only if this player hasn't had it before. */
bool trace_quake_send_data(void (*progress)(int))
{
    return tdoor_send_asset(&pak_blob, progress);
}

bool trace_quake_open(void)
{
    char buf[128];

    if (!tdoor_open(&module_blob, "exclusive=1", NULL))
        return false;

    /* First the player's own files, so they are there when the game looks; then the data, which starts the game */
    files_send_all(trace_quake_send);
    snprintf(buf, sizeof(buf), "pak=%s", pak_blob.hash);
    tdoor_send_text(buf);
    return true;
}

/* Waits until the player quits Quake (TERMinator sends Closed), or the call runs out of time. */
void trace_quake_wait(void)
{
    tdoor_wait_closed();
}

/*
 * Stops the game if it's still running (the player's time ran out). TERMinator's Close ends a module on the spot, so
 * the game is asked to quit first: it saves its files on the way out, sends them here, and closes itself. Close is
 * only the fallback if it doesn't within a few seconds.
 */
void trace_quake_close(void)
{
    tdoor_close("quit", 8000);
}
