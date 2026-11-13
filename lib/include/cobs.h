/**
 * @file    cobs.h
 * @brief   Consistent Overhead Byte Stuffing (Cheshire & Baker, 1999).
 *
 * COBS removes every 0x00 from a frame so that 0x00 can be used as an
 * unambiguous frame delimiter on a byte stream (here: the UART). The overhead
 * is at most 1 byte per 254 payload bytes, and a receiver can resynchronise
 * after any corruption simply by waiting for the next 0x00.
 */
#ifndef LIB_COBS_H
#define LIB_COBS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Worst-case encoded size (without the trailing 0x00 delimiter). */
#define COBS_MAX_ENCODED(n) ((n) + ((n) / 254u) + 1u)

/**
 * Encode @p len bytes. The output contains no 0x00 bytes and no delimiter.
 * @return encoded length, or 0 if @p cap < COBS_MAX_ENCODED(len).
 */
size_t cobs_encode(const uint8_t *in, size_t len, uint8_t *out, size_t cap);

/**
 * Decode one frame (delimiter already stripped).
 * @return false on malformed input (embedded 0x00, truncated block) or overflow.
 */
bool cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* LIB_COBS_H */
