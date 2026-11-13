/**
 * @file    crc16.h
 * @brief   CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xor-out).
 *
 * Check value: crc16_ccitt("123456789", 9, CRC16_INIT) == 0x29B1.
 * Table-free on purpose: 8 shifts per byte keeps flash use tiny, and the
 * telemetry frames it protects are only a few dozen bytes long.
 */
#ifndef LIB_CRC16_H
#define LIB_CRC16_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRC16_INIT 0xFFFFu

/** Continue a CRC over @p len bytes. Pass CRC16_INIT to start a new one. */
uint16_t crc16_ccitt(const void *data, size_t len, uint16_t crc);

#ifdef __cplusplus
}
#endif

#endif /* LIB_CRC16_H */
