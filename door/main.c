/*
 * main.c -- QUAKE, as a BBS door.
 *
 * The door doesn't run the game: it sends it. TERMinator runs the whole of Quake in its sandbox on the player's own
 * machine, so there are no pictures on the wire and it plays at full speed with sound. A terminal that can't do that
 * gets a polite screen instead.
 *
 * Shareware Quake: id's GPL WinQuake engine (via quakegeneric) with the first episode, which id allows to be passed on
 * free of charge, whole, compressed, and with its licence.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "door.h"
#include "files.h"
#include "trace_quake.h"

#define CSI "\033["

/* Where the source of what the player was sent lives (GPL-2) */
#define QUAKE_SOURCE_URL "https://github.com/omniphil/QuakeDoor"

static void cls(void)
{
    door_write(CSI "0m" CSI "2J" CSI "H");
}

static void title(void)
{
    cls();
    door_write(CSI "1;31m"
               "        ===============================================\r\n"
               "              Q U A K E   -   shareware episode\r\n"
               "        ===============================================\r\n" CSI "0m");
    door_write(CSI "1;34m" "                    BBS door by JSONBourne\r\n" CSI "0m" "\r\n");
}

/* The name in its own colours, the way the client writes it: TERM in magenta, inator in cyan. */
static void write_terminator(void)
{
    door_write(CSI "1;35m" "TERM" CSI "1;36m" "inator" CSI "0m");
}

static void press_any_key(void)
{
    door_write(CSI "0;37m\r\n  Press any key to return to the BBS...\r\n" CSI "0m");
    door_read_char();
}

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/*
 * "Detecting TRACE graphics...", centred, with a dot appearing every quarter second for a couple of seconds.
 * The look-and-see itself only takes a moment, so this is mostly for the player's benefit: something is happening,
 * and the screen isn't about to sit there silently. Returns what the terminal turned out to be.
 */
static bool detect_with_animation(void)
{
    static const char message[] = "Detecting TRACE graphics";   /* written in pieces below, TRACE in its own colour */
    const int dots = 8;                          /* 8 quarter-seconds: about two seconds in all */
    const int width = (int)sizeof(message) - 1 + dots;
    int column = (80 - width) / 2 + 1;           /* an 80-column screen, which every BBS terminal has */
    bool found;

    cls();
    door_write(CSI "12;1H");                     /* half way down */
    {
        char at[16];
        snprintf(at, sizeof(at), CSI "%dC", column - 1);
        door_write(at);
    }
    door_write(CSI "0;37m" "Detecting " CSI "1;35m" "TRACE" CSI "0;37m" " graphics.");

    {
        long start = now_ms();
        int shown = 1;

        sleep_ms(250);

        /* The actual question to the terminal, which answers in milliseconds, or not at all when there's no TRACE */
        found = trace_quake_detect();

        /* Keep the dots going to the two-second mark, however long that answer took */
        while (shown < dots)
        {
            long elapsed = now_ms() - start;
            if (elapsed >= 2000)
                break;
            if (elapsed >= (long)shown * 250)
            {
                door_write(".");
                shown++;
            }
            else
            {
                sleep_ms(25);
            }
        }
    }
    door_write(CSI "0m");
    return found;
}

/* Drawn while the game data goes up the first time, so the wait doesn't look like a hung door. */
static void data_progress(int percent)
{
    char bar[80];
    int filled = percent * 40 / 100;

    memset(bar, ' ', sizeof(bar));
    memcpy(bar, "  [", 3);
    for (int i = 0; i < 40; i++)
        bar[3 + i] = i < filled ? '#' : '.';
    snprintf(bar + 43, sizeof(bar) - 43, "] %3d%%", percent);
    door_write(CSI "s");                 /* remember where we are */
    door_write("\r");
    door_write(bar);
    door_write(CSI "u");
}

int main(int argc, char *argv[])
{
    door_init(argc > 1 ? argv[1] : NULL);

    /* Saved games belong to the player, not to the machine they called from */
    files_init(door_info.handle, door_info.user_record);

    title();

    if (!trace_quake_load_files()) {
        door_write(CSI "1;33m  This door isn't installed properly:\r\n"
                   "  quake.wasm or quake-shareware.zip is missing.\r\n" CSI "0m");
        door_write("  Please tell the sysop.\r\n");
        press_any_key();
        door_cleanup();
        return 1;
    }

    if (!detect_with_animation()) {
        title();
        door_write("\r\n  This door needs ");
        write_terminator();
        door_write(" 1.1.2 or newer with TRACE graphics support.\r\n\r\n");
        door_write(CSI "0;37m  https://deadmodemsociety.com/terminator/\r\n" CSI "0m");
        press_any_key();
        door_cleanup();
        return 0;
    }

    title();
    door_write(CSI "1;32m  ");
    write_terminator();
    door_write(CSI "1;32m found. Sending the game...\r\n" CSI "0m");

    /* Whose saved games these are. "player" means the BBS didn't tell us who is calling (no drop file), and everyone
     * would then share one set of saves, so it's worth the sysop seeing it. */
    door_write(CSI "0;37m  Saved games for: ");
    door_write(files_player());
    door_write("\r\n\r\n" CSI "0m");

    /* The data is 8.5 MB and only travels once: after that it's cached on the player's machine for good. */
    door_write(CSI "0;37m  Checking whether you already have the game data...\r\n" CSI "0m");
    if (!trace_quake_send_data(data_progress)) {
        door_write(CSI "1;33m\r\n  The game data couldn't be sent. Please try again later.\r\n" CSI "0m");
        press_any_key();
        door_cleanup();
        return 1;
    }

    door_write(CSI "0;37m\r\n  Starting QUAKE. Arrows move, Ctrl fires, Space jumps; ESC for the menu,\r\n"
               "  and Quit from there to come back to the BBS.\r\n" CSI "0m");

    if (!trace_quake_open()) {
        door_write(CSI "1;33m\r\n  QUAKE couldn't be started on your terminal.\r\n" CSI "0m");
        press_any_key();
        door_cleanup();
        return 1;
    }

    /*
     * The game now covers the terminal, so clear what's underneath it. Otherwise, the moment the player quits, the
     * text from a moment ago ("Sending the game...") flashes up before this door can draw its closing screen.
     */
    cls();

    /* From here the player's terminal has the keyboard and the screen; we wait for them to quit. */
    trace_quake_wait();
    trace_quake_close();

    cls();
    door_write(CSI "1;31m\r\n  Thanks for playing QUAKE.\r\n\r\n" CSI "0m");
    door_write(CSI "1;34m" "  BBS door by JSONBourne\r\n\r\n" CSI "0m");

    /* The player has just been sent a GPL-2 program, so this is where they're told where its source is: on the way
     * in it would flash past, because the game takes over the screen a moment later. */
    door_write(CSI "0;37m  QUAKE here is id Software's WinQuake engine, free software under the\r\n");
    door_write("  GNU GPL v2, with the shareware episode (c) id Software, passed on under\r\n");
    door_write("  id's shareware licence.\r\n");
    door_write("  Source: " CSI "1;37m" QUAKE_SOURCE_URL "\r\n" CSI "0m");
    press_any_key();

    door_cleanup();
    return 0;
}
