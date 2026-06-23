/*
 * test_atoms.c - TDD test for x11dnd_atoms module.
 *
 * Build (from repo root):
 *   cc -std=c99 -O2 -Wall -Ilibx11dnd/include $(pkg-config --cflags x11) \
 *      libx11dnd/src/x11dnd_atoms.c libx11dnd/tests/test_atoms.c \
 *      -o /tmp/test_atoms $(pkg-config --libs x11)
 *
 * Run under Xvfb:
 *   Xvfb :99 -screen 0 800x600x8 &
 *   DISPLAY=:99 /tmp/test_atoms
 */
#include "x11dnd_atoms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) \
	do { \
		if (!(cond)) { \
			fprintf(stderr, "FAIL: %s (line %d)\n", (msg), __LINE__); \
			failures++; \
		} else { \
			printf("ok: %s\n", (msg)); \
		} \
	} while (0)

int
main(void)
{
	Display *dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "Cannot open display - run under Xvfb (DISPLAY=:99)\n");
		return 99;
	}

	x11dnd_init_atoms(dpy);

	const X11DndAtoms *a = x11dnd_get_atoms();
	CHECK(a != NULL, "x11dnd_get_atoms returns non-NULL after init");

	Atom ref_enter  = XInternAtom(dpy, "XdndEnter", False);
	Atom ref_aware  = XInternAtom(dpy, "XdndAware", False);
	Atom ref_copy   = XInternAtom(dpy, "XdndActionCopy", False);
	Atom ref_uri    = XInternAtom(dpy, "text/uri-list", False);
	Atom ref_save0  = XInternAtom(dpy, "XdndDirectSave0", False);

	CHECK(a->XdndEnter == ref_enter, "XdndEnter matches XInternAtom(\"XdndEnter\")");
	CHECK(a->XdndAware == ref_aware, "XdndAware matches XInternAtom(\"XdndAware\")");
	CHECK(a->XdndActionCopy == ref_copy, "XdndActionCopy matches XInternAtom(\"XdndActionCopy\")");
	CHECK(a->text_uri_list == ref_uri, "text_uri_list matches XInternAtom(\"text/uri-list\")");
	CHECK(a->XdndDirectSave0 == ref_save0, "XdndDirectSave0 matches XInternAtom(\"XdndDirectSave0\")");

	Atom bad_enter = XInternAtom(dpy, "_XdndEnter", False);
	CHECK(a->XdndEnter != bad_enter,
	      "XdndEnter is NOT the underscore variant (_XdndEnter)");

	Atom bad_drop = XInternAtom(dpy, "_XdndDrop", False);
	CHECK(a->XdndDrop != bad_drop,
	      "XdndDrop is NOT the underscore variant (_XdndDrop)");

	const char *name = x11dnd_atom_name(a->XdndActionCopy);
	CHECK(name != NULL, "x11dnd_atom_name returns non-NULL");
	CHECK(strcmp(name, "XdndActionCopy") == 0,
	      "x11dnd_atom_name(XdndActionCopy) == \"XdndActionCopy\"");

	CHECK(strcmp(x11dnd_atom_name(a->XdndEnter), "XdndEnter") == 0,
	      "x11dnd_atom_name(XdndEnter) == \"XdndEnter\"");
	CHECK(strcmp(x11dnd_atom_name(a->text_uri_list), "text/uri-list") == 0,
	      "x11dnd_atom_name(text_uri_list) == \"text/uri-list\"");
	CHECK(strcmp(x11dnd_atom_name(a->application_x_file_list),
	            "application/x-file-list") == 0,
	      "x11dnd_atom_name(application_x_file_list) == \"application/x-file-list\"");

	Atom bogus = XInternAtom(dpy, "X11DND_TEST_BOGUS_ATOM_12345", False);
	CHECK(strcmp(x11dnd_atom_name(bogus), "unknown") == 0,
	      "x11dnd_atom_name(unknown atom) == \"unknown\"");

	CHECK(a->XdndPosition != None, "XdndPosition is not None");
	CHECK(a->XdndStatus != None, "XdndStatus is not None");
	CHECK(a->XdndLeave != None, "XdndLeave is not None");
	CHECK(a->XdndFinished != None, "XdndFinished is not None");
	CHECK(a->XdndSelection != None, "XdndSelection is not None");
	CHECK(a->XdndTypeList != None, "XdndTypeList is not None");
	CHECK(a->XdndActionList != None, "XdndActionList is not None");
	CHECK(a->XdndActionDescription != None, "XdndActionDescription is not None");
	CHECK(a->XdndProxy != None, "XdndProxy is not None");
	CHECK(a->XdndActionMove != None, "XdndActionMove is not None");
	CHECK(a->XdndActionLink != None, "XdndActionLink is not None");
	CHECK(a->XdndActionAsk != None, "XdndActionAsk is not None");
	CHECK(a->XdndActionPrivate != None, "XdndActionPrivate is not None");
	CHECK(a->XdndDirectSave != None, "XdndDirectSave is not None");
	CHECK(a->INCR != None, "INCR is not None");
	CHECK(a->TARGETS != None, "TARGETS is not None");
	CHECK(a->TIMESTAMP != None, "TIMESTAMP is not None");
	CHECK(a->UTF8_STRING != None, "UTF8_STRING is not None");
	CHECK(a->STRING != None, "STRING is not None");
	CHECK(a->FILE_NAME != None, "FILE_NAME is not None");
	CHECK(a->text_plain != None, "text_plain is not None");

	XCloseDisplay(dpy);

	if (failures == 0) {
		printf("\nALL TESTS PASSED\n");
		return 0;
	}
	fprintf(stderr, "\n%d TEST(S) FAILED\n", failures);
	return 1;
}