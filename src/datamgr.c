#define _POSIX_C_SOURCE 200809L

#include "node.h"

#include "logger.h"
#include "room_config.h"
#include "sensor_config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    sensor_id_t sensor_id;
    unsigned int room_id;

    /* Running average for temperature and humidity. */
    double samples[RUN_AVG_LENGTH];
    size_t sample_count;
    size_t next_sample;

    /* -1: below minimum, 0: normal, 1: above maximum. */
    int threshold_state;

    /* State tracking for door and smoke/gas sensors. */
    int door_known;
    int door_open;
    int gas_alarm;
} sensor_state_t;

typedef void (*sensor_handler_t)(
    const sensor_data_t *data,
    sensor_state_t *state
);

static void write_data_log(const char *message)
{
    (void)logger_write("DATA", message);
}

static void handle_temperature(
    const sensor_data_t *data,
    sensor_state_t *state
)
{
    double sum = 0.0;
    double average;
    int new_state;
    char message[256];

    if (!isfinite(data->value) ||
        data->value < -40.0 ||
        data->value > 85.0) {
        return;
    }

    state->samples[state->next_sample] = data->value;
    state->next_sample =
        (state->next_sample + 1U) % RUN_AVG_LENGTH;

    if (state->sample_count < RUN_AVG_LENGTH) {
        state->sample_count++;
    }

    /* Do not compare until five measurements are available. */
    if (state->sample_count < RUN_AVG_LENGTH) {
        return;
    }

    for (size_t i = 0; i < RUN_AVG_LENGTH; i++) {
        sum += state->samples[i];
    }

    average = sum / (double)RUN_AVG_LENGTH;

    if (average < TEMP_MIN_C) {
        new_state = -1;
    } else if (average > TEMP_MAX_C) {
        new_state = 1;
    } else {
        new_state = 0;
    }

    /* Log only when entering a new alarm state. */
    if (new_state != state->threshold_state) {
        if (new_state == -1) {
            (void)snprintf(
                message,
                sizeof(message),
                "The sensor node with %u reports it's too cold "
                "(running avg temperature = %.2f)",
                (unsigned)data->id,
                average
            );
            write_data_log(message);
        } else if (new_state == 1) {
            (void)snprintf(
                message,
                sizeof(message),
                "The sensor node with %u reports it's too hot "
                "(running avg temperature = %.2f)",
                (unsigned)data->id,
                average
            );
            write_data_log(message);
        }

        state->threshold_state = new_state;
    }
}

static void handle_humidity(
    const sensor_data_t *data,
    sensor_state_t *state
)
{
    double sum = 0.0;
    double average;
    int new_state;
    char message[256];

    if (!isfinite(data->value) ||
        data->value < 0.0 ||
        data->value > 100.0) {
        return;
    }

    state->samples[state->next_sample] = data->value;
    state->next_sample =
        (state->next_sample + 1U) % RUN_AVG_LENGTH;

    if (state->sample_count < RUN_AVG_LENGTH) {
        state->sample_count++;
    }

    if (state->sample_count < RUN_AVG_LENGTH) {
        return;
    }

    for (size_t i = 0; i < RUN_AVG_LENGTH; i++) {
        sum += state->samples[i];
    }

    average = sum / (double)RUN_AVG_LENGTH;

    if (average < HUMIDITY_MIN) {
        new_state = -1;
    } else if (average > HUMIDITY_MAX) {
        new_state = 1;
    } else {
        new_state = 0;
    }

    if (new_state != state->threshold_state) {
        if (new_state == -1) {
            (void)snprintf(
                message,
                sizeof(message),
                "The sensor node with %u reports it's too dry "
                "(running avg humidity = %.2f)",
                (unsigned)data->id,
                average
            );
            write_data_log(message);
        } else if (new_state == 1) {
            (void)snprintf(
                message,
                sizeof(message),
                "The sensor node with %u reports it's too humid "
                "(running avg humidity = %.2f)",
                (unsigned)data->id,
                average
            );
            write_data_log(message);
        }

        state->threshold_state = new_state;
    }
}

