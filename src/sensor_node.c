#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <netdb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define TBP_FRAME_SIZE 18U
#define SGP_FRAME_SIZE 11U
#define MAX_FRAME_SIZE 128U
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_BASE_PORT 9000U
#define DEFAULT_PERIOD_MS 1000U

typedef enum {
    NODE_TBP,
    NODE_HLP,
    NODE_EVP_MOTION,
    NODE_EVP_DOOR,
    NODE_SGP
} node_protocol_t;

typedef enum {
    MODE_NORMAL,
    MODE_FRAGMENT,
    MODE_GLUE,
    MODE_CORRUPT
} send_mode_t;

typedef struct {
    node_protocol_t protocol;
    uint16_t id;
    const char *host;
    uint16_t base_port;
    unsigned int period_ms;
    send_mode_t mode;
} node_config_t;

static uint16_t protocol_port_offset(node_protocol_t protocol)
{
    switch (protocol) {
    case NODE_TBP:
    case NODE_HLP:
    case NODE_EVP_MOTION:
    case NODE_EVP_DOOR:
    case NODE_SGP:
        break;
    }
    if (protocol == NODE_TBP) {
        return 0U;
    }
    if (protocol == NODE_HLP) {
        return 1U;
    }
    if (protocol == NODE_EVP_MOTION || protocol == NODE_EVP_DOOR) {
        return 2U;
    }
    return 3U;
}

static const char *protocol_name(node_protocol_t protocol)
{
    switch (protocol) {
    case NODE_TBP: return "TBP";
    case NODE_HLP: return "HLP";
    case NODE_EVP_MOTION: return "EVP-MOTION";
    case NODE_EVP_DOOR: return "EVP-DOOR";
    case NODE_SGP: return "SGP";
    }
    return "UNKNOWN";
}

static int parse_protocol(const char *text, node_protocol_t *protocol)
{
    if (strcmp(text, "tbp") == 0 || strcmp(text, "TBP") == 0) {
        *protocol = NODE_TBP;
    } else if (strcmp(text, "hlp") == 0 || strcmp(text, "HLP") == 0) {
        *protocol = NODE_HLP;
    } else if (strcmp(text, "evp-motion") == 0 ||
               strcmp(text, "EVP-MOTION") == 0 ||
               strcmp(text, "evp") == 0 || strcmp(text, "EVP") == 0) {
        *protocol = NODE_EVP_MOTION;
    } else if (strcmp(text, "evp-door") == 0 ||
               strcmp(text, "EVP-DOOR") == 0) {
        *protocol = NODE_EVP_DOOR;
    } else if (strcmp(text, "sgp") == 0 || strcmp(text, "SGP") == 0) {
        *protocol = NODE_SGP;
    } else {
        return -1;
    }
    return 0;
}

static int parse_unsigned(const char *text, unsigned long min,
                          unsigned long max, unsigned long *out)
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL || *text == '\0' || *text == '-') {
        return -1;
    }

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value < min || value > max) {
        return -1;
    }

    *out = value;
    return 0;
}

static void put_u16_le(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xffU);
    dst[1] = (uint8_t)((value >> 8) & 0xffU);
}

static void put_u16_be(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)((value >> 8) & 0xffU);
    dst[1] = (uint8_t)(value & 0xffU);
}

static void put_u32_be(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)((value >> 24) & 0xffU);
    dst[1] = (uint8_t)((value >> 16) & 0xffU);
    dst[2] = (uint8_t)((value >> 8) & 0xffU);
    dst[3] = (uint8_t)(value & 0xffU);
}

static void put_u64_le(uint8_t *dst, uint64_t value)
{
    for (unsigned int i = 0; i < 8U; ++i) {
        dst[i] = (uint8_t)((value >> (i * 8U)) & 0xffU);
    }
}

static uint16_t crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffffU;

    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned int bit = 0; bit < 8U; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

