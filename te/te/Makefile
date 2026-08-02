CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra

.PHONY: all clean

all: te

te: te.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f te
