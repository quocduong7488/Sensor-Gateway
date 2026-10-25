
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "protocol_adapter.h"
#include "sensor_types.h"
#include "sensor_config.h"

/* ---------- TBP: Temperature ---------- */
static parse_result_t parse_tbp(
    const uint8_t *buf,
    size_t len,
    sensor_data_t *out,
    size_t *consumed
);

/* ---------- HLP: Humidity ---------- */
static parse_result_t parse_hlp(
    const uint8_t *buf,
    size_t len,
    sensor_data_t *out,
    size_t *consumed
);

/* ---------- EVP: Motion / Door ---------- */
static parse_result_t parse_evp(
    const uint8_t *buf,
    size_t len,
    sensor_data_t *out,
    size_t *consumed
);

/* ---------- SGP: Smoke / Gas ---------- */
static parse_result_t parse_sgp(
    const uint8_t *buf,
    size_t len,
    sensor_data_t *out,
    size_t *consumed
);

/* SGP reply: ACK / NAK */
static size_t build_sgp_reply(
    parse_result_t result,
    uint8_t *reply,
    size_t cap
);

static const protocol_adapter_t adapters[] = {
    {
        .name = "TBP",
        .port_offset = 0,
        .parse = parse_tbp,
        .build_reply = NULL
    },
    {
        .name = "HLP",
        .port_offset = 1,
        .parse = parse_hlp,
        .build_reply = NULL
    },
    {
        .name = "EVP",
        .port_offset = 2,
        .parse = parse_evp,
        .build_reply = NULL
    },
    {
        .name = "SGP",
        .port_offset = 3,
        .parse = parse_sgp,
        .build_reply = build_sgp_reply
    }
};

/* Lay so luong adapter */
size_t protocol_adapter_count(void)
{
    return sizeof(adapters) / sizeof(adapters[0]);
}

/* Lay adapter theo index: 0=TBP, 1=HLP, 2=EVP, 3=SGP */
const protocol_adapter_t *protocol_adapter_get(size_t index)
{
    if (index >= protocol_adapter_count()) {
        return NULL;
    }

    return &adapters[index];
}
