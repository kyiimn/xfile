/*
 * test_dsave.c - TDD tests for XdndDirectSave (XDS) protocol.
 *
 * Verifies the XDS save sequence: target sets save path, source handles
 * SelectionRequest, reads path, gets drag data, saves file, sets result
 * property. Also tests failure path (unwritable directory).
 *
 * Run under Xvfb: Xvfb :99 & DISPLAY=:99 ./test_dsave
 */
#define _DEFAULT_SOURCE
#include "x11dnd.h"
#include "x11dnd_atoms.h"
#include "x11dnd_source.h"
#include "x11dnd_dsave.h"
#include "x11dnd_util.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

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

static const char *g_drag_data = "Hello, XDS world!";
static int g_get_drag_data_called = 0;

static void
get_drag_data_cb(X11DndSourceSession *sess, Atom target,
	unsigned char **data_ret, unsigned long *length_ret, int *format_ret)
{
	(void)sess;
	(void)target;
	g_get_drag_data_called++;
	*data_ret = (unsigned char *)Xmalloc(strlen(g_drag_data) + 1);
	if (*data_ret) {
		strcpy((char *)*data_ret, g_drag_data);
	}
	*length_ret = strlen(g_drag_data);
	*format_ret = 8;
}

static X11DndClass callbacks = {
	.on_drag_begin = NULL,
	.on_drag_end = NULL,
	.get_drag_data = get_drag_data_cb,
	.status_received = NULL,
	.finished_received = NULL,
	.on_enter = NULL,
	.position_received = NULL,
	.on_leave = NULL,
	.drop_received = NULL,
	.action_ask = NULL,
	.on_error = NULL
};

static void
test_is_direct_save(Display *dpy, const X11DndAtoms *atoms)
{
	printf("test_is_direct_save\n");
	CHECK(x11dnd_is_direct_save(atoms->XdndDirectSave) == True,
		"is_direct_save(XdndDirectSave) == True");
	CHECK(x11dnd_is_direct_save(atoms->XdndActionCopy) == False,
		"is_direct_save(XdndActionCopy) == False");
	CHECK(x11dnd_is_direct_save(None) == False,
		"is_direct_save(None) == False");
}

static void
test_set_get_path(Display *dpy, Window win, const X11DndAtoms *atoms)
{
	char *path;

	printf("test_set_get_path\n");

	CHECK(x11dnd_dsave_set_path(dpy, win, "/tmp/test_xds_path.txt") == 0,
		"dsave_set_path returns 0");

	path = x11dnd_dsave_get_path(dpy, win);
	CHECK(path != NULL, "dsave_get_path returns non-NULL");
	if (path) {
		CHECK(strcmp(path, "/tmp/test_xds_path.txt") == 0,
			"get_path == set_path");
		free(path);
	}
}

static void
test_dsave_success(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	Atom types[1];
	Atom actions[1];
	char *result;
	const char *save_path = "/tmp/test_xds_save.txt";
	struct stat st;

	printf("test_dsave_success\n");

	unlink(save_path);

	types[0] = atoms->XdndDirectSave;
	actions[0] = atoms->XdndActionCopy;

	g_get_drag_data_called = 0;
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	CHECK(x11dnd_dsave_set_path(dpy, target, save_path) == 0,
		"target sets save path on XdndDirectSave0");

	CHECK(x11dnd_dsave_handle_request(dpy, target,
		atoms->XdndDirectSave0, CurrentTime, sess) == 0,
		"source handles XDS request, returns 0");

	CHECK(g_get_drag_data_called == 1,
		"get_drag_data callback was called");

	result = x11dnd_dsave_get_result(dpy, target, atoms->XdndDirectSave0);
	CHECK(result != NULL, "target reads result property");
	if (result) {
		CHECK(strncmp(result, "file://", 7) == 0,
			"result starts with file://");
		CHECK(strstr(result, save_path) != NULL,
			"result contains save path");
		free(result);
	}

	CHECK(stat(save_path, &st) == 0,
		"file exists at save path");
	CHECK(st.st_size == (off_t)strlen(g_drag_data),
		"file has correct size");

	{
		FILE *f = fopen(save_path, "r");
		char buf[256];
		if (f) {
			size_t n = fread(buf, 1, sizeof(buf) - 1, f);
			buf[n] = '\0';
			fclose(f);
			CHECK(strcmp(buf, g_drag_data) == 0,
				"file content matches drag data");
		} else {
			CHECK(0, "can open saved file for reading");
		}
	}

	unlink(save_path);
	x11dnd_end_drag(sess);
}

