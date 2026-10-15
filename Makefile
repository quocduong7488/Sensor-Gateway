CUR_DIR := .
INC_DIR := $(CUR_DIR)/include
SRC_DIR := $(CUR_DIR)/src
TEST_DIR := $(CUR_DIR)/test

CFLAGS := -Wall -Wextra -Werror -pthread -Iinclude
CPPFLAGS := 
TARGET = server

.PHONY: all clean debug

all: $(TARGET)

$(TARGET): $(SRC_DIR)/main.c	$(SRC_DIR)/gateway.c	$(SRC_DIR)/logger.c		$(SRC_DIR)/node.c
	gcc	$(CPPFLAGS)	$(CFLAGS) $(SRC_DIR)/main.c $(SRC_DIR)/gateway.c	$(SRC_DIR)/logger.c		$(SRC_DIR)/node.c -o $(TARGET)

debug: CPPFLAGS += -DDEBUG
debug: clean all

clean:
	rm -f $(TARGET)
