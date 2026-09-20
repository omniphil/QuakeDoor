/*
 * trace_door.c -- the door side of TRACE, shared by every TRACE door. See trace_door.h.
 *
 * THE SOURCE IS HERE (BBSGames/TraceDoor); the door folders carry copies made by ./sync.sh.
 */

#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "trace_door.h"
#include "door.h"
#include "sha256.h"

#define APC "\033_TERMinator:TRACE;"
#define ST  "\033\\"

#define B64_CHUNK   3072    /* 4096 base64 characters per Put, under TERMinator's 8 KB limit per text command */
#define BIN_CHUNK   60000   /* a binary Put: a header line and this much, under TERMinator's 64 KB frame limit */
#define REPLY_MAX   (160 * 1024)   /* the longest reply we read: a 64 KB binary frame with every byte escaped */

static char             g_module[64];
static tdoor_message_fn g_on_message;
static char             g_info[512];      /* the Query reply */
static bool             g_binary;         /* the terminal takes binary frames (bin=1) */
static bool             g_open;
static char             g_have_hash[65];
static char             g_close_reason[128];

void tdoor_init(const char *module_id, tdoor_message_fn on_message)
{
    snprintf(g_module, sizeof(g_module), "%s", module_id);
    g_on_message = on_message;
}

/* ---- files that ship with the door ---- */

bool tdoor_load_blob(const char *filename, tdoor_blob_t *blob, size_t max_size)
{
    char path[PATH_MAX];
    FILE *fp = NULL;

    memset(blob, 0, sizeof(*blob));
    /* next to the door binary first, then the current directory */
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 64);
    if (len > 0) {
        path[len] = '\0';
        char *slash = strrchr(path, '/');
        if (slash) {
            snprintf(slash + 1, sizeof(path) - (size_t)(slash + 1 - path), "%s", filename);
            fp = fopen(path, "rb");
        }
    }
    if (!fp) fp = fopen(filename, "rb");
    if (!fp) return false;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size > 0 && (size_t)size <= max_size) {
        blob->data = (uint8_t *)malloc((size_t)size);
        if (blob->data && fread(blob->data, 1, (size_t)size, fp) == (size_t)size) {
            blob->size = (size_t)size;
            sha256_hex(blob->data, blob->size, blob->hash);
        } else {
            free(blob->data);
            blob->data = NULL;
        }
    }
    fclose(fp);
    return blob->data != NULL;
}

/* ---- detection ---- */

bool tdoor_detect(const char *const *need)
{
    int n = 0;

    door_write(APC "Query" ST);

    /* Up to half a second for the reply to start, then read it to its closing ESC \ so none of it is later
     * mistaken for keypresses. Terminals without TRACE never answer at all. */
    int c = door_read_char_timeout(500);
    while (c >= 0 && n < (int)sizeof(g_info) - 1) {
        g_info[n++] = (char)c;
        if (n >= 2 && g_info[n - 2] == '\033' && g_info[n - 1] == '\\') break;
        c = door_read_char_timeout(250);
    }
    g_info[n] = '\0';

    if (strstr(g_info, "TERMinator:TRACE") == NULL || !tdoor_has("wasm=1"))
        return false;
    for (; need != NULL && *need != NULL; need++)
        if (!tdoor_has(*need))
            return false;
    g_binary = tdoor_has("bin=1");
    return true;
}

bool tdoor_has(const char *flag)
{
    /* A whole field: preceded by ';' and followed by ';', ESC or the end, so "pad=1" never matches "xpad=10" */
    size_t len = strlen(flag);
    for (const char *p = strstr(g_info, flag); p != NULL; p = strstr(p + 1, flag))
        if (p > g_info && p[-1] == ';' && (p[len] == ';' || p[len] == '\033' || p[len] == '\0'))
            return true;
    return false;
}

