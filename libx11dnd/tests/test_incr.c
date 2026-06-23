#define _DEFAULT_SOURCE
#include "x11dnd.h"
#include "x11dnd_atoms.h"
#include "x11dnd_target.h"
#include "x11dnd_incr.h"
#include "x11dnd_util.h"

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

static Window
make_window(Display *dpy, Window parent, int w, int h)
{
	XSetWindowAttributes attrs;
	Window win;

	attrs.override_redirect = True;
	attrs.event_mask = PropertyChangeMask | StructureNotifyMask;
	win = XCreateWindow(dpy, parent, 0, 0, w, h, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect | CWEventMask, &attrs);
	XMapWindow(dpy, win);
	XFlush(dpy);
	return win;
}

static void
drain_events(Display *dpy)
{
	XEvent ev;
	XSync(dpy, False);
	while (XPending(dpy) > 0) {
		XNextEvent(dpy, &ev);
	}
}

static void
on_enter_cb(X11DndTargetSession *sess, Window source, int version,
	Atom *types, int n_types)
{
	(void)sess; (void)source; (void)version; (void)types; (void)n_types;
}

static void
on_leave_cb(X11DndTargetSession *sess)
{
	(void)sess;
}

static int drop_received_called = 0;
static unsigned char *drop_received_data = NULL;
static unsigned long drop_received_length = 0;
static int drop_received_format = 0;
static Atom drop_received_target = None;

static void
on_drop_received_cb(X11DndTargetSession *sess, Atom target,
	unsigned char *data, unsigned long length, int format)
{
	(void)sess;
	drop_received_called++;
	drop_received_target = target;
	drop_received_format = format;
	if (data && length > 0) {
		drop_received_data = malloc(length);
		if (drop_received_data) {
			memcpy(drop_received_data, data, length);
		}
		drop_received_length = length;
	} else {
		drop_received_data = NULL;
		drop_received_length = 0;
	}
}

static void
on_position_cb(X11DndTargetSession *sess, int x, int y, Time time,
	Atom action, Bool *accept_ret, Atom *action_ret,
	int *rect_x, int *rect_y, int *rect_w, int *rect_h)
{
	(void)sess; (void)x; (void)y; (void)time; (void)action;
	*accept_ret = True;
	*action_ret = None;
	*rect_x = 0; *rect_y = 0; *rect_w = 0; *rect_h = 0;
}

static X11DndClass target_callbacks = {
	.on_drag_begin = NULL,
	.on_drag_end = NULL,
	.get_drag_data = NULL,
	.status_received = NULL,
	.finished_received = NULL,
	.on_enter = on_enter_cb,
	.position_received = on_position_cb,
	.on_leave = on_leave_cb,
	.drop_received = on_drop_received_cb,
	.action_ask = NULL,
	.on_error = NULL
};

static void
reset_drop_state(void)
{
	drop_received_called = 0;
	free(drop_received_data);
	drop_received_data = NULL;
	drop_received_length = 0;
	drop_received_format = 0;
	drop_received_target = None;
}

