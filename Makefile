#
# Makefile for the ffind project
#

# --- Compiler and Flags ---
# Use clang or gcc
CC = gcc
# CFLAGS are for compilation.
# -g: Add debug symbols (for gdb)
# -Wall -Wextra: Enable all major warnings. ALWAYS use these.
# -O2: Optimization level 2. Use this for releases. For debugging, use -O0.
# -Iinclude: Tell the compiler to look for header files in the 'include' directory.
# -std=c11: Use the C11 standard.
CFLAGS = -g -Wall -Wextra -O2 -Iinclude -std=c11

# LDFLAGS are for the linker.
# -luring: Link against liburing
# -lpthread: Link against the POSIX threads library
LDFLAGS = -luring -lpthread


# --- Project Structure ---
# The name of our final executable
TARGET = ffind

# Where to put the compiled files (object files and the final executable)
BUILD_DIR = build

# Find all .c source files in the 'src' directory
SOURCES = $(wildcard src/*.c)

# Create a list of object files (.o) in the BUILD_DIR,
# corresponding to each source file.
# e.g., src/main.c -> build/main.o
OBJECTS = $(patsubst src/%.c, $(BUILD_DIR)/%.o, $(SOURCES))


# --- Main Targets ---

# The 'all' target is the default one. 'make' or 'make all' will build this.
# It depends on the final executable.
all: $(BUILD_DIR)/$(TARGET)

# Rule to link the final executable.
# It depends on all the object files.
$(BUILD_DIR)/$(TARGET): $(OBJECTS)
	@echo "LD   $@"
	@$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

# Rule to compile a source file (.c) into an object file (.o).
# This is a pattern rule: it applies to any file matching 'build/%.o'.
# It depends on the corresponding source file 'src/%.c'.
$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	@echo "CC   $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# This target creates the build directory if it doesn't exist.
# The '|' before $(BUILD_DIR) makes it an "order-only prerequisite".
# This means the rule is run if the directory needs to be made, but
# object files don't depend on the directory's timestamp.
$(BUILD_DIR):
	@echo "MKDIR $(BUILD_DIR)"
	@mkdir -p $(BUILD_DIR)


# --- Utility Targets ---

# 'make clean' will remove the build directory and all compiled files.
clean:
	@echo "CLEAN"
	@rm -rf $(BUILD_DIR)

# 'make run' will build the project and then run it.
# You need to pass arguments like this: 'make run ARGS="testdir file"'
run: all
	@echo "RUN $(BUILD_DIR)/$(TARGET) $(ARGS)"
	@$(BUILD_DIR)/$(TARGET) $(ARGS)

# Phony targets are ones that don't represent actual files.
.PHONY: all clean run