/* ---- encodings ---- */

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Writes len bytes as base64 (no line breaks). */
static void write_base64(const uint8_t *p, size_t len)
{
    char out[4096 + 4];
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16;
        if (i + 1 < len) v |= (uint32_t)p[i + 1] << 8;
        if (i + 2 < len) v |= p[i + 2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = i + 1 < len ? B64[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < len ? B64[v & 63] : '=';
        if (o >= 4096) {
            door_write_raw(out, o);
            o = 0;
        }
    }
    if (o > 0)
        door_write_raw(out, o);
}

/* base64 back to bytes. Returns the length, or 0 if it isn't valid base64 or doesn't fit. */
static size_t decode_base64(const char *text, unsigned char *out, size_t max)
{
    static signed char table[256];
    static bool ready = false;
    size_t len = 0;
    uint32_t bits = 0;
    int have = 0;

    if (!ready) {
        memset(table, -1, sizeof(table));
        for (int i = 0; i < 64; i++)
            table[(unsigned char)B64[i]] = (signed char)i;
        ready = true;
    }
    for (const char *p = text; *p != '\0'; p++) {
        signed char value;
        if (*p == '=')
            break;
        value = table[(unsigned char)*p];
        if (value < 0)
            continue;
        bits = (bits << 6) | (uint32_t)value;
        have += 6;
        if (have >= 8) {
            have -= 8;
            if (len >= max)
                return 0;
            out[len++] = (unsigned char)((bits >> have) & 0xFF);
        }
    }
    return len;
}

/*
 * Binary frames: the bytes a BBS link, telnet or the terminal could touch never travel raw. Each one becomes '=' and
 * the byte + 64 (mod 256): NUL, LF, CR, XON, XOFF, CAN (ZMODEM), ESC, '=' itself and 0xFF (telnet IAC). TERMinator
 * decodes any escaped byte, and escapes every control byte on the way here (our stdin may be a pseudo-terminal).
 */
static bool must_escape(uint8_t b)
{
    return b == 0x00 || b == 0x0A || b == 0x0D || b == 0x11 || b == 0x13 || b == 0x18 || b == 0x1B || b == '='
        || b == 0xFF;
}

static void write_escaped(const uint8_t *p, size_t len)
{
    uint8_t out[8192 + 2];
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        if (must_escape(p[i])) {
            out[o++] = '=';
            out[o++] = (uint8_t)(p[i] + 64);
        } else {
            out[o++] = p[i];
        }
        if (o >= 8192) {
            door_write_raw((const char *)out, o);
            o = 0;
        }
    }
    if (o > 0)
        door_write_raw((const char *)out, o);
}

/* One binary frame: a header line, then the payload, raw. */
static void write_frame(const char *head, const void *payload, size_t len)
{
    door_write(APC "Bin;");
    write_escaped((const uint8_t *)head, strlen(head));
    write_escaped((const uint8_t *)"\n", 1);
    if (payload != NULL && len > 0)
        write_escaped((const uint8_t *)payload, len);
    door_write(ST);
}

/* Undoes the escaping in place. Returns the decoded length. */
static size_t unescape(char *buf, size_t len)
{
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '=' && i + 1 < len)
            buf[o++] = (char)((uint8_t)buf[++i] - 64);
        else
            buf[o++] = buf[i];
    }
    return o;
}

/* ---- replies ---- */

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* Whether a reply names our module: "module=<id>" followed by ';' or the end. */
static bool is_ours(const char *fields)
{
    size_t len = strlen(g_module);
    return strncmp(fields, "module=", 7) == 0 && strncmp(fields + 7, g_module, len) == 0
        && (fields[7 + len] == ';' || fields[7 + len] == '\0' || fields[7 + len] == '\n');
}