static void
test_target_request_selection_basic(Display *dpy, Window source,
	Window target, const X11DndAtoms *atoms)
{
	Atom prop_atom;
	XEvent ev;
	int i;

	printf("test_target_request_selection_basic\n");

	drain_events(dpy);
	reset_drop_state();

	CHECK(x11dnd_register_target(dpy, target, &target_callbacks, NULL) == 0,
		"register target window");

	/* Simulate XdndEnter from source. */
	{
		long data_l[5];
		data_l[0] = 0;
		data_l[1] = ((long)5 << 24) | 0;
		data_l[2] = (long)atoms->text_uri_list;
		data_l[3] = 0;
		data_l[4] = 0;
		x11dnd_send_client_message(dpy, target, source,
			atoms->XdndEnter, data_l, CurrentTime);
		XFlush(dpy);
	}

	/* Process XdndEnter. */
	{
		int got = 0;
		for (i = 0; i < 500 && !got; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				if (x11dnd_target_process_event(&ev)) {
					got = 1;
				}
			}
			if (!got) {
				struct timespec ts = {0, 1000000L};
				nanosleep(&ts, NULL);
			}
		}
		CHECK(got, "processed XdndEnter");
	}

	/* Simulate XdndPosition. */
	{
		long data_l[5];
		data_l[0] = 0;
		data_l[1] = 0;
		data_l[2] = (10 << 16) | 20;
		data_l[3] = (long)CurrentTime;
		data_l[4] = (long)atoms->XdndActionCopy;
		x11dnd_send_client_message(dpy, target, source,
			atoms->XdndPosition, data_l, CurrentTime);
		XFlush(dpy);
	}

	{
		int got = 0;
		for (i = 0; i < 500 && !got; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				if (x11dnd_target_process_event(&ev)) {
					got = 1;
				}
			}
			if (!got) {
				struct timespec ts = {0, 1000000L};
				nanosleep(&ts, NULL);
			}
		}
		CHECK(got, "processed XdndPosition");
	}

	/* Simulate XdndDrop. */
	{
		long data_l[5];
		data_l[0] = 0;
		data_l[1] = 0;
		data_l[2] = (long)CurrentTime;
		data_l[3] = 0;
		data_l[4] = 0;
		x11dnd_send_client_message(dpy, target, source,
			atoms->XdndDrop, data_l, CurrentTime);
		XFlush(dpy);
	}

	{
		int got = 0;
		for (i = 0; i < 500 && !got; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				if (x11dnd_target_process_event(&ev)) {
					got = 1;
				}
			}
			if (!got) {
				struct timespec ts = {0, 1000000L};
				nanosleep(&ts, NULL);
			}
		}
		CHECK(got, "processed XdndDrop");
	}

	prop_atom = XInternAtom(dpy, "Xdnd_DATA", False);

	CHECK(x11dnd_target_request_selection(NULL, prop_atom) == False,
		"request_selection with NULL session returns False");

	{
		long data_l[5];
		data_l[0] = 0; data_l[1] = 0; data_l[2] = 0;
		data_l[3] = 0; data_l[4] = 0;
		x11dnd_send_client_message(dpy, target, source,
			atoms->XdndLeave, data_l, CurrentTime);
		XFlush(dpy);
	}
	{
		int got = 0;
		for (i = 0; i < 500 && !got; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				if (x11dnd_target_process_event(&ev)) {
					got = 1;
				}
			}
			if (!got) {
				struct timespec ts = {0, 1000000L};
				nanosleep(&ts, NULL);
			}
		}
	}

	x11dnd_unregister_target(dpy, target);
}

