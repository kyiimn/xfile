/*
 * test_source.c - TDD tests for drag source state machine.
 *
 * Verifies XdndEnter, XdndPosition, XdndLeave, XdndDrop message wire
 * formats, XdndSelection ownership at drag start, waiting_for_status
 * flood prevention, and state machine transitions.
 *
 * Run under Xvfb: Xvfb :99 & DISPLAY=:99 ./test_source
 */
#define _DEFAULT_SOURCE
#include "x11dnd.h"
#include "x11dnd_atoms.h"
#include "x11dnd_source.h"
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

static int drag_begin_called = 0;
static int drag_end_called = 0;
static int drag_end_completed = -1;
static int status_received_called = 0;
static int finished_received_called = 0;

static void
on_drag_begin_cb(X11DndSourceSession *sess)
{
	(void)sess;
	drag_begin_called++;
}

static void
on_drag_end_cb(X11DndSourceSession *sess, Bool completed)
{
	(void)sess;
	drag_end_called++;
	drag_end_completed = completed ? 1 : 0;
}

static void
status_received_cb(X11DndSourceSession *sess, Bool accept, int x, int y,
	int w, int h, Atom action)
{
	(void)sess;
	(void)accept;
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)action;
	status_received_called++;
}

static void
finished_received_cb(X11DndSourceSession *sess, Bool success,
	Atom performed_action)
{
	(void)sess;
	(void)success;
	(void)performed_action;
	finished_received_called++;
}

static void
reset_callbacks(void)
{
	drag_begin_called = 0;
	drag_end_called = 0;
	drag_end_completed = -1;
	status_received_called = 0;
	finished_received_called = 0;
}

static X11DndClass callbacks = {
	.on_drag_begin = on_drag_begin_cb,
	.on_drag_end = on_drag_end_cb,
	.get_drag_data = NULL,
	.status_received = status_received_cb,
	.finished_received = finished_received_cb,
	.on_enter = NULL,
	.position_received = NULL,
	.on_leave = NULL,
	.drop_received = NULL,
	.action_ask = NULL,
	.on_error = NULL
};

static void
test_start_drag_selection_ownership(Display *dpy, Window source,
	Window target, const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	Atom types[1];
	Atom actions[1];
	Window owner;

	printf("test_start_drag_selection_ownership\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");
	CHECK(drag_begin_called == 1, "on_drag_begin called at drag start");

	owner = XGetSelectionOwner(dpy, atoms->XdndSelection);
	CHECK(owner == source, "XdndSelection owner == source_win at drag start");

	x11dnd_end_drag(sess);
	CHECK(drag_end_called == 1, "on_drag_end called by end_drag");
	CHECK(drag_end_completed == 1, "on_drag_end completed=True for end_drag");

	owner = XGetSelectionOwner(dpy, atoms->XdndSelection);
	CHECK(owner == None, "XdndSelection released after end_drag");
}

