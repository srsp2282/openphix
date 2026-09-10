CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c99
LDLIBS ?= -lcrypto

keysearch: keysearch.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f keysearch

.PHONY: clean
