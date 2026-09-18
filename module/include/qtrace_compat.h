/*
 * qtrace_compat.h -- forced into every file with -include, so Quake's sources build unchanged.
 *
 * There is no file system in the sandbox. Quake opens its files with plain fopen: the pak, config.cfg, savegames.
 * Those go to src/qfiles.c instead, which hands back real stdio streams over memory, so every fprintf/fscanf Quake
 * does on them works as it always did. fclose goes there too, because closing a file Quake wrote is what sends it to
 * the BBS. printf goes to TERMinator's debug output, since a module has no console.
 */

#ifndef QTRACE_COMPAT_H
#define QTRACE_COMPAT_H

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

FILE *qfiles_fopen(const char *name, const char *mode);
int   qfiles_fclose(FILE *f);
int   qtrace_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void  qtrace_exit(int code) __attribute__((noreturn));

#define fopen(name, mode) qfiles_fopen((name), (mode))
#define fclose(f)         qfiles_fclose(f)
#define printf            qtrace_printf
#define exit(code)        qtrace_exit(code)

#endif
