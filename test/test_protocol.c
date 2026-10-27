#include "protocol_adapter.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t protocol_adapter_count(void);
const protocol_adapter_t *protocol_adapter_get(size_t index);

static int failures = 0;
static int tests = 0;

#define CHECK(cond, msg) do { \
    ++tests; \
    if (!(cond)) { \
        ++failures; \
        printf("FAIL: %s (line %d)\n", msg, __LINE__); \
    } else { \
        printf("PASS: %s\n", msg); \
    } \
} while (0)

static void put_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xffU);
    p[1] = (uint8_t)((v >> 8) & 0xffU);
}

static void put_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xffU);
    p[1] = (uint8_t)(v & 0xffU);
}

static void put_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xffU);
    p[1] = (uint8_t)((v >> 16) & 0xffU);
    p[2] = (uint8_t)((v >> 8) & 0xffU);
    p[3] = (uint8_t)(v & 0xffU);
}

static uint16_t crc16_ccitt_false(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xffffU;
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

static const protocol_adapter_t *get_adapter(const char *name)
{
    size_t i;

    for (i = 0; i < protocol_adapter_count(); ++i) {
        const protocol_adapter_t *a = protocol_adapter_get(i);
        if (a != NULL && strcmp(a->name, name) == 0) {
            return a;
        }
    }
    return NULL;
}

static void test_registry(void)
{
    const protocol_adapter_t *tbp = get_adapter("TBP");
    const protocol_adapter_t *hlp = get_adapter("HLP");
    const protocol_adapter_t *evp = get_adapter("EVP");
    const protocol_adapter_t *sgp = get_adapter("SGP");

    CHECK(protocol_adapter_count() == 4, "registry contains four adapters");

    CHECK(tbp != NULL && tbp->port_offset == 0, "TBP port offset is 0");

    CHECK(hlp != NULL && hlp->port_offset == 1, "HLP port offset is 1");

    CHECK(evp != NULL && evp->port_offset == 2, "EVP port offset is 2");

    CHECK(sgp != NULL && sgp->port_offset == 3, "SGP port offset is 3");

    CHECK(protocol_adapter_count() == 4, "registry contains four adapters");

}

static void test_tbp(void)
{
    const protocol_adapter_t *a = get_adapter("TBP");
    uint8_t frame[18] = {0};
    uint16_t id = 7;
    double temperature = 21.5;
    uint64_t bits;
    int64_t timestamp = 1730000000;
    sensor_data_t out = {0};
    size_t consumed = 0;
    parse_result_t r;

    CHECK(a != NULL, "TBP adapter exists");
    if (a == NULL) return;

    put_u16_le(frame, id);
    memcpy(&bits, &temperature, sizeof(bits));
    {
        size_t i;
        for (i = 0; i < 8; ++i) frame[2 + i] = (uint8_t)(bits >> (8U * i));
    }
    {
        uint64_t tsbits;
        size_t i;
        memcpy(&tsbits, &timestamp, sizeof(tsbits));
        for (i = 0; i < 8; ++i) frame[10 + i] = (uint8_t)(tsbits >> (8U * i));
    }

    r = a->parse(frame, sizeof(frame), &out, &consumed);
    CHECK(r == PARSE_OK, "TBP valid frame parses");
    CHECK(consumed == 18, "TBP consumes 18 bytes");
    CHECK(out.id == id && out.type == SENSOR_TEMPERATURE, "TBP ID and type decoded");
    CHECK(fabs(out.value - temperature) < 0.000001, "TBP temperature decoded");
    CHECK((int64_t)out.ts == timestamp, "TBP timestamp decoded");

    consumed = 99;
    r = a->parse(frame, 10, &out, &consumed);
    CHECK(r == PARSE_NEED_MORE && consumed == 0, "TBP partial frame waits for more bytes");
}

static void test_hlp(void)
{
    const protocol_adapter_t *a = get_adapter("HLP");
    const uint8_t good[] = "HUM 12 45.5 1730000000\n";
    const uint8_t partial[] = "HUM 12 45.5";
    const uint8_t bad[] = "HUM  12 45.5 1730000000\n";
    sensor_data_t out = {0};
    size_t consumed = 0;
    parse_result_t r;

    CHECK(a != NULL, "HLP adapter exists");
    if (a == NULL) return;

    r = a->parse(good, sizeof(good) - 1, &out, &consumed);
    CHECK(r == PARSE_OK, "HLP valid line parses");
    CHECK(consumed == sizeof(good) - 1, "HLP consumes exactly one line");
    CHECK(out.id == 12 && out.type == SENSOR_HUMIDITY, "HLP ID and type decoded");
    CHECK(fabs(out.value - 45.5) < 0.000001, "HLP humidity decoded");
    CHECK((int64_t)out.ts == 1730000000, "HLP timestamp decoded");

    consumed = 99;
    r = a->parse(partial, sizeof(partial) - 1, &out, &consumed);
    CHECK(r == PARSE_NEED_MORE && consumed == 0, "HLP incomplete line waits for LF");

    consumed = 0;
    r = a->parse(bad, sizeof(bad) - 1, &out, &consumed);
    CHECK(r == PARSE_ERROR && consumed > 0, "HLP rejects repeated spaces");
}

static void test_evp_motion(void)
{
    const protocol_adapter_t *a = get_adapter("EVP");
    uint8_t frame[8] = {0x01, 0x06};
    sensor_data_t out = {0};
    size_t consumed = 0;
    parse_result_t r;

    CHECK(a != NULL, "EVP adapter exists");
    if (a == NULL) return;

    put_u16_be(&frame[2], 42);
    put_u32_be(&frame[4], 1730000000U);

    r = a->parse(frame, sizeof(frame), &out, &consumed);
    CHECK(r == PARSE_OK, "EVP motion frame parses");
    CHECK(consumed == 8, "EVP motion consumes 8 bytes");
    CHECK(out.id == 42 && out.type == SENSOR_MOTION, "EVP motion ID and type decoded");
    CHECK(fabs(out.value - 1.0) < 0.000001, "EVP motion value is 1");
    CHECK((int64_t)out.ts == 1730000000LL, "EVP motion timestamp decoded");

    consumed = 99;
    r = a->parse(frame, 5, &out, &consumed);
    CHECK(r == PARSE_NEED_MORE && consumed == 0, "EVP partial motion waits for more bytes");
}

static void test_evp_door(void)
{
    const protocol_adapter_t *a = get_adapter("EVP");
    uint8_t frame[9] = {0x02, 0x07};
    sensor_data_t out = {0};
    size_t consumed = 0;
    parse_result_t r;

    if (a == NULL) {
        CHECK(0, "EVP adapter exists for door test");
        return;
    }

    put_u16_be(&frame[2], 9);
    frame[4] = 1;
    put_u32_be(&frame[5], 1730000001U);

    r = a->parse(frame, sizeof(frame), &out, &consumed);
    CHECK(r == PARSE_OK, "EVP door frame parses");
    CHECK(consumed == 9, "EVP door consumes 9 bytes");
    CHECK(out.id == 9 && out.type == SENSOR_DOOR, "EVP door ID and type decoded");
    CHECK(fabs(out.value - 1.0) < 0.000001, "EVP door open state decoded");
}

static void test_sgp(void)
{
    const protocol_adapter_t *a = get_adapter("SGP");
    uint8_t frame[11] = {0x7e};
    uint16_t crc;
    sensor_data_t out = {0};
    size_t consumed = 0;
    uint8_t reply[1] = {0};
    parse_result_t r;

    CHECK(a != NULL, "SGP adapter exists");
    if (a == NULL) return;

    put_u16_be(&frame[1], 23);
    put_u16_be(&frame[3], 350);
    put_u32_be(&frame[5], 1730000000U);
    crc = crc16_ccitt_false(&frame[1], 8);
    put_u16_be(&frame[9], crc);

    r = a->parse(frame, sizeof(frame), &out, &consumed);
    CHECK(r == PARSE_OK, "SGP valid frame parses");
    CHECK(consumed == 11, "SGP consumes 11 bytes");
    CHECK(out.id == 23 && out.type == SENSOR_SMOKE_GAS, "SGP ID and type decoded");
    CHECK(fabs(out.value - 350.0) < 0.000001, "SGP ppm decoded");

    CHECK(a->build_reply(PARSE_OK, reply, sizeof(reply)) == 1 &&
          reply[0] == 0x06, "SGP valid frame produces ACK");

    frame[10] ^= 0x01U;
    consumed = 0;
    r = a->parse(frame, sizeof(frame), &out, &consumed);
    CHECK(r == PARSE_ERROR && consumed > 0, "SGP rejects bad CRC");

    CHECK(a->build_reply(PARSE_ERROR, reply, sizeof(reply)) == 1 &&
          reply[0] == 0x15, "SGP invalid frame produces NAK");

    consumed = 99;
    r = a->parse(frame, 7, &out, &consumed);
    CHECK(r == PARSE_NEED_MORE && consumed == 0, "SGP partial frame waits for more bytes");
}

int main(void)
{
    test_registry();
    test_tbp();
    test_hlp();
    test_evp_motion();
    test_evp_door();
    test_sgp();

    printf("\nTests: %d, failures: %d\n", tests, failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}