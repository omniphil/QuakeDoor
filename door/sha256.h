/*
 * sha256.h - SHA-256, for naming the TRACE module the door sends (TERMinator caches modules by their hash)
 * Fractals - Mandelbrot BBS Door
 */

#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

// Hash len bytes of data; hex gets 64 lower-case hex digits plus a terminating NUL
void sha256_hex(const uint8_t *data, size_t len, char hex[65]);

#endif // SHA256_H
