/*
 * qfiles.c -- Quake's files, without a file system.
 *
 * Every file Quake opens comes through fopen (redirected here by include/qtrace_compat.h), under com_gamedir, "./id1":
 *   - id1/pak0.pak: the shareware episode, read-only, unpacked in memory from the door's archive (qtrace.c).
 *   - id1/config.cfg and the savegames id1/s0.sav .. id1/s11.sav: the player's own files. These live on the BBS, per
 *     player. Whenever Quake writes one, the new contents go up to the door a piece at a time.
 *   - Anything else (a screenshot, a demo recording, a save under another name typed at the console) has nowhere to
 *     go and simply fails to open, which Quake reports and carries on from.
 *
 * Savegames can be a few hundred KB each, so the door doesn't send them all at the start. It sends each one's first
 * two lines, which is all the Load menu reads (a version and the save's title), and the rest only when the game reads
 * past them: loading that save. That read waits here, on the game thread, for the door to send it, which takes a
 * moment on a fast link; the game shows its loading screen meanwhile.
 *
 * What Quake gets back is always a real stdio stream (fmemopen, open_memstream, fopencookie), because it uses
 * fprintf and fscanf on them.
 */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "trace_api.h"
#include "qtrace.h"

#undef fopen
#undef fclose
#undef printf
#undef exit

#define CHUNK        3000               /* bytes per message to the door; the door uses the same (door/files.h) */
#define MAX_BYTES    (4 * 1024 * 1024)  /* no save comes anywhere near this */
#define FETCH_WAIT   20000              /* ms to wait for a save the player is loading */

typedef struct
{
    char      name[16];         /* as the door knows it: "config.cfg", "s3.sav" */

    uint8_t  *data;             /* the whole file, when we have it */
    size_t    size;
    int       present;

    uint8_t  *prefix;           /* only its start (what the Load menu reads), until it's fetched */
    size_t    prefix_size;
    int       fetching;         /* asked the door for it; waiting */

    uint8_t  *incoming;         /* pieces arriving from the door */
    size_t    incoming_total, incoming_have;

    uint8_t  *outgoing;         /* a copy on its way to the door, so the game can write again meanwhile */
    size_t    outgoing_size, outgoing_sent;
    int       outgoing_busy;
} user_file_t;

#define USER_FILES 13
static user_file_t     g_files[USER_FILES];
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_arrived = PTHREAD_COND_INITIALIZER;

static void init_names(void)
{
    static int done;
    if (done)
        return;
    done = 1;
    snprintf(g_files[0].name, sizeof(g_files[0].name), "config.cfg");
    for (int i = 0; i < 12; i++)
        snprintf(g_files[i + 1].name, sizeof(g_files[i + 1].name), "s%d.sav", i);
}

static user_file_t *find(const char *name)
{
    init_names();
    for (int i = 0; i < USER_FILES; i++)
        if (strcmp(g_files[i].name, name) == 0)
            return &g_files[i];
    return NULL;
}

/* ---- from the door (TERMinator's thread) ---- */

void qfiles_on_file(const char *name, size_t off, size_t total, const uint8_t *data, size_t len)
{
    user_file_t *uf;

    pthread_mutex_lock(&g_lock);
    uf = find(name);
    if (uf == NULL || total > MAX_BYTES)
        goto done;
    if (off == 0)
    {
        free(uf->incoming);
        uf->incoming = (uint8_t *)malloc(total > 0 ? total : 1);
        uf->incoming_total = total;
        uf->incoming_have = 0;
    }
    if (uf->incoming == NULL || off != uf->incoming_have || off + len > uf->incoming_total)
    {
        qtrace_log("quake: a piece of %s arrived out of order; ignoring it", name);
        free(uf->incoming);
        uf->incoming = NULL;
        goto done;
    }
    memcpy(uf->incoming + off, data, len);
    uf->incoming_have += len;
    if (uf->incoming_have == uf->incoming_total)
    {
        free(uf->data);
        uf->data = uf->incoming;
        uf->size = uf->incoming_total;
        uf->present = 1;
        uf->incoming = NULL;
        uf->fetching = 0;
        pthread_cond_broadcast(&g_arrived);
        qtrace_log("quake: %s from the BBS, %zu bytes", name, uf->size);
    }
done:
    pthread_mutex_unlock(&g_lock);
}

