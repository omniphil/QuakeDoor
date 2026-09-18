/*
 * files.c -- the player's Quake files, kept on the BBS, one set per player.
 *
 * The game runs in a sandbox on the player's PC and has nowhere of its own to keep anything: its memory goes when the
 * call ends. So its files live here instead, and follow the player whichever machine they call from:
 *
 *   config.cfg          key bindings and settings, written when the player quits
 *   s0.sav .. s11.sav   the twelve saved-game slots of Quake's Save and Load menus
 *
 * The config is sent whole at the start. A saved game is tens of KB, so at the start only its first lines go, which
 * are all the Load menu reads (a version number and the save's title); the rest is sent when the game asks for it,
 * because the player is loading that save. The game sends a file back whenever it writes it. Only these names are
 * accepted, so nothing the module says can reach any other file.
 *
 * Files live in saves/<player>/ beside the door binary. A file arriving is written beside the old one and renamed
 * over it only once it's whole, and the one it replaces is kept as <name>.bak, so a dropped call can't lose a save.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "files.h"

#define MAX_FILE_BYTES (4 * 1024 * 1024)

static const char *const g_names[] =
{
    "config.cfg",
    "s0.sav", "s1.sav", "s2.sav", "s3.sav", "s4.sav", "s5.sav",
    "s6.sav", "s7.sav", "s8.sav", "s9.sav", "s10.sav", "s11.sav",
};
#define NAME_COUNT (sizeof(g_names) / sizeof(g_names[0]))

#define DIR_MAX 512
static char g_dir[DIR_MAX];
static char g_player[80];

/* A file on its way up from the game, one per name */
static struct
{
    unsigned char *data;
    size_t         have, total;
} g_incoming[NAME_COUNT];

/* Where the door keeps things, which is beside its own binary rather than the current directory */
static void door_dir(char *out, size_t size)
{
    ssize_t len = readlink("/proc/self/exe", out, size - 1);
    char *slash;

    if (len <= 0)
    {
        snprintf(out, size, ".");
        return;
    }
    out[len] = '\0';
    slash = strrchr(out, '/');
    if (slash != NULL)
        *slash = '\0';
}

static int name_index(const char *name)
{
    for (size_t i = 0; i < NAME_COUNT; i++)
        if (strcmp(g_names[i], name) == 0)
            return (int)i;
    return -1;
}

/*
 * A folder name for this player: their handle, plus the BBS's own user number.
 *
 * The handle alone isn't enough to tell two people apart, because cutting it down to plain characters can make two
 * different handles the same ("Phil" and "P.h.i.l" both become "phil"). The user number is unique on the board, so
 * the two together can't collide. Cutting the handle down also means it can only ever name a folder inside saves/.
 */
void files_init(const char *player, int user_number)
{
    char base[DIR_MAX - 128];
    char name[80];
    char handle[48];
    size_t n = 0;

    for (const char *p = player; *p != '\0' && n < sizeof(handle) - 1; p++)
        if (isalnum((unsigned char)*p) || *p == '-' || *p == '_')
            handle[n++] = (char)tolower((unsigned char)*p);
    handle[n] = '\0';
    if (n == 0)
        snprintf(handle, sizeof(handle), "player");

    if (user_number > 0)
        snprintf(name, sizeof(name), "%s-%d", handle, user_number);
    else
        snprintf(name, sizeof(name), "%s", handle);

    snprintf(g_player, sizeof(g_player), "%s", name);

    door_dir(base, sizeof(base));
    snprintf(g_dir, sizeof(g_dir), "%s/saves", base);
    mkdir(g_dir, 0755);
    snprintf(g_dir, sizeof(g_dir), "%s/saves/%s", base, name);
    mkdir(g_dir, 0755);
}

const char *files_player(void)
{
    return g_player;
}

/* A whole file into memory; NULL if there's no such file (or it's empty, or absurdly big) */
static unsigned char *read_file(const char *name, long *size_out)
{
    char path[DIR_MAX + 32];
    unsigned char *data;
    long size;
    FILE *fp;

    snprintf(path, sizeof(path), "%s/%s", g_dir, name);
    fp = fopen(path, "rb");
    if (fp == NULL)
        return NULL;
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0 || size > MAX_FILE_BYTES || (data = malloc((size_t)size)) == NULL)
    {
        fclose(fp);
        return NULL;
    }
    if (fread(data, 1, (size_t)size, fp) != (size_t)size)
    {
        free(data);
        data = NULL;
    }
    fclose(fp);
    *size_out = size;
    return data;
}