static void
test_send_enter_format(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	XClientMessageEvent cm;
	Atom types[1];
	Atom actions[1];

	printf("test_send_enter_format\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	x11dnd_source_send_enter(sess, target);

	if (recv_client_message(dpy, target, atoms->XdndEnter, &cm) != 0) {
		CHECK(0, "received XdndEnter ClientMessage");
	} else {
		CHECK((long)cm.data.l[0] == (long)source,
			"XdndEnter data.l[0] == source_win");
		CHECK(((long)cm.data.l[1] >> 24) == X11DND_VERSION_5,
			"XdndEnter data.l[1] version == 5 in bits 24-31");
		CHECK(((long)cm.data.l[1] & 0x1) == 0,
			"XdndEnter data.l[1] bit 0 == 0 (<=3 types, inline)");
		CHECK((long)cm.data.l[2] == (long)atoms->text_uri_list,
			"XdndEnter data.l[2] == text/uri-list atom");
		CHECK((long)cm.data.l[3] == 0,
			"XdndEnter data.l[3] == 0 (unused type slot)");
		CHECK((long)cm.data.l[4] == 0,
			"XdndEnter data.l[4] == 0 (unused type slot)");
	}

	CHECK(sess->state == X11DND_SOURCE_ENTERED,
		"state == ENTERED after send_enter");
	CHECK(sess->current_target == target,
		"current_target set after send_enter");

	x11dnd_end_drag(sess);
}

static void
test_send_enter_more_than_3_types(Display *dpy, Window source,
	Window target, const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	XClientMessageEvent cm;
	Atom types[4];
	Atom actions[1];
	unsigned long nitems;
	unsigned char *data;

	printf("test_send_enter_more_than_3_types\n");

	types[0] = atoms->text_uri_list;
	types[1] = atoms->UTF8_STRING;
	types[2] = atoms->STRING;
	types[3] = atoms->text_plain;
	actions[0] = atoms->XdndActionCopy;

	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 4, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	x11dnd_source_send_enter(sess, target);

	if (recv_client_message(dpy, target, atoms->XdndEnter, &cm) != 0) {
		CHECK(0, "received XdndEnter ClientMessage");
	} else {
		CHECK(((long)cm.data.l[1] >> 24) == X11DND_VERSION_5,
			"XdndEnter version == 5");
		CHECK(((long)cm.data.l[1] & 0x1) == 1,
			"XdndEnter bit 0 == 1 (>3 types, type list flag)");
	}

	data = NULL;
	nitems = 0;
	if (x11dnd_get_window_property(dpy, source, atoms->XdndTypeList,
		XA_ATOM, 32, &nitems, &data) == 0) {
		CHECK(nitems == 4, "XdndTypeList property has 4 types");
		if (data && nitems == 4) {
			Atom *atom_data = (Atom *)data;
			CHECK(atom_data[0] == atoms->text_uri_list,
				"XdndTypeList[0] == text/uri-list");
			CHECK(atom_data[3] == atoms->text_plain,
				"XdndTypeList[3] == text/plain");
		}
		if (data) {
			free(data);
		}
	} else {
		CHECK(0, "XdndTypeList property readable");
	}

	x11dnd_end_drag(sess);
}

static void
test_send_position_format(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	XClientMessageEvent cm;
	Atom types[1];
	Atom actions[1];

	printf("test_send_position_format\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	x11dnd_source_send_enter(sess, target);
	(void)recv_client_message(dpy, target, atoms->XdndEnter, &cm);

	x11dnd_source_send_position(sess, target, 100, 200, 12345,
		atoms->XdndActionCopy);

	if (recv_client_message(dpy, target, atoms->XdndPosition, &cm) != 0) {
		CHECK(0, "received XdndPosition ClientMessage");
	} else {
		CHECK((long)cm.data.l[0] == (long)source,
			"XdndPosition data.l[0] == source_win");
		CHECK((long)cm.data.l[1] == 0,
			"XdndPosition data.l[1] == 0 (reserved)");
		CHECK(((long)cm.data.l[2] >> 16) == 100,
			"XdndPosition data.l[2] x == 100 in high 16 bits");
		CHECK(((long)cm.data.l[2] & 0xFFFF) == 200,
			"XdndPosition data.l[2] y == 200 in low 16 bits");
		CHECK((long)cm.data.l[3] == 12345,
			"XdndPosition data.l[3] == timestamp");
		CHECK((long)cm.data.l[4] == (long)atoms->XdndActionCopy,
			"XdndPosition data.l[4] == XdndActionCopy");
	}

	CHECK(sess->waiting_for_status == True,
		"waiting_for_status == True after send_position");
	CHECK(sess->last_sent_x == 100, "last_sent_x == 100");
	CHECK(sess->last_sent_y == 200, "last_sent_y == 200");
	CHECK(sess->last_action == atoms->XdndActionCopy,
		"last_action == XdndActionCopy");

	x11dnd_end_drag(sess);
}

static void
test_position_flood_prevention(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	XClientMessageEvent cm;
	Atom types[1];
	Atom actions[1];
	int msg_count;

	printf("test_position_flood_prevention\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	x11dnd_source_send_enter(sess, target);
	(void)recv_client_message(dpy, target, atoms->XdndEnter, &cm);

	x11dnd_source_send_position(sess, target, 10, 20, 1,
		atoms->XdndActionCopy);
	(void)recv_client_message(dpy, target, atoms->XdndPosition, &cm);

	x11dnd_source_send_position(sess, target, 30, 40, 2,
		atoms->XdndActionCopy);
	x11dnd_source_send_position(sess, target, 50, 60, 3,
		atoms->XdndActionCopy);

	XFlush(dpy);
	msg_count = 0;
	while (recv_client_message(dpy, target, atoms->XdndPosition, &cm) == 0) {
		msg_count++;
	}
	CHECK(msg_count == 0,
		"no additional XdndPosition while waiting_for_status");

	CHECK(sess->waiting_for_status == True,
		"still waiting_for_status == True");

	x11dnd_end_drag(sess);
}

static void
test_send_leave_format(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	XClientMessageEvent cm;
	Atom types[1];
	Atom actions[1];

	printf("test_send_leave_format\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	x11dnd_source_send_enter(sess, target);
	(void)recv_client_message(dpy, target, atoms->XdndEnter, &cm);

	x11dnd_source_send_leave(sess, target);

	if (recv_client_message(dpy, target, atoms->XdndLeave, &cm) != 0) {
		CHECK(0, "received XdndLeave ClientMessage");
	} else {
		CHECK((long)cm.data.l[0] == (long)source,
			"XdndLeave data.l[0] == source_win");
		CHECK((long)cm.data.l[1] == 0, "XdndLeave data.l[1] == 0");
		CHECK((long)cm.data.l[2] == 0, "XdndLeave data.l[2] == 0");
		CHECK((long)cm.data.l[3] == 0, "XdndLeave data.l[3] == 0");
		CHECK((long)cm.data.l[4] == 0, "XdndLeave data.l[4] == 0");
	}

	CHECK(sess->current_target == None,
		"current_target == None after send_leave");
	CHECK(sess->state == X11DND_SOURCE_DRAGGING,
		"state == DRAGGING after send_leave");

	x11dnd_end_drag(sess);
}

static void
test_send_drop_format(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	XClientMessageEvent cm;
	Atom types[1];
	Atom actions[1];

	printf("test_send_drop_format\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	x11dnd_source_send_enter(sess, target);
	(void)recv_client_message(dpy, target, atoms->XdndEnter, &cm);

	x11dnd_source_send_drop(sess, target, 99999);

	if (recv_client_message(dpy, target, atoms->XdndDrop, &cm) != 0) {
		CHECK(0, "received XdndDrop ClientMessage");
	} else {
		CHECK((long)cm.data.l[0] == (long)source,
			"XdndDrop data.l[0] == source_win");
		CHECK((long)cm.data.l[1] == 0, "XdndDrop data.l[1] == 0 (reserved)");
		CHECK((long)cm.data.l[2] == 99999,
			"XdndDrop data.l[2] == timestamp");
		CHECK((long)cm.data.l[3] == 0, "XdndDrop data.l[3] == 0");
		CHECK((long)cm.data.l[4] == 0, "XdndDrop data.l[4] == 0");
	}

	CHECK(sess->state == X11DND_SOURCE_DROP_SENT,
		"state == DROP_SENT after send_drop");

	x11dnd_end_drag(sess);
}

static void
test_cancel_drag(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	XClientMessageEvent cm;
	Atom types[1];
	Atom actions[1];
	Window owner;

	printf("test_cancel_drag\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	x11dnd_source_send_enter(sess, target);
	(void)recv_client_message(dpy, target, atoms->XdndEnter, &cm);

	x11dnd_cancel_drag(sess);
	CHECK(drag_end_called == 1, "on_drag_end called by cancel_drag");
	CHECK(drag_end_completed == 0,
		"on_drag_end completed=False for cancel_drag");

	if (recv_client_message(dpy, target, atoms->XdndLeave, &cm) != 0) {
		CHECK(0, "cancel_drag sends XdndLeave");
	} else {
		CHECK((long)cm.data.l[0] == (long)source,
			"XdndLeave from cancel data.l[0] == source_win");
	}

	owner = XGetSelectionOwner(dpy, atoms->XdndSelection);
	CHECK(owner == None, "XdndSelection released after cancel_drag");
}

static void
test_handle_status(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	XClientMessageEvent cm;
	Atom types[1];
	Atom actions[1];
	XEvent ev;

	printf("test_handle_status\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	x11dnd_source_send_enter(sess, target);
	(void)recv_client_message(dpy, target, atoms->XdndEnter, &cm);

	x11dnd_source_send_position(sess, target, 50, 60, 111,
		atoms->XdndActionCopy);
	(void)recv_client_message(dpy, target, atoms->XdndPosition, &cm);

	CHECK(sess->waiting_for_status == True,
		"waiting_for_status True before XdndStatus");

	memset(&ev, 0, sizeof(ev));
	ev.type = ClientMessage;
	ev.xclient.window = source;
	ev.xclient.message_type = atoms->XdndStatus;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = (long)target;
	ev.xclient.data.l[1] = 0x1;
	ev.xclient.data.l[2] = ((long)10 << 16) | 20;
	ev.xclient.data.l[3] = ((long)100 << 16) | 200;
	ev.xclient.data.l[4] = (long)atoms->XdndActionCopy;

	{
		int ret = x11dnd_source_process_event(&ev);
		CHECK(ret == 1, "process_event returns 1 for XdndStatus");
	}

	CHECK(sess->waiting_for_status == False,
		"waiting_for_status False after XdndStatus");
	CHECK(status_received_called == 1, "status_received callback called");

	x11dnd_end_drag(sess);
}

static void
test_handle_finished(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	XClientMessageEvent cm;
	Atom types[1];
	Atom actions[1];
	XEvent ev;

	printf("test_handle_finished\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	x11dnd_source_send_enter(sess, target);
	(void)recv_client_message(dpy, target, atoms->XdndEnter, &cm);

	x11dnd_source_send_drop(sess, target, 555);
	(void)recv_client_message(dpy, target, atoms->XdndDrop, &cm);

	CHECK(sess->state == X11DND_SOURCE_DROP_SENT,
		"state == DROP_SENT before XdndFinished");

	memset(&ev, 0, sizeof(ev));
	ev.type = ClientMessage;
	ev.xclient.window = source;
	ev.xclient.message_type = atoms->XdndFinished;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = (long)target;
	ev.xclient.data.l[1] = 1;
	ev.xclient.data.l[2] = (long)atoms->XdndActionCopy;
	ev.xclient.data.l[3] = 0;
	ev.xclient.data.l[4] = 0;

	{
		int ret = x11dnd_source_process_event(&ev);
		CHECK(ret == 1, "process_event returns 1 for XdndFinished");
	}

	CHECK(sess->state == X11DND_SOURCE_FINISHED,
		"state == FINISHED after XdndFinished");
	CHECK(finished_received_called == 1,
		"finished_received callback called");
	CHECK(drag_end_called == 1,
		"on_drag_end called by handle_finished");
	CHECK(drag_end_completed == 1,
		"on_drag_end completed=True for successful drop");

	x11dnd_end_drag(sess);
}

static void
test_process_event_no_active(Display *dpy)
{
	XEvent ev;

	printf("test_process_event_no_active\n");

	memset(&ev, 0, sizeof(ev));
	ev.type = ClientMessage;
	{
		int ret = x11dnd_source_process_event(&ev);
		CHECK(ret == 0, "process_event returns 0 when no active source");
	}
}

static void
test_accessors(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	Atom types[1];
	Atom actions[1];
	int user_marker = 42;

	printf("test_accessors\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, &user_marker);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	CHECK(x11dnd_source_get_display(sess) == dpy,
		"get_display returns correct Display");
	CHECK(x11dnd_source_get_window(sess) == source,
		"get_window returns correct source window");
	CHECK(x11dnd_source_get_user_data(sess) == &user_marker,
		"get_user_data returns correct pointer");

	x11dnd_end_drag(sess);

	CHECK(x11dnd_source_get_display(NULL) == NULL,
		"get_display(NULL) returns NULL");
	CHECK(x11dnd_source_get_window(NULL) == None,
		"get_window(NULL) returns None");
	CHECK(x11dnd_source_get_user_data(NULL) == NULL,
		"get_user_data(NULL) returns NULL");
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

	source = make_window(dpy, root, 50, 50);
	target = make_window(dpy, root, 50, 50);
	CHECK(source != None && target != None,
		"source and target windows created");

	test_start_drag_selection_ownership(dpy, source, target, atoms);
	test_send_enter_format(dpy, source, target, atoms);
	test_send_enter_more_than_3_types(dpy, source, target, atoms);
	test_send_position_format(dpy, source, target, atoms);
	test_position_flood_prevention(dpy, source, target, atoms);
	test_send_leave_format(dpy, source, target, atoms);
	test_send_drop_format(dpy, source, target, atoms);
	test_cancel_drag(dpy, source, target, atoms);
	test_handle_status(dpy, source, target, atoms);
	test_handle_finished(dpy, source, target, atoms);
	test_process_event_no_active(dpy);
	test_accessors(dpy, source, target, atoms);

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