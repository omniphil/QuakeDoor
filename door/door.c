/*
 * door.c - BBS door interface implementation
 * For Mystic BBS Door Game
 * Adapted from Chess project
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/time.h>
#endif

#include "door.h"

// Global door info
DoorInfo door_info;

// Start time for time tracking
static time_t start_time;

// Terminal settings for raw mode
#ifndef _WIN32
static struct termios orig_termios;
static bool termios_saved = false;

static void restore_terminal(void) {
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    }
}

static void set_raw_mode(void) {
    struct termios raw;
    if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
        termios_saved = true;
        atexit(restore_terminal);
        raw = orig_termios;
        raw.c_lflag &= ~(ECHO | ICANON);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 1;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
}
#endif

// Parse a line from drop file, removing newline
static void read_drop_line(FILE *fp, char *buf, int maxlen) {
    if (fgets(buf, maxlen, fp) == NULL) {
        buf[0] = '\0';
        return;
    }
    int len = (int)strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
        buf[--len] = '\0';
    }
}

// Initialize door from door32.sys or local mode
bool door_init(const char *drop_file_path) {
    FILE *fp = NULL;
    char line[256];
    char path_buf[512];

    // Initialize with defaults
    memset(&door_info, 0, sizeof(DoorInfo));
    door_info.local_mode = true;
    door_info.time_remaining = 60;
    strcpy(door_info.handle, "Player");
    strcpy(door_info.real_name, "Local User");
    strcpy(door_info.bbs_id, "LOCAL");

    start_time = time(NULL);

    // Try to open drop file
    if (drop_file_path != NULL && drop_file_path[0] != '\0') {
        size_t len = strlen(drop_file_path);
        if (len < sizeof(path_buf) - 15) {
            strcpy(path_buf, drop_file_path);
            if (len > 0 && path_buf[len-1] != '/' && path_buf[len-1] != '\\') {
                strcat(path_buf, "/");
            }
            strcat(path_buf, "door32.sys");
            fp = fopen(path_buf, "r");
        }
        if (fp == NULL) {
            fp = fopen(drop_file_path, "r");
        }
    }

    // Auto-detect: try door32.sys in current directory
    if (fp == NULL) fp = fopen("door32.sys", "r");
    if (fp == NULL) fp = fopen("DOOR32.SYS", "r");

    if (fp != NULL) {
        door_info.local_mode = false;

        read_drop_line(fp, line, sizeof(line));
        door_info.comm_type = atoi(line);

        read_drop_line(fp, line, sizeof(line));
        door_info.comm_handle = atoi(line);

        read_drop_line(fp, line, sizeof(line));
        door_info.baud_rate = atoi(line);

        read_drop_line(fp, door_info.bbs_id, sizeof(door_info.bbs_id));

        read_drop_line(fp, line, sizeof(line));
        door_info.user_record = atoi(line);

        read_drop_line(fp, door_info.real_name, sizeof(door_info.real_name));
        read_drop_line(fp, door_info.handle, sizeof(door_info.handle));

        read_drop_line(fp, line, sizeof(line));
        door_info.security_level = atoi(line);

        read_drop_line(fp, line, sizeof(line));
        door_info.time_remaining = atoi(line);

        fclose(fp);

        if (door_info.handle[0] == '\0')
            strcpy(door_info.handle, door_info.real_name);
        if (door_info.real_name[0] == '\0')
            strcpy(door_info.real_name, door_info.handle);
    }

    // Set up terminal for raw input
#ifndef _WIN32
    set_raw_mode();
#endif

    return true;
}

// Check remaining time
int door_time_remaining(void) {
    time_t now = time(NULL);
    int elapsed = (int)(now - start_time) / 60;
    int remaining = door_info.time_remaining - elapsed;
    return remaining > 0 ? remaining : 0;
}

// Write string to output
void door_write(const char *str) {
    printf("%s", str);
    fflush(stdout);
}

// Write single character
void door_write_char(char c) {
    putchar(c);
    fflush(stdout);
}

// Write raw binary data (for sixel output)
void door_write_raw(const char *data, size_t len) {
    fwrite(data, 1, len, stdout);
    fflush(stdout);
}

// Read a character (blocking)
int door_read_char(void) {
#ifdef _WIN32
    return _getch();
#else
    unsigned char c;
    struct termios old_settings, new_settings;

    if (tcgetattr(STDIN_FILENO, &old_settings) == 0) {
        new_settings = old_settings;
        new_settings.c_cc[VMIN] = 1;
        new_settings.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &new_settings);
    }

    int result = -1;
    if (read(STDIN_FILENO, &c, 1) == 1) {
        result = (int)c;
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old_settings);
    return result;
#endif
}

// Check if key is available
bool door_kbhit(void) {
#ifdef _WIN32
    return _kbhit() != 0;
#else
    struct timeval tv = {0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
#endif
}

// Read a character with timeout (milliseconds). Returns -1 on timeout.
int door_read_char_timeout(int timeout_ms) {
#ifdef _WIN32
    DWORD start_tick = GetTickCount();
    while ((int)(GetTickCount() - start_tick) < timeout_ms) {
        if (_kbhit()) return _getch();
        Sleep(10);
    }
    return -1;
#else
    fd_set fds;
    struct timeval tv;
    unsigned char c;

    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
        if (read(STDIN_FILENO, &c, 1) == 1) {
            return (int)c;
        }
    }
    return -1;
#endif
}

// Read a line of input with echo
int door_read_line(char *buffer, int max_len) {
    int pos = 0;
    buffer[0] = '\0';

    while (pos < max_len - 1) {
        int c = door_read_char();
        if (c == '\r' || c == '\n') {
            door_write("\r\n");
            break;
        } else if (c == 8 || c == 127) {
            if (pos > 0) {
                pos--;
                door_write("\b \b");
            }
        } else if (c == 27) {
            buffer[0] = '\0';
            return 0;
        } else if (c >= 32 && c < 127) {
            buffer[pos++] = (char)c;
            door_write_char((char)c);
        }
    }

    buffer[pos] = '\0';
    return pos;
}

// Cleanup
void door_cleanup(void) {
#ifndef _WIN32
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        termios_saved = false;
    }
#endif
}
