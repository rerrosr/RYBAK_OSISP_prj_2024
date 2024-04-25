CC = gcc
CFLAGS_RELEASE = -std=c11 -W -Wall -Wextra -Werror
CFLAGS_DEBUG = -g -ggdb -std=c11 -W -Wall -Wextra

.PHONY: all clean

MODE = release

ifeq ($(MODE), release)
	CFLAGS = $(CFLAGS_RELEASE)
	TARGET = server
else
	CFLAGS = $(CFLAGS_DEBUG)
	TARGET = server_debug
endif

all: $(TARGET)

$(TARGET): ./server.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f server server_debug