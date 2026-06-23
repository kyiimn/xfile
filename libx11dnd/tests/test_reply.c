/*
 * test_reply.c - TDD tests for XdndStatus and XdndFinished reply builders.
 *
 * Verifies the wire format of the two XDnD v5 messages a drop target
 * sends back to a drag source. Captures the ClientMessage events via
 * XSendEvent to a window selected for SubstructureNotifyMask.
 *
 * Run under Xvfb: Xvfb :99 & DISPLAY=:99 ./test_reply
 */
#define _DEFAULT_SOURCE
#include "x11dnd.h"
#include "x11dnd_atoms.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/*
 * Wait for a ClientMessage event on 'win' with the given message_type
 * atom. Returns 0 on success (fills *out), non-zero on timeout/no-match.
 *
 * XSendEvent with NoEventMask delivers ClientMessage events to the
 * destination window's event queue regardless of its event mask
 * (ClientMessage is not selectable via XSelectInput in the normal
 * sense — XSendEvent forces delivery). We poll XPending/XNextEvent.
 */
static int
recv_client_message(Display *dpy, Window win, Atom type,
    XClientMessageEvent *out)
{
	XEvent ev;
	int i;

	for (i = 0; i < 100; i++) {
		while (XPending(dpy) > 0) {
			XNextEvent(dpy, &ev);
			if (ev.type == ClientMessage &&
			    ev.xclient.window == win &&
			    ev.xclient.message_type == type) {
				memcpy(out, &ev.xclient, sizeof(*out));
				return 0;
			}
		}
		XFlush(dpy);
		{
			struct timespec ts;
			ts.tv_sec = 0;
			ts.tv_nsec = 1000000L;
			nanosleep(&ts, NULL);
		}
	}
	return 1;
}

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

/*
 * Test 1: XdndStatus with accept=True, no rectangle.
 * Expected: bit 0 set (accept), bit 1 set (whole window),
 *           data.l[4] = XdndActionCopy.
 */
static void
test_status_accept_no_rect(Display *dpy, Window source, Window target,
    const X11DndAtoms *atoms)
{
	XClientMessageEvent cm;

	printf("test_status_accept_no_rect\n");

	x11dnd_send_status(dpy, source, target, True, 0, 0, 0, 0,
	    atoms->XdndActionCopy);

	if (recv_client_message(dpy, source, atoms->XdndStatus, &cm) != 0) {
		CHECK(0, "received XdndStatus ClientMessage");
		return;
	}

	CHECK((long)cm.data.l[0] == (long)target,
		"data.l[0] == target window");
	CHECK(((long)cm.data.l[1] & 0x1) == 0x1,
		"data.l[1] bit 0 set (accept)");
	CHECK(((long)cm.data.l[1] & 0x2) == 0x2,
		"data.l[1] bit 1 set (whole window, no rect)");
	CHECK((long)cm.data.l[2] == 0,
		"data.l[2] == 0 (no rect x/y)");
	CHECK((long)cm.data.l[3] == 0,
		"data.l[3] == 0 (no rect w/h)");
	CHECK((long)cm.data.l[4] == (long)atoms->XdndActionCopy,
		"data.l[4] == XdndActionCopy");
}

/*
 * Test 2: XdndStatus with accept=True and a rectangle (100,100,200,200).
 * Expected: bit 0 set, bit 1 CLEAR (rectangle mode),
 *           data.l[2] = 100<<16|100, data.l[3] = 200<<16|200.
 */
static void
test_status_accept_with_rect(Display *dpy, Window source, Window target,
    const X11DndAtoms *atoms)
{
	XClientMessageEvent cm;

	printf("test_status_accept_with_rect\n");

	x11dnd_send_status(dpy, source, target, True,
	    100, 100, 200, 200, atoms->XdndActionCopy);

	if (recv_client_message(dpy, source, atoms->XdndStatus, &cm) != 0) {
		CHECK(0, "received XdndStatus ClientMessage");
		return;
	}

	CHECK((long)cm.data.l[0] == (long)target,
		"data.l[0] == target window");
	CHECK(((long)cm.data.l[1] & 0x1) == 0x1,
		"data.l[1] bit 0 set (accept)");
	CHECK(((long)cm.data.l[1] & 0x2) == 0x0,
		"data.l[1] bit 1 clear (rectangle mode)");
	CHECK((long)cm.data.l[2] == ((100L << 16) | 100),
		"data.l[2] == 100<<16|100");
	CHECK((long)cm.data.l[3] == ((200L << 16) | 200),
		"data.l[3] == 200<<16|200");
	CHECK((long)cm.data.l[4] == (long)atoms->XdndActionCopy,
		"data.l[4] == XdndActionCopy");
}

