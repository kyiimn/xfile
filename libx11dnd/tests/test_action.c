/*
 * test_action.c - TDD tests for XDnD action negotiation module.
 *
 * Verifies: enum<->atom round-trip, action negotiation (match/fallback/ask),
 * XdndActionList property set/get, XdndActionDescription property set/get,
 * and action_to_string for all 6 standard actions.
 *
 * Run under Xvfb: Xvfb :99 & DISPLAY=:99 ./test_action
 */
#define _DEFAULT_SOURCE
#include "x11dnd.h"
#include "x11dnd_atoms.h"
#include "x11dnd_action.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) \
	do { \
		if (cond) { \
			printf("  PASS: %s\n", msg); \
		} else { \
			printf("  FAIL: %s\n", msg); \
			failures++; \
		} \
	} while (0)

static Window
make_window(Display *dpy, Window parent, int w, int h)
{
	XSetWindowAttributes attrs;
	Window win;

	attrs.override_redirect = True;
	attrs.event_mask = NoEventMask;
	win = XCreateWindow(dpy, parent, 0, 0, w, h, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect | CWEventMask, &attrs);
	XMapWindow(dpy, win);
	XFlush(dpy);
	return win;
}

static void
test_action_to_atom_roundtrip(Display *dpy, const X11DndAtoms *atoms)
{
	Atom a;

	(void)dpy;
	printf("test_action_to_atom_roundtrip\n");

	a = x11dnd_action_to_atom(dpy, X11DND_ACTION_COPY);
	CHECK(a == atoms->XdndActionCopy, "COPY maps to XdndActionCopy");
	CHECK(x11dnd_atom_to_action(dpy, a) == X11DND_ACTION_COPY,
		"XdndActionCopy maps back to COPY");

	a = x11dnd_action_to_atom(dpy, X11DND_ACTION_MOVE);
	CHECK(a == atoms->XdndActionMove, "MOVE maps to XdndActionMove");
	CHECK(x11dnd_atom_to_action(dpy, a) == X11DND_ACTION_MOVE,
		"XdndActionMove maps back to MOVE");

	a = x11dnd_action_to_atom(dpy, X11DND_ACTION_LINK);
	CHECK(a == atoms->XdndActionLink, "LINK maps to XdndActionLink");
	CHECK(x11dnd_atom_to_action(dpy, a) == X11DND_ACTION_LINK,
		"XdndActionLink maps back to LINK");

	a = x11dnd_action_to_atom(dpy, X11DND_ACTION_ASK);
	CHECK(a == atoms->XdndActionAsk, "ASK maps to XdndActionAsk");
	CHECK(x11dnd_atom_to_action(dpy, a) == X11DND_ACTION_ASK,
		"XdndActionAsk maps back to ASK");

	a = x11dnd_action_to_atom(dpy, X11DND_ACTION_PRIVATE);
	CHECK(a == atoms->XdndActionPrivate, "PRIVATE maps to XdndActionPrivate");
	CHECK(x11dnd_atom_to_action(dpy, a) == X11DND_ACTION_PRIVATE,
		"XdndActionPrivate maps back to PRIVATE");

	a = x11dnd_action_to_atom(dpy, X11DND_ACTION_DIRECT_SAVE);
	CHECK(a == atoms->XdndDirectSave, "DIRECT_SAVE maps to XdndDirectSave");
	CHECK(x11dnd_atom_to_action(dpy, a) == X11DND_ACTION_DIRECT_SAVE,
		"XdndDirectSave maps back to DIRECT_SAVE");
}

static void
test_negotiate_action(Display *dpy, const X11DndAtoms *atoms)
{
	Atom supported[3];
	Atom result;

	(void)dpy;
	printf("test_negotiate_action\n");

	supported[0] = atoms->XdndActionCopy;
	supported[1] = atoms->XdndActionMove;
	supported[2] = atoms->XdndActionLink;

	result = x11dnd_negotiate_action(atoms->XdndActionCopy,
		supported, 3);
	CHECK(result == atoms->XdndActionCopy,
		"offered Copy in [Copy,Move,Link] -> Copy");

	result = x11dnd_negotiate_action(atoms->XdndActionMove,
		supported, 3);
	CHECK(result == atoms->XdndActionMove,
		"offered Move in [Copy,Move,Link] -> Move");

	result = x11dnd_negotiate_action(atoms->XdndActionLink,
		supported, 3);
	CHECK(result == atoms->XdndActionLink,
		"offered Link in [Copy,Move,Link] -> Link");

	{
		Atom sup2[2];
		sup2[0] = atoms->XdndActionCopy;
		sup2[1] = atoms->XdndActionLink;
		result = x11dnd_negotiate_action(atoms->XdndActionMove,
			sup2, 2);
		CHECK(result == atoms->XdndActionCopy,
			"offered Move not in [Copy,Link] -> fallback Copy");
	}

	result = x11dnd_negotiate_action(atoms->XdndActionAsk,
		supported, 3);
	CHECK(result == atoms->XdndActionAsk,
		"offered Ask -> Ask (triggers callback)");

	result = x11dnd_negotiate_action(atoms->XdndActionPrivate,
		NULL, 0);
	CHECK(result == atoms->XdndActionCopy,
		"offered Private, NULL supported -> fallback Copy");
}

