CUR_DIR := .
INC_DIR := $(CUR_DIR)/include
SRC_DIR := $(CUR_DIR)/src
TEST_DIR := $(CUR_DIR)/test

CFLAGS := -Wall -Wextra -Werror -pthread -Iinclude
CPPFLAGS := 
TARGET = server

SOURCES = src/main.c \
          src/gateway.c \
          src/workers.c \
          src/logger.c

.PHONY: all clean debug

all: $(TARGET)

$(TARGET): $(SOURCE)
	gcc	$(CPPFLAGS)	$(CFLAGS) $(SOURCE) -o $(TARGET)

debug: CPPFLAGS += -DDEBUG
debug: clean all

clean:
	rm -f $(TARGET)
