# Compiler and flags
CC = gcc
CFLAGS = -Wall -Iinclude -g    # -g for debugging symbols, -Wall for warnings

# --- NEW: Linker Flags ---
# -luring: Link against the liburing library
# -lpthread: Link against the POSIX threads library
LDFLAGS = -luring -lpthread

# Source and build directories
SRC_DIR = src
BUILD_DIR = build
# A more robust way to find all source files automatically
SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRC))

# Target binary
TARGET = $(BUILD_DIR)/ffind

# --- IMPROVED: Rule to create build directory ---
# This is a better way to create the directory, as a prerequisite.
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Default rule
all: $(TARGET)

# Link object files to create the binary
# --- CHANGED: Added $(LDFLAGS) to the command ---
$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

# Compile source files to object files
# --- IMPROVED: This rule now correctly finds source files ---
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean