/*
 * test_util.c - TDD tests for x11dnd_util functions.
 * Run under Xvfb: Xvfb :99 & DISPLAY=:99 ./test_util
 */
#include "x11dnd_util.h"

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

static Atom get_atom(Display *dpy, const char *name)
{
	return XInternAtom(dpy, name, False);
}

static void
test_find_window_at_point(Display *dpy)
{
	Window root;
	Window parent, child;
	XSetWindowAttributes attrs;
	unsigned long valuemask;
	int screen;
	Atom aware;

	printf("test_find_window_at_point\n");

	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	attrs.override_redirect = True;
	attrs.event_mask = 0;
	valuemask = CWOverrideRedirect | CWEventMask;

	parent = XCreateWindow(dpy, root, 0, 0, 200, 200, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		valuemask, &attrs);
	CHECK(parent != None, "parent window created");

	child = XCreateWindow(dpy, parent, 50, 50, 100, 100, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		valuemask, &attrs);
	CHECK(child != None, "child window created");

	XMapWindow(dpy, parent);
	XMapWindow(dpy, child);
	XFlush(dpy);
	XSync(dpy, False);

	{
		Window found;
		found = x11dnd_find_window_at_point(dpy, root, 100, 100);
		CHECK(found == child, "find_window_at_point finds child at (100,100)");
	}

	{
		Window found;
		found = x11dnd_find_window_at_point(dpy, root, 10, 10);
		CHECK(found == parent, "find_window_at_point finds parent at (10,10)");
	}

	aware = get_atom(dpy, "XdndAware");
	{
		long ver = 5;
		XChangeProperty(dpy, child, aware, XA_ATOM, 32,
			PropModeReplace, (unsigned char *)&ver, 1);
		XFlush(dpy);
	}

	{
		Window ancestor;
		ancestor = x11dnd_find_aware_ancestor(dpy, child);
		CHECK(ancestor == child, "find_aware_ancestor finds child with XdndAware");
	}

	{
		Window ancestor;
		ancestor = x11dnd_find_aware_ancestor(dpy, parent);
		CHECK(ancestor == None, "find_aware_ancestor returns None for no XdndAware");
	}

	XDestroyWindow(dpy, parent);
	XFlush(dpy);
}

static void
test_get_window_property(Display *dpy)
{
	Window root, win;
	XSetWindowAttributes attrs;
	unsigned long valuemask;
	int screen;
	Atom test_prop, test_type;
	unsigned long nitems;
	unsigned char *data;
	long value;

	printf("test_get_window_property\n");

	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	attrs.override_redirect = True;
	attrs.event_mask = 0;
	valuemask = CWOverrideRedirect | CWEventMask;

	win = XCreateWindow(dpy, root, 0, 0, 50, 50, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		valuemask, &attrs);
	CHECK(win != None, "window created for property test");

	test_prop = get_atom(dpy, "X11DND_TEST_PROP");
	test_type = XA_INTEGER;
	value = 42;

	XChangeProperty(dpy, win, test_prop, test_type, 32,
		PropModeReplace, (unsigned char *)&value, 1);
	XFlush(dpy);

	data = NULL;
	nitems = 0;
	{
		int ret = x11dnd_get_window_property(dpy, win, test_prop, test_type,
			32, &nitems, &data);
		CHECK(ret == 0, "get_window_property succeeds on existing property");
		CHECK(nitems == 1, "get_window_property returns 1 item");
		CHECK(data != NULL, "get_window_property returns non-NULL data");
		if (data) {
			CHECK(((long *)data)[0] == 42, "get_window_property returns correct value");
			free(data);
		}
	}

	{
		Atom bogus = get_atom(dpy, "X11DND_NONEXISTENT_PROP");
		int ret = x11dnd_get_window_property(dpy, win, bogus, test_type,
			32, &nitems, &data);
		CHECK(ret != 0, "get_window_property fails on nonexistent property");
		CHECK(data == NULL, "get_window_property sets data=NULL on failure");
	}

	XDestroyWindow(dpy, win);
	XFlush(dpy);
}

