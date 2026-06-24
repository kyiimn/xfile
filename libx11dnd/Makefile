# libx11dnd - X11 Drag and Drop library
# Standalone static library for X11 Drag and Drop protocol support.
# Core files are Xlib-only; x11dnd_xt.c adds Xt integration.

CC ?= cc
AR ?= ar
RANLIB ?= ranlib

# X11/Xt include flags (via pkg-config if available, else fallback)
X11_CFLAGS := $(shell pkg-config --cflags x11 xt xext 2>/dev/null)
ifeq ($(X11_CFLAGS),)
X11_CFLAGS = -I/usr/include
endif

CFLAGS = -O2 -Wall -std=c99 -Iinclude -I../src $(X11_CFLAGS)

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)
LIB = libx11dnd.a

.PHONY: all clean test

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $(OBJS)
	$(RANLIB) $@

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	-rm -f $(OBJS) $(LIB)

test:
	@echo "No tests yet"

-include $(SRCS:.c=.d)