static void
test_dsave_failure(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	Atom types[1];
	Atom actions[1];
	char *result;
	const char *bad_path = "/nonexistent/dir/test_xds_fail.txt";

	printf("test_dsave_failure\n");

	types[0] = atoms->XdndDirectSave;
	actions[0] = atoms->XdndActionCopy;

	g_get_drag_data_called = 0;
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	CHECK(x11dnd_dsave_set_path(dpy, target, bad_path) == 0,
		"target sets bad save path");

	CHECK(x11dnd_dsave_handle_request(dpy, target,
		atoms->XdndDirectSave0, CurrentTime, sess) == 0,
		"source handles XDS request (returns 0 even on save failure)");

	CHECK(g_get_drag_data_called == 1,
		"get_drag_data callback was called");

	result = x11dnd_dsave_get_result(dpy, target, atoms->XdndDirectSave0);
	CHECK(result != NULL, "target reads result property");
	if (result) {
		CHECK(strcmp(result, "!") == 0,
			"result is \"!\" on failure");
		free(result);
	}

	CHECK(access(bad_path, F_OK) != 0,
		"file was NOT created at bad path");

	x11dnd_end_drag(sess);
}

static void
test_dsave_no_path(Display *dpy, Window source, Window target,
	const X11DndAtoms *atoms)
{
	X11DndSourceSession *sess;
	Atom types[1];
	Atom actions[1];
	char *result;

	printf("test_dsave_no_path\n");

	types[0] = atoms->XdndDirectSave;
	actions[0] = atoms->XdndActionCopy;

	g_get_drag_data_called = 0;
	sess = x11dnd_start_drag(dpy, source, &callbacks, CurrentTime,
		types, 1, actions, 1, NULL);
	CHECK(sess != NULL, "start_drag returns non-NULL session");

	XDeleteProperty(dpy, target, atoms->XdndDirectSave0);
	XFlush(dpy);

	CHECK(x11dnd_dsave_handle_request(dpy, target,
		atoms->XdndDirectSave0, CurrentTime, sess) == 0,
		"source handles XDS request with no path set");

	CHECK(g_get_drag_data_called == 0,
		"get_drag_data NOT called when no path");

	result = x11dnd_dsave_get_result(dpy, target, atoms->XdndDirectSave0);
	CHECK(result != NULL, "result property set even on no-path");
	if (result) {
		CHECK(strcmp(result, "!") == 0,
			"result is \"!\" when no path set");
		free(result);
	}

	x11dnd_end_drag(sess);
}

static void
test_dsave_null_safety(void)
{
	printf("test_dsave_null_safety\n");
	CHECK(x11dnd_is_direct_save(None) == False,
		"is_direct_save(None) == False");
	CHECK(x11dnd_dsave_set_path(NULL, None, NULL) != 0,
		"set_path(NULL,...) fails");
	CHECK(x11dnd_dsave_get_path(NULL, None) == NULL,
		"get_path(NULL,...) returns NULL");
	CHECK(x11dnd_dsave_get_result(NULL, None, None) == NULL,
		"get_result(NULL,...) returns NULL");
	CHECK(x11dnd_dsave_handle_request(NULL, None, None, 0, NULL) != 0,
		"handle_request(NULL,...) fails");
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

	test_is_direct_save(dpy, atoms);
	test_set_get_path(dpy, target, atoms);
	test_dsave_success(dpy, source, target, atoms);
	test_dsave_failure(dpy, source, target, atoms);
	test_dsave_no_path(dpy, source, target, atoms);
	test_dsave_null_safety();

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