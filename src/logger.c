#define _POSIX_C_SOURCE 200809L

#include "logger.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FIFO_PATH "logFifo"
#define LOG_PATH "gateway.log"

#define SOURCE_SIZE 8
#define MESSAGE_SIZE 201
#define EVENT_SIZE 256
#define READ_SIZE 1024

#define STOP_MESSAGE "__LOGGER_STOP__"

static int fifo_fd = -1;
static pid_t logger_pid = -1;

static pthread_mutex_t logger_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Write all bytes to a file descriptor.
 * This helper is used for the log file, not for FIFO events.
 */
static int write_all(int fd, const char *buffer, size_t length)
{
    size_t written = 0;

    while (written < length)
    {
        ssize_t result = write(fd, buffer + written, length - written);

        if (result < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        if (result == 0)
        {
            return -1;
        }

        written += (size_t)result;
    }

    return 0;
}

/*
 * Write one log event to gateway.log.
 * This function is called only by the logger process.
 */
static int write_log_entry(int log_fd,
                           unsigned long sequence,
                           const char *source,
                           const char *message)
{
    time_t now;
    struct tm local_time;
    char timestamp[20];
    char output[512];

    now = time(NULL);

    if (now == (time_t)-1)
    {
        return -1;
    }

    if (localtime_r(&now, &local_time) == NULL)
    {
        return -1;
    }

    if (strftime(timestamp, sizeof(timestamp),
                 "%Y-%m-%d %H:%M:%S", &local_time) == 0)
    {
        return -1;
    }

    int length = snprintf(output, sizeof(output),
                          "%lu %s %s %s\n",
                          sequence, timestamp, source, message);

    if (length < 0 || (size_t)length >= sizeof(output))
    {
        return -1;
    }

    return write_all(log_fd, output, (size_t)length);
}

/*
 * Process one complete event received from the FIFO.
 *
 * FIFO event format:
 * SOURCE message\n
 */
static int process_event(int log_fd,
                         unsigned long *sequence,
                         char *line)
{
    char *separator;
    char *source;
    char *message;

    separator = strchr(line, ' ');

    if (separator == NULL)
    {
        return 0;
    }

    *separator = '\0';

    source = line;
    message = separator + 1;

    if (strcmp(source, "MAIN") != 0 &&
        strcmp(source, "CONN") != 0 &&
        strcmp(source, "DATA") != 0 &&
        strcmp(source, "STOR") != 0 &&
        strcmp(source, "WEB") != 0)
    {
        return 0;
    }

    if (strcmp(source, "MAIN") == 0 &&
        strcmp(message, STOP_MESSAGE) == 0)
    {
        return 1;
    }

    if (strlen(message) > 200)
    {
        return 0;
    }

    if (write_log_entry(log_fd, *sequence, source, message) != 0)
    {
        return -1;
    }

    (*sequence)++;

    return 0;
}

/*
 * Main loop of the logger child process.
 */
static void logger_process(void)
{
    int log_fd;
    char read_buffer[READ_SIZE];
    char line[EVENT_SIZE];

    size_t line_length = 0;
    unsigned long sequence = 1;

    int discarding_line = 0;

    log_fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (log_fd == -1)
    {
        _exit(EXIT_FAILURE);
    }

    for (;;)
    {
        ssize_t bytes_read = read(fifo_fd,
                                  read_buffer,
                                  sizeof(read_buffer));

        if (bytes_read < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            break;
        }

        if (bytes_read == 0)
        {
            continue;
        }

        for (ssize_t i = 0; i < bytes_read; i++)
        {
            char ch = read_buffer[i];

            if (ch == '\n')
            {
                if (!discarding_line)
                {
                    line[line_length] = '\0';

                    int result = process_event(
                        log_fd, &sequence, line);

                    if (result == 1)
                    {
                        close(log_fd);
                        _exit(EXIT_SUCCESS);
                    }

                    if (result == -1)
                    {
                        close(log_fd);
                        _exit(EXIT_FAILURE);
                    }
                }

                line_length = 0;
                discarding_line = 0;
                continue;
            }

            if (discarding_line)
            {
                continue;
            }

            if (line_length < sizeof(line) - 1)
            {
                line[line_length++] = ch;
            }
            else
            {
                /*
                 * Discard an oversized event until its newline.
                 */
                discarding_line = 1;
                line_length = 0;
            }
        }
    }

    close(log_fd);
    _exit(EXIT_FAILURE);
}

/*
 * Initialize the FIFO and start the logger process.
 *
 * Call this before creating the worker threads.
 */
int logger_start(void)
{
    struct stat st;

    if (fifo_fd != -1 || logger_pid != -1)
    {
        errno = EALREADY;
        return -1;
    }

    if (lstat(FIFO_PATH, &st) == 0)
    {
        if (!S_ISFIFO(st.st_mode))
        {
            errno = EEXIST;
            return -1;
        }
    }
    else if (errno == ENOENT)
    {
        if (mkfifo(FIFO_PATH, 0666) == -1)
        {
            return -1;
        }
    }
    else
    {
        return -1;
    }

    /*
     * Linux allows opening a FIFO with O_RDWR.
     * This prevents open() from blocking while waiting
     * for the other end to be opened.
     */
    fifo_fd = open(FIFO_PATH, O_RDWR);

    if (fifo_fd == -1)
    {
        return -1;
    }

    logger_pid = fork();

    if (logger_pid == -1)
    {
        int saved_errno = errno;

        close(fifo_fd);
        fifo_fd = -1;

        errno = saved_errno;
        return -1;
    }

    if (logger_pid == 0)
    {
        logger_process();
    }

    return 0;
}

/*
 * Send one complete event through the FIFO.
 *
 * A mutex protects concurrent writes from multiple threads.
 */
int logger_write(const char *source, const char *message)
{
    char event[EVENT_SIZE];
    int length;
    int result = -1;

    if (source == NULL || message == NULL || fifo_fd == -1)
    {
        errno = EINVAL;
        return -1;
    }

    if (strchr(source, ' ') != NULL ||
        strchr(source, '\n') != NULL ||
        strchr(message, '\n') != NULL ||
        strchr(message, '\r') != NULL)
    {
        errno = EINVAL;
        return -1;
    }

    if (strcmp(source, "MAIN") != 0 &&
        strcmp(source, "CONN") != 0 &&
        strcmp(source, "DATA") != 0 &&
        strcmp(source, "STOR") != 0 &&
        strcmp(source, "WEB") != 0)
    {
        errno = EINVAL;
        return -1;
    }

    if (strlen(message) > 200)
    {
        errno = EMSGSIZE;
        return -1;
    }

    length = snprintf(event, sizeof(event),
                      "%s %s\n", source, message);

    if (length < 0 || (size_t)length >= sizeof(event))
    {
        errno = EMSGSIZE;
        return -1;
    }

    if ((long)length > PIPE_BUF)
    {
        errno = EMSGSIZE;
        return -1;
    }

    if (pthread_mutex_lock(&logger_mutex) != 0)
    {
        errno = EBUSY;
        return -1;
    }

    /*
     * Each event is sent using one write() call.
     */
    ssize_t written = write(fifo_fd, event, (size_t)length);

    if (written == length)
    {
        result = 0;
    }
    else if (written >= 0)
    {
        errno = EIO;
    }

    pthread_mutex_unlock(&logger_mutex);

    return result;
}

/*
 * Ask the logger process to flush pending events and exit.
 */
void logger_stop(void)
{
    if (logger_pid > 0)
    {
        /*
         * The stop event is processed after earlier events
         * in the FIFO.
         */
        if (logger_write("MAIN", STOP_MESSAGE) != 0)
        {
            /*
             * The logger may have failed; continue cleanup.
             */
        }

        int status;
        pid_t result;

        do
        {
            result = waitpid(logger_pid, &status, 0);
        }
        while (result == -1 && errno == EINTR);

        logger_pid = -1;
    }

    if (fifo_fd != -1)
    {
        close(fifo_fd);
        fifo_fd = -1;
    }

    if (unlink(FIFO_PATH) == -1 && errno != ENOENT)
    {
        perror("unlink logFifo");
    }
}