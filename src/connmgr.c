#define _POSIX_C_SOURCE 200809L

#include "connmgr.h"
#include "protocol_adapter.h"
#include "sensor_data.h"
#include "sbuffer.h"
#include "logger.h"

#include <arpa/inet.h>
#include <errno.h>
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

#define MAX_ADAPTERS 4
#define RECV_BUFFER_SIZE 1024
#define STREAM_BUFFER_SIZE 2048
#define CLIENT_RECV_TIMEOUT_SEC 1
#define LISTEN_POLL_TIMEOUT_MS 500
#define MAX_CONSECUTIVE_FRAME_ERRORS 5U

typedef struct {
    int client_fd;
    gateway_t *gateway;
    const protocol_adapter_t *adapter;
    unsigned int consecutive_frame_errors;
    int close_requested;
} client_context_t;

typedef struct client_thread_node {
    pthread_t thread;
    struct client_thread_node *next;
} client_thread_node_t;

static int gateway_is_stopping(const gateway_t *gateway)
{
    return atomic_load(&gateway->stop);
}

static void close_socket(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

static int configure_client_socket(int client_fd)
{
    struct timeval timeout = {
        .tv_sec = CLIENT_RECV_TIMEOUT_SEC,
        .tv_usec = 0
    };

    return setsockopt(
        client_fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout)
    );
}

static void process_stream(
    client_context_t *context,
    uint8_t *buffer,
    size_t *used)
{
    gateway_t *gateway = context->gateway;
    const protocol_adapter_t *adapter = context->adapter;

    while (*used > 0) {
        sensor_data_t data = {0};
        size_t consumed = 0;

        parse_result_t result = adapter->parse(
            buffer,
            *used,
            &data,
            &consumed
        );

        if (result == PARSE_NEED_MORE) {
            if (*used < STREAM_BUFFER_SIZE) {
                return;
            }

            logger_write(
                "CONN",
                "Parser needs more data but stream buffer is full"
            );

            consumed = 1;
        } else if (result == PARSE_OK) {
            context->consecutive_frame_errors = 0;
            if (consumed == 0 || consumed > *used) {
                logger_write(
                    "CONN",
                    "Protocol parser returned invalid consumed length"
                );
                consumed = 1;
            } else if (gateway->sbuffer == NULL) {
                logger_write(
                    "CONN",
                    "Shared buffer is not initialized"
                );
            } else if (sbuffer_insert(gateway->sbuffer, &data) != 0) {
                logger_write(
                    "CONN",
                    "Failed to insert sensor data"
                );
            } else {
                char message[160];

                snprintf(
                    message,
                    sizeof(message),
                    "%s frame accepted: sensor=%u type=%d",
                    adapter->name,
                    (unsigned int)data.id,
                    (int)data.type
                );

                logger_write("CONN", message);
            }
        } else {
            if (consumed == 0 || consumed > *used) {
                consumed = 1;
            }

            context->consecutive_frame_errors++;
            logger_write("CONN", "Invalid protocol frame");

            if (context->consecutive_frame_errors >= MAX_CONSECUTIVE_FRAME_ERRORS) {
                logger_write("CONN", "Too many consecutive frame errors; closing client connection");
                context->close_requested = 1;
            }
        }

        /*
         * Gửi phản hồi nếu adapter có quy định.
         * TBP/HLP/EVP không tạo phản hồi; SGP có ACK/NAK.
         */
        if (result != PARSE_NEED_MORE) {
            uint8_t reply[16];

            size_t reply_len = adapter->build_reply(
                result,
                reply,
                sizeof(reply)
            );

            if (reply_len > sizeof(reply)) {
                logger_write(
                    "CONN",
                    "Protocol adapter returned invalid reply length"
                );
            } else if (reply_len > 0) {
                size_t sent = 0;

                while (sent < reply_len) {
                    ssize_t n = send(
                        context->client_fd,
                        reply + sent,
                        reply_len - sent,
                        0
                    );

                    if (n > 0) {
                        sent += (size_t)n;
                    } else if (n < 0 && errno == EINTR) {
                        continue;
                    } else {
                        sent = reply_len;
                        logger_write(
                            "CONN",
                            "Failed to send protocol reply"
                        );
                    }
                }
            }
        }

        if (context->close_requested) {
            return;
        }

        *used -= consumed;

        if (*used > 0) {
            memmove(buffer, buffer + consumed, *used);
        }
    }
}

