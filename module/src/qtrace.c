/*
 * qtrace.c -- the TRACE side of the Quake module: what TERMinator talks to.
 *
 * The whole game runs here, in the sandbox on the player's PC: quakegeneric (id's GPL WinQuake, software renderer)
 * from ../third_party, with WinQuake's own sound mixer put back. The door sends this module and the shareware episode;
 * after that almost nothing crosses the wire.
 *
 * How it fits together:
 *   - The door sends the player's config and the titles of their saved games, then "pak=<sha256>": the archive holding
 *     the shareware pak and id's licence. That starts the game.
 *   - Quake runs on its own thread, calling Host_Frame as fast as it asks to (it holds itself to 72 frames a second).
 *     A thread, rather than one frame per trace_update, because loading a saved game has to wait for the BBS to send
 *     it, and Quake expects fopen to simply return with the file.
 *   - Keyboard and mouse events arrive on TERMinator's thread and are queued here for the game thread to read.
 */

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "trace_api.h"
#include "qtrace.h"
#include "tinf.h"

#undef printf
#undef exit

/* Quake's side (host.c, common.c) */
typedef struct
{
    char *basedir;
    char *cachedir;
    int argc;
    char **argv;
    void *membase;
    int memsize;
} quakeparms_t;
extern void Host_Init(quakeparms_t *parms);
extern void Host_Frame(float time);
extern void Host_Shutdown(void);
extern void COM_InitArgv(int argc, char **argv);
extern int com_argc;
extern char **com_argv;

#define HUNK_BYTES (32 * 1024 * 1024)   /* Quake's own memory: 8 MB was the 1996 minimum; this is room for anything */

/* ---- what the door told us ---- */
static char      g_pak_hash[65];
static uint8_t  *g_pak;
static size_t    g_pak_size;
static int       g_started;
static volatile int g_quit_requested;

/* ---- events from TERMinator, drained by the game thread ---- */
#define EVENT_MAX 512
static qtrace_event_t  g_events[EVENT_MAX];
static int             g_event_head, g_event_tail;
static pthread_mutex_t g_event_lock = PTHREAD_MUTEX_INITIALIZER;

void qtrace_log(const char *fmt, ...)
{
    char text[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    trace_log(text, (int32_t)strlen(text));
}

/*
 * Quake's own messages (Sys_Printf, and the odd printf). WASI throws stdout away, so they go to TERMinator's debug
 * output instead, a whole line at a time since that's how they're written.
 */
int qtrace_printf(const char *fmt, ...)
{
    static char line[512];
    static int  line_len;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    char text[1024];
    va_list args;
    int n;

    va_start(args, fmt);
    n = vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);

    pthread_mutex_lock(&lock);
    for (const char *p = text; *p; p++)
    {
        /* Quake marks some text for its console's second colour with the top bit */
        char c = (char)(*p & 0x7F);
        if (c == '\n' || line_len == (int)sizeof(line) - 1)
        {
            if (line_len > 0)
                trace_log(line, line_len);
            line_len = 0;
            if (c != '\n')
                line[line_len++] = c;
        }
        else if (c != '\r')
        {
            line[line_len++] = c;
        }
    }
    pthread_mutex_unlock(&lock);
    return n;
}

int qtrace_next_event(qtrace_event_t *out)
{
    int got = 0;
    pthread_mutex_lock(&g_event_lock);
    if (g_event_head != g_event_tail)
    {
        *out = g_events[g_event_head];
        g_event_head = (g_event_head + 1) % EVENT_MAX;
        got = 1;
    }
    pthread_mutex_unlock(&g_event_lock);
    return got;
}

static void queue_event(const qtrace_event_t *ev)
{
    int next;
    pthread_mutex_lock(&g_event_lock);
    next = (g_event_tail + 1) % EVENT_MAX;
    if (next != g_event_head)       /* full: drop the newest rather than block the terminal */
    {
        g_events[g_event_tail] = *ev;
        g_event_tail = next;
    }
    pthread_mutex_unlock(&g_event_lock);
}

static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/*
 * The end. Quake has already written its config (Host_Shutdown); whatever is still on its way up to the BBS is given
 * a few seconds to get there, then TERMinator is told to close the picture, which tells the door.
 */
void qtrace_exit(int code)
{
    for (int waited = 0; g_started && qfiles_pump() && waited < 8000; waited += 10)
        sleep_ms(10);
    qtrace_log("quake: quitting (%d)", code);
    trace_quit(code >= 0 && code < 126 ? code : 0);
    _Exit(0);   /* not reached */
}