/*
 * Test 3: XdndStatus with accept=False.
 * Expected: bit 0 clear, bit 1 clear, data.l[4] = None.
 */
static void
test_status_reject(Display *dpy, Window source, Window target,
    const X11DndAtoms *atoms)
{
	XClientMessageEvent cm;

	printf("test_status_reject\n");

	x11dnd_send_status(dpy, source, target, False, 0, 0, 0, 0, None);

	if (recv_client_message(dpy, source, atoms->XdndStatus, &cm) != 0) {
		CHECK(0, "received XdndStatus ClientMessage");
		return;
	}

	CHECK((long)cm.data.l[0] == (long)target,
		"data.l[0] == target window");
	CHECK(((long)cm.data.l[1] & 0x1) == 0x0,
		"data.l[1] bit 0 clear (reject)");
	CHECK(((long)cm.data.l[1] & 0x2) == 0x0,
		"data.l[1] bit 1 clear (no whole-window)");
	CHECK((long)cm.data.l[4] == 0,
		"data.l[4] == None (no action)");
}

/*
 * Test 4: XdndFinished with success=True, action=XdndActionCopy (v5).
 * Expected: data.l[0]=target, data.l[1]=1, data.l[2]=XdndActionCopy,
 *           data.l[3]=0, data.l[4]=0.
 */
static void
test_finished_success_v5(Display *dpy, Window source, Window target,
    const X11DndAtoms *atoms)
{
	XClientMessageEvent cm;

	printf("test_finished_success_v5\n");

	x11dnd_send_finished(dpy, source, target, True, atoms->XdndActionCopy);

	if (recv_client_message(dpy, source, atoms->XdndFinished, &cm) != 0) {
		CHECK(0, "received XdndFinished ClientMessage");
		return;
	}

	CHECK((long)cm.data.l[0] == (long)target,
		"data.l[0] == target window");
	CHECK((long)cm.data.l[1] == 1,
		"data.l[1] == 1 (success)");
	CHECK((long)cm.data.l[2] == (long)atoms->XdndActionCopy,
		"data.l[2] == XdndActionCopy (v5 action)");
	CHECK((long)cm.data.l[3] == 0,
		"data.l[3] == 0 (reserved)");
	CHECK((long)cm.data.l[4] == 0,
		"data.l[4] == 0 (reserved)");
}

/*
 * Test 5: XdndFinished with success=False, action=None (v4-style).
 * Expected: data.l[0]=target, data.l[1]=0, data.l[2]=0 (None).
 */
static void
test_finished_failure_v4(Display *dpy, Window source, Window target,
    const X11DndAtoms *atoms)
{
	XClientMessageEvent cm;

	printf("test_finished_failure_v4\n");

	x11dnd_send_finished(dpy, source, target, False, None);

	if (recv_client_message(dpy, source, atoms->XdndFinished, &cm) != 0) {
		CHECK(0, "received XdndFinished ClientMessage");
		return;
	}

	CHECK((long)cm.data.l[0] == (long)target,
		"data.l[0] == target window");
	CHECK((long)cm.data.l[1] == 0,
		"data.l[1] == 0 (failure)");
	CHECK((long)cm.data.l[2] == 0,
		"data.l[2] == 0 (None, v4 no action)");
	CHECK((long)cm.data.l[3] == 0,
		"data.l[3] == 0 (reserved)");
	CHECK((long)cm.data.l[4] == 0,
		"data.l[4] == 0 (reserved)");
}

int
main(void)
{
	Display *dpy;
	Window root, source, target;
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

	source = make_window(dpy, root, 50, 50);
	target = make_window(dpy, root, 50, 50);
	CHECK(source != None && target != None,
		"source and target windows created");

	test_status_accept_no_rect(dpy, source, target, atoms);
	test_status_accept_with_rect(dpy, source, target, atoms);
	test_status_reject(dpy, source, target, atoms);
	test_finished_success_v5(dpy, source, target, atoms);
	test_finished_failure_v4(dpy, source, target, atoms);

	XDestroyWindow(dpy, source);
	XDestroyWindow(dpy, target);
	XCloseDisplay(dpy);

	if (failures == 0) {
		printf("\nALL TESTS PASSED\n");
		return 0;
	}
	printf("\n%d TEST(S) FAILED\n", failures);
	return 1;
}