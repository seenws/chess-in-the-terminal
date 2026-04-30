CC     = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iheaders

SRC = src/main.c src/board.c src/parser.c
OBJ = $(SRC:.c=.o)

TARGET = citt

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: clean