static void handle_motion(
    const sensor_data_t *data,
    sensor_state_t *state
)
{
    char message[256];

    (void)state;

    if (!isfinite(data->value) ||
        (data->value != 0.0 && data->value != 1.0)) {
        return;
    }

    /* The specification says to record each motion event. */
    if (data->value == 1.0) {
        (void)snprintf(
            message,
            sizeof(message),
            "The sensor node with %u detected motion",
            (unsigned)data->id
        );
        write_data_log(message);
    }
}

static void handle_door(
    const sensor_data_t *data,
    sensor_state_t *state
)
{
    int is_open;
    char message[256];

    if (!isfinite(data->value) ||
        (data->value != 0.0 && data->value != 1.0)) {
        return;
    }

    is_open = (data->value == 1.0);

    /* Record the first known state and every later state change. */
    if (!state->door_known || state->door_open != is_open) {
        (void)snprintf(
            message,
            sizeof(message),
            "The sensor node with %u reports the door is %s",
            (unsigned)data->id,
            is_open ? "open" : "closed"
        );
        write_data_log(message);

        state->door_known = 1;
        state->door_open = is_open;
    }
}

static void handle_smoke_gas(
    const sensor_data_t *data,
    sensor_state_t *state
)
{
    int alarm;
    char message[256];

    if (!isfinite(data->value) ||
        data->value < 0.0 ||
        data->value > 10000.0) {
        return;
    }

    /* Smoke/gas uses the current measurement, not a running average. */
    alarm = (data->value >= GAS_ALARM_PPM);

    if (alarm && !state->gas_alarm) {
        (void)snprintf(
            message,
            sizeof(message),
            "The sensor node with %u reports a smoke/gas alarm "
            "(concentration = %.2f ppm)",
            (unsigned)data->id,
            data->value
        );
        write_data_log(message);
    }

    state->gas_alarm = alarm;
}

/*
 * Handler table indexed by sensor_type_t.
 * Index zero is intentionally unused.
 */
static const sensor_handler_t handlers[] = {
    NULL,
    handle_temperature,
    handle_humidity,
    handle_motion,
    handle_door,
    handle_smoke_gas
};

static sensor_state_t *find_sensor_state(
    sensor_state_t *states,
    size_t count,
    sensor_id_t sensor_id
)
{
    for (size_t i = 0; i < count; i++) {
        if (states[i].sensor_id == sensor_id) {
            return &states[i];
        }
    }

    return NULL;
}

void *datamgr(void *arg)
{
    gateway_t *gateway = arg;
    room_config_t config = {0};
    sensor_state_t *states = NULL;
    sensor_data_t data;
    int read_result;

    if (gateway == NULL || gateway->sbuffer == NULL) {
        return NULL;
    }

    if (room_config_load(&config, "room_sensor.conf") != 0) {
        write_data_log(
            "Unable to load room-sensor configuration file"
        );
        return NULL;
    }

    states = calloc(config.count, sizeof(*states));
    if (states == NULL) {
        write_data_log(
            "Unable to allocate Data Manager sensor states"
        );
        room_config_destroy(&config);
        return NULL;
    }

    /*
     * Create one independent state entry for each configured sensor.
     */
    for (size_t i = 0; i < config.count; i++) {
        states[i].sensor_id = config.entries[i].sensor_id;
        states[i].room_id = config.entries[i].room_id;
    }

    write_data_log("Data Manager started");

    while ((read_result = sbuffer_read(
                gateway->sbuffer,
                SBUFFER_DATA_MANAGER,
                &data
            )) > 0) {
        const room_sensor_entry_t *entry;
        sensor_state_t *state;
        sensor_handler_t handler;

        /*
         * Validate both sensor ID and sensor type against the
         * room-sensor configuration.
         */
        entry = room_config_find(&config, data.id);

        if (entry == NULL || entry->type != data.type) {
            char message[256];

            (void)snprintf(
                message,
                sizeof(message),
                "Received sensor data with invalid sensor node ID %u",
                (unsigned)data.id
            );
            write_data_log(message);
            continue;
        }

        if (data.type < SENSOR_TEMPERATURE ||
            data.type > SENSOR_SMOKE_GAS) {
            continue;
        }

        state = find_sensor_state(
            states,
            config.count,
            data.id
        );

        if (state == NULL) {
            continue;
        }

        handler = handlers[data.type];
        if (handler != NULL) {
            handler(&data, state);
        }
    }

    write_data_log("Data Manager stopped");

    free(states);
    room_config_destroy(&config);

    return NULL;
}
