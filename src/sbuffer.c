#include <stdlib.h>
#include <string.h>

#include "sbuffer.h"

int sbuffer_init(sbuffer_t *buffer)
{
    if (buffer == NULL) {
        return -1;
    }

    buffer->head = NULL;
    buffer->tail = NULL;
    buffer->shutdown = false;

    if (pthread_mutex_init(&buffer->mutex, NULL) != 0) {
        return -1;
    }

    if (pthread_cond_init(&buffer->cond, NULL) != 0) {
        pthread_mutex_destroy(&buffer->mutex);
        return -1;
    }

    return 0;
}

int sbuffer_insert(
    sbuffer_t *buffer,
    const sensor_data_t *data
)
{
    sbuffer_node_t *node;

    if (buffer == NULL || data == NULL) {
        return -1;
    }

    node = malloc(sizeof(*node));

    if (node == NULL) {
        return -1;
    }

    node->data = *data;
    node->read_by_data = false;
    node->read_by_storage = false;
    node->next = NULL;

    pthread_mutex_lock(&buffer->mutex);

    if (buffer->shutdown) {
        pthread_mutex_unlock(&buffer->mutex);
        free(node);
        return -1;
    }

    if (buffer->tail == NULL) {
        buffer->head = node;
        buffer->tail = node;
    } else {
        buffer->tail->next = node;
        buffer->tail = node;
    }

    pthread_cond_broadcast(&buffer->cond);

    pthread_mutex_unlock(&buffer->mutex);

    return 0;
}

int sbuffer_read(
    sbuffer_t *buffer,
    sbuffer_consumer_t consumer,
    sensor_data_t *data
)
{
    sbuffer_node_t *node;
    sbuffer_node_t *previous;
    bool *read_flag;

    if (buffer == NULL || data == NULL) {
        return -1;
    }

    if (consumer != SBUFFER_DATA_MANAGER &&
        consumer != SBUFFER_STORAGE_MANAGER) {
        return -1;
    }

    pthread_mutex_lock(&buffer->mutex);

    for (;;) {
        /*
         * Tìm phép đo đầu tiên mà consumer
         * hiện tại chưa đọc.
         */
        previous = NULL;
        node = buffer->head;

        while (node != NULL) {
            read_flag = (consumer == SBUFFER_DATA_MANAGER)
                ? &node->read_by_data
                : &node->read_by_storage;

            if (!*read_flag) {
                break;
            }

            previous = node;
            node = node->next;
        }

        if (node != NULL) {
            /*
             * Sao chép dữ liệu khi đang giữ mutex.
             * Sau khi mở khóa, consumer sử dụng
             * bản sao độc lập.
             */
            *data = node->data;

            read_flag = (consumer == SBUFFER_DATA_MANAGER)
                ? &node->read_by_data
                : &node->read_by_storage;

            *read_flag = true;

            /*
             * Xóa các node đã được cả hai consumer
             * đọc. Các node bị xóa phải nằm ở đầu
             * danh sách để bảo toàn cấu trúc queue.
             */
            while (buffer->head != NULL &&
                   buffer->head->read_by_data &&
                   buffer->head->read_by_storage) {

                sbuffer_node_t *old = buffer->head;

                buffer->head = old->next;

                if (buffer->head == NULL) {
                    buffer->tail = NULL;
                }

                free(old);
            }

            pthread_mutex_unlock(&buffer->mutex);
            return 1;
        }

        if (buffer->shutdown) {
            pthread_mutex_unlock(&buffer->mutex);
            return 0;
        }

        /*
         * Chưa có dữ liệu dành cho consumer này.
         * Ngủ đến khi được thông báo.
         */
        pthread_cond_wait(
            &buffer->cond,
            &buffer->mutex
        );

        /*
         * Sau khi thức dậy, vòng lặp sẽ kiểm tra
         * lại điều kiện dưới mutex.
         */
    }
}

void sbuffer_shutdown(sbuffer_t *buffer)
{
    if (buffer == NULL) {
        return;
    }

    pthread_mutex_lock(&buffer->mutex);

    buffer->shutdown = true;

    pthread_cond_broadcast(&buffer->cond);

    pthread_mutex_unlock(&buffer->mutex);
}

void sbuffer_destroy(sbuffer_t *buffer)
{
    sbuffer_node_t *node;

    if (buffer == NULL) {
        return;
    }

    pthread_mutex_lock(&buffer->mutex);

    node = buffer->head;

    while (node != NULL) {
        sbuffer_node_t *next = node->next;

        free(node);
        node = next;
    }

    buffer->head = NULL;
    buffer->tail = NULL;

    pthread_mutex_unlock(&buffer->mutex);

    pthread_cond_destroy(&buffer->cond);
    pthread_mutex_destroy(&buffer->mutex);
}