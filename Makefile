# Makefile
CC=gcc
CFLAGS=-Wall -Wextra -g -std=c11
TARGET=ffind

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c

clean:
	rm -f $(TARGET)
