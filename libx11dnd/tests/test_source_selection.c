/*
 * test_source_selection.c - TDD tests for source-side X Selection transfer.
 *
 * Verifies SelectionRequest handling for text/uri-list, UTF8_STRING, STRING,
 * FILE_NAME, text/plain, TARGETS, and TIMESTAMP targets, plus SelectionClear
 * handling and XdndSelection ownership lifecycle.
 *
 * Run under Xvfb: Xvfb :99 & DISPLAY=:99 ./test_source_selection
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

static int error_called = 0;
static int drag_end_called = 0;
static int drag_end_completed = -1;

static void
on_error_cb(const char *msg, int sev)
{
	(void)msg;
	(void)sev;
	error_called++;
}

static void
on_drag_end_cb(X11DndSourceSession *sess, Bool completed)
{
	(void)sess;
	drag_end_called++;
	drag_end_completed = completed ? 1 : 0;
}

static void
reset_callbacks(void)
{
	error_called = 0;
	drag_end_called = 0;
	drag_end_completed = -1;
}

static const char *test_paths[] = {
	"/tmp/test_file1.txt",
	"/tmp/test_file2.txt"
};

static void
get_drag_data_cb(X11DndSourceSession *sess, Atom target,
	unsigned char **data_ret, unsigned long *length_ret, int *format_ret)
{
	(void)sess;
	(void)target;

	if (data_ret) {
		*data_ret = NULL;
	}
	if (length_ret) {
		*length_ret = 0;
	}
	if (format_ret) {
		*format_ret = 0;
	}

	{
		size_t len1 = strlen(test_paths[0]);
		size_t len2 = strlen(test_paths[1]);
		char *buf = malloc(len1 + 1 + len2 + 1);
		if (buf == NULL) {
			return;
		}
		memcpy(buf, test_paths[0], len1);
		buf[len1] = '\n';
		memcpy(buf + len1 + 1, test_paths[1], len2);
		buf[len1 + 1 + len2] = '\0';

		*data_ret = (unsigned char *)buf;
		*length_ret = (unsigned long)(len1 + 1 + len2);
		*format_ret = 8;
	}
}

static X11DndClass callbacks = {
	.on_drag_begin = NULL,
	.on_drag_end = on_drag_end_cb,
	.get_drag_data = get_drag_data_cb,
	.status_received = NULL,
	.finished_received = NULL,
	.on_enter = NULL,
	.position_received = NULL,
	.on_leave = NULL,
	.drop_received = NULL,
	.action_ask = NULL,
	.on_error = on_error_cb
};

static Atom prop_atom;

static int
wait_for_selection_notify(Display *dpy, Window win, Atom property,
	XSelectionEvent *out, int timeout_ms)
{
	XEvent ev;
	int i;

	for (i = 0; i < timeout_ms; i++) {
		while (XPending(dpy) > 0) {
			XNextEvent(dpy, &ev);
			if (ev.type == SelectionNotify &&
			    ev.xselection.requestor == win &&
			    ev.xselection.property == property) {
				memcpy(out, &ev.xselection, sizeof(*out));
				return 0;
			}
			if (ev.type == SelectionRequest) {
				x11dnd_source_process_event(&ev);
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

static unsigned char *
read_property(Display *dpy, Window win, Atom prop, int format,
	unsigned long *nitems_ret)
{
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	unsigned char *data;
	int status;

	*nitems_ret = 0;
	status = XGetWindowProperty(dpy, win, prop, 0, 0x7FFFFFFF, False,
		AnyPropertyType, &actual_type, &actual_format, &nitems,
		&bytes_after, &data);
	if (status != Success) {
		return NULL;
	}
	if (format != 0 && actual_format != format) {
		if (data) {
			XFree(data);
		}
		return NULL;
	}
	*nitems_ret = nitems;
	return data;
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
test_selection_ownership_at_start(Display *dpy, Window source,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	Atom types[1];
	Atom actions[1];
	Window owner;

	printf("test_selection_ownership_at_start\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	drain_events(dpy);
	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	owner = XGetSelectionOwner(dpy, atoms->XdndSelection);
	CHECK(owner == source, "XdndSelection owner == source_win at drag start");

	x11dnd_end_drag(sess);
	CHECK(drag_end_called == 1, "on_drag_end called by end_drag");

	owner = XGetSelectionOwner(dpy, atoms->XdndSelection);
	CHECK(owner == None, "XdndSelection released after end_drag");
}

static void
test_uri_list_conversion(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	Atom types[1];
	Atom actions[1];
	XSelectionEvent sel_notify;
	unsigned char *data;
	unsigned long nitems;
	char *str;

	printf("test_uri_list_conversion\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	drain_events(dpy);
	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	XConvertSelection(dpy, atoms->XdndSelection, atoms->text_uri_list,
		prop_atom, target, CurrentTime);
	XFlush(dpy);

	if (wait_for_selection_notify(dpy, target, prop_atom, &sel_notify,
		2000) != 0) {
		CHECK(0, "received SelectionNotify");
	} else {
		CHECK(sel_notify.selection == atoms->XdndSelection,
			"SelectionNotify selection == XdndSelection");
		CHECK(sel_notify.target == atoms->text_uri_list,
			"SelectionNotify target == text/uri-list");
		CHECK(sel_notify.property == prop_atom,
			"SelectionNotify property == prop_atom (success)");

		data = read_property(dpy, target, prop_atom,
			8, &nitems);
		if (data == NULL) {
			CHECK(0, "read property data");
		} else {
			str = malloc(nitems + 1);
			memcpy(str, data, nitems);
			str[nitems] = '\0';

			CHECK(strstr(str, "file:///tmp/test_file1.txt") != NULL,
				"uri-list contains file:///tmp/test_file1.txt");
			CHECK(strstr(str, "file:///tmp/test_file2.txt") != NULL,
				"uri-list contains file:///tmp/test_file2.txt");
			CHECK(strstr(str, "\r\n") != NULL,
				"uri-list uses CRLF line endings");

			free(str);
			XFree(data);
		}
		XDeleteProperty(dpy, target, prop_atom);
	}

	x11dnd_end_drag(sess);
}

static void
test_utf8_string_conversion(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	Atom types[1];
	Atom actions[1];
	XSelectionEvent sel_notify;
	unsigned char *data;
	unsigned long nitems;
	char *str;

	printf("test_utf8_string_conversion\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	drain_events(dpy);
	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	XConvertSelection(dpy, atoms->XdndSelection, atoms->UTF8_STRING,
		prop_atom, target, CurrentTime);
	XFlush(dpy);

	{
		XEvent ev;
		int i;
		int got_request = 0;
		for (i = 0; i < 500; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				if (ev.type == SelectionRequest) {
					x11dnd_source_process_event(&ev);
					got_request = 1;
				}
			}
			if (got_request) {
				break;
			}
			{
				struct timespec ts;
				ts.tv_sec = 0;
				ts.tv_nsec = 1000000L;
				nanosleep(&ts, NULL);
			}
		}
	}

	if (wait_for_selection_notify(dpy, target, prop_atom, &sel_notify,
		2000) != 0) {
		CHECK(0, "received SelectionNotify for UTF8_STRING");
	} else {
		CHECK(sel_notify.property == prop_atom,
			"UTF8_STRING SelectionNotify property set (success)");

		data = read_property(dpy, target, prop_atom,
			8, &nitems);
		if (data == NULL) {
			CHECK(0, "read UTF8_STRING property data");
		} else {
			str = malloc(nitems + 1);
			memcpy(str, data, nitems);
			str[nitems] = '\0';

			CHECK(strstr(str, "/tmp/test_file1.txt") != NULL,
				"UTF8_STRING contains first path");
			CHECK(strstr(str, "/tmp/test_file2.txt") != NULL,
				"UTF8_STRING contains second path");

			free(str);
			XFree(data);
		}
		XDeleteProperty(dpy, target, prop_atom);
	}

	x11dnd_end_drag(sess);
}

static void
test_string_conversion(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	Atom types[1];
	Atom actions[1];
	XSelectionEvent sel_notify;
	unsigned char *data;
	unsigned long nitems;

	printf("test_string_conversion\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	drain_events(dpy);
	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	XConvertSelection(dpy, atoms->XdndSelection, atoms->STRING,
		prop_atom, target, CurrentTime);
	XFlush(dpy);
	if (wait_for_selection_notify(dpy, target, prop_atom, &sel_notify,
		2000) != 0) {
		CHECK(0, "received SelectionNotify for STRING");
	} else {
		CHECK(sel_notify.property == prop_atom,
			"STRING SelectionNotify property set (success)");

		data = read_property(dpy, target, prop_atom,
			8, &nitems);
		if (data == NULL) {
			CHECK(0, "read STRING property data");
		} else {
			char *str = malloc(nitems + 1);
			memcpy(str, data, nitems);
			str[nitems] = '\0';
			CHECK(strstr(str, "/tmp/test_file1.txt") != NULL,
				"STRING contains first path");
			free(str);
			XFree(data);
		}
		XDeleteProperty(dpy, target, prop_atom);
	}

	x11dnd_end_drag(sess);
}

static void
test_targets_conversion(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	Atom types[1];
	Atom actions[1];
	XSelectionEvent sel_notify;
	unsigned char *data;
	unsigned long nitems;
	Atom *atom_list;
	int found_uri_list = 0;
	int found_utf8 = 0;
	int found_targets = 0;
	int found_timestamp = 0;
	unsigned long i;

	printf("test_targets_conversion\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	drain_events(dpy);
	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	XConvertSelection(dpy, atoms->XdndSelection, atoms->TARGETS,
		prop_atom, target, CurrentTime);
	XFlush(dpy);
	if (wait_for_selection_notify(dpy, target, prop_atom, &sel_notify,
		2000) != 0) {
		CHECK(0, "received SelectionNotify for TARGETS");
	} else {
		CHECK(sel_notify.property == prop_atom,
			"TARGETS SelectionNotify property set (success)");

		data = read_property(dpy, target, prop_atom,
			32, &nitems);
		if (data == NULL) {
			CHECK(0, "read TARGETS property data");
		} else {
			atom_list = (Atom *)data;
			CHECK(nitems >= 5,
				"TARGETS returns at least 5 atoms");

			for (i = 0; i < nitems; i++) {
				if (atom_list[i] == atoms->text_uri_list) {
					found_uri_list = 1;
				}
				if (atom_list[i] == atoms->UTF8_STRING) {
					found_utf8 = 1;
				}
				if (atom_list[i] == atoms->TARGETS) {
					found_targets = 1;
				}
				if (atom_list[i] == atoms->TIMESTAMP) {
					found_timestamp = 1;
				}
			}
			CHECK(found_uri_list, "TARGETS contains text/uri-list");
			CHECK(found_utf8, "TARGETS contains UTF8_STRING");
			CHECK(found_targets, "TARGETS contains TARGETS");
			CHECK(found_timestamp, "TARGETS contains TIMESTAMP");

			XFree(data);
		}
		XDeleteProperty(dpy, target, prop_atom);
	}

	x11dnd_end_drag(sess);
}

static void
test_timestamp_conversion(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	Atom types[1];
	Atom actions[1];
	XSelectionEvent sel_notify;
	unsigned char *data;
	unsigned long nitems;
	Time drag_time;
	XEvent dummy;

	printf("test_timestamp_conversion\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	XChangeProperty(dpy, source, prop_atom, XA_INTEGER, 8,
		PropModeAppend, NULL, 0);
	XFlush(dpy);
	XWindowEvent(dpy, source, PropertyChangeMask, &dummy);
	drag_time = dummy.xproperty.time;

	drain_events(dpy);
	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, drag_time,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	XConvertSelection(dpy, atoms->XdndSelection, atoms->TIMESTAMP,
		prop_atom, target, CurrentTime);
	XFlush(dpy);
	if (wait_for_selection_notify(dpy, target, prop_atom, &sel_notify,
		2000) != 0) {
		CHECK(0, "received SelectionNotify for TIMESTAMP");
	} else {
		CHECK(sel_notify.property == prop_atom,
			"TIMESTAMP SelectionNotify property set (success)");

		data = read_property(dpy, target, prop_atom,
			32, &nitems);
		if (data == NULL) {
			CHECK(0, "read TIMESTAMP property data");
		} else {
			long *ts = (long *)data;
			CHECK(nitems == 1, "TIMESTAMP returns 1 item");
			CHECK((unsigned int)ts[0] == (unsigned int)drag_time,
				"TIMESTAMP value == drag start time");
			XFree(data);
		}
		XDeleteProperty(dpy, target, prop_atom);
	}

	x11dnd_end_drag(sess);
}

static void
test_file_name_conversion(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	Atom types[1];
	Atom actions[1];
	XSelectionEvent sel_notify;
	unsigned char *data;
	unsigned long nitems;

	printf("test_file_name_conversion\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	drain_events(dpy);
	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	XConvertSelection(dpy, atoms->XdndSelection, atoms->FILE_NAME,
		prop_atom, target, CurrentTime);
	XFlush(dpy);
	if (wait_for_selection_notify(dpy, target, prop_atom, &sel_notify,
		2000) != 0) {
		CHECK(0, "received SelectionNotify for FILE_NAME");
	} else {
		CHECK(sel_notify.property == prop_atom,
			"FILE_NAME SelectionNotify property set (success)");

		data = read_property(dpy, target, prop_atom,
			8, &nitems);
		if (data == NULL) {
			CHECK(0, "read FILE_NAME property data");
		} else {
			char *str = malloc(nitems + 1);
			memcpy(str, data, nitems);
			str[nitems] = '\0';
			CHECK(strstr(str, "/tmp/test_file1.txt") != NULL,
				"FILE_NAME contains first path");
			free(str);
			XFree(data);
		}
		XDeleteProperty(dpy, target, prop_atom);
	}

	x11dnd_end_drag(sess);
}

static void
test_text_plain_conversion(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	Atom types[1];
	Atom actions[1];
	XSelectionEvent sel_notify;
	unsigned char *data;
	unsigned long nitems;

	printf("test_text_plain_conversion\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	drain_events(dpy);
	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	XConvertSelection(dpy, atoms->XdndSelection, atoms->text_plain,
		prop_atom, target, CurrentTime);
	XFlush(dpy);
	if (wait_for_selection_notify(dpy, target, prop_atom, &sel_notify,
		2000) != 0) {
		CHECK(0, "received SelectionNotify for text/plain");
	} else {
		CHECK(sel_notify.property == prop_atom,
			"text/plain SelectionNotify property set (success)");

		data = read_property(dpy, target, prop_atom,
			8, &nitems);
		if (data == NULL) {
			CHECK(0, "read text/plain property data");
		} else {
			char *str = malloc(nitems + 1);
			memcpy(str, data, nitems);
			str[nitems] = '\0';
			CHECK(strstr(str, "/tmp/test_file1.txt") != NULL,
				"text/plain contains first path");
			free(str);
			XFree(data);
		}
		XDeleteProperty(dpy, target, prop_atom);
	}

	x11dnd_end_drag(sess);
}

static void
test_selection_clear_cancels_drag(Display *dpy, Window source,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	Atom types[1];
	Atom actions[1];
	Window owner;
	XEvent clear_ev;

	printf("test_selection_clear_cancels_drag\n");

	types[0] = atoms->text_uri_list;
	actions[0] = atoms->XdndActionCopy;

	drain_events(dpy);
	reset_callbacks();
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	/* Steal the selection from the source window */
	{
		Window thief = XCreateSimpleWindow(dpy,
			RootWindow(dpy, DefaultScreen(dpy)),
			0, 0, 10, 10, 0, 0, 0);
		XSetSelectionOwner(dpy, atoms->XdndSelection, thief, CurrentTime);
		XFlush(dpy);

		/* Send a synthetic SelectionClear event to simulate what the
		 * X server should generate. Xvfb does not reliably deliver
		 * SelectionClear events in test environments, so we send it
		 * manually to test the handler logic. */
		memset(&clear_ev, 0, sizeof(clear_ev));
		clear_ev.type = SelectionClear;
		clear_ev.xselectionclear.selection = atoms->XdndSelection;
		clear_ev.xselectionclear.window = source;
		clear_ev.xselectionclear.time = CurrentTime;
		XSendEvent(dpy, source, False, 0, &clear_ev);
		XFlush(dpy);
	}

	/* Process the SelectionClear event */
	{
		XEvent ev;
		int i;
		int got_clear = 0;
		for (i = 0; i < 2000; i++) {
			while (XPending(dpy) > 0) {
				XNextEvent(dpy, &ev);
				if (ev.type == SelectionClear) {
					int ret = x11dnd_source_process_event(&ev);
					CHECK(ret == 1,
						"process_event returns 1 for SelectionClear");
					got_clear = 1;
				}
			}
			if (got_clear) {
				break;
			}
			{
				struct timespec ts;
				ts.tv_sec = 0;
				ts.tv_nsec = 1000000L;
				nanosleep(&ts, NULL);
			}
		}
		CHECK(got_clear, "received SelectionClear event");
	}

	CHECK(error_called >= 1, "on_error called for lost selection");
	CHECK(drag_end_called == 1, "on_drag_end called by SelectionClear handler");
	CHECK(drag_end_completed == 0,
		"on_drag_end completed=False for SelectionClear");

	owner = XGetSelectionOwner(dpy, atoms->XdndSelection);
	CHECK(owner != source,
		"XdndSelection no longer owned by source after clear");
}

