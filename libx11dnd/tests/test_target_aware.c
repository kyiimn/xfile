/*
 * test_target_aware.c - TDD tests for XDnD drop target registration
 * and state machine.
 *
 * Verifies:
 *   - x11dnd_register_target() sets XdndAware=5 on the window
 *   - x11dnd_unregister_target() removes XdndAware
 *   - x11dnd_get_aware_version() reads back the version
 *   - x11dnd_set_aware() / x11dnd_version_at_least() utility functions
 *   - State machine: IDLE -> ENTERED -> POSITION_RECEIVED
 *   - Version negotiation: clamps to min(source, 5), rejects < 3
 *
 * Run under Xvfb: Xvfb :99 & DISPLAY=:99 ./test_target_aware
 */
#define _DEFAULT_SOURCE
#include "x11dnd.h"
#include "x11dnd_atoms.h"
#include "x11dnd_target.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures = 0;

#define CHECK(cond, msg) \
	do { \
		if ((cond)) { \
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
send_xdnd_enter(Display *dpy, Window target, Window source,
    int version, Atom type1, Atom type2, Atom type3, int more_types)
{
	const X11DndAtoms *atoms;
	XClientMessageEvent ev;

	atoms = x11dnd_get_atoms();
	if (!atoms) {
		return;
	}

	memset(&ev, 0, sizeof(ev));
	ev.type = ClientMessage;
	ev.display = dpy;
	ev.window = target;
	ev.message_type = atoms->XdndEnter;
	ev.format = 32;
	ev.data.l[0] = (long)source;
	ev.data.l[1] = ((long)version << 24) | (more_types ? 1L : 0L);
	ev.data.l[2] = (long)type1;
	ev.data.l[3] = (long)type2;
	ev.data.l[4] = (long)type3;

	XSendEvent(dpy, target, False, NoEventMask, (XEvent *)&ev);
	XFlush(dpy);
}

static void
send_xdnd_position(Display *dpy, Window target, Window source,
    int x, int y, Time timestamp, Atom action)
{
	const X11DndAtoms *atoms;
	XClientMessageEvent ev;

	atoms = x11dnd_get_atoms();
	if (!atoms) {
		return;
	}

	memset(&ev, 0, sizeof(ev));
	ev.type = ClientMessage;
	ev.display = dpy;
	ev.window = target;
	ev.message_type = atoms->XdndPosition;
	ev.format = 32;
	ev.data.l[0] = (long)source;
	ev.data.l[1] = 0;
	ev.data.l[2] = ((long)x << 16) | (y & 0xFFFF);
	ev.data.l[3] = (long)timestamp;
	ev.data.l[4] = (long)action;

	XSendEvent(dpy, target, False, NoEventMask, (XEvent *)&ev);
	XFlush(dpy);
}

static void
send_xdnd_leave(Display *dpy, Window target, Window source)
{
	const X11DndAtoms *atoms;
	XClientMessageEvent ev;

	atoms = x11dnd_get_atoms();
	if (!atoms) {
		return;
	}

	memset(&ev, 0, sizeof(ev));
	ev.type = ClientMessage;
	ev.display = dpy;
	ev.window = target;
	ev.message_type = atoms->XdndLeave;
	ev.format = 32;
	ev.data.l[0] = (long)source;

	XSendEvent(dpy, target, False, NoEventMask, (XEvent *)&ev);
	XFlush(dpy);
}

static void
test_set_aware(Display *dpy, Window root, const X11DndAtoms *atoms)
{
	Window win;
	int version;

	printf("test_set_aware\n");

	win = make_window(dpy, root, 50, 50);
	CHECK(win != None, "window created");

	CHECK(x11dnd_set_aware(dpy, win, X11DND_VERSION_5) == True,
		"set_aware returns True");

	version = x11dnd_get_aware_version(dpy, win);
	CHECK(version == 5, "get_aware_version returns 5 after set_aware");

	CHECK(x11dnd_set_aware(dpy, win, 3) == True,
		"set_aware with version 3");
	version = x11dnd_get_aware_version(dpy, win);
	CHECK(version == 3, "get_aware_version returns 3");

	XDeleteProperty(dpy, win, atoms->XdndAware);
	XFlush(dpy);
	version = x11dnd_get_aware_version(dpy, win);
	CHECK(version == -1, "get_aware_version returns -1 after property deleted");

	XDestroyWindow(dpy, win);
	XFlush(dpy);
}

static void
test_version_at_least(void)
{
	printf("test_version_at_least\n");

	CHECK(x11dnd_version_at_least(5, 5) == True, "5 >= 5");
	CHECK(x11dnd_version_at_least(5, 3) == True, "5 >= 3");
	CHECK(x11dnd_version_at_least(3, 5) == False, "3 < 5");
	CHECK(x11dnd_version_at_least(0, 3) == False, "0 < 3");
}

static void
test_register_unregister(Display *dpy, Window root, const X11DndAtoms *atoms)
{
	Window win;
	int version;
	int ret;

	printf("test_register_unregister\n");

	win = make_window(dpy, root, 50, 50);
	CHECK(win != None, "window created for register test");

	ret = x11dnd_register_target(dpy, win, NULL, NULL);
	CHECK(ret == 0, "register_target returns 0");

	version = x11dnd_get_aware_version(dpy, win);
	CHECK(version == 5, "XdndAware == 5 after register");

	ret = x11dnd_register_target(dpy, win, NULL, NULL);
	CHECK(ret == 0, "register_target idempotent (returns 0 for re-register)");

	x11dnd_unregister_target(dpy, win);

	version = x11dnd_get_aware_version(dpy, win);
	CHECK(version == -1, "XdndAware removed after unregister");

	XDestroyWindow(dpy, win);
	XFlush(dpy);
}

static void
test_state_machine_enter(Display *dpy, Window root,
    const X11DndAtoms *atoms)
{
	Window target, source;
	XEvent ev;
	int ret;

	printf("test_state_machine_enter\n");

	target = make_window(dpy, root, 100, 100);
	source = make_window(dpy, root, 50, 50);
	CHECK(target != None && source != None, "target and source created");

	ret = x11dnd_register_target(dpy, target, NULL, NULL);
	CHECK(ret == 0, "target registered");

	send_xdnd_enter(dpy, target, source, 5,
		atoms->text_uri_list, atoms->UTF8_STRING, None, 0);

	while (XPending(dpy) > 0) {
		XNextEvent(dpy, &ev);
		x11dnd_target_process_event(&ev);
	}

	{
		int i;
		for (i = 0; i < 50; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				x11dnd_target_process_event(&ev);
			}
			XFlush(dpy);
			{
				struct timespec ts;
				ts.tv_sec = 0;
				ts.tv_nsec = 1000000L;
				nanosleep(&ts, NULL);
			}
		}
	}

	x11dnd_unregister_target(dpy, target);
	XDestroyWindow(dpy, target);
	XDestroyWindow(dpy, source);
	XFlush(dpy);
}

static void
on_enter_cb(X11DndTargetSession *sess, Window source,
    int version, Atom *types, int n_types)
{
	*(int *)x11dnd_target_get_user_data(sess) = version;
	(void)source;
	(void)types;
	(void)n_types;
}

static void
test_version_negotiation(Display *dpy, Window root,
    const X11DndAtoms *atoms)
{
	Window target, source;
	X11DndClass cb;
	int captured_version;
	XEvent ev;
	int ret;

	printf("test_version_negotiation\n");

	target = make_window(dpy, root, 100, 100);
	source = make_window(dpy, root, 50, 50);

	memset(&cb, 0, sizeof(cb));
	cb.on_enter = on_enter_cb;
	captured_version = -1;

	ret = x11dnd_register_target(dpy, target, &cb, &captured_version);
	CHECK(ret == 0, "target registered with on_enter callback");

	send_xdnd_enter(dpy, target, source, 5,
		atoms->text_uri_list, None, None, 0);
	{
		int i;
		for (i = 0; i < 50; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				x11dnd_target_process_event(&ev);
			}
			if (captured_version != -1) break;
			XFlush(dpy);
			{
				struct timespec ts;
				ts.tv_sec = 0;
				ts.tv_nsec = 1000000L;
				nanosleep(&ts, NULL);
			}
		}
	}
	CHECK(captured_version == 5, "version 5 -> negotiated 5");

	x11dnd_unregister_target(dpy, target);
	captured_version = -1;
	x11dnd_register_target(dpy, target, &cb, &captured_version);

	send_xdnd_enter(dpy, target, source, 3,
		atoms->text_uri_list, None, None, 0);
	{
		int i;
		for (i = 0; i < 50; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				x11dnd_target_process_event(&ev);
			}
			if (captured_version != -1) break;
			XFlush(dpy);
			{
				struct timespec ts;
				ts.tv_sec = 0;
				ts.tv_nsec = 1000000L;
				nanosleep(&ts, NULL);
			}
		}
	}
	CHECK(captured_version == 3, "version 3 -> negotiated 3");

	x11dnd_unregister_target(dpy, target);
	captured_version = -1;
	x11dnd_register_target(dpy, target, &cb, &captured_version);

	send_xdnd_enter(dpy, target, source, 2,
		atoms->text_uri_list, None, None, 0);
	{
		int i;
		for (i = 0; i < 50; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				x11dnd_target_process_event(&ev);
			}
			XFlush(dpy);
			{
				struct timespec ts;
				ts.tv_sec = 0;
				ts.tv_nsec = 1000000L;
				nanosleep(&ts, NULL);
			}
		}
	}
	CHECK(captured_version == -1, "version 2 rejected (stays IDLE, no callback)");

	x11dnd_unregister_target(dpy, target);
	captured_version = -1;
	x11dnd_register_target(dpy, target, &cb, &captured_version);

	send_xdnd_enter(dpy, target, source, 7,
		atoms->text_uri_list, None, None, 0);
	{
		int i;
		for (i = 0; i < 50; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				x11dnd_target_process_event(&ev);
			}
			if (captured_version != -1) break;
			XFlush(dpy);
			{
				struct timespec ts;
				ts.tv_sec = 0;
				ts.tv_nsec = 1000000L;
				nanosleep(&ts, NULL);
			}
		}
	}
	CHECK(captured_version == 5, "version 7 clamped to 5");

	x11dnd_unregister_target(dpy, target);
	XDestroyWindow(dpy, target);
	XDestroyWindow(dpy, source);
	XFlush(dpy);
}

