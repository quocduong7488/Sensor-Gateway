CUR_DIR := .
INC_DIR := $(CUR_DIR)/include
SRC_DIR := $(CUR_DIR)/src
TEST_DIR := $(CUR_DIR)/test
BUILD_DIR := $(CUR_DIR)/build
TARGET = server

CFLAGS := -std=c11 -Wall -Wextra -Werror -Wpedantic -pthread
CPPFLAGS := -I$(INC_DIR) -D_POSIX_C_SOURCE=200809L
LDFLAGS := -pthread
LDLIBS := -lm

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS := $(OBJS:.o=.d)

.PHONY: all clean debug

all: $(TARGET)

$(TARGET): $(OBJS)
	gcc $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	gcc $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

debug: CPPFLAGS += -DDEBUG
debug: clean all

clean:
	rm -rf $(BUILD_DIR) $(TARGET)