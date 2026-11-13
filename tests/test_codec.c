/* CRC-16 and COBS: published vectors, edge cases and randomised round trips. */
#include <stdint.h>
#include <stdlib.h>

#include "cobs.h"
#include "crc16.h"
#include "unit.h"

void test_crc16_check_value(void)
{
    CHECK_EQ(crc16_ccitt("123456789", 9, CRC16_INIT), 0x29B1);
    CHECK_EQ(crc16_ccitt("", 0, CRC16_INIT), 0xFFFF);
    /* Incremental == one-shot */
    uint16_t c = crc16_ccitt("12345", 5, CRC16_INIT);
    CHECK_EQ(crc16_ccitt("6789", 4, c), 0x29B1);
}

static void cobs_vector(const uint8_t *in, size_t n, const uint8_t *exp, size_t m)
{
    uint8_t enc[600];
    uint8_t dec[600];
    size_t  dlen = 0;

    CHECK_EQ(cobs_encode(in, n, enc, sizeof enc), m);
    CHECK_MEM(enc, exp, m);
    CHECK(cobs_decode(enc, m, dec, sizeof dec, &dlen));
    CHECK_EQ(dlen, n);
    if (n > 0) {
        CHECK_MEM(dec, in, n);
    }
}

void test_cobs_wikipedia_vectors(void)
{
    const uint8_t a_in[] = {0x00};
    const uint8_t a_ex[] = {0x01, 0x01};
    cobs_vector(a_in, 1, a_ex, 2);

    const uint8_t b_in[] = {0x00, 0x00};
    const uint8_t b_ex[] = {0x01, 0x01, 0x01};
    cobs_vector(b_in, 2, b_ex, 3);

    const uint8_t c_in[] = {0x00, 0x11, 0x00};
    const uint8_t c_ex[] = {0x01, 0x02, 0x11, 0x01};
    cobs_vector(c_in, 3, c_ex, 4);

    const uint8_t d_in[] = {0x11, 0x22, 0x00, 0x33};
    const uint8_t d_ex[] = {0x03, 0x11, 0x22, 0x02, 0x33};
    cobs_vector(d_in, 4, d_ex, 5);

    const uint8_t e_in[] = {0x11, 0x22, 0x33, 0x44};
    const uint8_t e_ex[] = {0x05, 0x11, 0x22, 0x33, 0x44};
    cobs_vector(e_in, 4, e_ex, 5);

    const uint8_t f_in[] = {0x11, 0x00, 0x00, 0x00};
    const uint8_t f_ex[] = {0x02, 0x11, 0x01, 0x01, 0x01};
    cobs_vector(f_in, 4, f_ex, 5);

    const uint8_t empty_ex[] = {0x01};
    cobs_vector(NULL, 0, empty_ex, 1);
}

void test_cobs_block_boundaries(void)
{
    uint8_t in[256];
    uint8_t ex[260];

    /* 254 non-zero bytes: one full block, no trailing code byte. */
    for (int i = 0; i < 254; i++) {
        in[i] = (uint8_t)(i + 1);
    }
    ex[0] = 0xFF;
    memcpy(&ex[1], in, 254);
    cobs_vector(in, 254, ex, 255);

    /* 255 non-zero bytes (0x01..0xFF): FF + 254 bytes + 02 FF */
    for (int i = 0; i < 255; i++) {
        in[i] = (uint8_t)(i + 1);
    }
    ex[0] = 0xFF;
    memcpy(&ex[1], in, 254);
    ex[255] = 0x02;
    ex[256] = 0xFF;
    cobs_vector(in, 255, ex, 257);

    /* 0x00 followed by 254 non-zero bytes: 01 FF + 254 bytes */
    in[0] = 0x00;
    for (int i = 1; i < 255; i++) {
        in[i] = (uint8_t)i;
    }
    ex[0] = 0x01;
    ex[1] = 0xFF;
    memcpy(&ex[2], &in[1], 254);
    cobs_vector(in, 255, ex, 256);
}

void test_cobs_rejects_bad_input(void)
{
    uint8_t out[16];
    size_t  n = 0;

    const uint8_t zero_inside[] = {0x03, 0x11, 0x00};
    CHECK(!cobs_decode(zero_inside, 3, out, sizeof out, &n));
    const uint8_t truncated[] = {0x05, 0x11, 0x22};
    CHECK(!cobs_decode(truncated, 3, out, sizeof out, &n));
    const uint8_t leading_zero[] = {0x00, 0x01};
    CHECK(!cobs_decode(leading_zero, 2, out, sizeof out, &n));
    CHECK(!cobs_decode(zero_inside, 0, out, sizeof out, &n));

    const uint8_t ok[] = {0x05, 0x11, 0x22, 0x33, 0x44};
    CHECK(!cobs_decode(ok, 5, out, 3, &n)); /* output too small */

    uint8_t small[2];
    const uint8_t data[] = {1, 2, 3};
    CHECK_EQ(cobs_encode(data, 3, small, sizeof small), 0); /* capacity check */
}

void test_cobs_random_roundtrip(void)
{
    uint8_t in[700];
    uint8_t enc[COBS_MAX_ENCODED(700)];
    uint8_t dec[700];
    srand(12345);

    for (int iter = 0; iter < 2000; iter++) {
        const size_t n    = (size_t)(rand() % 700);
        const int    zpct = rand() % 100; /* density of zeros varies per run */
        for (size_t i = 0; i < n; i++) {
            in[i] = (uint8_t)((rand() % 100 < zpct) ? 0 : 1 + rand() % 255);
        }
        const size_t m = cobs_encode(in, n, enc, sizeof enc);
        CHECK(m >= 1 && m <= COBS_MAX_ENCODED(n));
        for (size_t i = 0; i < m; i++) {
            if (enc[i] == 0) {
                CHECK(enc[i] != 0);
                break;
            }
        }
        size_t dlen = 0;
        CHECK(cobs_decode(enc, m, dec, sizeof dec, &dlen));
        CHECK_EQ(dlen, n);
        CHECK(n == 0 || memcmp(in, dec, n) == 0);
    }
}
