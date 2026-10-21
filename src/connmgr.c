#define _POSIX_C_SOURCE 200809L

#include "connmgr.h"

#include "sbuffer.h"
#include "sensor_data.h"
#include "logger.h"

#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define TBP_FRAME_SIZE 18
#define CLIENT_RECV_TIMEOUT_SEC 1
#define LISTEN_POLL_TIMEOUT_MS 500

typedef struct 
{
    int client_fd;
    gateway_t *gateway;
} client_context_t;

typedef struct client_thread_node 
{
    pthread_t thread;
    struct client_thread_node *next;
} client_thread_node_t;


//GATEWAY STATE


static int gateway_is_stopping(const gateway_t *gateway)
{
    return atomic_load(&gateway->stop);
}


//SOCKET HELPERS


static void close_socket(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

static int configure_client_socket(int client_fd)
{
    struct timeval timeout;

    timeout.tv_sec = CLIENT_RECV_TIMEOUT_SEC;
    timeout.tv_usec = 0;

    if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) 
    {
        return -1;
    }

    return 0;
}


//FRAME DECODING


static int decode_frame(const uint8_t frame[TBP_FRAME_SIZE], sensor_data_t *data)
{
    uint16_t sensor_id;
    double sensor_value;
    int64_t timestamp;

    if (frame == NULL || data == NULL)
    {
        return -1;
    }

    memcpy(&sensor_id, frame, sizeof(sensor_id));
    memcpy(&sensor_value, frame + 2, sizeof(sensor_value));
    memcpy(&timestamp, frame + 10, sizeof(timestamp));

    if (!isfinite(sensor_value))
    {
        return -1;
    }

    if ((int64_t)(time_t)timestamp != timestamp)
    {
        return -1;
    }

    data->id = sensor_id;
    data->value = sensor_value;
    data->ts = (time_t)timestamp;

    return 0;
}


//SHARED BUFFER


static int process_frame(gateway_t *gateway, const uint8_t frame[TBP_FRAME_SIZE])
{
    sensor_data_t data;

    if (gateway == NULL || frame == NULL) 
    {
        return -1;
    }

    if (decode_frame(frame, &data) != 0) 
    {
        logger_write("CONNMGR", "Invalid sensor frame");

        return -1;
    }

    if (gateway->buffer == NULL)
    {
        logger_write("CONNMGR", "Shared buffer is not initialized");

        return -1;
    }

    if (sbuffer_insert(gateway->buffer, &data) != 0) 
    {
        logger_write("CONNMGR", "Failed to insert sensor data");

        return -1;
    }

    return 0;
}


//CLIENT CONNECTION


static void handle_client(client_context_t *context)
{
    uint8_t recv_buffer[1024];
    uint8_t frame_buffer[TBP_FRAME_SIZE];
    size_t frame_used = 0;
    gateway_t *gateway = context->gateway;
    int client_fd = context->client_fd;
    char log_message[160];

    snprintf(log_message, sizeof(log_message), "Sensor client connected (socket=%d)", client_fd);

    logger_write("CONNMGR", log_message);

    if (configure_client_socket(client_fd) != 0) 
    {
        snprintf(log_message, sizeof(log_message), "Cannot configure client socket: %s", strerror(errno));
        logger_write("CONNMGR", log_message);
        goto cleanup;
    }

    while (!gateway_is_stopping(gateway)) 
    {
        ssize_t received = recv(client_fd, recv_buffer, sizeof(recv_buffer), 0);

        if (received == 0) 
        {
            break;
        }

        if (received < 0) {
            if (errno == EINTR) 
            {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }

            snprintf(log_message, sizeof(log_message), "Receive error on socket %d: %s", client_fd, strerror(errno));
            logger_write("CONNMGR", log_message);

            break;
        }

        size_t offset = 0;
        size_t bytes_received = (size_t)received;

        while (offset < bytes_received) 
        {
            size_t available = TBP_FRAME_SIZE - frame_used;
            size_t remaining = bytes_received - offset;
            size_t to_copy = remaining < available
                           ? remaining
                           : available;

            memcpy(frame_buffer + frame_used, recv_buffer + offset, to_copy);

            frame_used += to_copy;
            offset += to_copy;

            if (frame_used == TBP_FRAME_SIZE) 
            {
                if (process_frame(gateway, frame_buffer) != 0)
                {
                    logger_write("CONNMGR", "Failed to process received frame");
                }

                frame_used = 0;
            }
        }
    }

    if (gateway_is_stopping(gateway))
    {
        logger_write("CONNMGR", "Closing client connection during shutdown");
    }

cleanup:

    close_socket(client_fd);

    snprintf(log_message, sizeof(log_message), "Sensor client disconnected (socket=%d)", client_fd);

    logger_write("CONNMGR", log_message);
}


