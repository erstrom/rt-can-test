CC ?= gcc
PREFIX ?= /usr/local
INSTALL ?= install
OBJS = rt-can-test.o lib.o can.o
CFLAGS = -Wall -Wextra -Wdeclaration-after-statement

all: rt-can-test

rt-can-test: $(OBJS)
	$(CC) $(OBJS) $(EXTRA_LDFLAGS) -o rt-can-test -lpthread

%.o: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

.PHONY: clean install uninstall

clean:
	rm -f rt-can-test *.o

install:
	mkdir -p $(PREFIX)/bin
	$(INSTALL) rt-can-test $(PREFIX)/bin

uninstall:
	rm -f $(PREFIX)/bin/rt-can-test
