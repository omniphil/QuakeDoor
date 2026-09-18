/*
 * sys_trace.c -- Quake's system layer for TRACE. Replaces quakegeneric's sys_null.c.
 *
 * Files go through fopen, which include/qtrace_compat.h sends to qfiles.c; the clock is trace_time_ms; messages go to
 * TERMinator's debug output; quitting writes the config and closes TERMinator's picture.
 */

#include "quakedef.h"
#include "errno.h"

#include "trace_api.h"
#include "qtrace.h"

qboolean isDedicated;

/* ---- file IO: handles over the streams qfiles.c hands out ---- */

#define MAX_HANDLES 32
static FILE *sys_handles[MAX_HANDLES];

static int findhandle(void)
{
    for (int i = 1; i < MAX_HANDLES; i++)
        if (!sys_handles[i])
            return i;
    Sys_Error("out of handles");
    return -1;
}

static int filelength(FILE *f)
{
    int pos = ftell(f), end;
    fseek(f, 0, SEEK_END);
    end = ftell(f);
    fseek(f, pos, SEEK_SET);
    return end;
}

int Sys_FileOpenRead(char *path, int *hndl)
{
    int i = findhandle();
    FILE *f = fopen(path, "rb");

    if (!f)
    {
        *hndl = -1;
        return -1;
    }
    sys_handles[i] = f;
    *hndl = i;
    return filelength(f);
}

int Sys_FileOpenWrite(char *path)
{
    int i = findhandle();
    FILE *f = fopen(path, "wb");

    if (!f)
        Sys_Error("Error opening %s: %s", path, strerror(errno));
    sys_handles[i] = f;
    return i;
}

void Sys_FileClose(int handle)
{
    fclose(sys_handles[handle]);
    sys_handles[handle] = NULL;
}

void Sys_FileSeek(int handle, int position)
{
    fseek(sys_handles[handle], position, SEEK_SET);
}

int Sys_FileRead(int handle, void *dest, int count)
{
    return (int)fread(dest, 1, count, sys_handles[handle]);
}

int Sys_FileWrite(int handle, void *data, int count)
{
    return (int)fwrite(data, 1, count, sys_handles[handle]);
}

int Sys_FileTime(char *path)
{
    FILE *f = fopen(path, "rb");
    if (f)
    {
        fclose(f);
        return 1;
    }
    return -1;
}

void Sys_mkdir(char *path)
{
    (void)path;
}

/* ---- system ---- */

void Sys_MakeCodeWriteable(unsigned long startaddr, unsigned long length)
{
    (void)startaddr; (void)length;
}

void Sys_DebugLog(char *file, char *fmt, ...)
{
    (void)file; (void)fmt;
}

/* A fatal error: say why in TERMinator's log, and leave (the door hears the module closed) */
void Sys_Error(char *error, ...)
{
    char text[1024];
    va_list argptr;

    va_start(argptr, error);
    vsnprintf(text, sizeof(text), error, argptr);
    va_end(argptr);
    qtrace_log("quake: Sys_Error: %s", text);
    exit(1);
}

void Sys_Printf(char *fmt, ...)
{
    char text[2048];
    va_list argptr;

    va_start(argptr, fmt);
    vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);
    printf("%s", text);
}

/* The player chose Quit: write the config (Host_Shutdown), let it reach the BBS, and close */
void Sys_Quit(void)
{
    Host_Shutdown();
    exit(0);
}

double Sys_FloatTime(void)
{
    return trace_time_ms() / 1000.0;
}

char *Sys_ConsoleInput(void)
{
    return NULL;
}

void Sys_Sleep(void)
{
}

void Sys_SendKeyEvents(void)
{
}

void Sys_HighFPPrecision(void)
{
}

void Sys_LowFPPrecision(void)
{
}

void Sys_SetFPCW(void)
{
}