static void
test_validate_proxy(Display *dpy)
{
	Window root, win_a, win_b, win_c;
	XSetWindowAttributes attrs;
	unsigned long valuemask;
	int screen;
	Atom proxy_atom;
	Window result;

	printf("test_validate_proxy\n");

	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	attrs.override_redirect = True;
	attrs.event_mask = 0;
	valuemask = CWOverrideRedirect | CWEventMask;

	win_a = XCreateWindow(dpy, root, 0, 0, 50, 50, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		valuemask, &attrs);
	win_b = XCreateWindow(dpy, root, 0, 0, 50, 50, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		valuemask, &attrs);
	win_c = XCreateWindow(dpy, root, 0, 0, 50, 50, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		valuemask, &attrs);
	CHECK(win_a != None && win_b != None && win_c != None,
		"three windows created for proxy test");

	proxy_atom = get_atom(dpy, "XdndProxy");

	{
		Window self_ref = win_b;
		XChangeProperty(dpy, win_a, proxy_atom, XA_WINDOW, 32,
			PropModeReplace, (unsigned char *)&self_ref, 1);
		XChangeProperty(dpy, win_b, proxy_atom, XA_WINDOW, 32,
			PropModeReplace, (unsigned char *)&self_ref, 1);
		XFlush(dpy);

		result = x11dnd_validate_proxy(dpy, win_a);
		CHECK(result == win_b, "validate_proxy returns B when A->B and B->B (self-ref)");
	}

	{
		Window bad_ref = win_c;
		XChangeProperty(dpy, win_b, proxy_atom, XA_WINDOW, 32,
			PropModeReplace, (unsigned char *)&bad_ref, 1);
		XFlush(dpy);

		result = x11dnd_validate_proxy(dpy, win_a);
		CHECK(result == None, "validate_proxy returns None when B->C (not self-ref)");
	}

	{
		result = x11dnd_validate_proxy(dpy, win_c);
		CHECK(result == None, "validate_proxy returns None when no XdndProxy set");
	}

	XDestroyWindow(dpy, win_a);
	XDestroyWindow(dpy, win_b);
	XDestroyWindow(dpy, win_c);
	XFlush(dpy);
}

static void
test_window_exists_and_root(Display *dpy)
{
	Window root, win;
	XSetWindowAttributes attrs;
	unsigned long valuemask;
	int screen;
	Window found_root;

	printf("test_window_exists_and_root\n");

	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	attrs.override_redirect = True;
	attrs.event_mask = 0;
	valuemask = CWOverrideRedirect | CWEventMask;

	win = XCreateWindow(dpy, root, 0, 0, 50, 50, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		valuemask, &attrs);
	CHECK(win != None, "window created for exists test");

	CHECK(x11dnd_window_exists(dpy, win) == True,
		"window_exists returns True for valid window");

	{
		Window fake = 0x12345678;
		Bool exists = x11dnd_window_exists(dpy, fake);
		CHECK(exists == False, "window_exists returns False for invalid window");
	}

	found_root = x11dnd_get_root_window(dpy, win);
	CHECK(found_root == root, "get_root_window returns correct root");

	XDestroyWindow(dpy, win);
	XFlush(dpy);
}

int
main(void)
{
	Display *dpy;

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "Cannot open display — is Xvfb running?\n");
		return 99;
	}

	test_find_window_at_point(dpy);
	test_get_window_property(dpy);
	test_validate_proxy(dpy);
	test_window_exists_and_root(dpy);

	XCloseDisplay(dpy);

	if (failures == 0) {
		printf("\nALL TESTS PASSED\n");
		return 0;
	}
	printf("\n%d TEST(S) FAILED\n", failures);
	return 1;
}