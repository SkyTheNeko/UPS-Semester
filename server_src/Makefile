CC=gcc
CFLAGS=-Wall -Wextra -O2 -std=c11
SRC=main.c net.c protocol.c lobby.c game.c config.c
OUT=server

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

clean:
	rm -f $(OUT)
