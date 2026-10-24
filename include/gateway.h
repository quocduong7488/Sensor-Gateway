#ifndef GATEWAY_H
#define GATEWAY_H

#include <stdint.h>
#include <stdatomic.h>

#include "sbuffer.h"

typedef struct{
    uint16_t base_port;
    uint16_t http_port;
    atomic_bool stop;
    sbuffer_t *sbuffer;
} gateway_t;

void gateway_start_message(void);
void gateway_shutdown_message(void);

#endif