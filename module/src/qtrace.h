/* qtrace.h -- what the TRACE platform layer's files share with each other (see qtrace.c). */

#ifndef QTRACE_H
#define QTRACE_H

#include <stddef.h>
#include <stdint.h>

/* One event from TERMinator (engine contract TE_IN_*), queued for the game thread */
typedef struct
{
    int32_t type, flags, a, b, c;
} qtrace_event_t;

#define TE_IN_KEY          1   /* flags bit0 pressed, bit1 extended (E0); a = set-1 scancode */
#define TE_IN_MOUSE_MOVE   3   /* b = dx, c = dy */
#define TE_IN_MOUSE_BUTTON 4   /* flags bit0 pressed; a = 1 left, 2 middle, 3 right, 4 wheel up, 5 wheel down */
#define TE_IN_FOCUS        5   /* flags bit0 focused */
#define TE_IN_QUIT         6   /* the player closed the picture, or the door went away */

void qtrace_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Events, oldest first; 0 when there are none */
int  qtrace_next_event(qtrace_event_t *out);

/* The shareware pak, unpacked from the door's archive */
const uint8_t *qtrace_pak(size_t *size);

/* ---- the player's files, which live on the BBS (qfiles.c) ---- */
void qfiles_on_file(const char *name, size_t off, size_t total, const uint8_t *data, size_t len);
void qfiles_on_list(const char *name, const uint8_t *prefix, size_t len);
void qfiles_on_none(const char *name);
int  qfiles_pump(void);          /* sends what it can; 1 while anything is still waiting to go */

/* Sound: hand TERMinator what Quake has mixed (snd_trace.c) */
void qtrace_pump_audio(void);

/* Stop: saves are made sure of, then TERMinator closes the picture. Does not return. */
void qtrace_exit(int code) __attribute__((noreturn));

#endif
