#define _POSIX_C_SOURCE 200809L

#include "room_config.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static int parse_unsigned(
    const char *text,
    unsigned long maximum,
    unsigned long *result
) {
    char *end = NULL;
    unsigned long value;

    if (text == NULL || *text == '\0' || *text == '-') {
        return -1;
    }

    errno = 0;
    value = strtoul(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' || value == 0) {
        return -1;
    }

    if (value > maximum) {
        return -1;
    }

    *result = value;
    return 0;
}

static int parse_sensor_type(
    const char *text,
    sensor_type_t *type
) {
    if (strcmp(text, "temperature") == 0) {
        *type = SENSOR_TEMPERATURE;
    } else if (strcmp(text, "humidity") == 0) {
        *type = SENSOR_HUMIDITY;
    } else if (strcmp(text, "motion") == 0) {
        *type = SENSOR_MOTION;
    } else if (strcmp(text, "door") == 0) {
        *type = SENSOR_DOOR;
    } else if (strcmp(text, "smoke_gas") == 0) {
        *type = SENSOR_SMOKE_GAS;
    } else {
        return -1;
    }

    return 0;
}

static int sensor_id_exists(
    const room_config_t *config,
    sensor_id_t sensor_id
) {
    for (size_t i = 0; i < config->count; i++) {
        if (config->entries[i].sensor_id == sensor_id) {
            return 1;
        }
    }

    return 0;
}

int room_config_load(
    room_config_t *config,
    const char *filename
) {
    FILE *file;
    char *line = NULL;
    size_t line_capacity = 0;
    ssize_t line_length;
    size_t line_number = 0;
    int result = -1;

    if (config == NULL || filename == NULL) {
        return -1;
    }

    config->entries = NULL;
    config->count = 0;

    file = fopen(filename, "r");
    if (file == NULL) {
        return -1;
    }

    while ((line_length = getline(&line, &line_capacity, file)) != -1) {
        char *saveptr = NULL;
        char *room_text;
        char *sensor_text;
        char *type_text;
        char *extra_text;
        unsigned long room_id;
        unsigned long sensor_id;
        sensor_type_t type;
        room_sensor_entry_t *new_entries;

        (void)line_length;
        line_number++;

        room_text = strtok_r(line, " \t\r\n", &saveptr);

        if (room_text == NULL || room_text[0] == '#') {
            continue;
        }

        sensor_text = strtok_r(NULL, " \t\r\n", &saveptr);
        type_text = strtok_r(NULL, " \t\r\n", &saveptr);
        extra_text = strtok_r(NULL, " \t\r\n", &saveptr);

        if (sensor_text == NULL || type_text == NULL) {
            fprintf(
                stderr,
                "room_sensor.conf:%zu: expected 3 fields\n",
                line_number
            );
            goto cleanup;
        }

        /*
         * Allow an inline comment after the three required fields.
         */
        if (extra_text != NULL && extra_text[0] != '#') {
            fprintf(
                stderr,
                "room_sensor.conf:%zu: unexpected extra field\n",
                line_number
            );
            goto cleanup;
        }

        if (parse_unsigned(room_text, UINT_MAX, &room_id) != 0) {
            fprintf(
                stderr,
                "room_sensor.conf:%zu: invalid room ID\n",
                line_number
            );
            goto cleanup;
        }

        if (parse_unsigned(
                sensor_text,
                UINT16_MAX,
                &sensor_id
            ) != 0) {
            fprintf(
                stderr,
                "room_sensor.conf:%zu: invalid sensor ID\n",
                line_number
            );
            goto cleanup;
        }

        if (parse_sensor_type(type_text, &type) != 0) {
            fprintf(
                stderr,
                "room_sensor.conf:%zu: unknown sensor type '%s'\n",
                line_number,
                type_text
            );
            goto cleanup;
        }

        if (sensor_id_exists(config, (sensor_id_t)sensor_id)) {
            fprintf(
                stderr,
                "room_sensor.conf:%zu: duplicate sensor ID %lu\n",
                line_number,
                sensor_id
            );
            goto cleanup;
        }

        new_entries = realloc(
            config->entries,
            (config->count + 1) * sizeof(*config->entries)
        );

        if (new_entries == NULL) {
            fprintf(stderr, "Cannot allocate room configuration memory\n");
            goto cleanup;
        }

        config->entries = new_entries;

        config->entries[config->count].room_id =
            (unsigned int)room_id;

        config->entries[config->count].sensor_id =
            (sensor_id_t)sensor_id;

        config->entries[config->count].type = type;

        config->count++;
    }

    if (ferror(file)) {
        fprintf(stderr, "Error reading configuration file '%s'\n", filename);
        goto cleanup;
    }

    if (config->count == 0) {
        fprintf(stderr, "Configuration file contains no sensor entries\n");
        goto cleanup;
    }

    result = 0;

cleanup:
    free(line);
    fclose(file);

    if (result != 0) {
        room_config_destroy(config);
    }

    return result;
}

const room_sensor_entry_t *room_config_find(
    const room_config_t *config,
    sensor_id_t sensor_id
) {
    if (config == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < config->count; i++) {
        if (config->entries[i].sensor_id == sensor_id) {
            return &config->entries[i];
        }
    }

    return NULL;
}

void room_config_destroy(room_config_t *config) {
    if (config == NULL) {
        return;
    }

    free(config->entries);
    config->entries = NULL;
    config->count = 0;
}
