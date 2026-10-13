#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <time.h>
#include <stdatomic.h>

#include "node.h"

static void wait_for_stop(gateway_t *gateway){
    const struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000}; // 100ms

    while(!atomic_load(&gateway->stop)){
        nanosleep(&delay, NULL);
    }
}

void *connmgr(void *arg){
    gateway_t *gateway = arg;

#ifdef DEBUG
    printf("[DEBUG] Connection manager started\n");
#endif

    wait_for_stop(gateway);

#ifdef DEBUG
    printf("[DEBUG] Connection manager stopped\n");
#endif

    return NULL;
}

void *storagemgr(void *arg){
    gateway_t *gateway = arg;

#ifdef DEBUG
    printf("[DEBUG] Storage manager started\n");
#endif

    wait_for_stop(gateway);

#ifdef DEBUG
    printf("[DEBUG] Storage manager stopped\n");
#endif

    return NULL;
}

void *datamgr(void *arg){
    gateway_t *gateway = arg;

#ifdef DEBUG
    printf("[DEBUG] Data manager started\n");
#endif

    wait_for_stop(gateway);

#ifdef DEBUG
    printf("[DEBUG] Data manager stopped\n");
#endif

    return NULL;
}