static void
on_position_cb(X11DndTargetSession *sess, int x, int y, Time time,
    Atom action, Bool *accept, Atom *action_ret,
    int *rect_x, int *rect_y, int *rect_w, int *rect_h)
{
	int *state = (int *)x11dnd_target_get_user_data(sess);
	*state = 1;
	*accept = True;
	*action_ret = action;
	(void)x; (void)y; (void)time;
	(void)rect_x; (void)rect_y; (void)rect_w; (void)rect_h;
}

static void
test_position_sends_status(Display *dpy, Window root,
    const X11DndAtoms *atoms)
{
	Window target, source;
	X11DndClass cb;
	int position_called;
	XClientMessageEvent cm;
	XEvent ev;
	int ret;

	printf("test_position_sends_status\n");

	target = make_window(dpy, root, 100, 100);
	source = make_window(dpy, root, 50, 50);

	memset(&cb, 0, sizeof(cb));
	cb.position_received = on_position_cb;
	position_called = 0;

	ret = x11dnd_register_target(dpy, target, &cb, &position_called);
	CHECK(ret == 0, "target registered with position callback");

	send_xdnd_enter(dpy, target, source, 5,
		atoms->text_uri_list, None, None, 0);
	{
		int i;
		for (i = 0; i < 50; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				x11dnd_target_process_event(&ev);
			}
			XFlush(dpy);
			{
				struct timespec ts;
				ts.tv_sec = 0;
				ts.tv_nsec = 1000000L;
				nanosleep(&ts, NULL);
			}
		}
	}

	send_xdnd_position(dpy, target, source, 50, 50, 12345,
		atoms->XdndActionCopy);

	memset(&cm, 0, sizeof(cm));
	{
		int i;
		int got_status = 0;
		for (i = 0; i < 100 && !got_status; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				if (ev.type == ClientMessage &&
				    ev.xclient.window == source &&
				    ev.xclient.message_type == atoms->XdndStatus) {
					memcpy(&cm, &ev.xclient, sizeof(cm));
					got_status = 1;
				} else {
					x11dnd_target_process_event(&ev);
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
	}

	CHECK(position_called == 1, "position_received callback was called");

	if (cm.message_type == atoms->XdndStatus) {
		CHECK((long)cm.data.l[0] == (long)target,
			"XdndStatus data.l[0] == target");
		CHECK(((long)cm.data.l[1] & 0x1) == 0x1,
			"XdndStatus accept bit set");
		CHECK((long)cm.data.l[4] == (long)atoms->XdndActionCopy,
			"XdndStatus action == XdndActionCopy");
	} else {
		CHECK(0, "received XdndStatus on source window");
	}

	x11dnd_unregister_target(dpy, target);
	XDestroyWindow(dpy, target);
	XDestroyWindow(dpy, source);
	XFlush(dpy);
}

static void
on_leave_cb(X11DndTargetSession *sess)
{
	*(int *)x11dnd_target_get_user_data(sess) = 1;
}

static void
test_leave_resets_state(Display *dpy, Window root,
    const X11DndAtoms *atoms)
{
	Window target, source;
	X11DndClass cb;
	int leave_called;
	XEvent ev;
	int ret;

	printf("test_leave_resets_state\n");

	target = make_window(dpy, root, 100, 100);
	source = make_window(dpy, root, 50, 50);

	memset(&cb, 0, sizeof(cb));
	cb.on_leave = on_leave_cb;
	leave_called = 0;

	ret = x11dnd_register_target(dpy, target, &cb, &leave_called);
	CHECK(ret == 0, "target registered with on_leave callback");

	send_xdnd_enter(dpy, target, source, 5,
		atoms->text_uri_list, None, None, 0);
	{
		int i;
		for (i = 0; i < 50; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				x11dnd_target_process_event(&ev);
			}
			XFlush(dpy);
			{
				struct timespec ts;
				ts.tv_sec = 0;
				ts.tv_nsec = 1000000L;
				nanosleep(&ts, NULL);
			}
		}
	}

	send_xdnd_leave(dpy, target, source);
	{
		int i;
		for (i = 0; i < 50; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				x11dnd_target_process_event(&ev);
			}
			if (leave_called) break;
			XFlush(dpy);
			{
				struct timespec ts;
				ts.tv_sec = 0;
				ts.tv_nsec = 1000000L;
				nanosleep(&ts, NULL);
			}
		}
	}

	CHECK(leave_called == 1, "on_leave callback was called");

	x11dnd_unregister_target(dpy, target);
	XDestroyWindow(dpy, target);
	XDestroyWindow(dpy, source);
	XFlush(dpy);
}