//CLIENT THREAD

static void *client_thread_run(void *arg)
{
    client_context_t *context = arg;

    if (context == NULL)
    {
        return NULL;
    }

    handle_client(context);
    free(context);

    return NULL;
}


//THREAD MANAGEMENT


static void append_client_thread(client_thread_node_t **head, client_thread_node_t *node)
{
    if (*head == NULL)
    {
        *head = node;
        return;
    }

    client_thread_node_t *current = *head;

    while (current->next != NULL) 
    {
        current = current->next;
    }

    current->next = node;
}

static void join_client_threads(client_thread_node_t *head)
{
    while (head != NULL)
    {
        client_thread_node_t *next = head->next;

        int result = pthread_join(head->thread, NULL);

        if (result != 0)
        {
            char message[128];

            snprintf(message, sizeof(message), "Failed to join client thread: %s", strerror(result));
            logger_write("CONNMGR", message);
        }

        free(head);

        head = next;
    }
}


//CONNECTION MANAGER


void *connmgr_run(void *arg)
{
    gateway_t *gateway = arg;

    int server_fd = -1;
    int reuse_addr = 1;
    struct sockaddr_in server_addr;
    char log_message[160];

    client_thread_node_t *thread_list = NULL;

    if (gateway == NULL)
    {
        logger_write("CONNMGR", "Invalid Gateway argument");

        return NULL;
    }

    if (gateway->base_port < 1 || gateway->base_port > 65535)
    {
        logger_write("CONNMGR", "Invalid sensor TCP port");

        return NULL;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        snprintf(log_message,sizeof(log_message), "socket() failed: %s", strerror(errno));
        logger_write("CONNMGR", log_message);

        return NULL;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) < 0)
    {
        snprintf(log_message, sizeof(log_message), "setsockopt() failed: %s", strerror(errno));
        logger_write("CONNMGR", log_message);
        close_socket(server_fd);

        return NULL;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((uint16_t)gateway->base_port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        snprintf(log_message, sizeof(log_message), "bind() failed on port %d: %s", gateway->base_port, strerror(errno));
        logger_write("CONNMGR", log_message);
        close_socket(server_fd);

        return NULL;
    }

    if (listen(server_fd, SOMAXCONN) < 0)
    {
        snprintf(log_message, sizeof(log_message), "listen() failed: %s", strerror(errno));
        logger_write("CONNMGR", log_message);
        close_socket(server_fd);

        return NULL;
    }

    snprintf(log_message, sizeof(log_message), "Listening for sensor connections on port %d", gateway->base_port);

    logger_write("CONNMGR", log_message);

    while (!gateway_is_stopping(gateway))
    {
        struct pollfd poll_fd;
        int poll_result = poll(&poll_fd, 1, LISTEN_POLL_TIMEOUT_MS);

        poll_fd.fd = server_fd;
        poll_fd.events = POLLIN;
        poll_fd.revents = 0;



        if (poll_result < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            snprintf(log_message, sizeof(log_message), "poll() failed: %s", strerror(errno));

            logger_write("CONNMGR", log_message);

            break;
        }

        if (poll_result == 0)
        {
            continue;
        }

        if (poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            logger_write("CONNMGR", "Listening socket reported an error");

            break;
        }

        if (!(poll_fd.revents & POLLIN))
        {
            continue;
        }

        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

        if (client_fd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNABORTED)
            {
                continue;
            }

            snprintf(log_message, sizeof(log_message), "accept() failed: %s", strerror(errno));

            logger_write("CONNMGR", log_message);

            if (gateway_is_stopping(gateway))
            {
                break;
            }

            continue;
        }

        if (gateway_is_stopping(gateway))
        {
            close_socket(client_fd);
            break;
        }

        client_context_t *context = malloc(sizeof(*context));

        if (context == NULL)
        {
            logger_write("CONNMGR", "Cannot allocate client context");
            close_socket(client_fd);

            continue;
        }

        context->client_fd = client_fd;
        context->gateway = gateway;
        client_thread_node_t *node = malloc(sizeof(*node));

        if (node == NULL)
        {
            logger_write("CONNMGR", "Cannot allocate client thread node");
            free(context);
            close_socket(client_fd);

            continue;
        }

        node->next = NULL;

        int thread_result = pthread_create(&node->thread, NULL, client_thread_run, context);

        if (thread_result != 0)
        {
            snprintf(log_message, sizeof(log_message), "pthread_create() failed: %s", strerror(thread_result));
            logger_write("CONNMGR", log_message);
            free(context);
            free(node);
            close_socket(client_fd);

            continue;
        }

        append_client_thread(&thread_list, node);
    }

    close_socket(server_fd);
    join_client_threads(thread_list);
    logger_write("CONNMGR", "Connection Manager stopped");

    return NULL;
}