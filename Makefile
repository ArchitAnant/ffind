CC := gcc
INC_DIR := headers

# pkg-config if available
PKG_CFLAGS := $(shell pkg-config --cflags liburing 2>/dev/null)
PKG_LIBS   := $(shell pkg-config --libs liburing 2>/dev/null)

CFLAGS := -Wall -Wextra -O2 -I$(INC_DIR) $(PKG_CFLAGS)
LDLIBS := $(if $(PKG_LIBS),$(PKG_LIBS),-luring)

SRC_DIR := src
BUILD_DIR := build

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
TARGET := $(BUILD_DIR)/ffind

all: $(TARGET)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET) testdir

.PHONY: all clean run
