/*
 * test_proxy.c - TDD tests for x11dnd_proxy functions.
 * Run under Xvfb: Xvfb :99 & DISPLAY=:99 ./test_proxy
 */
#include "x11dnd_proxy.h"
#include "x11dnd_util.h"
#include "x11dnd_atoms.h"

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
create_window(Display *dpy, Window parent, int x, int y, int w, int h)
{
	XSetWindowAttributes attrs;
	unsigned long valuemask;

	attrs.override_redirect = True;
	attrs.event_mask = 0;
	valuemask = CWOverrideRedirect | CWEventMask;

	return XCreateWindow(dpy, parent, x, y, w, h, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		valuemask, &attrs);
}

static void
set_xdnd_aware(Display *dpy, Window win, int version)
{
	Atom aware = XInternAtom(dpy, "XdndAware", False);
	long val = (long)version;
	XChangeProperty(dpy, win, aware, XA_ATOM, 32,
		PropModeReplace, (unsigned char *)&val, 1);
	XFlush(dpy);
}

static void
set_xdnd_proxy(Display *dpy, Window win, Window target)
{
	Atom proxy = XInternAtom(dpy, "XdndProxy", False);
	XChangeProperty(dpy, win, proxy, XA_WINDOW, 32,
		PropModeReplace, (unsigned char *)&target, 1);
	XFlush(dpy);
}

static Window
read_xdnd_proxy(Display *dpy, Window win)
{
	Atom proxy = XInternAtom(dpy, "XdndProxy", True);
	unsigned long nitems;
	unsigned char *data;
	Window result = None;

	if (proxy == None) {
		return None;
	}

	data = NULL;
	if (x11dnd_get_window_property(dpy, win, proxy, XA_WINDOW, 32,
		&nitems, &data) == 0) {
		if (nitems >= 1 && data != NULL) {
			result = ((Window *)data)[0];
		}
		if (data) {
			free(data);
		}
	}
	return result;
}

static void
test_proxy_check_valid(Display *dpy)
{
	Window root, win_a, win_b;
	Window result;

	printf("test_proxy_check_valid\n");

	root = RootWindow(dpy, DefaultScreen(dpy));
	win_a = create_window(dpy, root, 0, 0, 50, 50);
	win_b = create_window(dpy, root, 0, 0, 50, 50);
	CHECK(win_a != None && win_b != None, "windows created");

	set_xdnd_proxy(dpy, win_a, win_b);
	set_xdnd_proxy(dpy, win_b, win_b);

	result = x11dnd_proxy_check(dpy, win_a);
	CHECK(result == win_b, "proxy_check returns B when A->B and B->B");

	XDestroyWindow(dpy, win_a);
	XDestroyWindow(dpy, win_b);
	XFlush(dpy);
}

static void
test_proxy_check_invalid(Display *dpy)
{
	Window root, win_a, win_b, win_c;
	Window result;

	printf("test_proxy_check_invalid\n");

	root = RootWindow(dpy, DefaultScreen(dpy));
	win_a = create_window(dpy, root, 0, 0, 50, 50);
	win_b = create_window(dpy, root, 0, 0, 50, 50);
	win_c = create_window(dpy, root, 0, 0, 50, 50);
	CHECK(win_a != None && win_b != None && win_c != None,
		"three windows created");

	set_xdnd_proxy(dpy, win_a, win_b);
	set_xdnd_proxy(dpy, win_b, win_c);

	result = x11dnd_proxy_check(dpy, win_a);
	CHECK(result == None, "proxy_check returns None when B->C (not self-ref)");

	XDestroyWindow(dpy, win_a);
	XDestroyWindow(dpy, win_b);
	XDestroyWindow(dpy, win_c);
	XFlush(dpy);
}

static void
test_proxy_check_no_proxy(Display *dpy)
{
	Window root, win;
	Window result;

	printf("test_proxy_check_no_proxy\n");

	root = RootWindow(dpy, DefaultScreen(dpy));
	win = create_window(dpy, root, 0, 0, 50, 50);
	CHECK(win != None, "window created");

	result = x11dnd_proxy_check(dpy, win);
	CHECK(result == None, "proxy_check returns None when no XdndProxy set");

	XDestroyWindow(dpy, win);
	XFlush(dpy);
}

