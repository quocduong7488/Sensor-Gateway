CUR_DIR := .
INC_DIR := $(CUR_DIR)/include
SRC_DIR := $(CUR_DIR)/src
TEST_DIR := $(CUR_DIR)/test
BUILD_DIR := $(CUR_DIR)/build
TARGET := server
SENSOR_NODE := sensor_node

CFLAGS := -std=c11 -Wall -Wextra -Werror -Wpedantic -pthread
CPPFLAGS := -I$(INC_DIR) -D_POSIX_C_SOURCE=200809L
LDFLAGS := -pthread
LDLIBS := -lm

SOURCES := 	\
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/gateway.c \
	$(SRC_DIR)/logger.c \
	$(SRC_DIR)/node.c \
	$(SRC_DIR)/connmgr.c \
	$(SRC_DIR)/sbuffer.c \
	$(SRC_DIR)/protocol_adapter.c

OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS := $(OBJS:.o=.d)

SENSOR_NODE_SRC := $(SRC_DIR)/sensor_node.c
SENSOR_NODE_OBJ := $(BUILD_DIR)/sensor_node.o
SENSOR_NODE_DEP := $(SENSOR_NODE_OBJ:.o=.d)

.PHONY: all clean debug

all: $(TARGET) $(SENSOR_NODE)

$(TARGET): $(OBJS)
	gcc $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(SENSOR_NODE): $(SENSOR_NODE_OBJ)
	gcc $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	gcc $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)
-include $(SENSOR_NODE_DEP)

debug: CPPFLAGS += -DDEBUG
debug: clean all

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(SENSOR_NODE) test_protocol_adapter