static void
test_action_to_string(Display *dpy, const X11DndAtoms *atoms)
{
	(void)dpy;
	printf("test_action_to_string\n");

	CHECK(strcmp(x11dnd_action_to_string(atoms->XdndActionCopy),
		"XdndActionCopy") == 0, "string(XdndActionCopy)");
	CHECK(strcmp(x11dnd_action_to_string(atoms->XdndActionMove),
		"XdndActionMove") == 0, "string(XdndActionMove)");
	CHECK(strcmp(x11dnd_action_to_string(atoms->XdndActionLink),
		"XdndActionLink") == 0, "string(XdndActionLink)");
	CHECK(strcmp(x11dnd_action_to_string(atoms->XdndActionAsk),
		"XdndActionAsk") == 0, "string(XdndActionAsk)");
	CHECK(strcmp(x11dnd_action_to_string(atoms->XdndActionPrivate),
		"XdndActionPrivate") == 0, "string(XdndActionPrivate)");
	CHECK(strcmp(x11dnd_action_to_string(atoms->XdndDirectSave),
		"XdndActionDirectSave") == 0, "string(XdndDirectSave)");
	CHECK(strcmp(x11dnd_action_to_string(None), "unknown") == 0,
		"string(None) = unknown");
}

static void
test_string_to_action(Display *dpy, const X11DndAtoms *atoms)
{
	Atom a;

	printf("test_string_to_action\n");

	a = x11dnd_string_to_action("XdndActionCopy", dpy);
	CHECK(a == atoms->XdndActionCopy, "parse XdndActionCopy");

	a = x11dnd_string_to_action("XdndActionMove", dpy);
	CHECK(a == atoms->XdndActionMove, "parse XdndActionMove");

	a = x11dnd_string_to_action("XdndActionLink", dpy);
	CHECK(a == atoms->XdndActionLink, "parse XdndActionLink");

	a = x11dnd_string_to_action("XdndActionAsk", dpy);
	CHECK(a == atoms->XdndActionAsk, "parse XdndActionAsk");

	a = x11dnd_string_to_action("XdndActionPrivate", dpy);
	CHECK(a == atoms->XdndActionPrivate, "parse XdndActionPrivate");

	a = x11dnd_string_to_action("XdndDirectSave", dpy);
	CHECK(a == atoms->XdndDirectSave, "parse XdndDirectSave");

	a = x11dnd_string_to_action("NonexistentAtom", dpy);
	CHECK(a == None, "parse nonexistent -> None");

	a = x11dnd_string_to_action(NULL, dpy);
	CHECK(a == None, "parse NULL -> None");
}

static void
test_set_get_action_list(Display *dpy, Window win, const X11DndAtoms *atoms)
{
	Atom actions[3];
	Atom *result;
	int n;
	int rc;

	printf("test_set_get_action_list\n");

	actions[0] = atoms->XdndActionCopy;
	actions[1] = atoms->XdndActionMove;
	actions[2] = atoms->XdndActionLink;

	rc = x11dnd_set_action_list(dpy, win, actions, 3);
	CHECK(rc == 0, "set_action_list returns 0");

	XFlush(dpy);

	result = x11dnd_get_action_list(dpy, win, &n);
	CHECK(result != NULL, "get_action_list returns non-NULL");
	CHECK(n == 3, "get_action_list returns 3 actions");
	if (result != NULL && n == 3) {
		CHECK(result[0] == atoms->XdndActionCopy,
			"action[0] == Copy");
		CHECK(result[1] == atoms->XdndActionMove,
			"action[1] == Move");
		CHECK(result[2] == atoms->XdndActionLink,
			"action[2] == Link");
	}
	if (result) {
		free(result);
	}

	rc = x11dnd_set_action_list(dpy, win, NULL, 3);
	CHECK(rc != 0, "set_action_list NULL actions -> non-zero");

	rc = x11dnd_set_action_list(dpy, win, actions, 0);
	CHECK(rc != 0, "set_action_list n=0 -> non-zero");
}