/* ---- the game data: a zip holding id1/pak0.pak and id's licence (tools/mkzip.py) ---- */

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (rd16(p + 2) << 16); }

/* Finds one file in the zip and unpacks it. Only what mkzip.py writes is needed: stored or deflated, no zip64. */
static int unzip_file(const uint8_t *zip, size_t zip_size, const char *want, uint8_t **out, size_t *out_size)
{
    size_t eocd = 0;
    uint32_t count, cd;

    if (zip_size < 22)
        return 0;
    for (size_t i = zip_size - 22; i + 1 > 0 && zip_size - i <= 22 + 65535; i--)
    {
        if (rd32(zip + i) == 0x06054b50)
        {
            eocd = i;
            break;
        }
        if (i == 0)
            break;
    }
    if (rd32(zip + eocd) != 0x06054b50)
        return 0;
    count = rd16(zip + eocd + 10);
    cd = rd32(zip + eocd + 16);

    for (uint32_t n = 0, p = cd; n < count && p + 46 <= zip_size; n++)
    {
        uint32_t method = rd16(zip + p + 10);
        uint32_t csize = rd32(zip + p + 20), usize = rd32(zip + p + 24);
        uint32_t name_len = rd16(zip + p + 28), extra_len = rd16(zip + p + 30), comment_len = rd16(zip + p + 32);
        uint32_t local = rd32(zip + p + 42);

        if (name_len == strlen(want) && memcmp(zip + p + 46, want, name_len) == 0)
        {
            uint32_t data;
            uint8_t *buffer;

            if ((size_t)local + 30 > zip_size || rd32(zip + local) != 0x04034b50)
                return 0;
            data = local + 30 + rd16(zip + local + 26) + rd16(zip + local + 28);
            if ((size_t)data + csize > zip_size)
                return 0;
            buffer = (uint8_t *)malloc(usize ? usize : 1);
            if (buffer == NULL)
                return 0;
            if (method == 0 && csize == usize)
            {
                memcpy(buffer, zip + data, usize);
            }
            else if (method == 8)
            {
                unsigned int got = usize;
                if (tinf_uncompress(buffer, &got, zip + data, csize) != TINF_OK || got != usize)
                {
                    free(buffer);
                    return 0;
                }
            }
            else
            {
                free(buffer);
                return 0;
            }
            *out = buffer;
            *out_size = usize;
            return 1;
        }
        p += 46 + name_len + extra_len + comment_len;
    }
    return 0;
}

static int load_pak(void)
{
    int32_t size = trace_asset_size(g_pak_hash);
    uint8_t *zip;
    int ok;

    if (size <= 0)
    {
        qtrace_log("quake: the door's game data isn't here (%s)", g_pak_hash);
        return 0;
    }
    zip = (uint8_t *)malloc((size_t)size);
    if (zip == NULL)
        return 0;
    for (int32_t off = 0; off < size; )
    {
        int32_t got = trace_asset_read(g_pak_hash, off, zip + off, size - off);
        if (got <= 0)
        {
            qtrace_log("quake: couldn't read the game data at %d", off);
            free(zip);
            return 0;
        }
        off += got;
    }
    ok = unzip_file(zip, (size_t)size, "id1/pak0.pak", &g_pak, &g_pak_size);
    free(zip);
    if (!ok)
        qtrace_log("quake: the game data has no usable id1/pak0.pak");
    else
        qtrace_log("quake: pak0.pak unpacked, %zu bytes", g_pak_size);
    return ok;
}

const uint8_t *qtrace_pak(size_t *size)
{
    *size = g_pak_size;
    return g_pak;
}

/* ---- the game thread ---- */

extern double Sys_FloatTime(void);

/*
 * Host_Frame decides for itself whether enough time has passed for a frame (72 a second at most), so it's simply
 * called often, with a millisecond's sleep between, which also keeps the sound and the saves moving.
 */
static void *main_loop(void *arg)
{
    double last = Sys_FloatTime();
    (void)arg;

    for (;;)
    {
        double now = Sys_FloatTime();

        if (g_quit_requested)
        {
            /* The window closed or the player's time is up: straight out, with the config written */
            Host_Shutdown();
            qtrace_exit(0);
        }
        Host_Frame((float)(now - last));
        last = now;
        qtrace_pump_audio();
        qfiles_pump();
        sleep_ms(1);
    }
    return NULL;
}

