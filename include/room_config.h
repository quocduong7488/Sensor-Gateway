#ifndef ROOM_CONFIG_H
#define ROOM_CONFIG_H

#include <stddef.h>

#include "sensor_types.h"

typedef struct {
    unsigned int room_id;
    sensor_id_t sensor_id;
    sensor_type_t type;
} room_sensor_entry_t;

typedef struct {
    room_sensor_entry_t *entries;
    size_t count;
} room_config_t;

/*
 * Load room-sensor mapping from configuration file.
 * Return 0 on success, -1 on failure.
 */
int room_config_load(room_config_t *config, const char *filename);

/*
 * Find a sensor by ID.
 * Return pointer to entry if found, otherwise NULL.
 */
const room_sensor_entry_t *room_config_find(
    const room_config_t *config,
    sensor_id_t sensor_id
);

/*
 * Release configuration memory.
 */
void room_config_destroy(room_config_t *config);

#endif