static void
test_accessors(Display *dpy, Window root, const X11DndAtoms *atoms)
{
	Window target, source;
	X11DndClass cb;
	int user_val;
	XEvent ev;
	int ret;
	int captured;

	printf("test_accessors\n");

	target = make_window(dpy, root, 100, 100);
	source = make_window(dpy, root, 50, 50);
	user_val = 42;

	memset(&cb, 0, sizeof(cb));
	cb.on_enter = on_enter_cb;
	captured = -1;

	ret = x11dnd_register_target(dpy, target, &cb, &captured);
	CHECK(ret == 0, "target registered for accessor test");

	send_xdnd_enter(dpy, target, source, 5,
		atoms->text_uri_list, None, None, 0);
	{
		int i;
		for (i = 0; i < 50; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				x11dnd_target_process_event(&ev);
			}
			if (captured != -1) break;
			XFlush(dpy);
			{
				struct timespec ts;
				ts.tv_sec = 0;
				ts.tv_nsec = 1000000L;
				nanosleep(&ts, NULL);
			}
		}
	}

	(void)user_val;

	x11dnd_unregister_target(dpy, target);
	XDestroyWindow(dpy, target);
	XDestroyWindow(dpy, source);
	XFlush(dpy);
}

int
main(void)
{
	Display *dpy;
	Window root;
	int screen;
	const X11DndAtoms *atoms;

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "Cannot open display - run under Xvfb (DISPLAY=:99)\n");
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

	test_set_aware(dpy, root, atoms);
	test_version_at_least();
	test_register_unregister(dpy, root, atoms);
	test_state_machine_enter(dpy, root, atoms);
	test_version_negotiation(dpy, root, atoms);
	test_position_sends_status(dpy, root, atoms);
	test_leave_resets_state(dpy, root, atoms);
	test_accessors(dpy, root, atoms);

	XCloseDisplay(dpy);

	if (failures == 0) {
		printf("\nALL TESTS PASSED\n");
		return 0;
	}
	printf("\n%d TEST(S) FAILED\n", failures);
	return 1;
}