static int start_thread(void *(*entry)(void *))
{
    pthread_attr_t attr;
    pthread_t thread;
    int ok;

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);   /* the software renderer keeps big tables on the stack */
    ok = pthread_create(&thread, &attr, entry, NULL) == 0;
    pthread_attr_destroy(&attr);
    if (!ok)
        qtrace_log("quake: couldn't start the game thread");
    return ok;
}

static void *game_thread(void *arg)
{
    static char *args[] = { "quake", NULL };
    static quakeparms_t parms;

    parms.memsize = HUNK_BYTES;
    parms.membase = malloc((size_t)parms.memsize);
    parms.basedir = ".";
    parms.cachedir = NULL;
    if (parms.membase == NULL)
    {
        qtrace_log("quake: not enough memory");
        qtrace_exit(1);
    }

    COM_InitArgv(1, args);
    parms.argc = com_argc;
    parms.argv = com_argv;
    Host_Init(&parms);
    return main_loop(arg);
}

static void start_game(void)
{
    if (g_started || !g_pak_hash[0])
        return;
    if (!load_pak())
        return;
    g_started = 1;
    start_thread(game_thread);
}

/* ---- TRACE entry points ---- */

int32_t trace_init(void)
{
    trace_set_tick(0);      /* nothing happens until the door says which data to use */
    return 0;
}

void *trace_alloc(int32_t size)
{
    return size > 0 ? malloc((size_t)size) : NULL;
}

void trace_free(void *ptr)
{
    free(ptr);
}

void trace_on_resize(int32_t width, int32_t height)
{
    (void)width; (void)height;   /* Quake renders at its own resolution and TERMinator scales it */
}

static long field(const char *head, const char *name)
{
    const char *p = strstr(head, name);
    return p != NULL ? strtol(p + strlen(name), NULL, 10) : 0;
}

/*
 * What the door sends. Everything up to the first newline is the message; anything after it is its payload.
 *   file name=<n> off=<o> total=<t>\n...   part of one of the player's files (config.cfg, or a save it asked for)
 *   list name=<n>\n<start of the file>     the player has this save; its first lines are what the Load menu shows
 *   none name=<n>                          a save it asked for isn't there after all
 *   pak=<sha256>                           the game data; everything else was sent before it, so this starts the game
 *   quit                                   the player's time on the BBS is up: save the config and close
 */
void trace_on_data(const char *data, int32_t length)
{
    char head[256], name[40] = "";
    const char *payload = NULL;
    int32_t payload_len = 0;
    int head_len;
    const char *newline;

    if (length <= 0)
        return;

    newline = (const char *)memchr(data, '\n', (size_t)length);
    head_len = newline != NULL ? (int)(newline - data) : length;
    if (head_len >= (int)sizeof(head))
        head_len = (int)sizeof(head) - 1;
    memcpy(head, data, (size_t)head_len);
    head[head_len] = 0;
    if (newline != NULL)
    {
        payload = newline + 1;
        payload_len = length - (int32_t)(payload - data);
    }
    if (strstr(head, "name="))
        sscanf(strstr(head, "name="), "name=%39s", name);

    if (!strncmp(head, "pak=", 4))
    {
        if (strlen(head + 4) == 64 && !g_pak_hash[0])
        {
            memcpy(g_pak_hash, head + 4, 65);
            start_game();
        }
    }
    else if (!strcmp(head, "quit"))
    {
        if (g_started)
            g_quit_requested = 1;
        else
            trace_quit(0);
    }
    else if (!strncmp(head, "file ", 5) && payload != NULL && name[0])
        qfiles_on_file(name, (size_t)field(head, "off="), (size_t)field(head, "total="),
                       (const uint8_t *)payload, (size_t)payload_len);
    else if (!strncmp(head, "list ", 5) && payload != NULL && name[0])
        qfiles_on_list(name, (const uint8_t *)payload, (size_t)payload_len);
    else if (!strncmp(head, "none ", 5) && name[0])
        qfiles_on_none(name);
}

void trace_on_input(int32_t type, int32_t flags, int32_t a, int32_t b, int32_t c)
{
    qtrace_event_t ev = { type, flags, a, b, c };

    if (type == TE_IN_QUIT)
    {
        if (g_started)
            g_quit_requested = 1;
        else
            trace_quit(0);
        return;
    }
    queue_event(&ev);
}

void trace_update(void)
{
    /* The game thread does the work; there is nothing to do here between events. */
}
