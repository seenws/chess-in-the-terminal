CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iheaders

SRC = src/main.c src/board.c
OBJ = $(SRC:.c=.o)

TARGET = citt

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: clean