void qfiles_on_list(const char *name, const uint8_t *prefix, size_t len)
{
    user_file_t *uf;

    pthread_mutex_lock(&g_lock);
    uf = find(name);
    if (uf != NULL && !uf->present && len > 0 && len < 4096)
    {
        free(uf->prefix);
        uf->prefix = (uint8_t *)malloc(len);
        if (uf->prefix != NULL)
        {
            memcpy(uf->prefix, prefix, len);
            uf->prefix_size = len;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

void qfiles_on_none(const char *name)
{
    user_file_t *uf;

    pthread_mutex_lock(&g_lock);
    uf = find(name);
    if (uf != NULL)
    {
        uf->fetching = 0;
        free(uf->prefix);
        uf->prefix = NULL;
        uf->prefix_size = 0;
        pthread_cond_broadcast(&g_arrived);
    }
    pthread_mutex_unlock(&g_lock);
}

/* ---- to the door (the game thread) ---- */

static int send_message(const char *head, const void *payload, size_t len)
{
    static uint8_t message[CHUNK + 128];
    size_t head_len = strlen(head);

    memcpy(message, head, head_len);
    if (payload != NULL)
    {
        message[head_len++] = '\n';
        memcpy(message + head_len, payload, len);
    }
    else
    {
        len = 0;
    }
    if (trace_send_room() < (int32_t)(head_len + len))
        return 0;
    return trace_send(message, (int32_t)(head_len + len)) > 0;
}

/*
 * Sends what the link will take right now:  put name=<n> off=<o> total=<t>\n<bytes>
 * Returns 1 while anything is still waiting to go.
 */
int qfiles_pump(void)
{
    int waiting = 0;

    pthread_mutex_lock(&g_lock);
    init_names();
    for (int i = 0; i < USER_FILES; i++)
    {
        user_file_t *uf = &g_files[i];

        while (uf->outgoing_busy)
        {
            char head[96];
            size_t len = uf->outgoing_size - uf->outgoing_sent;

            if (len > CHUNK)
                len = CHUNK;
            snprintf(head, sizeof(head), "put name=%s off=%zu total=%zu", uf->name, uf->outgoing_sent,
                     uf->outgoing_size);
            if (!send_message(head, uf->outgoing + uf->outgoing_sent, len))
                break;      /* the link is busy: the rest goes on a later call */
            uf->outgoing_sent += len;
            if (uf->outgoing_sent >= uf->outgoing_size)
            {
                qtrace_log("quake: %s saved to the BBS, %zu bytes", uf->name, uf->outgoing_size);
                free(uf->outgoing);
                uf->outgoing = NULL;
                uf->outgoing_busy = 0;
            }
        }
        waiting |= uf->outgoing_busy;
    }
    pthread_mutex_unlock(&g_lock);
    return waiting;
}

/* Quake wrote a file: keep it, and send it to the BBS unless it's what the BBS already has */
static void written(user_file_t *uf, const uint8_t *data, size_t size)
{
    uint8_t *copy, *outgoing;

    pthread_mutex_lock(&g_lock);
    if (uf->present && uf->size == size && (size == 0 || memcmp(uf->data, data, size) == 0))
    {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    copy = (uint8_t *)malloc(size > 0 ? size : 1);
    outgoing = (uint8_t *)malloc(size > 0 ? size : 1);
    if (copy != NULL && outgoing != NULL)
    {
        memcpy(copy, data, size);
        memcpy(outgoing, data, size);
        free(uf->data);
        uf->data = copy;
        uf->size = size;
        uf->present = 1;
        free(uf->prefix);
        uf->prefix = NULL;
        uf->prefix_size = 0;
        /* The newest copy replaces one still on its way: the door starts again when a piece at offset 0 arrives */
        free(uf->outgoing);
        uf->outgoing = outgoing;
        uf->outgoing_size = size;
        uf->outgoing_sent = 0;
        uf->outgoing_busy = 1;
    }
    else
    {
        free(copy);
        free(outgoing);
    }
    pthread_mutex_unlock(&g_lock);
}

/* The rest of a save the player is loading: ask the door, and wait. Called with g_lock held. */
static int fetch_locked(user_file_t *uf)
{
    char head[64];
    struct timespec deadline;

    if (uf->present)
        return 1;
    snprintf(head, sizeof(head), "get name=%s", uf->name);
    for (int tries = 0; tries < 500 && !send_message(head, NULL, 0); tries++)
    {
        pthread_mutex_unlock(&g_lock);
        struct timespec ts = { 0, 10 * 1000000L };
        nanosleep(&ts, NULL);
        pthread_mutex_lock(&g_lock);
    }
    uf->fetching = 1;
    qtrace_log("quake: fetching %s from the BBS", uf->name);

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += FETCH_WAIT / 1000;
    while (uf->fetching && !uf->present)
        if (pthread_cond_timedwait(&g_arrived, &g_lock, &deadline) != 0)
            break;
    if (!uf->present)
        qtrace_log("quake: %s didn't arrive from the BBS", uf->name);
    uf->fetching = 0;
    return uf->present;
}

/* ---- the streams Quake gets ---- */

/* A save we only have the start of: served from the start until the game reads past it, then fetched whole */
typedef struct
{
    user_file_t *uf;
    size_t       pos;
} partial_t;

static ssize_t partial_read(void *cookie, char *buf, size_t size)
{
    partial_t *p = (partial_t *)cookie;
    user_file_t *uf = p->uf;
    const uint8_t *src;
    size_t have, n;

    pthread_mutex_lock(&g_lock);
    /* stdio always asks for a whole buffer, so only fetch once what we have of the start has been read: the Load
     * menu stops inside it, loading the save reads on past it */
    if (!uf->present && p->pos >= uf->prefix_size)
        fetch_locked(uf);
    src = uf->present ? uf->data : uf->prefix;
    have = uf->present ? uf->size : uf->prefix_size;
    n = p->pos < have ? have - p->pos : 0;
    if (n > size)
        n = size;
    if (n > 0 && src != NULL)
        memcpy(buf, src + p->pos, n);
    p->pos += n;
    pthread_mutex_unlock(&g_lock);
    return (ssize_t)n;
}

static int partial_close(void *cookie)
{
    free(cookie);
    return 0;
}

/* Files Quake is reading from a copy we made (freed on close), or writing (sent to the BBS on close) */
#define OPEN_MAX 16
static struct
{
    FILE        *f;
    uint8_t     *copy;          /* reading: our copy of the bytes */
    char        *buffer;        /* writing: open_memstream's */
    size_t       size;
    user_file_t *uf;            /* writing: whose file it becomes */
} g_open[OPEN_MAX];

static int remember(FILE *f, uint8_t *copy, user_file_t *writing)
{
    for (int i = 0; i < OPEN_MAX; i++)
        if (g_open[i].f == NULL)
        {
            g_open[i].f = f;
            g_open[i].copy = copy;
            g_open[i].uf = writing;
            return i;
        }
    return -1;
}

/* "./id1/s3.sav" → "s3.sav"; NULL for anything outside id1 */
static const char *base_name(const char *path)
{
    if (!strncmp(path, "./", 2))
        path += 2;
    if (strncmp(path, "id1/", 4) != 0)
        return NULL;
    return path + 4;
}

FILE *qfiles_fopen(const char *path, const char *mode)
{
    const char *name = base_name(path);
    int writing = strchr(mode, 'w') != NULL || strchr(mode, 'a') != NULL;
    user_file_t *uf;
    FILE *f;

    if (name == NULL)
    {
        errno = ENOENT;
        return NULL;
    }

    if (!strcmp(name, "pak0.pak"))
    {
        size_t size;
        const uint8_t *pak = qtrace_pak(&size);
        if (writing || pak == NULL)
        {
            errno = writing ? EROFS : ENOENT;
            return NULL;
        }
        return fmemopen((void *)pak, size, "rb");
    }

    /* quake.rc runs autoexec.cfg after config.cfg: WASD for a player who hasn't had it yet (vid_trace.c) */
    if (!strcmp(name, "autoexec.cfg") && !writing)
    {
        extern const char *qtrace_autoexec(void);
        const char *text = qtrace_autoexec();
        if (text == NULL)
        {
            errno = ENOENT;
            return NULL;
        }
        return fmemopen((void *)text, strlen(text), "rb");
    }

    uf = find(name);
    if (uf == NULL)
    {
        errno = writing ? EACCES : ENOENT;     /* nowhere to keep it, and nothing to read */
        return NULL;
    }

    if (writing)
    {
        int slot;
        f = NULL;
        for (slot = 0; slot < OPEN_MAX && g_open[slot].f != NULL; slot++)
            ;
        if (slot == OPEN_MAX)
        {
            errno = EMFILE;
            return NULL;
        }
        g_open[slot].buffer = NULL;
        g_open[slot].size = 0;
        f = open_memstream(&g_open[slot].buffer, &g_open[slot].size);
        if (f == NULL)
            return NULL;
        g_open[slot].f = f;
        g_open[slot].copy = NULL;
        g_open[slot].uf = uf;
        return f;
    }

    pthread_mutex_lock(&g_lock);
    if (uf->present)
    {
        uint8_t *copy = (uint8_t *)malloc(uf->size ? uf->size : 1);
        size_t size = uf->size;
        if (copy != NULL)
            memcpy(copy, uf->data, size);
        pthread_mutex_unlock(&g_lock);
        if (copy == NULL || size == 0)      /* fmemopen won't take an empty buffer */
        {
            free(copy);
            errno = ENOENT;
            return NULL;
        }
        f = fmemopen(copy, size, "rb");
        if (f == NULL || remember(f, copy, NULL) < 0)
        {
            if (f != NULL)
                fclose(f);
            free(copy);
            return NULL;
        }
        return f;
    }
    if (uf->prefix != NULL)
    {
        partial_t *p = (partial_t *)calloc(1, sizeof(*p));
        cookie_io_functions_t io = { partial_read, NULL, NULL, partial_close };
        pthread_mutex_unlock(&g_lock);
        if (p == NULL)
            return NULL;
        p->uf = uf;
        f = fopencookie(p, "r", io);
        if (f == NULL)
            free(p);
        return f;
    }
    pthread_mutex_unlock(&g_lock);
    errno = ENOENT;
    return NULL;
}

int qfiles_fclose(FILE *f)
{
    int result;

    for (int i = 0; i < OPEN_MAX; i++)
    {
        if (g_open[i].f != f)
            continue;
        result = fclose(f);                    /* open_memstream's buffer and size are final after this */
        if (g_open[i].uf != NULL && result == 0)
            written(g_open[i].uf, (const uint8_t *)g_open[i].buffer, g_open[i].size);
        free(g_open[i].buffer);
        free(g_open[i].copy);
        g_open[i].f = NULL;
        g_open[i].buffer = NULL;
        g_open[i].copy = NULL;
        g_open[i].uf = NULL;
        return result;
    }
    return fclose(f);
}
