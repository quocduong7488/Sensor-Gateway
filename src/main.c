// #define _POSIX_C_SOURCE 200809L

// #include <stdio.h>
// #include <stdlib.h>
// #include <time.h>
// #include <signal.h>
// #include <pthread.h>
// #include <stdatomic.h>
// #include <errno.h>

// #include "gateway.h"
// #include "node.h"

// static volatile sig_atomic_t signal_received = 0;

// static void handle_signal(int signal_number){
//     (void)signal_number;
//     signal_received = 1;
// }

// static int parse_port(const char *string, uint16_t *port){
//     char *endptr = NULL;
//     long value;

//     if(string == NULL || string[0] == '\0'){
//         return -1;
//     }

//     errno = 0;
//     value = strtol(string, &endptr, 10);

//     if(errno == 0 || endptr == string || *endptr != '\0'){
//         return -1;
//     }

//     if(value < 0 || value > 65535){
//         return -1;
//     }

//     *port = (uint16_t)value;
//     return 0;
// }

// int main(int argc, char *argv[]){
//     gateway_t gateway;
//     pthread_t connmgr_thread, datamgr_thread, storagemgr_thread;

//     int conn_created = 0, data_created = 0, store_created = 0;
//     int result = EXIT_SUCCESS;
//     int error_code;

//     struct sigaction action = {0};

//     if(argc != 3){
//         fprintf(stderr, "Usage: %s <base_sensor_port> <http_port>\n", argv[0]);
//         return EXIT_FAILURE;
//     }

//     if(parse_port(argv[1], &gateway.base_port) != 0 || parse_port(argv[2], &gateway.http_port) != 0){
//         fprintf(stderr, "Error: ports must be integers in range 1 - 65535\n");
//         return EXIT_FAILURE;
//     }

//     if(gateway.base_port > 65532){
//         fprintf(stderr, "Error: base sensor port must be <= 65532.\n");
//         return EXIT_FAILURE;
//     }

//     if(gateway.http_port >= gateway.base_port && gateway.http_port <= gateway.base_port + 3){
//         fprintf(stderr, "Error: http port must not be in range [base_sensor_port, base_sensor_port + 3]\n");
//         return EXIT_FAILURE;
//     }


// #ifdef DEBUG
//     printf("[DEBUG] Main process is running\n");
// #endif


//     return 0;
// }


#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "gateway.h"
#include "node.h"

static volatile sig_atomic_t signal_received = 0;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    signal_received = 1;
}

static int parse_port(const char *text, uint16_t *port)
{
    char *end = NULL;
    long value;

    if (text == NULL || text[0] == '\0')
    {
        return -1;
    }

    errno = 0;
    value = strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0')
    {
        return -1;
    }

    if (value < 1 || value > 65535)
    {
        return -1;
    }

    *port = (uint16_t)value;
    return 0;
}

int main(int argc, char *argv[])
{
    gateway_t gateway;
    pthread_t conn_thread;
    pthread_t data_thread;
    pthread_t storage_thread;

    int conn_created = 0;
    int data_created = 0;
    int storage_created = 0;
    int result = EXIT_SUCCESS;
    int error_code;

    struct sigaction action = {0};

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <base_sensor_port> <http_port>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    if (parse_port(argv[1], &gateway.base_port) != 0 ||
        parse_port(argv[2], &gateway.http_port) != 0)
    {
        fprintf(stderr, "Error: ports must be integers in range 1-65535.\n");
        return EXIT_FAILURE;
    }

    /*
     * Four sensor ports will be base_port through base_port + 3.
     * Check the range now, even though sockets are not implemented yet.
     */
    if (gateway.base_port > 65532)
    {
        fprintf(stderr, "Error: base sensor port must be <= 65532.\n");
        return EXIT_FAILURE;
    }

    if (gateway.http_port >= gateway.base_port &&
        gateway.http_port <= gateway.base_port + 3)
    {
        fprintf(stderr, "Error: HTTP port conflicts with sensor ports.\n");
        return EXIT_FAILURE;
    }

    atomic_init(&gateway.stop, false);

    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, NULL) == -1 ||
        sigaction(SIGTERM, &action, NULL) == -1)
    {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    gateway_start_message();

#ifdef DEBUG
    printf("[DEBUG] Base sensor port: %u\n",
           (unsigned int)gateway.base_port);
    printf("[DEBUG] HTTP port: %u\n",
           (unsigned int)gateway.http_port);
#endif

    error_code = pthread_create(&conn_thread, NULL,
                                connmgr, &gateway);
    if (error_code != 0)
    {
        fprintf(stderr, "pthread_create connmgr: %s\n",
                strerror(error_code));
        result = EXIT_FAILURE;
        goto shutdown;
    }
    conn_created = 1;

    error_code = pthread_create(&data_thread, NULL,
                                datamgr, &gateway);
    if (error_code != 0)
    {
        fprintf(stderr, "pthread_create datamgr: %s\n",
                strerror(error_code));
        result = EXIT_FAILURE;
        goto shutdown;
    }
    data_created = 1;

    error_code = pthread_create(&storage_thread, NULL,
                                storagemgr, &gateway);
    if (error_code != 0)
    {
        fprintf(stderr, "pthread_create storagemgr: %s\n",
                strerror(error_code));
        result = EXIT_FAILURE;
        goto shutdown;
    }
    storage_created = 1;

    /*
     * Main thread waits for SIGINT or SIGTERM.
     * Signal handler only sets signal_received.
     */
    while (!signal_received)
    {
        const struct timespec delay = {
            .tv_sec = 0,
            .tv_nsec = 100000000L
        };

        nanosleep(&delay, NULL);
    }

shutdown:
    gateway_shutdown_message();
    atomic_store(&gateway.stop, true);

    if (conn_created)
    {
        error_code = pthread_join(conn_thread, NULL);
        if (error_code != 0)
        {
            fprintf(stderr, "pthread_join connmgr: %s\n",
                    strerror(error_code));
            result = EXIT_FAILURE;
        }
    }

    if (data_created)
    {
        error_code = pthread_join(data_thread, NULL);
        if (error_code != 0)
        {
            fprintf(stderr, "pthread_join datamgr: %s\n",
                    strerror(error_code));
            result = EXIT_FAILURE;
        }
    }

    if (storage_created)
    {
        error_code = pthread_join(storage_thread, NULL);
        if (error_code != 0)
        {
            fprintf(stderr, "pthread_join storagemgr: %s\n",
                    strerror(error_code));
            result = EXIT_FAILURE;
        }
    }

    return result;
}
