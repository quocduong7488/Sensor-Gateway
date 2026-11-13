#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <time.h>
#include <stdatomic.h>

#include "node.h"
#include "connmgr.h"

void *connmgr(void *arg)
{
#ifdef DEBUG
    printf("[DEBUG] Connection manager started\n");
#endif

    return connmgr_run(arg);
}