/* A whole file, in pieces of FILES_CHUNK:  file name=<n> off=<o> total=<t>\n<bytes> */
static void send_whole(const char *name, const unsigned char *data, long size,
                       void (*send)(const char *head, const void *payload, size_t len))
{
    char head[96];

    for (long off = 0; off < size; off += FILES_CHUNK)
    {
        size_t len = size - off < FILES_CHUNK ? (size_t)(size - off) : FILES_CHUNK;
        snprintf(head, sizeof(head), "file name=%s off=%ld total=%ld", name, off, size);
        send(head, data + off, len);
    }
}

/*
 * The config whole; for each save, its start:  list name=<n>\n<bytes>
 * The start is the first two lines (version and title, what Quake's Load menu reads with fscanf) and the first
 * character of the third: fscanf's trailing "\n" reads on to the next non-blank character, which has to be there or it
 * would ask for more of the file.
 */
void files_send_all(void (*send)(const char *head, const void *payload, size_t len))
{
    for (size_t i = 0; i < NAME_COUNT; i++)
    {
        long size = 0;
        unsigned char *data = read_file(g_names[i], &size);

        if (data == NULL)
            continue;
        if (i == 0)
        {
            send_whole(g_names[i], data, size, send);
        }
        else
        {
            long lines = 0, end = 0;
            char head[64];
            while (end < size && lines < 2)
                if (data[end++] == '\n')
                    lines++;
            if (lines == 2 && end < size && end < 2048)
            {
                snprintf(head, sizeof(head), "list name=%s", g_names[i]);
                send(head, data, (size_t)end + 1);
            }
        }
        free(data);
    }
}

void files_send_one(const char *name, void (*send)(const char *head, const void *payload, size_t len))
{
    long size = 0;
    unsigned char *data = name_index(name) > 0 ? read_file(name, &size) : NULL;
    char head[64];

    if (data == NULL)
    {
        snprintf(head, sizeof(head), "none name=%s", name);
        send(head, NULL, 0);
        return;
    }
    send_whole(name, data, size, send);
    free(data);
}

/* Pieces arrive in order; a piece at offset 0 starts the file again (the game wrote a newer copy mid-send). */
int files_receive_chunk(const char *name, size_t offset, size_t total, const unsigned char *data, size_t size)
{
    int i = name_index(name);
    char path[DIR_MAX + 32], temp[DIR_MAX + 40], backup[DIR_MAX + 40];
    FILE *fp;

    if (i < 0 || total == 0 || total > MAX_FILE_BYTES)
        return 0;

    if (offset == 0)
    {
        free(g_incoming[i].data);
        g_incoming[i].data = malloc(total);
        g_incoming[i].have = 0;
        g_incoming[i].total = total;
    }
    if (g_incoming[i].data == NULL || offset != g_incoming[i].have || total != g_incoming[i].total ||
        offset + size > total)
    {
        /* A piece went missing: drop the whole file rather than keep one with a hole in it */
        free(g_incoming[i].data);
        g_incoming[i].data = NULL;
        return 0;
    }
    memcpy(g_incoming[i].data + offset, data, size);
    g_incoming[i].have += size;
    if (g_incoming[i].have < total)
        return 0;

    snprintf(path, sizeof(path), "%s/%s", g_dir, name);
    snprintf(temp, sizeof(temp), "%s/%s.new", g_dir, name);
    snprintf(backup, sizeof(backup), "%s/%s.bak", g_dir, name);
    fp = fopen(temp, "wb");
    if (fp != NULL)
    {
        int ok = fwrite(g_incoming[i].data, 1, total, fp) == total;
        ok &= fclose(fp) == 0;
        if (ok)
        {
            rename(path, backup);      /* the previous copy, in case this one turns out to be bad */
            ok = rename(temp, path) == 0;
        }
        if (!ok)
            remove(temp);
    }
    free(g_incoming[i].data);
    g_incoming[i].data = NULL;
    return 1;
}
