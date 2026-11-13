#include "telemetry.h"

#include <stdio.h>

#include "cobs.h"
#include "crc16.h"

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)get16(p) | ((uint32_t)get16(p + 2) << 16);
}

size_t tlm_pack(const tlm_snapshot_t *s, uint16_t seq, uint32_t uptime_ms, uint8_t *out, size_t cap)
{
    if (s == NULL || out == NULL || cap < TLM_RAW_LEN) {
        return 0u;
    }
    out[0] = TLM_VERSION;
    out[1] = TLM_TYPE_SNAPSHOT;
    put16(&out[2], seq);
    put32(&out[4], uptime_ms);
    put16(&out[8], (uint16_t)s->temp_cdeg);
    put16(&out[10], s->vdda_mv);
    put16(&out[12], s->ain_mv);
    put16(&out[14], s->ain_min_mv);
    put16(&out[16], s->ain_max_mv);
    put16(&out[18], s->ain_rms_mv);
    out[20] = s->alarms;
    out[21] = s->cpu_pct;
    put16(&out[22], crc16_ccitt(out, 22u, CRC16_INIT));
    return TLM_RAW_LEN;
}

bool tlm_unpack(const uint8_t *raw, size_t len, tlm_snapshot_t *s, uint16_t *seq, uint32_t *uptime_ms)
{
    if (raw == NULL || s == NULL || len != TLM_RAW_LEN) {
        return false;
    }
    if (raw[0] != TLM_VERSION || raw[1] != TLM_TYPE_SNAPSHOT) {
        return false;
    }
    if (crc16_ccitt(raw, 22u, CRC16_INIT) != get16(&raw[22])) {
        return false;
    }
    if (seq != NULL) {
        *seq = get16(&raw[2]);
    }
    if (uptime_ms != NULL) {
        *uptime_ms = get32(&raw[4]);
    }
    s->temp_cdeg  = (int16_t)get16(&raw[8]);
    s->vdda_mv    = get16(&raw[10]);
    s->ain_mv     = get16(&raw[12]);
    s->ain_min_mv = get16(&raw[14]);
    s->ain_max_mv = get16(&raw[16]);
    s->ain_rms_mv = get16(&raw[18]);
    s->alarms     = raw[20];
    s->cpu_pct    = raw[21];
    return true;
}

size_t tlm_encode_wire(const tlm_snapshot_t *s, uint16_t seq, uint32_t uptime_ms, uint8_t *out, size_t cap)
{
    uint8_t raw[TLM_RAW_LEN];

    if (tlm_pack(s, seq, uptime_ms, raw, sizeof raw) == 0u || cap < 1u) {
        return 0u;
    }
    const size_t n = cobs_encode(raw, sizeof raw, out, cap - 1u);
    if (n == 0u) {
        return 0u;
    }
    out[n] = 0x00u;
    return n + 1u;
}

const char *tlm_csv_header(void)
{
    return "#tlm,seq,uptime_ms,temp_c,vdda_mv,ain_mv,ain_min_mv,ain_max_mv,ain_rms_mv,alarms,cpu_pct\r\n";
}

int tlm_format_csv(const tlm_snapshot_t *s, uint16_t seq, uint32_t uptime_ms, char *out, size_t cap)
{
    const int32_t t    = s->temp_cdeg;
    const int32_t mag  = (t < 0) ? -t : t;
    const int     n    = snprintf(out, cap, "tlm,%u,%lu,%s%ld.%02ld,%u,%u,%u,%u,%u,0x%02X,%u\r\n",
                                  (unsigned)seq, (unsigned long)uptime_ms, (t < 0) ? "-" : "",
                                  (long)(mag / 100), (long)(mag % 100), (unsigned)s->vdda_mv,
                                  (unsigned)s->ain_mv, (unsigned)s->ain_min_mv, (unsigned)s->ain_max_mv,
                                  (unsigned)s->ain_rms_mv, (unsigned)s->alarms, (unsigned)s->cpu_pct);
    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    return n;
}
