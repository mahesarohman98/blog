CC = cc
CFLAGS = -Wall -Wextra -std=c99 -pedantic
LDFLAGS = -static -s
SRC = build.c
BIN = build

.PHONY: all clean

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $(BIN) $(SRC) $(LDFLAGS)

clean:
	rm -f $(BIN)

