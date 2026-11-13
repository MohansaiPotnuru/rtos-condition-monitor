#include "cobs.h"

size_t cobs_encode(const uint8_t *in, size_t len, uint8_t *out, size_t cap)
{
    if (out == NULL || cap < COBS_MAX_ENCODED(len) || (in == NULL && len != 0u)) {
        return 0u;
    }

    size_t  code_idx = 0u; /* where the current block's code byte goes */
    size_t  w        = 1u; /* next write position */
    uint8_t code     = 1u; /* 1 + number of data bytes in the current block */

    for (size_t i = 0u; i < len; i++) {
        if (in[i] != 0u) {
            out[w++] = in[i];
            code++;
        }
        if (in[i] == 0u || code == 0xFFu) {
            out[code_idx] = code;
            code          = 1u;
            code_idx      = w;
            /* Reserve a code byte unless a full block ended exactly at the end. */
            if (in[i] == 0u || (i + 1u) < len) {
                w++;
            }
        }
    }
    if (code_idx < w) {
        out[code_idx] = code;
    }
    return w;
}

bool cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t cap, size_t *out_len)
{
    if (in == NULL || out == NULL || out_len == NULL || len == 0u) {
        return false;
    }

    size_t r = 0u;
    size_t w = 0u;

    while (r < len) {
        const uint8_t code = in[r++];
        if (code == 0u) {
            return false;
        }
        for (uint8_t i = 1u; i < code; i++) {
            if (r >= len || in[r] == 0u || w >= cap) {
                return false;
            }
            out[w++] = in[r++];
        }
        /* A short block implies a zero, except at the very end of the frame. */
        if (code != 0xFFu && r < len) {
            if (w >= cap) {
                return false;
            }
            out[w++] = 0u;
        }
    }
    *out_len = w;
    return true;
}
