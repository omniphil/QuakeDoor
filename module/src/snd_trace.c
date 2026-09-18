/*
 * snd_trace.c -- the "sound card" for WinQuake's own mixer (snd_dma.c, snd_mix.c, snd_mem.c from id's release).
 *
 * Quake mixes a little ahead into a ring buffer, as it did into a Sound Blaster's DMA buffer, and asks how far the card
 * has played. Here "played" means handed to TERMinator: frames go out as fast as its queue takes them, and that
 * position is what Quake is told. 16-bit stereo at 44.1 kHz, TERMinator's own rate, so Quake resamples its 11 kHz
 * sounds once, when it loads them, and nothing is resampled again.
 */

#include "quakedef.h"

#include "trace_api.h"
#include "qtrace.h"

#define RING_FRAMES  32768                 /* a power of two, as Quake's mixer needs */
#define QUEUE_TARGET 4096                  /* frames kept queued in TERMinator: under a tenth of a second */

/* (Quake's level is brought down inside its mixer, before clipping: src/overrides/snd_mix.c) */

static short    g_ring[RING_FRAMES * 2];
static unsigned g_sent;                    /* frames handed to TERMinator so far */
static int      g_largest_room;            /* the most room TERMinator has reported: its queue when empty */
static int      g_ready;

qboolean SNDDMA_Init(void)
{
    shm = &sn;
    shm->splitbuffer = 0;
    shm->channels = 2;
    shm->samplebits = 16;
    shm->speed = 44100;                    /* TE_AUDIO_RATE */
    shm->samples = RING_FRAMES * 2;        /* mono samples in the buffer */
    shm->samplepos = 0;
    shm->submission_chunk = 1;
    shm->buffer = (unsigned char *)g_ring;
    g_sent = 0;
    g_ready = 1;
    return true;
}

/* Where "the card" is, in samples, within the ring */
int SNDDMA_GetDMAPos(void)
{
    shm->samplepos = (int)((g_sent * 2) & (unsigned)(shm->samples - 1));
    return shm->samplepos;
}

/* Everything mixed and not yet sent goes to TERMinator, as much as it will take */
void qtrace_pump_audio(void)
{
    int room;
    unsigned have;

    if (!g_ready)
        return;
    room = trace_audio_room();
    if (room > g_largest_room)
        g_largest_room = room;
    /* Keep only a little queued, so what's heard follows what's happening on screen */
    if (g_largest_room - room >= QUEUE_TARGET)
        return;
    if (room > QUEUE_TARGET - (g_largest_room - room))
        room = QUEUE_TARGET - (g_largest_room - room);

    have = (unsigned)paintedtime - g_sent;
    if ((int)have <= 0)
        return;
    if (have > (unsigned)room)
        have = (unsigned)room;
    while (have > 0)
    {
        unsigned at = g_sent & (RING_FRAMES - 1);
        unsigned run = RING_FRAMES - at < have ? RING_FRAMES - at : have;
        int took = trace_audio_write(g_ring + at * 2, (int32_t)run);
        if (took <= 0)
            break;
        g_sent += (unsigned)took;
        have -= (unsigned)took;
        if ((unsigned)took < run)
            break;
    }
}

void SNDDMA_Submit(void)
{
    qtrace_pump_audio();
}

void SNDDMA_Shutdown(void)
{
    g_ready = 0;
}
