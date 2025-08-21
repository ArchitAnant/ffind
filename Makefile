# Compiler and flags
CC = gcc
CFLAGS = -Wall -Iinclude -g    # -g for debugging symbols

# Source and build directories
SRC_DIR = src
BUILD_DIR = build
SRC = $(SRC_DIR)/main.c $(SRC_DIR)/workqueue.c $(SRC_DIR)/worker.c
OBJ = $(SRC:%.c=$(BUILD_DIR)/%.o)

# Target binary
TARGET = $(BUILD_DIR)/ffind

# Ensure build directory exists
$(shell mkdir -p $(BUILD_DIR)/$(SRC_DIR))

# Default rule
all: $(TARGET)

# Link object files to create the binary
$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET)

# Compile source files to object files
$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean

