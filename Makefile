CC = gcc
CFLAGS = -Wall -Iheader -g
LDFLAGS = -luring -lpthread

TARGET = build/ffind
SRC = src/main.c src/worker.c src/workqueue.c
OBJ = build/main.o build/worker.o build/workqueue.o

all: $(TARGET)

# Link the objects into the binary
$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

# Compile .c → .o
build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build

.PHONY: all clean
