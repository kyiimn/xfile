# libx11dnd test suite Makefile

CC ?= cc

X11_CFLAGS := $(shell pkg-config --cflags x11 2>/dev/null)
ifeq ($(X11_CFLAGS),)
X11_CFLAGS = -I/usr/include
endif

X11_LIBS := $(shell pkg-config --libs x11 2>/dev/null)
ifeq ($(X11_LIBS),)
X11_LIBS = -lX11
endif

CFLAGS = -O2 -Wall -std=c99 -I../include -I. $(X11_CFLAGS)
LDLIBS = -L.. -lx11dnd $(X11_LIBS)

TESTS = test_atoms test_target_aware test_util test_reply test_action test_source test_source_selection test_dsave test_proxy test_incr

.PHONY: all clean test check

all: $(TESTS)

test_atoms: test_atoms.c test_framework.h
	$(CC) $(CFLAGS) -o $@ test_atoms.c $(LDLIBS)

test_target_aware: test_target_aware.c test_framework.h
	$(CC) $(CFLAGS) -o $@ test_target_aware.c $(LDLIBS)

test_util: test_util.c test_framework.h
	$(CC) $(CFLAGS) -o $@ test_util.c $(LDLIBS)

test_reply: test_reply.c test_framework.h
	$(CC) $(CFLAGS) -o $@ test_reply.c $(LDLIBS)

test_action: test_action.c test_framework.h
	$(CC) $(CFLAGS) -o $@ test_action.c $(LDLIBS)

test_source_selection: test_source_selection.c test_framework.h
	$(CC) $(CFLAGS) -o $@ test_source_selection.c $(LDLIBS)

test_dsave: test_dsave.c test_framework.h
	$(CC) $(CFLAGS) -o $@ test_dsave.c $(LDLIBS)

test_incr: test_incr.c
	$(CC) $(CFLAGS) -o $@ test_incr.c $(LDLIBS)

test: all
	@./test_runner.sh

check: test

clean:
	-rm -f $(TESTS)