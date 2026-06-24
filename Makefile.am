## Top-level Makefile.am for XFile
## Copyright (C) 2023-2026 alx@fastestcode.org

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = libx11dnd src

EXTRA_DIST = \
	COPYING \
	README \
	AGENTS.md

DISTCHECK_CONFIGURE_FLAGS = \
	--with-app-defaults-dir='$${datadir}/X11/app-defaults'