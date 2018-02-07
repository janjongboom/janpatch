NAME = janpatch-cli

CC ?= gcc
CFLAGS ?= -Wall

MACROS += -DJANPATCH_STREAM=FILE

all: build

.PHONY: build clean

build:
	$(CC) $(MACROS) $(CFLAGS) janpatch-cli.c wrapper.c -o $(NAME)

clean:
	rm $(NAME)