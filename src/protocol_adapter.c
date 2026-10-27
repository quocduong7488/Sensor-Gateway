#include "protocol_adapter.h"
#include "sensor_types.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Common helpers
 * ============================================================ */

static uint16_t read_u16_le(const uint8_t *buf)
{
    return (uint16_t)(
        (uint16_t)buf[0] |
        ((uint16_t)buf[1] << 8)
    );
}

static uint16_t read_u16_be(const uint8_t *buf)
{
    return (uint16_t)(
        ((uint16_t)buf[0] << 8) |
        (uint16_t)buf[1]
    );
}

static uint32_t read_u32_be(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) |
           (uint32_t)buf[3];
}

static uint64_t read_u64_le(const uint8_t *buf)
{
    uint64_t value = 0;
    size_t i;

    for (i = 0; i < 8; ++i) {
        value |= (uint64_t)buf[i] << (i * 8);
    }

    return value;
}

static int64_t read_i64_le(const uint8_t *buf)
{
    uint64_t bits = read_u64_le(buf);
    int64_t value;

    memcpy(&value, &bits, sizeof(value));

    return value;
}

static double read_double_le(const uint8_t *buf)
{
    uint64_t bits = read_u64_le(buf);
    double value;

    memcpy(&value, &bits, sizeof(value));

    return value;
}

static parse_result_t parse_error(size_t *consumed, size_t discard)
{
    *consumed = discard;
    return PARSE_ERROR;
}

/* ============================================================
 * TBP - Temperature Binary Protocol
 * ============================================================ */

static parse_result_t parse_tbp(
    const uint8_t *buf,
    size_t len,
    sensor_data_t *out,
    size_t *consumed)
{
    uint16_t id;
    double temperature;
    int64_t timestamp;

    if (buf == NULL || out == NULL || consumed == NULL) {
        return PARSE_ERROR;
    }

    *consumed = 0;

    if (len < 18) {
        return PARSE_NEED_MORE;
    }

    id = read_u16_le(&buf[0]);
    temperature = read_double_le(&buf[2]);
    timestamp = read_i64_le(&buf[10]);

    if (!isfinite(temperature)) {
        return parse_error(consumed, 1);
    }

    out->id = (sensor_id_t)id;
    out->type = SENSOR_TEMPERATURE;
    out->value = temperature;
    out->ts = (time_t)timestamp;

    *consumed = 18;

    return PARSE_OK;
}

/* ============================================================
 * HLP - Humidity Line Protocol
 * ============================================================ */

static int parse_unsigned_decimal(
    const char *text,
    uint64_t max_value,
    uint64_t *result)
{
    uint64_t value = 0;
    const unsigned char *p = (const unsigned char *)text;

    if (text == NULL || result == NULL || *p == '\0') {
        return 0;
    }

    while (*p != '\0') {
        unsigned int digit;

        if (*p < (unsigned char)'0' ||
            *p > (unsigned char)'9') {
            return 0;
        }

        digit = (unsigned int)(*p - (unsigned char)'0');

        if (value > (max_value - digit) / 10U) {
            return 0;
        }

        value = value * 10U + digit;
        ++p;
    }

    *result = value;
    return 1;
}

int parse_signed_timestamp(
    const char *text,
    int64_t *result)
{
    char *end = NULL;
    long long value;

    if (text == NULL || result == NULL || *text == '\0') {
        return 0;
    }

    errno = 0;
    value = strtoll(text, &end, 10);

    if (errno == ERANGE || end == text || *end != '\0') {
        return 0;
    }

    *result = (int64_t)value;
    return 1;
}

static int parse_humidity_value(
    const char *text,
    double *result)
{
    char *end = NULL;
    double value;

    if (text == NULL || result == NULL || *text == '\0') {
        return 0;
    }

    errno = 0;
    value = strtod(text, &end);

    if (errno == ERANGE ||
        end == text ||
        *end != '\0' ||
        !isfinite(value)) {
        return 0;
    }

    *result = value;
    return 1;
}

static parse_result_t parse_hlp(
    const uint8_t *buf,
    size_t len,
    sensor_data_t *out,
    size_t *consumed)
{
    size_t line_len = 0;
    size_t i;
    char line[65];
    char *id_text;
    char *value_text;
    char *timestamp_text;
    char *first_space;
    char *second_space;
    char *third_space;
    uint64_t id;
    int64_t timestamp;
    double humidity;

    if (buf == NULL || out == NULL || consumed == NULL) {
        return PARSE_ERROR;
    }

    *consumed = 0;

    for (i = 0; i < len && i < 64; ++i) {
        if (buf[i] == (uint8_t)'\n') {
            line_len = i + 1;
            break;
        }
    }

    if (line_len == 0) {
        if (len >= 64) {
            return parse_error(consumed, 1);
        }

        return PARSE_NEED_MORE;
    }

    memcpy(line, buf, line_len);
    line[line_len] = '\0';
    line[line_len - 1] = '\0';

    if (line_len <= 1) {
        return parse_error(consumed, line_len);
    }

    first_space = strchr(line, ' ');

    if (first_space == NULL) {
        return parse_error(consumed, line_len);
    }

    *first_space = '\0';

    if (strcmp(line, "HUM") != 0) {
        return parse_error(consumed, line_len);
    }

    id_text = first_space + 1;

    if (*id_text == '\0' || *id_text == ' ') {
        return parse_error(consumed, line_len);
    }

    second_space = strchr(id_text, ' ');

    if (second_space == NULL) {
        return parse_error(consumed, line_len);
    }

    *second_space = '\0';
    value_text = second_space + 1;

    if (*value_text == '\0' || *value_text == ' ') {
        return parse_error(consumed, line_len);
    }

    third_space = strchr(value_text, ' ');

    if (third_space == NULL) {
        return parse_error(consumed, line_len);
    }

    *third_space = '\0';
    timestamp_text = third_space + 1;

    if (*timestamp_text == '\0' ||
        strchr(timestamp_text, ' ') != NULL) {
        return parse_error(consumed, line_len);
    }

    if (!parse_unsigned_decimal(
            id_text, UINT16_MAX, &id)) {
        return parse_error(consumed, line_len);
    }

    if (!parse_humidity_value(value_text, &humidity)) {
        return parse_error(consumed, line_len);
    }

    if (!parse_signed_timestamp(timestamp_text, &timestamp)) {
        return parse_error(consumed, line_len);
    }

    out->id = (sensor_id_t)id;
    out->type = SENSOR_HUMIDITY;
    out->value = humidity;
    out->ts = (time_t)timestamp;

    *consumed = line_len;

    return PARSE_OK;
}