static void
test_proxy_set(Display *dpy)
{
	Window root, win_a, win_b;
	Window proxy_on_a, proxy_on_b;
	int ret;

	printf("test_proxy_set\n");

	root = RootWindow(dpy, DefaultScreen(dpy));
	win_a = create_window(dpy, root, 0, 0, 50, 50);
	win_b = create_window(dpy, root, 0, 0, 50, 50);
	CHECK(win_a != None && win_b != None, "windows created");

	ret = x11dnd_proxy_set(dpy, win_a, win_b);
	CHECK(ret == 0, "proxy_set returns 0 on success");

	proxy_on_a = read_xdnd_proxy(dpy, win_a);
	CHECK(proxy_on_a == win_b, "XdndProxy on A points to B");

	proxy_on_b = read_xdnd_proxy(dpy, win_b);
	CHECK(proxy_on_b == win_b, "XdndProxy on B points to B (self-ref)");

	{
		Window result = x11dnd_proxy_check(dpy, win_a);
		CHECK(result == win_b, "proxy_check returns B after proxy_set");
	}

	XDestroyWindow(dpy, win_a);
	XDestroyWindow(dpy, win_b);
	XFlush(dpy);
}

static void
test_proxy_unset(Display *dpy)
{
	Window root, win_a, win_b;
	Window result;

	printf("test_proxy_unset\n");

	root = RootWindow(dpy, DefaultScreen(dpy));
	win_a = create_window(dpy, root, 0, 0, 50, 50);
	win_b = create_window(dpy, root, 0, 0, 50, 50);
	CHECK(win_a != None && win_b != None, "windows created");

	x11dnd_proxy_set(dpy, win_a, win_b);

	x11dnd_proxy_unset(dpy, win_a);

	result = read_xdnd_proxy(dpy, win_a);
	CHECK(result == None, "XdndProxy removed from A after unset");

	result = x11dnd_proxy_check(dpy, win_a);
	CHECK(result == None, "proxy_check returns None after unset");

	XDestroyWindow(dpy, win_a);
	XDestroyWindow(dpy, win_b);
	XFlush(dpy);
}

static void
test_proxy_find_target_aware(Display *dpy)
{
	Window root, parent, child;
	Window result;

	printf("test_proxy_find_target_aware\n");

	root = RootWindow(dpy, DefaultScreen(dpy));
	parent = create_window(dpy, root, 0, 0, 200, 200);
	child = create_window(dpy, parent, 50, 50, 100, 100);
	CHECK(parent != None && child != None, "parent and child created");

	XMapWindow(dpy, parent);
	XMapWindow(dpy, child);
	XFlush(dpy);
	XSync(dpy, False);

	set_xdnd_aware(dpy, parent, 5);

	result = x11dnd_proxy_find_target(dpy, child);
	CHECK(result == parent, "find_target returns parent with XdndAware");

	XDestroyWindow(dpy, parent);
	XFlush(dpy);
}

static void
test_proxy_find_target_via_proxy(Display *dpy)
{
	Window root, parent, child, proxy_win;
	Window result;

	printf("test_proxy_find_target_via_proxy\n");

	root = RootWindow(dpy, DefaultScreen(dpy));
	parent = create_window(dpy, root, 0, 0, 200, 200);
	child = create_window(dpy, parent, 50, 50, 100, 100);
	proxy_win = create_window(dpy, root, 0, 0, 50, 50);
	CHECK(parent != None && child != None && proxy_win != None,
		"parent, child, proxy_win created");

	XMapWindow(dpy, parent);
	XMapWindow(dpy, child);
	XFlush(dpy);
	XSync(dpy, False);

	set_xdnd_aware(dpy, parent, 5);
	set_xdnd_aware(dpy, proxy_win, 5);

	x11dnd_proxy_set(dpy, parent, proxy_win);

	result = x11dnd_proxy_find_target(dpy, child);
	CHECK(result == proxy_win,
		"find_target returns proxy_win when parent has valid proxy with XdndAware");

	XDestroyWindow(dpy, parent);
	XDestroyWindow(dpy, proxy_win);
	XFlush(dpy);
}