static void handle_client(client_context_t *context)
{
    uint8_t recv_buffer[RECV_BUFFER_SIZE];
    uint8_t stream_buffer[STREAM_BUFFER_SIZE];
    size_t used = 0;

    gateway_t *gateway = context->gateway;
    int client_fd = context->client_fd;
    char message[160];

    snprintf(
        message,
        sizeof(message),
        "%s client connected (socket=%d)",
        context->adapter->name,
        client_fd
    );

    logger_write("CONN", message);

    if (configure_client_socket(client_fd) != 0) {
        snprintf(
            message,
            sizeof(message),
            "Cannot configure client socket: %s",
            strerror(errno)
        );

        logger_write("CONN", message);
        goto cleanup;
    }

    while (!gateway_is_stopping(gateway)) {
        ssize_t received = recv(
            client_fd,
            recv_buffer,
            sizeof(recv_buffer),
            0
        );

        if (received == 0) {
            break;
        }

        if (received < 0) {
            if (errno == EINTR ||
                errno == EAGAIN ||
                errno == EWOULDBLOCK) {
                continue;
            }

            snprintf(
                message,
                sizeof(message),
                "Receive error on socket %d: %s",
                client_fd,
                strerror(errno)
            );

            logger_write("CONN", message);
            break;
        }

        size_t incoming = (size_t)received;

        while (incoming > 0) {
            process_stream(context, stream_buffer, &used);

            if (context->close_requested) {
                goto cleanup;
            }

            size_t available = sizeof(stream_buffer) - used;

            if (available == 0) {
                logger_write(
                    "CONN",
                    "Stream buffer is full; discarding one byte"
                );

                memmove(
                    stream_buffer,
                    stream_buffer + 1,
                    used - 1
                );

                --used;
                continue;
            }

            size_t to_copy = incoming < available
                           ? incoming
                           : available;

            memcpy(
                stream_buffer + used,
                recv_buffer + ((size_t)received - incoming),
                to_copy
            );

            used += to_copy;
            incoming -= to_copy;

            process_stream(context, stream_buffer, &used);

            if (context->close_requested) {
                goto cleanup;
            }
        }
    }

    if (gateway_is_stopping(gateway)) {
        logger_write(
            "CONN",
            "Closing client connection during shutdown"
        );
    }

cleanup:
    close_socket(client_fd);

    snprintf(
        message,
        sizeof(message),
        "%s client disconnected (socket=%d)",
        context->adapter->name,
        client_fd
    );

    logger_write("CONN", message);
}

static void *client_thread_run(void *arg)
{
    client_context_t *context = arg;

    if (context == NULL) {
        return NULL;
    }

    handle_client(context);
    free(context);

    return NULL;
}

static void append_client_thread(
    client_thread_node_t **head,
    client_thread_node_t *node)
{
    if (*head == NULL) {
        *head = node;
        return;
    }

    client_thread_node_t *current = *head;

    while (current->next != NULL) {
        current = current->next;
    }

    current->next = node;
}

static void join_client_threads(client_thread_node_t *head)
{
    while (head != NULL) {
        client_thread_node_t *next = head->next;

        int result = pthread_join(head->thread, NULL);

        if (result != 0) {
            char message[128];

            snprintf(
                message,
                sizeof(message),
                "Failed to join client thread: %s",
                strerror(result)
            );

            logger_write("CONN", message);
        }

        free(head);
        head = next;
    }
}

/*
 * Tạo listener cho một adapter.
 * Cổng thực tế = base_port + adapter->port_offset.
 */
static int create_listener(
    uint16_t port,
    const protocol_adapter_t *adapter)
{
    int server_fd;
    int reuse_addr = 1;
    struct sockaddr_in address;
    char message[160];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        snprintf(
            message,
            sizeof(message),
            "%s socket() failed: %s",
            adapter->name,
            strerror(errno)
        );

        logger_write("CONN", message);
        return -1;
    }

    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse_addr,
            sizeof(reuse_addr)) < 0) {
        snprintf(
            message,
            sizeof(message),
            "%s setsockopt() failed: %s",
            adapter->name,
            strerror(errno)
        );

        logger_write("CONN", message);
        close_socket(server_fd);
        return -1;
    }

    memset(&address, 0, sizeof(address));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(
            server_fd,
            (struct sockaddr *)&address,
            sizeof(address)) < 0) {
        snprintf(
            message,
            sizeof(message),
            "%s bind() failed on port %u: %s",
            adapter->name,
            (unsigned int)port,
            strerror(errno)
        );

        logger_write("CONN", message);
        close_socket(server_fd);
        return -1;
    }

    if (listen(server_fd, SOMAXCONN) < 0) {
        snprintf(
            message,
            sizeof(message),
            "%s listen() failed: %s",
            adapter->name,
            strerror(errno)
        );

        logger_write("CONN", message);
        close_socket(server_fd);
        return -1;
    }

    snprintf(
        message,
        sizeof(message),
        "%s listening on TCP port %u",
        adapter->name,
        (unsigned int)port
    );

    logger_write("CONN", message);

    return server_fd;
}

