/*
 * vid_trace.c -- Quake's screen, drawn by TRACE. Replaces quakegeneric's vid_null.c.
 *
 * The software renderer draws 8-bit pixels; each finished frame goes through the palette to the BGRA frame
 * trace_present takes. The size is the largest of these that TERMinator's frame holds:
 *   1024x768   square pixels (TERMinator 1.1.3 onwards: its frame holds 1280x1024)
 *   640x480    square pixels
 *   640x400    shown at 4:3, WinQuake's own 640x400 mode, with vid.aspect telling the renderer about the tall pixels
 *              (TERMinator 1.1.2, whose frame holds 1280x400)
 */

#include "quakedef.h"
#include "d_local.h"

#include "trace_api.h"
#include "qtrace.h"

#define MAX_W 1024
#define MAX_H 768

static const struct { int w, h; } g_modes[] = { { 1024, 768 }, { 640, 480 }, { 640, 400 } };

/* "showfps 1" at the console: the resolution and frames a second in the top right corner. Saved with the player's config (the third
 * field: archive), so it stays on for them. WinQuake never had one; this lives here so the engine stays unmodified. */
static cvar_t showfps = { "showfps", "1", true };   /* on unless the player turns it off */

/* WASD, which Quake (1996) predates: given to each player once, as an autoexec.cfg that only exists until they've had
 * it (qfiles.c asks qtrace_autoexec). "wasd_given 1" is saved with their config, so from then on the keys are theirs
 * to rebind. The arrows keep working; a and d lose Quake's old look-up and swim-up, which z/PGDN/c still do. */
static cvar_t wasd_given = { "wasd_given", "0", true };

const char *qtrace_autoexec(void)
{
    if (wasd_given.value)
        return NULL;
    return "// TERMinator: WASD for new players, once (\"wasd_given 0\" brings it back next time)\n"
           "bind w +forward\n"
           "bind s +back\n"
           "bind a +moveleft\n"
           "bind d +moveright\n"
           "wasd_given 1\n";
}
static int    g_fps, g_fps_frames, g_fps_since;
static int g_width, g_height;

viddef_t vid;                           /* global video state */

static byte     vid_buffer[MAX_W * MAX_H];
static short    zbuffer[MAX_W * MAX_H];
static byte    *surfcache;
static uint32_t g_frame[MAX_W * MAX_H];
static uint32_t g_palette[256];

/* The palette arrives with the game's brightness and its screen flashes (damage, pickups) already applied */
void VID_SetPalette(unsigned char *palette)
{
    for (int i = 0; i < 256; i++)
        g_palette[i] = 0xFF000000u | ((uint32_t)palette[i * 3] << 16) | ((uint32_t)palette[i * 3 + 1] << 8)
                     | palette[i * 3 + 2];
}

void VID_ShiftPalette(unsigned char *palette)
{
    VID_SetPalette(palette);
}

void VID_Init(unsigned char *palette)
{
    int surfcache_size, cap_w = 0, cap_h = 0;

    trace_frame_capacity(&cap_w, &cap_h);
    g_width = 640;
    g_height = 400;
    for (size_t i = 0; i < sizeof(g_modes) / sizeof(g_modes[0]); i++)
        if (cap_w >= g_modes[i].w && cap_h >= g_modes[i].h)
        {
            g_width = g_modes[i].w;
            g_height = g_modes[i].h;
            break;
        }

    vid.width = vid.conwidth = g_width;
    vid.height = vid.conheight = g_height;
    vid.maxwarpwidth = WARP_WIDTH;      /* the underwater wobble is drawn at 320x200 and stretched, as WinQuake did */
    vid.maxwarpheight = WARP_HEIGHT;
    vid.aspect = ((float)g_height / (float)g_width) * (320.0f / 240.0f);   /* 1.0 for the square-pixel modes */
    vid.numpages = 1;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));
    vid.buffer = vid.conbuffer = vid_buffer;
    vid.rowbytes = vid.conrowbytes = g_width;

    d_pzbuffer = zbuffer;

    surfcache_size = D_SurfaceCacheForRes(g_width, g_height);
    surfcache = malloc(surfcache_size);
    if (surfcache == NULL)
        Sys_Error("Not enough memory for the surface cache");
    D_InitCaches(surfcache, surfcache_size);

    VID_SetPalette(palette);
    Cvar_RegisterVariable(&showfps);   /* before quake.rc runs config.cfg, so a saved "showfps 1" sticks */
    Cvar_RegisterVariable(&wasd_given);
    qtrace_log("quake: %dx%d (TERMinator's frame holds %dx%d)", g_width, g_height, cap_w, cap_h);
}

void VID_Shutdown(void)
{
    free(surfcache);
    surfcache = NULL;
}

void VID_Update(vrect_t *rects)
{
    (void)rects;

    /* Counted over whole seconds, drawn in Quake's own font on top of everything else */
    {
        int now = trace_time_ms();
        g_fps_frames++;
        if (now - g_fps_since >= 1000)
        {
            g_fps = g_fps_frames * 1000 / (now - g_fps_since);
            g_fps_frames = 0;
            g_fps_since = now;
        }
        if (showfps.value)
        {
            char text[32];
            snprintf(text, sizeof(text), "%dx%d %3d fps", g_width, g_height, g_fps);
            Draw_String(g_width - 8 * (int)strlen(text) - 8, 8, text);
        }
    }

    for (int i = 0; i < g_width * g_height; i++)
        g_frame[i] = g_palette[vid_buffer[i]];
    trace_present(g_frame, g_width, g_height, g_width * 3 == g_height * 4 ? 0 : TRACE_PRESENT_ASPECT_4_3);
}

/* The disc icon drawn straight to the screen while loading: nothing to do, the next frame shows the screen as is */
void D_BeginDirectRect(int x, int y, byte *pbitmap, int width, int height)
{
    (void)x; (void)y; (void)pbitmap; (void)width; (void)height;
}

void D_EndDirectRect(int x, int y, int width, int height)
{
    (void)x; (void)y; (void)width; (void)height;
}
