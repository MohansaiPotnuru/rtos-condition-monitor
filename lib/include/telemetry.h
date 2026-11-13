/**
 * @file    telemetry.h
 * @brief   Telemetry snapshot encoding: binary frames and human-readable CSV.
 *
 * Binary frame (little-endian), then CRC, then COBS, then a 0x00 delimiter:
 *
 *   off size field
 *   0   1    version (TLM_VERSION)
 *   1   1    type    (TLM_TYPE_SNAPSHOT)
 *   2   2    seq     (wraps; gaps reveal dropped frames)
 *   4   4    uptime_ms
 *   8   2    temp_cdeg   (int16, 0.01 degC)
 *   10  2    vdda_mv
 *   12  2    ain_mv      (window mean)
 *   14  2    ain_min_mv
 *   16  2    ain_max_mv
 *   18  2    ain_rms_mv  (AC RMS over the window)
 *   20  1    alarms      (bits 0-2 active, bits 4-6 latched: temp, vdda, rms)
 *   21  1    cpu_pct
 *   22  2    crc16       (CRC-16/CCITT-FALSE over bytes 0..21)
 *
 * tools/tlm_decode.py implements the same format; tests/test_telemetry.c
 * and tools/test_tlm_decode.py share a golden vector so the two cannot drift.
 */
#ifndef LIB_TELEMETRY_H
#define LIB_TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TLM_VERSION        1u
#define TLM_TYPE_SNAPSHOT  1u
#define TLM_RAW_LEN        24u
#define TLM_WIRE_MAX       (TLM_RAW_LEN + TLM_RAW_LEN / 254u + 2u) /* COBS + delimiter */

#define TLM_ALARM_TEMP     0x01u
#define TLM_ALARM_VDDA     0x02u
#define TLM_ALARM_RMS      0x04u
#define TLM_LATCH_SHIFT    4u

typedef struct {
    int16_t  temp_cdeg;
    uint16_t vdda_mv;
    uint16_t ain_mv;
    uint16_t ain_min_mv;
    uint16_t ain_max_mv;
    uint16_t ain_rms_mv;
    uint8_t  alarms;
    uint8_t  cpu_pct;
} tlm_snapshot_t;

/** Serialise to the raw (pre-COBS) frame. Returns TLM_RAW_LEN, or 0 if @p cap is too small. */
size_t tlm_pack(const tlm_snapshot_t *s, uint16_t seq, uint32_t uptime_ms, uint8_t *out, size_t cap);

/** Parse and CRC-check a raw frame. */
bool tlm_unpack(const uint8_t *raw, size_t len, tlm_snapshot_t *s, uint16_t *seq, uint32_t *uptime_ms);

/** Pack + COBS + delimiter, ready for the wire. Returns bytes written, or 0. */
size_t tlm_encode_wire(const tlm_snapshot_t *s, uint16_t seq, uint32_t uptime_ms, uint8_t *out, size_t cap);

/** One CSV line terminated with CRLF. Returns the length, or -1 if it does not fit. */
int tlm_format_csv(const tlm_snapshot_t *s, uint16_t seq, uint32_t uptime_ms, char *out, size_t cap);

/** Header matching tlm_format_csv(). */
const char *tlm_csv_header(void);

#ifdef __cplusplus
}
#endif

#endif /* LIB_TELEMETRY_H */
