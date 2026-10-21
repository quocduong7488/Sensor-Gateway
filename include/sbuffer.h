#ifndef SBUFFER_H
#define SBUFFER_H

#include <pthread.h>
#include <stdbool.h>
#include "sensor_data.h"

typedef enum {
    SBUFFER_DATA_MANAGER,
    SBUFFER_STORAGE_MANAGER
} sbuffer_consumer_t;

typedef struct sbuffer_node {
    sensor_data_t data;

    bool read_by_data;
    bool read_by_storage;

    struct sbuffer_node *next;
} sbuffer_node_t;

typedef struct {
    sbuffer_node_t *head;
    sbuffer_node_t *tail;

    pthread_mutex_t mutex;
    pthread_cond_t cond;

    bool shutdown;
} sbuffer_t;

int sbuffer_init(sbuffer_t *buffer);

int sbuffer_insert(
    sbuffer_t *buffer,
    const sensor_data_t *data
);

int sbuffer_read(
    sbuffer_t *buffer,
    sbuffer_consumer_t consumer,
    sensor_data_t *data
);

void sbuffer_shutdown(sbuffer_t *buffer);

void sbuffer_destroy(sbuffer_t *buffer);

#endif