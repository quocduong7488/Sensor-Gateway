#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H

#include <stdint.h>
#include <time.h>

typedef struct {
    uint16_t id;
    double value;
    time_t ts;
} sensor_data_t;

#endif