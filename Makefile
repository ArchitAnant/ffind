# Compiler and flags
CC := gcc
CFLAGS := -Wall -Wextra -O2 -Iheaders

# Directories
SRC_DIR := src
BUILD_DIR := build
INC_DIR := headers

# Files
SRCS := $(SRC_DIR)/main.c $(SRC_DIR)/request.c
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TARGET := $(BUILD_DIR)/ffind

# Default target
all: $(TARGET)

# Build target
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Compile .c to .o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Ensure build dir exists
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Clean
clean:
	rm -rf $(BUILD_DIR)/*

# Run
run: $(TARGET)
	./$(TARGET) testdir

.PHONY: all clean run

