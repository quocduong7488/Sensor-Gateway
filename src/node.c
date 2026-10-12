#include <stdio.h>
#include <time.h>
#include <stdatomic.h>

#include "node.h"

static void wait_for_stop(gateway_t *gateway){
    const struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000L}; // 100ms

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

void *storemgr(void *arg){
    gateway_t *gateway = arg;

#ifdef DEBUG
    printf("[DEBUG] Store manager started\n");
#endif

    wait_for_stop(gateway);

#ifdef DEBUG
    printf("[DEBUG] Store manager stopped\n");
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