void *connmgr_run(void *arg)
{
    gateway_t *gateway = arg;

    int listeners[MAX_ADAPTERS];
    struct pollfd poll_fds[MAX_ADAPTERS];
    const protocol_adapter_t *adapters[MAX_ADAPTERS];

    size_t adapter_count;
    size_t created_listeners = 0;

    client_thread_node_t *thread_list = NULL;
    char message[160];

    for (size_t i = 0; i < MAX_ADAPTERS; ++i) {
        listeners[i] = -1;
    }

    if (gateway == NULL) {
        logger_write("CONN", "Invalid Gateway argument");
        return NULL;
    }

    if (gateway->base_port == 0 ||
        gateway->base_port > 65532) {
        logger_write("CONN", "Invalid sensor base port");
        return NULL;
    }

    adapter_count = protocol_adapter_count();

    if (adapter_count == 0 || adapter_count > MAX_ADAPTERS) {
        logger_write(
            "CONN",
            "Invalid number of protocol adapters"
        );

        return NULL;
    }

    /*
     * Khởi tạo toàn bộ listener trước khi vào vòng poll.
     * Nếu một listener thất bại, đóng các listener đã mở.
     */
    for (size_t i = 0; i < adapter_count; ++i) {
        adapters[i] = protocol_adapter_get(i);

        if (adapters[i] == NULL ||
            adapters[i]->parse == NULL ||
            adapters[i]->build_reply == NULL) {
            logger_write(
                "CONN",
                "Invalid protocol adapter configuration"
            );

            goto cleanup;
        }

        uint32_t port =
            (uint32_t)gateway->base_port +
            (uint32_t)adapters[i]->port_offset;

        if (port == 0 || port > 65535) {
            logger_write(
                "CONN",
                "Protocol adapter port is out of range"
            );

            goto cleanup;
        }

        listeners[i] = create_listener(
            (uint16_t)port,
            adapters[i]
        );

        if (listeners[i] < 0) {
            goto cleanup;
        }

        poll_fds[i].fd = listeners[i];
        poll_fds[i].events = POLLIN;
        poll_fds[i].revents = 0;

        ++created_listeners;
    }

    while (!gateway_is_stopping(gateway)) {
        int poll_result = poll(
            poll_fds,
            (nfds_t)adapter_count,
            LISTEN_POLL_TIMEOUT_MS
        );

        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }

            snprintf(
                message,
                sizeof(message),
                "poll() failed: %s",
                strerror(errno)
            );

            logger_write("CONN", message);
            break;
        }

        if (poll_result == 0) {
            continue;
        }

        for (size_t i = 0; i < adapter_count; ++i) {
            if (!(poll_fds[i].revents & POLLIN)) {
                if (poll_fds[i].revents &
                    (POLLERR | POLLHUP | POLLNVAL)) {
                    snprintf(
                        message,
                        sizeof(message),
                        "%s listener reported an error",
                        adapters[i]->name
                    );

                    logger_write("CONN", message);
                }

                continue;
            }

            struct sockaddr_in client_addr;
            socklen_t client_addr_len = sizeof(client_addr);

            int client_fd = accept(
                listeners[i],
                (struct sockaddr *)&client_addr,
                &client_addr_len
            );

            if (client_fd < 0) {
                if (errno == EINTR ||
                    errno == ECONNABORTED) {
                    continue;
                }

                snprintf(
                    message,
                    sizeof(message),
                    "%s accept() failed: %s",
                    adapters[i]->name,
                    strerror(errno)
                );

                logger_write("CONN", message);
                continue;
            }

            if (gateway_is_stopping(gateway)) {
                close_socket(client_fd);
                break;
            }

            client_context_t *context = malloc(sizeof(*context));

            if (context == NULL) {
                logger_write(
                    "CONN",
                    "Cannot allocate client context"
                );

                close_socket(client_fd);
                continue;
            }

            context->client_fd = client_fd;
            context->gateway = gateway;
            context->adapter = adapters[i];
            context->consecutive_frame_errors = 0;
            context->close_requested = 0;

            client_thread_node_t *node = malloc(sizeof(*node));

            if (node == NULL) {
                logger_write(
                    "CONN",
                    "Cannot allocate client thread node"
                );

                free(context);
                close_socket(client_fd);
                continue;
            }

            node->next = NULL;

            int thread_result = pthread_create(
                &node->thread,
                NULL,
                client_thread_run,
                context
            );

            if (thread_result != 0) {
                snprintf(
                    message,
                    sizeof(message),
                    "pthread_create() failed: %s",
                    strerror(thread_result)
                );

                logger_write("CONN", message);

                free(context);
                free(node);
                close_socket(client_fd);
                continue;
            }

            append_client_thread(&thread_list, node);
        }
    }

cleanup:
    for (size_t i = 0; i < created_listeners; ++i) {
        close_socket(listeners[i]);
        listeners[i] = -1;
    }

    join_client_threads(thread_list);

    logger_write("CONN", "Connection Manager stopped");

    return NULL;
}