/* One complete reply (the text between ESC _ and ESC \, which may hold escaped binary). */
static int handle_reply(char *buf, size_t n)
{
    static unsigned char message[65536 + 1024];
    static const char prefix[] = "TERMinator:TRACE;";
    char *cmd;

    if (n < sizeof(prefix) - 1 || strncmp(buf, prefix, sizeof(prefix) - 1) != 0)
        return 0;
    cmd = buf + sizeof(prefix) - 1;
    n -= sizeof(prefix) - 1;

    /* A binary frame from the module: "Data;module=<id>\n<raw>" */
    if (strncmp(cmd, "Bin;", 4) == 0) {
        size_t len = unescape(cmd + 4, n - 4);
        char *body = cmd + 4;
        char *newline = memchr(body, '\n', len);
        if (newline != NULL && strncmp(body, "Data;", 5) == 0) {
            *newline = '\0';
            if (is_ours(body + 5) && g_on_message != NULL)
                g_on_message((const unsigned char *)newline + 1, len - (size_t)(newline + 1 - body));
        }
        return 0;
    }
    buf[sizeof(prefix) - 1 + n] = '\0';

    /* The module's message as base64 (a terminal without binary frames, or a door that didn't ask for them) */
    if (strncmp(cmd, "Data;", 5) == 0 && is_ours(cmd + 5)) {
        const char *b64 = strstr(cmd, ";b64=");
        if (b64 != NULL) {
            size_t got = decode_base64(b64 + 5, message, sizeof(message));
            if (got > 0 && g_on_message != NULL)
                g_on_message(message, got);
        }
        return 0;
    }

    char *fields = strchr(cmd, ';');
    if (fields == NULL || !is_ours(fields + 1))
        return 0;
    size_t verb = (size_t)(fields - cmd);
#define VERB(name) (verb == sizeof(name) - 1 && strncmp(cmd, name, verb) == 0)
    if (VERB("Ready")) return TDOOR_REPLY_READY;
    if (VERB("Need")) return TDOOR_REPLY_NEED;
    if (VERB("NeedAsset")) return TDOOR_REPLY_NEEDASSET;
    if (VERB("Closed")) {
        /* Keep the reason: a module that won't start (e.g. it imports something
         * this TERMinator doesn't have) reports it here, and without it a door
         * can only say "it didn't work". */
        const char *why = strstr(fields, ";error=");
        g_close_reason[0] = '\0';
        if (why != NULL) {
            size_t n = 0;
            for (why += 7; *why && *why != ';' && *why != 27 && n < sizeof(g_close_reason) - 1; why++)
                g_close_reason[n++] = *why;
            g_close_reason[n] = '\0';
        }
        g_open = false;
        return TDOOR_REPLY_CLOSED;
    }
    if (VERB("NoImport")) return TDOOR_REPLY_NOIMPORT;
    if (VERB("Have")) {
        const char *hash = strstr(fields, ";sha256=");
        g_have_hash[0] = '\0';
        if (hash != NULL)
            snprintf(g_have_hash, sizeof(g_have_hash), "%.64s", hash + 8);
        return TDOOR_REPLY_HAVE;
    }
#undef VERB
    return 0;
}

int tdoor_wait_reply(int timeout_ms)
{
    static char *buf;
    size_t n = 0;
    int prev = -1;
    bool in_apc = false;
    long deadline = now_ms() + timeout_ms;

    if (buf == NULL && (buf = (char *)malloc(REPLY_MAX + 1)) == NULL)
        return TDOOR_REPLY_NONE;
    for (;;) {
        long left = deadline - now_ms();
        if (left <= 0) return TDOOR_REPLY_NONE;
        int c = door_read_char_timeout((int)left);
        if (c < 0) return TDOOR_REPLY_NONE;

        if (prev == 27 && c == '_') {
            in_apc = true;
            n = 0;
        } else if (in_apc && prev == 27 && c == '\\') {
            in_apc = false;
            if (n > 0) n--;            /* drop the ESC */
            int reply = handle_reply(buf, n);
            if (reply != 0)
                return reply;
        } else if (in_apc && n < REPLY_MAX) {
            buf[n++] = (char)c;
        }
        prev = c;
    }
}

const char *tdoor_last_have_hash(void)
{
    return g_have_hash;
}

const char *tdoor_last_close_reason(void)
{
    return g_close_reason;
}

const char *tdoor_info(void)
{
    return g_info;
}

/* ---- sending ---- */

/* Sends one blob in Put chunks, then PutDone. asset_hash is NULL for the module itself. */
static void upload(const tdoor_blob_t *blob, const char *asset_hash, void (*progress)(int))
{
    char head[256];
    int last_percent = -1;
    size_t chunk = g_binary ? BIN_CHUNK : B64_CHUNK;

    for (size_t off = 0; off < blob->size; off += chunk) {
        size_t len = blob->size - off < chunk ? blob->size - off : chunk;
        if (asset_hash)
            snprintf(head, sizeof(head), "Put;module=%s;asset=%s;offset=%zu", g_module, asset_hash, off);
        else
            snprintf(head, sizeof(head), "Put;module=%s;offset=%zu", g_module, off);
        if (g_binary) {
            write_frame(head, blob->data + off, len);
        } else {
            door_write(APC);
            door_write(head);
            door_write(";data=");
            write_base64(blob->data + off, len);
            door_write(ST);
        }
        if (progress) {
            int percent = (int)((off + len) * 100 / blob->size);
            if (percent != last_percent) {
                progress(percent);
                last_percent = percent;
            }
        }
    }
    if (asset_hash)
        snprintf(head, sizeof(head), APC "PutDone;module=%s;asset=%s" ST, g_module, asset_hash);
    else
        snprintf(head, sizeof(head), APC "PutDone;module=%s" ST, g_module);
    door_write(head);
}