/* ============================================================
 * EVP - Event Protocol
 * ============================================================ */

static parse_result_t parse_evp(
    const uint8_t *buf,
    size_t len,
    sensor_data_t *out,
    size_t *consumed)
{
    uint8_t type;
    uint8_t payload_len;
    uint16_t id;
    uint32_t timestamp;

    if (buf == NULL || out == NULL || consumed == NULL) {
        return PARSE_ERROR;
    }

    *consumed = 0;

    if (len < 2) {
        return PARSE_NEED_MORE;
    }

    type = buf[0];
    payload_len = buf[1];

    if (type == 0x01) {
        if (payload_len != 6) {
            return parse_error(consumed, 2);
        }

        if (len < 8) {
            return PARSE_NEED_MORE;
        }

        id = read_u16_be(&buf[2]);
        timestamp = read_u32_be(&buf[4]);

        out->id = (sensor_id_t)id;
        out->type = SENSOR_MOTION;
        out->value = 1.0;
        out->ts = (time_t)timestamp;

        *consumed = 8;

        return PARSE_OK;
    }

    if (type == 0x02) {
        uint8_t state;

        if (payload_len != 7) {
            return parse_error(consumed, 2);
        }

        if (len < 9) {
            return PARSE_NEED_MORE;
        }

        id = read_u16_be(&buf[2]);
        state = buf[4];
        timestamp = read_u32_be(&buf[5]);

        if (state > 1) {
            return parse_error(consumed, 9);
        }

        out->id = (sensor_id_t)id;
        out->type = SENSOR_DOOR;
        out->value = (double)state;
        out->ts = (time_t)timestamp;

        *consumed = 9;

        return PARSE_OK;
    }

    return parse_error(consumed, 2);
}

/* ============================================================
 * SGP - Smoke / Gas Protocol
 * ============================================================ */

static uint16_t crc16_ccitt_false(
    const uint8_t *buf,
    size_t len)
{
    uint16_t crc = 0xFFFF;
    size_t i;
    unsigned int bit;

    for (i = 0; i < len; ++i) {
        crc ^= (uint16_t)((uint16_t)buf[i] << 8);

        for (bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

static parse_result_t parse_sgp(
    const uint8_t *buf,
    size_t len,
    sensor_data_t *out,
    size_t *consumed)
{
    uint16_t id;
    uint16_t ppm;
    uint16_t received_crc;
    uint16_t calculated_crc;
    uint32_t timestamp;

    if (buf == NULL || out == NULL || consumed == NULL) {
        return PARSE_ERROR;
    }

    *consumed = 0;

    if (len < 11) {
        return PARSE_NEED_MORE;
    }

    if (buf[0] != 0x7E) {
        return parse_error(consumed, 1);
    }

    id = read_u16_be(&buf[1]);
    ppm = read_u16_be(&buf[3]);
    timestamp = read_u32_be(&buf[5]);

    received_crc = read_u16_be(&buf[9]);
    calculated_crc = crc16_ccitt_false(&buf[1], 8);

    if (received_crc != calculated_crc) {
        return parse_error(consumed, 1);
    }

    out->id = (sensor_id_t)id;
    out->type = SENSOR_SMOKE_GAS;
    out->value = (double)ppm;
    out->ts = (time_t)timestamp;

    *consumed = 11;

    return PARSE_OK;
}

static size_t build_sgp_reply(
    parse_result_t result,
    uint8_t *reply,
    size_t cap)
{
    if (reply == NULL || cap < 1) {
        return 0;
    }

    if (result == PARSE_OK) {
        reply[0] = 0x06;
        return 1;
    }

    if (result == PARSE_ERROR) {
        reply[0] = 0x15;
        return 1;
    }

    return 0;
}

/* ============================================================
 * Adapter registry
 * ============================================================ */

static size_t build_no_reply(
    parse_result_t result,
    uint8_t *reply,
    size_t cap)
{
    (void)result;
    (void)reply;
    (void)cap;

    return 0;
}

static const protocol_adapter_t adapters[] = {
    {
        .name = "TBP",
        .port_offset = 0,
        .parse = parse_tbp,
        .build_reply = build_no_reply
    },
    {
        .name = "HLP",
        .port_offset = 1,
        .parse = parse_hlp,
        .build_reply = build_no_reply
    },
    {
        .name = "EVP",
        .port_offset = 2,
        .parse = parse_evp,
        .build_reply = build_no_reply
    },
    {
        .name = "SGP",
        .port_offset = 3,
        .parse = parse_sgp,
        .build_reply = build_sgp_reply
    }
};

size_t protocol_adapter_count(void)
{
    return sizeof(adapters) / sizeof(adapters[0]);
}

const protocol_adapter_t *protocol_adapter_get(size_t index)
{
    if (index >= protocol_adapter_count()) {
        return NULL;
    }

    return &adapters[index];
}