static void
test_process_event_no_active_selection(Display *dpy)
{
	XEvent ev;

	printf("test_process_event_no_active_selection\n");

	memset(&ev, 0, sizeof(ev));
	ev.type = SelectionRequest;
	{
		int ret = x11dnd_source_process_event(&ev);
		CHECK(ret == 0,
			"process_event returns 0 for SelectionRequest with no active source");
	}

	memset(&ev, 0, sizeof(ev));
	ev.type = SelectionClear;
	{
		int ret = x11dnd_source_process_event(&ev);
		CHECK(ret == 0,
			"process_event returns 0 for SelectionClear with no active source");
	}
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

	prop_atom = XInternAtom(dpy, "XFILE_TEST_PROP", False);

	test_selection_ownership_at_start(dpy, source, atoms);
	test_uri_list_conversion(dpy, source, target, atoms);
	test_utf8_string_conversion(dpy, source, target, atoms);
	test_string_conversion(dpy, source, target, atoms);
	test_targets_conversion(dpy, source, target, atoms);
	test_timestamp_conversion(dpy, source, target, atoms);
	test_file_name_conversion(dpy, source, target, atoms);
	test_text_plain_conversion(dpy, source, target, atoms);
	test_selection_clear_cancels_drag(dpy, source, atoms);
	test_process_event_no_active_selection(dpy);

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