static void
test_set_get_action_descriptions(Display *dpy, Window win,
	const X11DndAtoms *atoms)
{
	char *descs[3];
	char **result;
	int n;
	int rc;
	int i;

	(void)atoms;
	printf("test_set_get_action_descriptions\n");

	descs[0] = "Copy";
	descs[1] = "Move";
	descs[2] = "Link";

	rc = x11dnd_set_action_descriptions(dpy, win, descs, 3);
	CHECK(rc == 0, "set_action_descriptions returns 0");

	XFlush(dpy);

	result = x11dnd_get_action_descriptions(dpy, win, &n);
	CHECK(result != NULL, "get_action_descriptions returns non-NULL");
	CHECK(n == 3, "get_action_descriptions returns 3 descriptions");
	if (result != NULL && n == 3) {
		CHECK(strcmp(result[0], "Copy") == 0,
			"desc[0] == 'Copy'");
		CHECK(strcmp(result[1], "Move") == 0,
			"desc[1] == 'Move'");
		CHECK(strcmp(result[2], "Link") == 0,
			"desc[2] == 'Link'");
	}
	if (result) {
		for (i = 0; i < n; i++) {
			free(result[i]);
		}
		free(result);
	}

	rc = x11dnd_set_action_descriptions(dpy, win, NULL, 3);
	CHECK(rc != 0, "set_action_descriptions NULL -> non-zero");

	rc = x11dnd_set_action_descriptions(dpy, win, descs, 0);
	CHECK(rc != 0, "set_action_descriptions n=0 -> non-zero");
}

static void
test_default_action_description(Display *dpy, const X11DndAtoms *atoms)
{
	(void)dpy;
	printf("test_default_action_description\n");

	CHECK(strcmp(x11dnd_default_action_description(atoms->XdndActionCopy),
		"Copy") == 0, "default desc Copy");
	CHECK(strcmp(x11dnd_default_action_description(atoms->XdndActionMove),
		"Move") == 0, "default desc Move");
	CHECK(strcmp(x11dnd_default_action_description(atoms->XdndActionLink),
		"Link") == 0, "default desc Link");
	CHECK(strcmp(x11dnd_default_action_description(atoms->XdndActionAsk),
		"Ask") == 0, "default desc Ask");
	CHECK(strcmp(x11dnd_default_action_description(atoms->XdndActionPrivate),
		"Private") == 0, "default desc Private");
	CHECK(strcmp(x11dnd_default_action_description(atoms->XdndDirectSave),
		"Direct Save") == 0, "default desc Direct Save");
	CHECK(strcmp(x11dnd_default_action_description(None),
		"Unknown") == 0, "default desc Unknown");
}

static void
test_get_action_list_missing(Display *dpy, Window win,
	const X11DndAtoms *atoms)
{
	Atom *result;
	int n;

	(void)atoms;
	printf("test_get_action_list_missing\n");

	result = x11dnd_get_action_list(dpy, win, &n);
	CHECK(result == NULL, "get_action_list on no-property -> NULL");
	CHECK(n == 0, "n_actions == 0 on no-property");
}

static void
test_get_action_descriptions_missing(Display *dpy, Window win,
	const X11DndAtoms *atoms)
{
	char **result;
	int n;

	(void)atoms;
	printf("test_get_action_descriptions_missing\n");

	result = x11dnd_get_action_descriptions(dpy, win, &n);
	CHECK(result == NULL, "get_action_descriptions on no-property -> NULL");
	CHECK(n == 0, "n_desc == 0 on no-property");
}

int
main(void)
{
	Display *dpy;
	Window root, win;
	int screen;
	const X11DndAtoms *atoms;

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "Cannot open display — is Xvfb running?\n");
		return 99;
	}

	x11dnd_init_atoms(dpy);
	atoms = x11dnd_get_atoms();
	if (!atoms) {
		fprintf(stderr, "x11dnd_init_atoms failed\n");
		XCloseDisplay(dpy);
		return 99;
	}

	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	win = make_window(dpy, root, 50, 50);
	CHECK(win != None, "test window created");

	test_action_to_atom_roundtrip(dpy, atoms);
	test_negotiate_action(dpy, atoms);
	test_action_to_string(dpy, atoms);
	test_string_to_action(dpy, atoms);
	test_set_get_action_list(dpy, win, atoms);
	test_set_get_action_descriptions(dpy, win, atoms);
	test_default_action_description(dpy, atoms);

	{
		Window win2 = make_window(dpy, root, 50, 50);
		test_get_action_list_missing(dpy, win2, atoms);
		test_get_action_descriptions_missing(dpy, win2, atoms);
		XDestroyWindow(dpy, win2);
	}

	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);

	if (failures == 0) {
		printf("\nALL TESTS PASSED\n");
		return 0;
	}
	printf("\n%d TEST(S) FAILED\n", failures);
	return 1;
}