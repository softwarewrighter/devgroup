CC ?= cc
CC24 ?= cc24
LD24 ?= ld24
LONGLGO ?= longlgo
LOADER_ADDRESS=0fc000

.PHONY: all clean

all: te2 te2.lgo

te2: te2.c
	$(CC) -O2 -Wall -Wextra -Werror -o $@ $<

te2-loader.o: te2-loader.c
	$(CC24) -O -c -o $@ $<

te2-loader.lgs: te2-loader.o
	$(LD24) -a $(LOADER_ADDRESS) -s -x -o $@ $<

te2.lgo: te2-loader.lgs
	$(LONGLGO) < $< > $@

clean:
	rm -f te2 te2-loader.o te2-loader.lgs te2.lgo
