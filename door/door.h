/*
 * door.h - BBS door interface declarations
 * For Mystic BBS Door Game
 */

#ifndef DOOR_H
#define DOOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Maximum string lengths
#define MAX_USERNAME 64
#define MAX_BBSID    32

// Communication types from door32.sys
#define COMM_LOCAL   0
#define COMM_SERIAL  1
#define COMM_TELNET  2

// Door information structure
typedef struct {
    int comm_type;                  // 0=local, 1=serial, 2=telnet
    int comm_handle;                // Socket/COM handle
    int baud_rate;
    char bbs_id[MAX_BBSID];
    int user_record;
    char real_name[MAX_USERNAME];
    char handle[MAX_USERNAME];
    int security_level;
    int time_remaining;             // Minutes
    bool local_mode;                // true if running locally without drop file
} DoorInfo;

// Global door info
extern DoorInfo door_info;

// Initialize door - parse drop file or set local mode
bool door_init(const char *drop_file_path);

// Check if time is remaining
int door_time_remaining(void);

// Output functions
void door_write(const char *str);
void door_write_char(char c);
void door_write_raw(const char *data, size_t len);

// Input functions
int door_read_char(void);
int door_read_char_timeout(int timeout_ms);
bool door_kbhit(void);
int door_read_line(char *buffer, int max_len);

// Cleanup
void door_cleanup(void);

#endif // DOOR_H
