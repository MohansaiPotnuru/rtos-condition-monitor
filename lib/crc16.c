#include "crc16.h"

uint16_t crc16_ccitt(const void *data, size_t len, uint16_t crc)
{
    const uint8_t *p = (const uint8_t *)data;

    while (len--) {
        crc ^= (uint16_t)((uint16_t)*p++ << 8);
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}
