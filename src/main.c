#include <stdio.h>
#include "gateway.h"

int main(void){
    gateway_start_message();

#ifdef DEBUG
    printf("[DEBUG] Main process is running\n");
#endif

    gateway_shutdown_message();

    return 0;
}