static void
test_proxy_find_target_no_aware(Display *dpy)
{
	Window root, win;
	Window result;

	printf("test_proxy_find_target_no_aware\n");

	root = RootWindow(dpy, DefaultScreen(dpy));
	win = create_window(dpy, root, 0, 0, 50, 50);
	CHECK(win != None, "window created");

	result = x11dnd_proxy_find_target(dpy, win);
	CHECK(result == None, "find_target returns None when no XdndAware anywhere");

	XDestroyWindow(dpy, win);
	XFlush(dpy);
}

static void
test_proxy_find_target_proxy_no_aware(Display *dpy)
{
	Window root, parent, child, proxy_win;
	Window result;

	printf("test_proxy_find_target_proxy_no_aware\n");

	root = RootWindow(dpy, DefaultScreen(dpy));
	parent = create_window(dpy, root, 0, 0, 200, 200);
	child = create_window(dpy, parent, 50, 50, 100, 100);
	proxy_win = create_window(dpy, root, 0, 0, 50, 50);
	CHECK(parent != None && child != None && proxy_win != None,
		"windows created");

	XMapWindow(dpy, parent);
	XMapWindow(dpy, child);
	XFlush(dpy);
	XSync(dpy, False);

	set_xdnd_aware(dpy, parent, 5);

	x11dnd_proxy_set(dpy, parent, proxy_win);

	result = x11dnd_proxy_find_target(dpy, child);
	CHECK(result == parent,
		"find_target falls back to parent when proxy has no XdndAware");

	XDestroyWindow(dpy, parent);
	XDestroyWindow(dpy, proxy_win);
	XFlush(dpy);
}

static void
test_proxy_stale(Display *dpy)
{
	Window root, win_a, win_b;
	Window result;

	printf("test_proxy_stale\n");

	root = RootWindow(dpy, DefaultScreen(dpy));
	win_a = create_window(dpy, root, 0, 0, 50, 50);
	win_b = create_window(dpy, root, 0, 0, 50, 50);
	CHECK(win_a != None && win_b != None, "windows created");

	x11dnd_proxy_set(dpy, win_a, win_b);

	XDestroyWindow(dpy, win_b);
	XFlush(dpy);
	XSync(dpy, False);

	result = x11dnd_proxy_check(dpy, win_a);
	CHECK(result == None, "proxy_check returns None for stale proxy (B destroyed)");

	XDestroyWindow(dpy, win_a);
	XFlush(dpy);
}

static void
test_proxy_check_null_args(void)
{
	Window result;
	int ret;

	printf("test_proxy_check_null_args\n");

	result = x11dnd_proxy_check(NULL, 12345);
	CHECK(result == None, "proxy_check returns None for NULL display");

	result = x11dnd_proxy_check(NULL, None);
	CHECK(result == None, "proxy_check returns None for NULL display + None window");

	ret = x11dnd_proxy_set(NULL, 12345, 67890);
	CHECK(ret != 0, "proxy_set returns non-zero for NULL display");

	x11dnd_proxy_unset(NULL, 12345);

	result = x11dnd_proxy_find_target(NULL, 12345);
	CHECK(result == None, "find_target returns None for NULL display");
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

	x11dnd_init_atoms(dpy);

	test_proxy_check_valid(dpy);
	test_proxy_check_invalid(dpy);
	test_proxy_check_no_proxy(dpy);
	test_proxy_set(dpy);
	test_proxy_unset(dpy);
	test_proxy_find_target_aware(dpy);
	test_proxy_find_target_via_proxy(dpy);
	test_proxy_find_target_no_aware(dpy);
	test_proxy_find_target_proxy_no_aware(dpy);
	test_proxy_stale(dpy);
	test_proxy_check_null_args();

	XCloseDisplay(dpy);

	if (failures == 0) {
		printf("\nALL TESTS PASSED\n");
		return 0;
	}
	printf("\n%d TEST(S) FAILED\n", failures);
	return 1;
}