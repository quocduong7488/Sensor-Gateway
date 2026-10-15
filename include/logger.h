#ifndef LOGGER_H
#define LOGGER_H

#include <sys/types.h>

int logger_start(void);
int logger_write(const char *source, const char *message);
void logger_stop(void);

#endif