void tdoor_send(const char *head, const void *payload, size_t len)
{
    char start[128];

    if (g_binary) {
        snprintf(start, sizeof(start), "Data;module=%s", g_module);
        /* The frame's own header line is ours; the module gets head (and the payload) exactly as it would via base64 */
        size_t head_len = strlen(head), total = head_len + (payload != NULL ? 1 + len : 0);
        uint8_t *message = (uint8_t *)malloc(total ? total : 1);
        if (message == NULL)
            return;
        memcpy(message, head, head_len);
        if (payload != NULL) {
            message[head_len] = '\n';
            memcpy(message + head_len + 1, payload, len);
        }
        write_frame(start, message, total);
        free(message);
        return;
    }

    size_t head_len = strlen(head), total = head_len + (payload != NULL ? 1 + len : 0);
    uint8_t *message = (uint8_t *)malloc(total ? total : 1);
    if (message == NULL)
        return;
    memcpy(message, head, head_len);
    if (payload != NULL) {
        message[head_len] = '\n';
        memcpy(message + head_len + 1, payload, len);
    }
    snprintf(start, sizeof(start), APC "Data;module=%s;b64=", g_module);
    door_write(start);
    write_base64(message, total);
    door_write(ST);
    free(message);
}

void tdoor_send_text(const char *text)
{
    char start[128];
    snprintf(start, sizeof(start), APC "Data;module=%s;", g_module);
    door_write(start);
    door_write(text);
    door_write(ST);
}

bool tdoor_send_asset(const tdoor_blob_t *asset, void (*progress)(int))
{
    char buf[256];
    int reply;

    snprintf(buf, sizeof(buf), APC "Asset;module=%s;sha256=%s;size=%zu" ST, g_module, asset->hash, asset->size);
    door_write(buf);

    reply = tdoor_wait_reply(5000);
    if (reply == TDOOR_REPLY_HAVE) return true;          /* already cached from an earlier call */
    if (reply != TDOOR_REPLY_NEEDASSET) return false;

    upload(asset, asset->hash, progress);
    return tdoor_wait_reply(60000) == TDOOR_REPLY_HAVE;
}

bool tdoor_open(const tdoor_blob_t *module, const char *options, void (*progress)(int))
{
    char buf[512];
    int reply;

    snprintf(buf, sizeof(buf), APC "Open;module=%s;wasm=%s;size=%zu%s%s%s" ST, g_module, module->hash, module->size,
             options != NULL && *options ? ";" : "", options != NULL ? options : "", g_binary ? ";bin=1" : "");
    door_write(buf);

    reply = tdoor_wait_reply(5000);
    if (reply == TDOOR_REPLY_NEED) {
        /* First time on this PC: send the game, TERMinator checks its hash, caches it and starts it */
        upload(module, NULL, progress);
        reply = tdoor_wait_reply(30000);
    }
    g_open = reply == TDOOR_REPLY_READY;
    return g_open;
}

bool tdoor_import(const char *hashes, const char *name, int timeout_ms)
{
    char buf[1400];

    if (!tdoor_has("import=1"))
        return false;
    snprintf(buf, sizeof(buf), APC "Import;module=%s;sha256=%s;name=%s" ST, g_module, hashes, name);
    door_write(buf);
    for (long deadline = now_ms() + timeout_ms; now_ms() < deadline;) {
        int reply = tdoor_wait_reply((int)(deadline - now_ms()));
        if (reply == TDOOR_REPLY_HAVE)
            return true;
        if (reply == TDOOR_REPLY_NOIMPORT || reply == TDOOR_REPLY_CLOSED || reply == TDOOR_REPLY_NONE)
            return false;
    }
    return false;
}

void tdoor_wait_closed(void)
{
    while (g_open) {
        if (tdoor_wait_reply(2000) == TDOOR_REPLY_CLOSED)
            break;
        if (door_time_remaining() <= 0)
            break;
    }
}

bool tdoor_is_open(void)
{
    return g_open;
}

void tdoor_close(const char *quit_message, int grace_ms)
{
    char buf[128];

    if (!g_open)
        return;
    if (quit_message != NULL) {
        tdoor_send(quit_message, NULL, 0);
        for (int waited = 0; waited < grace_ms && g_open; waited += 1000)
            tdoor_wait_reply(1000);
        if (!g_open)
            return;
    }
    snprintf(buf, sizeof(buf), APC "Close;module=%s" ST, g_module);
    door_write(buf);
    g_open = false;
}
