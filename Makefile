CC = gcc
CFLAGS = -Wall -Iinclude
SRC = src/main.c src/work_queue.c src/utils.c
OBJ = $(SRC:.c=.o)
TARGET = build/my_program

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

