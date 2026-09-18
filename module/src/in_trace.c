/*
 * in_trace.c -- keyboard and mouse from TERMinator, for quakegeneric's in_null.c (which asks through QG_GetKey and
 * QG_GetMouseMove).
 *
 * TERMinator sends physical keys as PC set-1 scancodes, which is exactly what DOS and Windows Quake read, so this is
 * WinQuake's own scancode table. The E0 keys (the arrow block, right Ctrl/Alt) share their scancodes with the keypad
 * and mean the same thing to Quake.
 */

#include "quakedef.h"
#include "quakegeneric.h"

#include "qtrace.h"

static const unsigned char scantokey[128] =
{
//  0           1           2           3           4           5           6           7
//  8           9           A           B           C           D           E           F
    0,          27,         '1',        '2',        '3',        '4',        '5',        '6',
    '7',        '8',        '9',        '0',        '-',        '=',        K_BACKSPACE, 9,          // 0
    'q',        'w',        'e',        'r',        't',        'y',        'u',        'i',
    'o',        'p',        '[',        ']',        13,         K_CTRL,     'a',        's',        // 1
    'd',        'f',        'g',        'h',        'j',        'k',        'l',        ';',
    '\'',       '`',        K_SHIFT,    '\\',       'z',        'x',        'c',        'v',        // 2
    'b',        'n',        'm',        ',',        '.',        '/',        K_SHIFT,    '*',
    K_ALT,      ' ',        0,          K_F1,       K_F2,       K_F3,       K_F4,       K_F5,       // 3
    K_F6,       K_F7,       K_F8,       K_F9,       K_F10,      0,          0,          K_HOME,
    K_UPARROW,  K_PGUP,     '-',        K_LEFTARROW, '5',       K_RIGHTARROW, '+',      K_END,      // 4
    K_DOWNARROW, K_PGDN,    K_INS,      K_DEL,      0,          0,          0,          K_F11,
    K_F12,      0,          0,          0,          0,          0,          0,          0,          // 5
};

static int g_mouse_dx, g_mouse_dy;
static int g_wheel_up_pending;             /* a wheel click is a press and a release; the release comes next call */

/* Called by in_null.c until it returns 0; mouse movement met on the way is saved for QG_GetMouseMove */
int QG_GetKey(int *down, int *key)
{
    qtrace_event_t ev;

    if (g_wheel_up_pending)
    {
        *key = g_wheel_up_pending;
        *down = 0;
        g_wheel_up_pending = 0;
        return 1;
    }

    while (qtrace_next_event(&ev))
    {
        switch (ev.type)
        {
        case TE_IN_KEY:
            if ((unsigned)ev.a < 128 && scantokey[ev.a] != 0)
            {
                *key = scantokey[ev.a];
                *down = ev.flags & 1;
                return 1;
            }
            break;

        case TE_IN_MOUSE_MOVE:
            g_mouse_dx += ev.b;
            g_mouse_dy += ev.c;
            break;

        case TE_IN_MOUSE_BUTTON:
            /* Quake's MOUSE2 is the right button and MOUSE3 the middle one */
            if (ev.a >= 1 && ev.a <= 3)
            {
                static const int buttons[4] = { 0, K_MOUSE1, K_MOUSE3, K_MOUSE2 };
                *key = buttons[ev.a];
                *down = ev.flags & 1;
                return 1;
            }
            if ((ev.a == 4 || ev.a == 5) && (ev.flags & 1))
            {
                *key = g_wheel_up_pending = ev.a == 4 ? K_MWHEELUP : K_MWHEELDOWN;
                *down = 1;
                return 1;
            }
            break;

        default:
            break;
        }
    }
    return 0;
}

void QG_GetMouseMove(int *x, int *y)
{
    *x = g_mouse_dx;
    *y = g_mouse_dy;
    g_mouse_dx = g_mouse_dy = 0;
}

void QG_GetJoyAxes(float *axes)
{
    for (int i = 0; i < QUAKEGENERIC_JOY_MAX_AXES; i++)
        axes[i] = 0;
}