static int connect_to_server(const char *host, uint16_t port)
{
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    char port_text[6];
    int fd = -1;
    int gai_result;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    (void)snprintf(port_text, sizeof(port_text), "%u", (unsigned int)port);
    gai_result = getaddrinfo(host, port_text, &hints, &results);
    if (gai_result != 0) {
        fprintf(stderr, "getaddrinfo(%s): %s\n", host,
                gai_strerror(gai_result));
        return -1;
    }

    for (struct addrinfo *item = results; item != NULL; item = item->ai_next) {
        fd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, item->ai_addr, item->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(results);
    if (fd < 0) {
        fprintf(stderr, "Cannot connect to %s:%u: %s\n",
                host, (unsigned int)port, strerror(errno));
    }
    return fd;
}

static int send_all(int fd, const uint8_t *data, size_t length)
{
    size_t sent = 0U;

    while (sent < length) {
        ssize_t result = send(fd, data + sent, length - sent, MSG_NOSIGNAL);
        if (result > 0) {
            sent += (size_t)result;
        } else if (result < 0 && errno == EINTR) {
            continue;
        } else {
            fprintf(stderr, "send() failed: %s\n",
                    result == 0 ? "connection closed" : strerror(errno));
            return -1;
        }
    }
    return 0;
}

static int receive_sgp_reply(int fd)
{
    uint8_t reply;
    ssize_t result;

    do {
        result = recv(fd, &reply, sizeof(reply), 0);
    } while (result < 0 && errno == EINTR);

    if (result == 1) {
        if (reply == 0x06U) {
            printf("SGP reply: ACK (0x06)\n");
            return 0;
        }
        if (reply == 0x15U) {
            printf("SGP reply: NAK (0x15)\n");
            return 1;
        }
        printf("SGP reply: unexpected byte 0x%02X\n", (unsigned int)reply);
        return -1;
    }

    if (result == 0) {
        fprintf(stderr, "Server closed connection before SGP reply\n");
    } else {
        fprintf(stderr, "recv() for SGP reply failed: %s\n", strerror(errno));
    }
    return -1;
}

static size_t build_frame(const node_config_t *config, uint8_t *frame,
                          size_t capacity, unsigned int sequence)
{
    time_t now = time(NULL);
    uint64_t timestamp;
    double value;

    if (now == (time_t)-1) {
        now = 0;
    }
    timestamp = (uint64_t)now;

    switch (config->protocol) {
    case NODE_TBP: {
        if (capacity < TBP_FRAME_SIZE) {
            return 0U;
        }
        /* TBP: id LE16, IEEE-754 double bytes, timestamp LE64. */
        value = 20.0 + (double)(sequence % 100U) / 10.0;
        put_u16_le(frame, config->id);
        memcpy(frame + 2U, &value, sizeof(value));
        put_u64_le(frame + 10U, timestamp);
        return TBP_FRAME_SIZE;
    }
    case NODE_HLP: {
        int length;
        value = 40.0 + (double)(sequence % 500U) / 10.0;
        length = snprintf((char *)frame, capacity, "HUM %u %.1f %llu\n",
                          (unsigned int)config->id, value,
                          (unsigned long long)timestamp);
        if (length < 0 || (size_t)length >= capacity) {
            return 0U;
        }
        return (size_t)length;
    }
    case NODE_EVP_MOTION:
        if (capacity < 8U) {
            return 0U;
        }
        /* EVP TLV: type 0x01, length 0x06, id BE16, timestamp BE32. */
        frame[0] = 0x01U;
        frame[1] = 0x06U;
        put_u16_be(frame + 2U, config->id);
        put_u32_be(frame + 4U, (uint32_t)timestamp);
        return 8U;
    case NODE_EVP_DOOR:
        if (capacity < 9U) {
            return 0U;
        }
        /* EVP TLV: type 0x02, length 0x07, id BE16, state, timestamp BE32. */
        frame[0] = 0x02U;
        frame[1] = 0x07U;
        put_u16_be(frame + 2U, config->id);
        frame[4] = (uint8_t)(sequence % 2U);
        put_u32_be(frame + 5U, (uint32_t)timestamp);
        return 9U;
    case NODE_SGP: {
        uint16_t ppm;
        uint16_t crc;
        if (capacity < SGP_FRAME_SIZE) {
            return 0U;
        }
        /* SGP: 0x7E, id BE16, ppm BE16, timestamp BE32, CRC BE16. */
        ppm = (uint16_t)(250U + (sequence % 100U));
        frame[0] = 0x7eU;
        put_u16_be(frame + 1U, config->id);
        put_u16_be(frame + 3U, ppm);
        put_u32_be(frame + 5U, (uint32_t)timestamp);
        crc = crc16_ccitt_false(frame + 1U, 8U);
        put_u16_be(frame + 9U, crc);
        return SGP_FRAME_SIZE;
    }
    }
    return 0U;
}

static int send_one_frame(int fd, const uint8_t *frame, size_t length,
                          send_mode_t mode, node_protocol_t protocol)
{
    uint8_t corrupted[MAX_FRAME_SIZE];

    if (length > sizeof(corrupted)) {
        return -1;
    }

    if (mode == MODE_CORRUPT) {
        memcpy(corrupted, frame, length);
        if (protocol == NODE_SGP && length == SGP_FRAME_SIZE) {
            corrupted[length - 1U] ^= 0x01U; /* intentionally break CRC */
        } else if (protocol == NODE_TBP && length > 2U) {
            corrupted[2] ^= 0x7fU;
        } else if (protocol == NODE_HLP && length > 1U) {
            corrupted[length - 1U] = 'X'; /* remove the newline terminator */
        } else {
            corrupted[0] ^= 0xffU;
        }
        return send_all(fd, corrupted, length);
    }

    if (mode == MODE_FRAGMENT && length > 1U) {
        size_t first = length / 2U;

        if (send_all(fd, frame, first) != 0) {
            return -1;
        }

        {
            struct timespec delay = {0, 20L * 1000L * 1000L};

            while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
            }
        }
        return send_all(fd, frame + first, length - first);
    }   

    return send_all(fd, frame, length);
}