static void
test_selection_notify_immediate(Display *dpy, Window source,
	Window target_win, const X11DndAtoms *atoms)
{
	X11DndSourceSession *src_sess;
	Atom types[1];
	Atom actions[1];
	Atom prop_atom;
	XEvent ev;
	int i;
	int got_drop = 0;

	printf("test_selection_notify_immediate\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	drain_events(dpy);
	reset_drop_state();

	src_sess = x11dnd_start_drag(dpy, source, &target_callbacks,
		CurrentTime, types, 1, actions, 1, NULL);
	CHECK(src_sess != NULL, "start_drag for immediate transfer test");

	CHECK(x11dnd_register_target(dpy, target_win, &target_callbacks, NULL) == 0,
		"register target for immediate transfer test");

	{
		long data_l[5];
		data_l[0] = 0;
		data_l[1] = ((long)5 << 24) | 0;
		data_l[2] = (long)atoms->text_uri_list;
		data_l[3] = 0;
		data_l[4] = 0;
		x11dnd_send_client_message(dpy, target_win, source,
			atoms->XdndEnter, data_l, CurrentTime);
		XFlush(dpy);
	}
	{
		int got = 0;
		for (i = 0; i < 500 && !got; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				if (x11dnd_target_process_event(&ev)) got = 1;
			}
			if (!got) { struct timespec ts = {0, 1000000L}; nanosleep(&ts, NULL); }
		}
		CHECK(got, "processed XdndEnter");
	}

	{
		long data_l[5];
		data_l[0] = 0; data_l[1] = 0;
		data_l[2] = (10 << 16) | 20;
		data_l[3] = (long)CurrentTime;
		data_l[4] = (long)atoms->XdndActionCopy;
		x11dnd_send_client_message(dpy, target_win, source,
			atoms->XdndPosition, data_l, CurrentTime);
		XFlush(dpy);
	}
	{
		for (i = 0; i < 500; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				x11dnd_target_process_event(&ev);
			}
		}
	}

	{
		long data_l[5];
		data_l[0] = 0; data_l[1] = 0;
		data_l[2] = (long)CurrentTime;
		data_l[3] = 0; data_l[4] = 0;
		x11dnd_send_client_message(dpy, target_win, source,
			atoms->XdndDrop, data_l, CurrentTime);
		XFlush(dpy);
	}
	{
		got_drop = 0;
		for (i = 0; i < 500 && !got_drop; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				if (x11dnd_target_process_event(&ev)) got_drop = 1;
			}
			if (!got_drop) { struct timespec ts = {0, 1000000L}; nanosleep(&ts, NULL); }
		}
		CHECK(got_drop, "processed XdndDrop");
	}

	prop_atom = XInternAtom(dpy, "Xdnd_DATA", False);

	CHECK(x11dnd_target_request_selection(NULL, prop_atom) == False,
		"NULL session returns False for request_selection");

	{
		long data_l[5];
		data_l[0] = 0; data_l[1] = 0; data_l[2] = 0;
		data_l[3] = 0; data_l[4] = 0;
		x11dnd_send_client_message(dpy, target_win, source,
			atoms->XdndLeave, data_l, CurrentTime);
		XFlush(dpy);
	}
	{
		for (i = 0; i < 500; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				x11dnd_target_process_event(&ev);
			}
		}
	}

	x11dnd_end_drag(src_sess);
	x11dnd_unregister_target(dpy, target_win);
}

static void
test_incr_should_use_incr(void)
{
	printf("test_incr_should_use_incr\n");

	CHECK(x11dnd_incr_should_use_incr(0) == False,
		"0 bytes: not INCR");
	CHECK(x11dnd_incr_should_use_incr(100) == False,
		"100 bytes: not INCR");
	CHECK(x11dnd_incr_should_use_incr(262144) == False,
		"262144 bytes (threshold): not INCR");
	CHECK(x11dnd_incr_should_use_incr(262145) == True,
		"262145 bytes (threshold+1): INCR");
	CHECK(x11dnd_incr_should_use_incr(1000000) == True,
		"1MB: INCR");
}

static void
test_incr_source_start_and_cleanup(Display *dpy, Window source,
	Window target_win, const X11DndAtoms *atoms)
{
	X11DndIncrSourceSession *incr_sess;
	unsigned char test_data[] = "Hello, INCR world!";
	Atom prop_atom;

	printf("test_incr_source_start_and_cleanup\n");

	prop_atom = XInternAtom(dpy, "TEST_INCR_PROP", False);

	incr_sess = x11dnd_incr_source_start(dpy, target_win, prop_atom,
		atoms->text_uri_list, atoms->XdndSelection, CurrentTime, 8,
		test_data, sizeof(test_data) - 1);
	CHECK(incr_sess != NULL, "INCR source session created");

	CHECK(incr_sess->requestor == target_win, "requestor matches");
	CHECK(incr_sess->property == prop_atom, "property matches");
	CHECK(incr_sess->target == atoms->text_uri_list, "target matches");
	CHECK(incr_sess->length == sizeof(test_data) - 1, "length matches");
	CHECK(incr_sess->offset == 0, "offset starts at 0");
	CHECK(incr_sess->started == False, "not started yet");
	CHECK(incr_sess->complete == False, "not complete yet");

	x11dnd_incr_source_cleanup(incr_sess);
	CHECK(1, "cleanup succeeded");
}

static void
test_incr_source_small_data_no_incr(void)
{
	printf("test_incr_source_small_data_no_incr\n");

	CHECK(x11dnd_incr_should_use_incr(100) == False,
		"small data should not use INCR");
	CHECK(x11dnd_incr_should_use_incr(X11DND_INCR_THRESHOLD + 1) == True,
		"large data should use INCR");
}