static void print_usage(const char *program)
{
    fprintf(stderr,
        "Usage: %s <tbp|hlp|evp-motion|evp-door|sgp> <id> "
        "[host] [base_port] [period_ms] [--fragment|--glue|--corrupt]\n"
        "Defaults: host=%s base_port=%u period_ms=%u\n",
        program, DEFAULT_HOST, DEFAULT_BASE_PORT, DEFAULT_PERIOD_MS);
}

int main(int argc, char **argv)
{
    node_config_t config;
    unsigned long number;
    uint16_t port;
    int fd;
    unsigned int sequence = 0U;

    if (argc < 3 || argc > 7) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    memset(&config, 0, sizeof(config));
    config.host = DEFAULT_HOST;
    config.base_port = (uint16_t)DEFAULT_BASE_PORT;
    config.period_ms = DEFAULT_PERIOD_MS;
    config.mode = MODE_NORMAL;

    if (parse_protocol(argv[1], &config.protocol) != 0) {
        fprintf(stderr, "Unknown protocol: %s\n", argv[1]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (parse_unsigned(argv[2], 1UL, 65535UL, &number) != 0) {
        fprintf(stderr, "Sensor ID must be between 1 and 65535\n");
        return EXIT_FAILURE;
    }
    config.id = (uint16_t)number;

    if (argc >= 4) {
        config.host = argv[3];
    }
    if (argc >= 5) {
        if (parse_unsigned(argv[4], 1UL, 65532UL, &number) != 0) {
            fprintf(stderr, "Base port must be between 1 and 65532\n");
            return EXIT_FAILURE;
        }
        config.base_port = (uint16_t)number;
    }
    if (argc >= 6) {
        if (parse_unsigned(argv[5], 1UL, 3600000UL, &number) != 0) {
            fprintf(stderr, "period_ms must be between 1 and 3600000\n");
            return EXIT_FAILURE;
        }
        config.period_ms = (unsigned int)number;
    }
    if (argc >= 7) {
        if (strcmp(argv[6], "--fragment") == 0) {
            config.mode = MODE_FRAGMENT;
        } else if (strcmp(argv[6], "--glue") == 0) {
            config.mode = MODE_GLUE;
        } else if (strcmp(argv[6], "--corrupt") == 0) {
            config.mode = MODE_CORRUPT;
        } else {
            fprintf(stderr, "Unknown mode: %s\n", argv[6]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if ((uint32_t)config.base_port + protocol_port_offset(config.protocol) > 65535U) {
        fprintf(stderr, "Protocol port is out of range\n");
        return EXIT_FAILURE;
    }

    port = (uint16_t)(config.base_port + protocol_port_offset(config.protocol));
    (void)signal(SIGPIPE, SIG_IGN);

    fd = connect_to_server(config.host, port);
    if (fd < 0) {
        return EXIT_FAILURE;
    }

    printf("Connected: protocol=%s sensor_id=%u server=%s:%u mode=%d\n",
           protocol_name(config.protocol), (unsigned int)config.id,
           config.host, (unsigned int)port, (int)config.mode);

    for (;;) {
        uint8_t frame[MAX_FRAME_SIZE];
        size_t length = build_frame(&config, frame, sizeof(frame), sequence);

        if (length == 0U) {
            fprintf(stderr, "Could not build frame\n");
            close(fd);
            return EXIT_FAILURE;
        }

        if (config.mode == MODE_GLUE) {
            uint8_t pair[MAX_FRAME_SIZE * 2U];
            size_t second_length = build_frame(&config, pair + length,
                                               sizeof(pair) - length,
                                               sequence + 1U);
            if (second_length == 0U || length + second_length > sizeof(pair)) {
                fprintf(stderr, "Could not build glued frames\n");
                close(fd);
                return EXIT_FAILURE;
            }
            memcpy(pair, frame, length);
            if (send_all(fd, pair, length + second_length) != 0) {
                break;
            }
            printf("Sent two glued %s frames (%zu + %zu bytes)\n",
                   protocol_name(config.protocol), length, second_length);
            sequence += 2U;
            if (config.protocol == NODE_SGP) {
                (void)receive_sgp_reply(fd);
                (void)receive_sgp_reply(fd);
            }
        } else {
            if (send_one_frame(fd, frame, length, config.mode,
                               config.protocol) != 0) {
                break;
            }
            printf("Sent %s frame: %zu bytes, sequence=%u\n",
                   protocol_name(config.protocol), length, sequence);
            ++sequence;
            if (config.protocol == NODE_SGP) {
                if (receive_sgp_reply(fd) < 0) {
                    break;
                }
            }
        }

        if (config.mode == MODE_CORRUPT) {
            break;
        }

        struct timespec delay;
        delay.tv_sec = (time_t)(config.period_ms / 1000U);
        delay.tv_nsec = (long)(config.period_ms % 1000U) * 1000000L;
        while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
        }
    }

    close(fd);
    return EXIT_SUCCESS;
}