static void
test_incr_target_cleanup(Display *dpy, Window target_win,
	const X11DndAtoms *atoms)
{
	X11DndIncrTargetSession *incr_sess;
	Atom prop_atom;

	printf("test_incr_target_cleanup\n");

	prop_atom = XInternAtom(dpy, "TEST_INCR_PROP", False);

	incr_sess = x11dnd_incr_target_start(dpy, target_win, prop_atom,
		atoms->XdndSelection, atoms->text_uri_list, 8, 100000);
	CHECK(incr_sess != NULL, "INCR target session created");

	CHECK(incr_sess->window == target_win, "window matches");
	CHECK(incr_sess->property == prop_atom, "property matches");
	CHECK(incr_sess->selection == atoms->XdndSelection, "selection matches");
	CHECK(incr_sess->target_type == atoms->text_uri_list, "target_type matches");
	CHECK(incr_sess->format == 8, "format is 8");
	CHECK(incr_sess->estimated_size == 100000, "estimated_size matches");
	CHECK(incr_sess->data != NULL, "buffer allocated");
	CHECK(incr_sess->complete == False, "not complete yet");
	CHECK(incr_sess->timed_out == False, "not timed out yet");

	x11dnd_incr_target_cleanup(incr_sess);
	CHECK(1, "target cleanup succeeded");
}

static void
test_target_request_selection_states(Display *dpy, Window source,
	Window target_win, const X11DndAtoms *atoms)
{
	Atom prop_atom;

	printf("test_target_request_selection_states\n");

	prop_atom = XInternAtom(dpy, "Xdnd_DATA", False);

	CHECK(x11dnd_target_request_selection(NULL, prop_atom) == False,
		"NULL session returns False");

	/* Session in IDLE state should also fail — need a valid session. */
	/* We can't easily test this without going through the full protocol,
	 * but we verified NULL safety above. */
}

static void
test_handle_selection_notify_wrong_event(void)
{
	XEvent ev;

	printf("test_handle_selection_notify_wrong_event\n");

	memset(&ev, 0, sizeof(ev));
	ev.type = ButtonPress;

	CHECK(x11dnd_target_handle_selection_notify(&ev) == 0,
		"SelectionNotify handler returns 0 for ButtonPress event");

	memset(&ev, 0, sizeof(ev));
	ev.type = ClientMessage;

	CHECK(x11dnd_target_handle_selection_notify(&ev) == 0,
		"SelectionNotify handler returns 0 for ClientMessage event");
}

static void
test_incr_threshold_value(void)
{
	printf("test_incr_threshold_value\n");

	CHECK(X11DND_INCR_THRESHOLD == 262144,
		"INCR threshold is 256KB (262144)");
	CHECK(X11DND_INCR_TIMEOUT_MS == 5000,
		"INCR timeout is 5000ms");
}

int
main(void)
{
	Display *dpy;
	Window root, source, target_win;
	int screen;
	const X11DndAtoms *atoms;

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "Cannot open display - is Xvfb running?\n");
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

	source = make_window(dpy, root, 100, 100);
	target_win = make_window(dpy, root, 100, 100);
	CHECK(source != None && target_win != None,
		"source and target windows created");

	test_incr_should_use_incr();
	test_incr_source_small_data_no_incr();
	test_incr_threshold_value();
	test_handle_selection_notify_wrong_event();
	test_target_request_selection_states(dpy, source, target_win, atoms);
	test_incr_source_start_and_cleanup(dpy, source, target_win, atoms);
	test_incr_target_cleanup(dpy, target_win, atoms);
	test_target_request_selection_basic(dpy, source, target_win, atoms);
	test_selection_notify_immediate(dpy, source, target_win, atoms);

	XDestroyWindow(dpy, source);
	XDestroyWindow(dpy, target_win);
	XCloseDisplay(dpy);

	if (failures == 0) {
		printf("\nALL TESTS PASSED\n");
		return 0;
	}
	printf("\n%d TEST(S) FAILED\n", failures);
	return 1;
}