/*
 * Copyright (C) 2025-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

/*
 * Drag-and-drop support for the file list widget.
 *
 * Architecture:
 *   - XDnD source drag:  via libx11dnd (x11dnd_xt_start_drag)
 *   - XDnD drop target:  via libx11dnd (x11dnd_xt_register_target)
 *   - Motif drop target: via XmDropSiteRegister (legacy compatibility for
 *     receiving drops from Motif apps like nedit)
 *
 * All Motif DnD SOURCE code (XmDragStart, XmCreateDragIcon, etc.) has been
 * removed. The drag source path now goes entirely through libx11dnd.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <X11/Xatom.h>
#include <Xm/XmP.h>
#include <Xm/DragDrop.h>
#include <Xm/AtomMgr.h>
#include "x11dnd.h"
#include "x11dnd_xt.h"
#include "x11dnd_source.h"
#include "dnd.h"

#include "main.h"
#include "const.h"
#include "listw.h"
#include "listwp.h"
#include "mbstr.h"
#include "fsproc.h"
#include "comdlgs.h"
#include "menu.h"
#include "filemgr.h"
#include <limits.h>
#include <errno.h>

/* Drag/drop target atoms */
Atom XA_TEXT_URI_LIST = None;
Atom XA_FILE_NAME = None;
Atom XA_XFILE_FILE_LIST = None;
Atom XA_UTF8_STRING = None;
Atom XA_TARGETS = None;
Atom XA_TIMESTAMP = None;

/* Custom selection atom for cross-instance DnD data transfer */
Atom XA_XFILE_DND_DATA = None;

/* Widget reference for drag/drop callbacks */
static Widget wlist_ref = NULL;

/* Import targets: atoms the drop site accepts */
Atom dnd_import_targets[DND_NUM_IMPORT_TARGETS];

/* libx11dnd session handle for the active drag source */
static X11DndSourceSession *dnd_source_session = NULL;

/* Source widget for the active drag (used in on_drag_end callback) */
static Widget dnd_source_widget = NULL;

/* libx11dnd callback table (static, lives for app lifetime) */
static X11DndClass dnd_callbacks;

/* XDnD action atoms (interned by x11dnd_init, cached here for fast lookup) */
static Atom XA_XdndActionCopy = None;
static Atom XA_XdndActionMove = None;
static Atom XA_XdndActionLink = None;

/*
 * Targeted X error handler for Motif DnD operations.
 *
 * Open Motif 2.3's internal _MOTIF_DROP selection protocol has the same
 * 64-bit Atom / ICCCM format=32 mismatch bug: it passes Atoms through
 * XConvertSelection with format=32, so on x86-64 the upper 32 bits of
 * heap Atoms can leak into the atom ID and trigger a BadAtom (invalid
 * Atom parameter) X error. That error occurs inside libXm during
 * cross-instance DnD, not in our code, so we catch and suppress only
 * BadAtom while a drag is active. All other errors are forwarded to the
 * previous handler so we do not mask real bugs.
 */
static XErrorHandler dnd_prev_xerr = NULL;
static Boolean dnd_active = False;

int
dnd_xerror_handler(Display *dpy, XErrorEvent *ev)
{
	(void)dpy;

	if(dnd_active && ev->error_code == BadAtom) {
		return 0;
	}

	if(dnd_prev_xerr != NULL) {
		return dnd_prev_xerr(dpy, ev);
	}

	return 0;
}

#define FL_PART(w) &((struct file_list_rec*)w)->file_list
#define FL_REC(w) (struct file_list_rec*)w

/* Data passed from dnd_drop_cb to dnd_transfer_cb via client_data */
struct dnd_transfer_data {
	char *dest_dir;
	unsigned char operation;  /* XfDROP_COPY, XfDROP_MOVE, etc. */
	Widget drag_context;      /* DragContext for protocol completion */
};

/* Local routines */
static void dnd_drop_cb(Widget w, XtPointer client, XtPointer call);
static void dnd_drag_site_cb(Widget w, XtPointer client, XtPointer call);
static void dnd_transfer_cb(Widget w, XtPointer client_data,
	Atom *selection, Atom *type, XtPointer value,
	unsigned long *length, int *format);
static char *dnd_uri_to_path(const char *uri);
static char *dnd_make_absolute_path(const char *dir, const char *name);
static char *dnd_dir_path_from_widget(Widget w);
static Boolean item_at_xy(Widget w, Position x, Position y, unsigned int *index);
static void dnd_highlight_item(Widget w, unsigned int index);
static void dnd_clear_highlight(Widget w);
static void dnd_update_highlight(Widget w, Position x, Position y);
static void dnd_refresh_cb(XtPointer client_data, XtIntervalId *id);
static Boolean dnd_move_confirm_wp(XtPointer client_data);
static void dnd_perform_operation(const char *src_dir, char **names,
	unsigned int count, const char *dest_dir, unsigned char operation);
static void dnd_status_clear_cb(XtPointer client_data, XtIntervalId *id);



/* ========================================================================
 * libx11dnd callback implementations
 * ======================================================================== */

/* Called when a drag operation begins (after XdndEnter is sent) */
static void
dnd_on_drag_begin(X11DndSourceSession *sess)
{
	(void)sess;
	/* Nothing needed — we track state via dnd_source_session */
}

/* Called when a drag operation ends (after XdndFinished or cancel) */
static void
dnd_on_drag_end(X11DndSourceSession *sess, Bool completed)
{
	Widget src_widget;
	struct file_list_part *fl;

	(void)completed;

	if(sess == NULL) return;

	src_widget = dnd_source_widget;

	if(src_widget != NULL) {
		fl = FL_PART(src_widget);
		if(fl != NULL) fl->dnd_state = DND_NONE;
		force_update();
		XtAppAddTimeOut(app_inst.context, 1000,
			dnd_refresh_cb, (XtPointer)src_widget);
	}

	dnd_source_session = NULL;
	dnd_source_widget = NULL;
}

/* Provide drag data for SelectionRequest (XDnD source) */
static void
dnd_get_drag_data(X11DndSourceSession *sess, Atom target,
	unsigned char **data_ret, unsigned long *length_ret, int *format_ret)
{
	Widget src_widget;
	struct file_list_selection *sel;
	char *data;
	size_t len;
	unsigned int i;
	char *path_buf;
	char *real_path;

	if(sess == NULL || data_ret == NULL || length_ret == NULL
		|| format_ret == NULL) {
		return;
	}

	src_widget = dnd_source_widget;
	if(src_widget == NULL) return;

	sel = file_list_get_selection(src_widget);
	if(sel == NULL || sel->count == 0) return;

	if(target == XA_TEXT_URI_LIST) {
		len = 0;
		for(i = 0; i < sel->count; i++) {
			path_buf = dnd_make_absolute_path(
				(const char*)app_inst.location, sel->names[i]);
			if(!path_buf) continue;
			len += strlen("file://") + strlen(path_buf) + 2;
			free(path_buf);
		}
		if(len == 0) return;

		data = malloc(len + 1);
		if(!data) return;
		data[0] = '\0';

		for(i = 0; i < sel->count; i++) {
			path_buf = dnd_make_absolute_path(
				(const char*)app_inst.location, sel->names[i]);
			if(!path_buf) continue;
			if(i > 0) strcat(data, "\r\n");
			strcat(data, "file://");
			strcat(data, path_buf);
			free(path_buf);
		}

		*data_ret = (unsigned char*)data;
		*length_ret = strlen(data);
		*format_ret = 8;

	} else if(target == XA_UTF8_STRING || target == XA_STRING) {
		len = 0;
		for(i = 0; i < sel->count; i++) {
			path_buf = dnd_make_absolute_path(
				(const char*)app_inst.location, sel->names[i]);
			if(!path_buf) continue;
			len += strlen(path_buf) + 1;
			free(path_buf);
		}
		if(len == 0) return;

		data = malloc(len + 1);
		if(!data) return;
		data[0] = '\0';

		for(i = 0; i < sel->count; i++) {
			path_buf = dnd_make_absolute_path(
				(const char*)app_inst.location, sel->names[i]);
			if(!path_buf) continue;
			if(i > 0) strcat(data, " ");
			strcat(data, path_buf);
			free(path_buf);
		}

		if(target == XA_STRING)
			mbs_to_latin1(data, data);

		*data_ret = (unsigned char*)data;
		*length_ret = strlen(data);
		*format_ret = 8;

	} else if(target == XA_FILE_NAME) {
		if(sel->count == 0) return;

		path_buf = dnd_make_absolute_path(
			(const char*)app_inst.location, sel->names[0]);
		if(!path_buf) return;

		data = malloc(strlen(path_buf) + 1);
		if(!data) { free(path_buf); return; }
		strcpy(data, path_buf);
		free(path_buf);

		*data_ret = (unsigned char*)data;
		*length_ret = strlen(data);
		*format_ret = 8;

	} else if(target == XA_XFILE_FILE_LIST) {
		char *pos;

		real_path = realpath(app_inst.location, NULL);
		if(!real_path) return;

		len = strlen(real_path) + 1;
		for(i = 0; i < sel->count; i++) {
			len += strlen(sel->names[i]) + 1;
		}

		data = malloc(len + 1);
		if(!data) { free(real_path); return; }

		strcpy(data, real_path);
		pos = data + strlen(real_path) + 1;

		for(i = 0; i < sel->count; i++) {
			strcpy(pos, sel->names[i]);
			pos += strlen(sel->names[i]);
			*pos = '\0';
			pos++;
		}
		*pos = '\0';

		*data_ret = (unsigned char*)data;
		*length_ret = len + 1;
		*format_ret = 8;
		free(real_path);
	}
}

/* XdndStatus received from target (XDnD source) */
static void
dnd_status_received(X11DndSourceSession *sess, Bool accept,
	int x, int y, int w, int h, Atom action)
{
	(void)sess;
	(void)accept;
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)action;
	/* Could update cursor/feedback here; currently a no-op */
}

/* XdndFinished received from target (XDnD source) */
static void
dnd_finished_received(X11DndSourceSession *sess, Bool success,
	Atom performed_action)
{
	(void)performed_action;

	if(sess == NULL) return;

	if(!success) {
		/* Drop was rejected or failed — nothing to do */
	}
}

/* XdndEnter received: source is beginning a drag over this target */
static void
dnd_on_enter(X11DndTargetSession *sess, Window source,
	int version, Atom *types, int n_types)
{
	(void)sess;
	(void)source;
	(void)version;
	(void)types;
	(void)n_types;
	/* Could inspect offered types here; currently a no-op */
}

/* XdndPosition received (XDnD target) */
static void
dnd_position_received(X11DndTargetSession *sess,
	int x, int y, Time time, Atom action,
	Bool *accept_ret, Atom *action_ret,
	int *rect_x, int *rect_y, int *rect_w, int *rect_h)
{
	Widget w;
	struct file_list_part *fl;
	unsigned int index;
	Position wx, wy;

	(void)time;

	if(sess == NULL) {
		*accept_ret = False;
		return;
	}

	w = wlist_ref;
	if(w == NULL || !XtIsRealized(w)) {
		*accept_ret = False;
		return;
	}

	fl = FL_PART(w);

	/* Translate root coordinates to widget-local */
	{
		Window child;
		int dest_x, dest_y;
		XTranslateCoordinates(XtDisplay(w), XtWindow(w),
			RootWindowOfScreen(XtScreen(w)),
			0, 0, &dest_x, &dest_y, &child);
		wx = (Position)(x - dest_x);
		wy = (Position)(y - dest_y);
	}

	if(item_at_xy(w, wx, wy, &index)
		&& S_ISDIR(fl->items[index].mode)) {
		dnd_highlight_item(w, index);
	} else {
		dnd_clear_highlight(w);
	}

	*accept_ret = True;

	if(action != None) {
		*action_ret = action;
	} else {
		*action_ret = XA_XdndActionCopy;
	}

	*rect_x = 0;
	*rect_y = 0;
	*rect_w = 0;
	*rect_h = 0;
}

/* XdndLeave received: source has left the target window */
static void
dnd_on_leave(X11DndTargetSession *sess)
{
	(void)sess;

	if(wlist_ref != NULL) {
		dnd_clear_highlight(wlist_ref);
	}
}

/* Drop data received (after SelectionNotify) — the main XDnD drop handler */
static void
dnd_drop_received(X11DndTargetSession *sess, Atom target,
	unsigned char *data, unsigned long length, int format)
{
	Widget w;
	char *dest_dir;
	char *src_dir;
	char **paths;
	unsigned int count;
	unsigned int i;
	char *line;
	char *next;
	char *end;
	unsigned char operation;
	Display *dpy;
	Window source_win;
	Window target_win;
	Bool success = False;

	if(sess == NULL || data == NULL || length == 0) {
		return;
	}

	w = wlist_ref;

	dpy = x11dnd_target_get_display(sess);
	source_win = x11dnd_target_get_source_window(sess);
	target_win = x11dnd_target_get_window(sess);

	/* Determine destination directory */
	dest_dir = dnd_dir_path_from_widget(w);
	if(dest_dir == NULL) {
		dest_dir = realpath(app_inst.location, NULL);
		if(dest_dir == NULL) return;
	}

	/* Map the negotiated action to our operation.
	 * We'll use XfDROP_COPY as default. The action was already
	 * agreed upon in dnd_position_received. For now, use copy. */
	operation = XfDROP_COPY;

	if(target == XA_TEXT_URI_LIST) {
		char *buf;

		buf = malloc(length + 1);
		if(!buf) { free(dest_dir); return; }
		memcpy(buf, data, length);
		buf[length] = '\0';

		/* Count items */
		for(count = 0, line = buf; *line; ) {
			if(*line == '#' || *line == '\r' || *line == '\n') {
				line++;
				continue;
			}
			count++;
			while(*line && *line != '\r' && *line != '\n') line++;
			if(*line == '\r') line++;
			if(*line == '\n') line++;
		}

		if(count == 0) {
			free(buf);
			free(dest_dir);
			return;
		}

		paths = (char**)malloc(sizeof(char*) * count);
		if(!paths) { free(buf); free(dest_dir); return; }

		for(i = 0, line = buf; *line; ) {
			if(*line == '#' || *line == '\r' || *line == '\n') {
				line++;
				continue;
			}

			end = line;
			while(*end && *end != '\r' && *end != '\n') end++;
			next = end;
			if(*next == '\r') next++;
			if(*next == '\n') next++;
			*end = '\0';

			paths[i] = dnd_uri_to_path(line);
			i++;
			line = next;
		}

		/* Derive source directory from first valid path */
		src_dir = NULL;
		for(i = 0; i < count; i++) {
			if(paths[i] != NULL) {
				src_dir = strdup(paths[i]);
				if(src_dir) {
					char *tail = strrchr(src_dir, '/');
					if(tail) *tail = '\0';
				}
				break;
			}
		}

		if(src_dir) {
			char *wd = realpath(src_dir, NULL);
			if(wd && dest_dir && !strcmp(wd, dest_dir)) {
				free(wd);
			} else {
				free(wd);
				dnd_perform_operation(src_dir, paths, count,
					dest_dir, operation);
				success = True;
			}
			free(src_dir);
		}

		for(i = 0; i < count; i++) free(paths[i]);
		free(paths);
		free(buf);

	} else if(target == XA_XFILE_FILE_LIST) {
		char *ptr;
		char *sb;
		char **list;
		unsigned int n;
		size_t slen;

		ptr = (char*)data;
		slen = length;
		for(n = 0; ; ) {
			if((size_t)(ptr - (char*)data) >= slen || ptr[0] == '\0') {
				n++;
				if((size_t)(ptr - (char*)data) + 1 >= slen || ptr[1] == '\0')
					break;
			}
			ptr++;
		}

		if(n == 0) { free(dest_dir); return; }

		list = (char**)malloc(sizeof(char*) * n);
		if(!list) { free(dest_dir); return; }

		ptr = (char*)data;
		sb = ptr;
		for(i = 0; ; ) {
			if((size_t)(ptr - (char*)data) >= slen || ptr[0] == '\0') {
				list[i] = sb;
				i++;
				sb = ptr + 1;
				if((size_t)(ptr - (char*)data) + 1 >= slen || ptr[1] == '\0')
					break;
			}
			ptr++;
		}

		n--;
		src_dir = realpath(list[0], NULL);
		if(!src_dir) src_dir = strdup(list[0]);

		if(src_dir) {
			char *wd = realpath(src_dir, NULL);
			if(wd && dest_dir && !strcmp(wd, dest_dir)) {
				/* Same directory — skip */
			} else {
				free(wd);
				dnd_perform_operation(src_dir, list + 1, n,
					dest_dir, operation);
				success = True;
			}
			free(src_dir);
		}

		free(list);

	} else if(target == XA_UTF8_STRING || target == XA_STRING
		|| target == XA_FILE_NAME) {
		/* Single-path target: treat as a file path */
		char *path_copy;

		path_copy = malloc(length + 1);
		if(!path_copy) { free(dest_dir); return; }
		memcpy(path_copy, data, length);
		path_copy[length] = '\0';

		paths = (char**)malloc(sizeof(char*));
		if(!paths) { free(path_copy); free(dest_dir); return; }

		if(target == XA_FILE_NAME) {
			/* FILE_NAME is a single absolute path */
			paths[0] = dnd_uri_to_path(path_copy);
			if(!paths[0]) {
				/* Not a URI — treat as plain path */
				paths[0] = realpath(path_copy, NULL);
				if(!paths[0]) paths[0] = strdup(path_copy);
			}
		} else {
			/* UTF8_STRING / STRING: try URI parse, fall back to path */
			paths[0] = dnd_uri_to_path(path_copy);
			if(!paths[0]) {
				paths[0] = realpath(path_copy, NULL);
				if(!paths[0]) paths[0] = strdup(path_copy);
			}
		}
		count = 1;

		if(paths[0]) {
			src_dir = strdup(paths[0]);
			if(src_dir) {
				char *tail = strrchr(src_dir, '/');
				if(tail) *tail = '\0';

				{
					char *wd = realpath(src_dir, NULL);
					if(wd && dest_dir && !strcmp(wd, dest_dir)) {
						/* Same directory — skip */
					} else {
						free(wd);
						dnd_perform_operation(src_dir, paths, count,
							dest_dir, operation);
						success = True;
					}
				}
				free(src_dir);
			}
		}

		free(paths[0]);
		free(paths);
		free(path_copy);
	}

	free(dest_dir);

	/* Send XdndFinished */
	if(dpy != NULL && source_win != None && target_win != None) {
		x11dnd_send_finished(dpy, source_win, target_win,
			success ? True : False,
			success ? XA_XdndActionCopy : None);
	}

	dnd_active = False;
}

/* XdndActionAsk callback — present a menu of actions */
static void
dnd_action_ask(X11DndTargetSession *sess, Atom *actions, int n_actions,
	char **descriptions, int n_desc, Atom *chosen_action_ret)
{
	(void)sess;
	(void)descriptions;
	(void)n_desc;

	/* Default to copy if we can't ask the user */
	int i;

	if(actions == NULL || n_actions == 0 || chosen_action_ret == NULL) {
		*chosen_action_ret = XA_XdndActionCopy;
		return;
	}

	/* Prefer copy, then move */
	for(i = 0; i < n_actions; i++) {
		if(actions[i] == XA_XdndActionCopy) {
			*chosen_action_ret = XA_XdndActionCopy;
			return;
		}
	}
	for(i = 0; i < n_actions; i++) {
		if(actions[i] == XA_XdndActionMove) {
			*chosen_action_ret = XA_XdndActionMove;
			return;
		}
	}

	*chosen_action_ret = actions[0];
}

/* Error callback */
static void
dnd_on_error(const char *message, int severity)
{
	if(severity >= 2) {
		stderr_msg("DnD error: %s\n", message);
	}
}

/* ========================================================================
 * Helper functions (preserved from original)
 * ======================================================================== */

static char *
dnd_make_absolute_path(const char *dir, const char *name)
{
	char *path;
	size_t dlen;
	size_t nlen;
	int add_slash;

	if(!dir || !name) return NULL;

	dlen = strlen(dir);
	nlen = strlen(name);
	add_slash = (dlen == 0 || dir[dlen - 1] != '/');

	path = XtMalloc(dlen + nlen + (add_slash ? 2 : 1));
	if(!path) return NULL;

	memcpy(path, dir, dlen);
	if(add_slash) {
		path[dlen] = '/';
		memcpy(path + dlen + 1, name, nlen + 1);
	} else {
		memcpy(path + dlen, name, nlen + 1);
	}

	return path;
}

static char *
dnd_dir_path_from_widget(Widget w)
{
	const char *location;
	char buf[PATH_MAX];
	char *result;

	(void)w;

	location = app_inst.location;
	if(!location) return NULL;

	if(!realpath(location, buf)) return NULL;
	result = XtMalloc(strlen(buf) + 1);
	if(!result) return NULL;
	strcpy(result, buf);
	return result;
}

static Boolean
item_at_xy(Widget w, Position x, Position y, unsigned int *index)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int i;
	int mx = (int)x + fl->xoff;
	int my = (int)y + fl->yoff;

	for(i = 0; i < fl->num_items; i++) {
		Dimension item_width = (fl->view_mode == XfCOMPACT) ?
			fl->items[i].width : fl->item_width_max[fl->view_mode];

		if(mx > fl->items[i].x && my > fl->items[i].y &&
			mx < (int)(fl->items[i].x + item_width) &&
			my < (int)(fl->items[i].y + fl->item_height_max)) {
			*index = i;
			return True;
		}
	}
	return False;
}

static void
dnd_highlight_item(Widget w, unsigned int index)
{
	struct file_list_part *fl = FL_PART(w);
	Display *dpy = XtDisplay(w);
	Window wnd = XtWindow(w);
	struct item_rec *r;
	int x, y;
	Dimension width, height;

	if(!fl->xor_gc || !XtIsRealized(w)) return;

	if(index >= fl->num_items) return;
	r = &fl->items[index];

	x = r->x - fl->xoff;
	y = r->y - fl->yoff;
	width = (fl->view_mode == XfCOMPACT) ?
		r->width : fl->item_width_max[fl->view_mode];
	height = fl->item_height_max;

	if((int)(x + width) < 0 || (int)(y + height) < 0) return;

	XDrawRectangle(dpy, wnd, fl->xor_gc, x, y, width, height);
}

static void
dnd_clear_highlight(Widget w)
{
	struct file_list_part *fl = FL_PART(w);

	if(fl->dnd_highlight_active) {
		dnd_highlight_item(w, fl->dnd_highlight_item);
	}
	fl->dnd_highlight_item = 0;
	fl->dnd_highlight_active = False;
}

/* ========================================================================
 * Motif drop target receiving (preserved for legacy compatibility)
 * ======================================================================== */

static void
dnd_drag_site_cb(Widget w, XtPointer client, XtPointer call)
{
	XmDragProcCallbackStruct *ds = (XmDragProcCallbackStruct*)call;

	(void)client;

	if(ds == NULL) return;

	switch(ds->reason) {
		case XmCR_DROP_SITE_ENTER_MESSAGE:
			dnd_update_highlight(w, ds->x, ds->y);
			break;
		case XmCR_DROP_SITE_LEAVE_MESSAGE:
			dnd_clear_highlight(w);
			break;
		case XmCR_DROP_SITE_MOTION_MESSAGE:
			dnd_update_highlight(w, ds->x, ds->y);
			break;
		default:
			break;
	}

	/* The entire FileList widget is a valid drop site. Drops on
	 * non-directory items fall through to the current directory,
	 * which is valid. Only directory items get a highlight ring. */
	ds->dropSiteStatus = XmDROP_SITE_VALID;
	if(ds->reason != XmCR_DROP_SITE_LEAVE_MESSAGE)
		ds->operation = ds->operations;
}

static void
dnd_update_highlight(Widget w, Position x, Position y)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int index;

	dnd_clear_highlight(w);

	if(!item_at_xy(w, x, y, &index)) return;
	if(!S_ISDIR(fl->items[index].mode)) return;

	fl->dnd_highlight_item = index;
	fl->dnd_highlight_active = True;
	dnd_highlight_item(w, index);
}

static char *
dnd_item_dir_path(Widget w, Position x, Position y)
{
	struct file_list_part *fl = FL_PART(w);
	unsigned int index;
	char *path;

	if(!item_at_xy(w, x, y, &index)) return NULL;
	if(!S_ISDIR(fl->items[index].mode)) return NULL;

	path = dnd_make_absolute_path(app_inst.location, fl->items[index].name);
	if(!path) return NULL;

	return realpath(path, NULL);
}

static void
dnd_drop_cb(Widget w, XtPointer client, XtPointer call)
{
	XmDropProcCallbackStruct *dropInfo = (XmDropProcCallbackStruct*)call;
	Arg args[8];
	Cardinal n = 0;
	char *dest_dir;
	Atom target;
	struct dnd_transfer_data *td;
	Widget transfer;

	(void)client;

	if(dropInfo == NULL) {
		return;
	}

	if(dropInfo->dropAction == XmDROP_HELP) {
		XtSetArg(args[0], XmNtransferStatus, XmTRANSFER_FAILURE);
		XtSetValues(w, args, 1);
		return;
	}

	dest_dir = dnd_item_dir_path(w, dropInfo->x, dropInfo->y);
	if(dest_dir == NULL) {
		dest_dir = realpath(app_inst.location, NULL);
		if(dest_dir == NULL) {
			XtSetArg(args[0], XmNtransferStatus, XmTRANSFER_FAILURE);
			XtSetValues(w, args, 1);
			return;
		}
	}

	/* Check if source is same directory as destination */
	{
		Widget src_widget = wlist_ref ? wlist_ref : app_inst.wlist;
		if(src_widget != NULL && XtIsWidget(src_widget)) {
			char *src_location = dnd_dir_path_from_widget(src_widget);
			if(src_location && dest_dir && !strcmp(src_location, dest_dir)) {
				XtSetArg(args[0], XmNtransferStatus, XmTRANSFER_FAILURE);
				XtSetValues(w, args, 1);
				free(src_location);
				free(dest_dir);
				return;
			}
			if(src_location) free(src_location);
		}
	}

	target = XA_TEXT_URI_LIST;

	td = (struct dnd_transfer_data*)malloc(sizeof(*td));
	if(!td) {
		free(dest_dir);
		XtSetArg(args[0], XmNtransferStatus, XmTRANSFER_FAILURE);
		XtSetValues(w, args, 1);
		return;
	}
	td->dest_dir = dest_dir;
	td->operation = dropInfo->operation;
	td->drag_context = dropInfo->dragContext;

	dnd_active = True;
	if(dnd_prev_xerr == NULL) {
		dnd_prev_xerr = XSetErrorHandler(dnd_xerror_handler);
	}

	{
		XmDropTransferEntryRec entry;

		entry.client_data = (XtPointer)td;
		entry.target = target;

		n = 0;
		XtSetArg(args[n], XmNdropTransfers, &entry); n++;
		XtSetArg(args[n], XmNnumDropTransfers, 1); n++;
		XtSetArg(args[n], XmNtransferProc, dnd_transfer_cb); n++;
		XtSetArg(args[n], XmNtransferStatus, XmTRANSFER_SUCCESS); n++;

		transfer = XmDropTransferStart(dropInfo->dragContext, args, n);
		if(transfer == NULL) {
			dnd_active = False;
			free(td->dest_dir);
			free(td);
			XtSetArg(args[0], XmNtransferStatus, XmTRANSFER_FAILURE);
			XtSetValues(w, args, 1);
		}
	}
}

static void
dnd_transfer_cb(Widget w, XtPointer client_data,
	Atom *selection, Atom *type, XtPointer value,
	unsigned long *length, int *format)
{
	struct dnd_transfer_data *td = (struct dnd_transfer_data*)client_data;
	char *dest_dir;
	unsigned char operation;

	(void)w;
	(void)selection;
	(void)format;

	if(td == NULL) {
		if(value != NULL) XtFree((char*)value);
		return;
	}

	if(type == NULL || length == NULL) {
		free(td->dest_dir);
		free(td);
		if(value != NULL) XtFree((char*)value);
		return;
	}

	dest_dir = td->dest_dir;
	operation = td->operation;
	free(td);

	if(*type == XA_TEXT_URI_LIST) {
		char *data;
		char *line;
		char *next;
		char **paths;
		unsigned int count;
		unsigned int i;
		char *src_dir;

		if(value == NULL || *length == 0) {
			free(dest_dir);
			return;
		}
		data = XtMalloc(*length + 1);
		if(!data) {
			free(dest_dir);
			return;
		}
		memcpy(data, value, *length);
		data[*length] = '\0';

		for(count = 0, line = data; *line; ) {
			if(*line == '#' || *line == '\r' || *line == '\n') {
				line++;
				continue;
			}
			count++;
			while(*line && *line != '\r' && *line != '\n') line++;
			if(*line == '\r') line++;
			if(*line == '\n') line++;
		}

		if(count == 0) {
			XtFree(data);
			free(dest_dir);
			return;
		}

		paths = (char**)XtMalloc(sizeof(char*) * count);
		if(!paths) {
			XtFree(data);
			free(dest_dir);
			return;
		}

		for(i = 0, line = data; *line; ) {
			char *end;

			if(*line == '#' || *line == '\r' || *line == '\n') {
				line++;
				continue;
			}

			end = line;
			while(*end && *end != '\r' && *end != '\n') end++;
			next = end;
			if(*next == '\r') next++;
			if(*next == '\n') next++;
			*end = '\0';

			paths[i] = dnd_uri_to_path(line);
			i++;
			line = next;
		}

		src_dir = NULL;
		for(i = 0; i < count; i++) {
			if(paths[i] != NULL) {
				src_dir = strdup(paths[i]);
				if(src_dir) {
					char *tail = strrchr(src_dir, '/');
					if(tail) *tail = '\0';
				}
				break;
			}
		}
		if(!src_dir) {
			for(i = 0; i < count; i++) free(paths[i]);
			XtFree((char*)paths);
			XtFree(data);
			free(dest_dir);
			return;
		}

		/* Map Motif operation to XfDROP_* */
		{
			unsigned char xf_op;
			if(operation == XmDROP_MOVE) xf_op = XfDROP_MOVE;
			else if(operation == XmDROP_LINK) xf_op = XfDROP_LINK;
			else xf_op = XfDROP_COPY;

			dnd_perform_operation(src_dir, paths, count, dest_dir, xf_op);
		}

		free(src_dir);
		for(i = 0; i < count; i++) free(paths[i]);
		XtFree((char*)paths);
		XtFree(data);

	} else if(*type == XA_XFILE_FILE_LIST) {
		char *val;
		char *ptr;
		char *sb;
		char **list;
		unsigned int n, i;
		size_t len;
		char *src_dir;

		if(value == NULL || *length == 0) {
			free(dest_dir);
			return;
		}
		val = (char*)value;

		len = *length;
		ptr = val;
		for(n = 0; ; ) {
			if((size_t)(ptr - val) >= len || ptr[0] == '\0') {
				n++;
				if((size_t)(ptr - val) + 1 >= len || ptr[1] == '\0')
					break;
			}
			ptr++;
		}

		if(n == 0) {
			free(dest_dir);
			return;
		}

		list = (char**)XtMalloc(sizeof(char*) * n);
		if(!list) {
			free(dest_dir);
			return;
		}

		ptr = val;
		sb = ptr;
		for(i = 0; ; ) {
			if((size_t)(ptr - val) >= len || ptr[0] == '\0') {
				list[i] = sb;
				i++;
				sb = ptr + 1;
				if((size_t)(ptr - val) + 1 >= len || ptr[1] == '\0')
					break;
			}
			ptr++;
		}

		n--;
		src_dir = realpath(list[0], NULL);
		if(!src_dir) src_dir = strdup(list[0]);

		if(src_dir) {
			unsigned char xf_op;
			if(operation == XmDROP_MOVE) xf_op = XfDROP_MOVE;
			else if(operation == XmDROP_LINK) xf_op = XfDROP_LINK;
			else xf_op = XfDROP_COPY;

			dnd_perform_operation(src_dir, list + 1, n, dest_dir, xf_op);
			free(src_dir);
		}

		XtFree((char*)list);

	} else {
		if(value != NULL) XtFree((char*)value);
	}

	free(dest_dir);
	dnd_active = False;
}

/* ========================================================================
 * URI-to-path conversion (preserved)
 * ======================================================================== */

static char *
dnd_uri_to_path(const char *uri)
{
	const char *p;
	char *path;
	char *decoded;
	char *r;

	if(!uri) return NULL;

	if(strncmp(uri, "file://", 7) == 0) {
		p = uri + 7;
	} else if(strncmp(uri, "file:/", 5) == 0 ||
		strncmp(uri, "file:", 5) == 0) {
		p = uri + 5;
		if(*p == '/') p++;
	} else {
		return NULL;
	}

	if(!*p) return NULL;
	if(*p != '/') return NULL;

	decoded = XtMalloc(strlen(p) + 1);
	if(!decoded) return NULL;

	r = decoded;
	while(*p) {
		if(*p == '%' && p[1] && p[2]) {
			char val[3];
			char *endptr;
			long v;

			val[0] = p[1];
			val[1] = p[2];
			val[2] = '\0';
			v = strtol(val, &endptr, 16);
			if(endptr == val + 2 && v >= 0 && v <= 255) {
				*r++ = (char)v;
				p += 3;
			} else {
				*r++ = *p++;
			}
		} else {
			*r++ = *p++;
		}
	}
	*r = '\0';

	path = realpath(decoded, NULL);
	if(!path) {
		path = strdup(decoded);
	}
	XtFree(decoded);

	return path;
}

/* ========================================================================
 * File operation dispatch (preserved)
 * ======================================================================== */

struct dnd_confirm_data {
	char *src_dir;
	char **names;
	unsigned int count;
	char *dest_dir;
	unsigned char operation;
	Widget dest_widget;
};

static void
dnd_refresh_cb(XtPointer client_data, XtIntervalId *id)
{
	Widget wdest = (Widget)client_data;
	(void)id;

	if(wdest && XtIsWidget(wdest)) force_update();
	if(app_inst.wlist && app_inst.wlist != wdest && XtIsWidget(app_inst.wlist))
		force_update();
}

static void
dnd_status_clear_cb(XtPointer client_data, XtIntervalId *id)
{
	(void)client_data;
	(void)id;

	set_status_text("");
}

static void
dnd_perform_operation(const char *src_dir, char **names,
	unsigned int count, const char *dest_dir, unsigned char operation)
{
	char *wd;
	char *dest;
	char **basenames;
	unsigned int i;
	int rv;
	Boolean do_move = False;
	Boolean do_copy = False;
	struct file_list_part *fl = NULL;
	Widget dest_widget;

	if(!src_dir || !dest_dir || count == 0) return;

	wd = realpath(src_dir, NULL);
	dest = realpath(dest_dir, NULL);
	if(!wd || !dest) {
		if(wd) free(wd);
		if(dest) free(dest);
		return;
	}

	if(!strcmp(wd, dest)) {
		set_status_text("Cannot drop: same directory");
		XtAppAddTimeOut(XtWidgetToApplicationContext(app_inst.wshell),
			3000, dnd_status_clear_cb, NULL);
		message_box(app_inst.wshell, MB_WARN, APP_TITLE,
			"Source and destination are the same directory.");
		free(wd);
		free(dest);
		return;
	}

	if(operation == XfDROP_LINK) {
		set_status_text("Symbolic links not supported");
		XtAppAddTimeOut(XtWidgetToApplicationContext(app_inst.wshell),
			3000, dnd_status_clear_cb, NULL);
		message_box(app_inst.wshell, MB_WARN, APP_TITLE,
			"Symbolic links are not supported in drag-and-drop.");
		free(wd);
		free(dest);
		return;
	}

	if(operation == XfDROP_COPY) {
		do_copy = True;
	} else if(operation == XfDROP_MOVE) {
		do_move = True;
	} else {
		/* Fallback: operation unclear. Default to copy for safety. */
		do_copy = True;
	}

	if(!do_copy && !do_move) {
		free(wd);
		free(dest);
		return;
	}

	basenames = (char**)XtMalloc(sizeof(char*) * count);
	if(!basenames) {
		free(wd);
		free(dest);
		return;
	}
	for(i = 0; i < count; i++) {
		const char *base;
		if(names[i]) {
			base = strrchr(names[i], '/');
			basenames[i] = base ? strdup(base + 1) : strdup(names[i]);
		} else {
			basenames[i] = NULL;
		}
	}

	dest_widget = wlist_ref ? wlist_ref : app_inst.wlist;
	if(dest_widget) fl = FL_PART(dest_widget);

	if(do_move && fl && fl->confirm_dnd_move) {
		struct dnd_confirm_data *cd;

		cd = (struct dnd_confirm_data*)malloc(
			sizeof(struct dnd_confirm_data));
		if(cd) {
			cd->src_dir = wd;
			cd->dest_dir = dest;
			cd->names = basenames;
			cd->count = count;
			cd->operation = operation;
			cd->dest_widget = dest_widget;
			XtAppAddWorkProc(XtWidgetToApplicationContext(dest_widget),
				dnd_move_confirm_wp, (XtPointer)cd);
			return;
		}
	}

	if(do_move) {
		set_status_text("Moving %u item%s to %s...",
			count, count == 1 ? "" : "s", dest);
		rv = move_files(wd, basenames, count, dest);
	} else {
		set_status_text("Copying %u item%s to %s...",
			count, count == 1 ? "" : "s", dest);
		rv = copy_files(wd, basenames, count, dest);
	}

	if(rv) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Could not complete requested action.\n%s.",
			strerror(errno), NULL);
	}

	XtAppAddTimeOut(XtWidgetToApplicationContext(dest_widget),
		1000, dnd_refresh_cb, (XtPointer)dest_widget);
	if(app_inst.wlist && app_inst.wlist != dest_widget)
		XtAppAddTimeOut(XtWidgetToApplicationContext(app_inst.wlist),
			1000, dnd_refresh_cb, (XtPointer)app_inst.wlist);

	XtAppAddTimeOut(XtWidgetToApplicationContext(dest_widget),
		3000, dnd_status_clear_cb, NULL);

	for(i = 0; i < count; i++) {
		if(basenames[i]) free(basenames[i]);
	}
	XtFree((char*)basenames);
	free(wd);
	free(dest);
}

static Boolean
dnd_move_confirm_wp(XtPointer client_data)
{
	struct dnd_confirm_data *cd = (struct dnd_confirm_data*)client_data;
	enum mb_result res;
	int rv;
	Boolean refresh_src;
	unsigned int i;

	res = va_message_box(app_inst.wshell, MB_QUESTION, APP_TITLE,
		"Move %u item%s from\n%s\nto\n%s?",
		cd->count, cd->count == 1 ? "" : "s",
		cd->src_dir, cd->dest_dir, NULL);

	if(res != MBR_CONFIRM) {
		free(cd->src_dir);
		free(cd->dest_dir);
		for(i = 0; i < cd->count; i++) free(cd->names[i]);
		XtFree((char*)cd->names);
		free(cd);
		return True;
	}

	rv = move_files(cd->src_dir, cd->names, cd->count, cd->dest_dir);
	refresh_src = (app_inst.wlist && app_inst.wlist != cd->dest_widget);

	if(rv) {
		va_message_box(app_inst.wshell, MB_ERROR, APP_TITLE,
			"Could not complete requested action.\n%s.",
			strerror(errno), NULL);
	}

	XtAppAddTimeOut(XtWidgetToApplicationContext(cd->dest_widget),
		1000, dnd_refresh_cb, (XtPointer)cd->dest_widget);
	if(refresh_src)
		XtAppAddTimeOut(XtWidgetToApplicationContext(app_inst.wlist),
			1000, dnd_refresh_cb, (XtPointer)app_inst.wlist);

	free(cd->src_dir);
	free(cd->dest_dir);
	for(i = 0; i < cd->count; i++) free(cd->names[i]);
	XtFree((char*)cd->names);
	free(cd);
	return True;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

void
dnd_register_drop_site(Widget wlist)
{
	Arg args[8];
	Cardinal n = 0;

	XtSetArg(args[n], XmNdropSiteOperations, DND_DROP_OPS); n++;
	XtSetArg(args[n], XmNimportTargets, dnd_import_targets); n++;
	XtSetArg(args[n], XmNnumImportTargets, DND_NUM_IMPORT_TARGETS); n++;
	XtSetArg(args[n], XmNdropProc, dnd_drop_cb); n++;
	XtSetArg(args[n], XmNdragProc, dnd_drag_site_cb); n++;

	XmDropSiteRegister(wlist, args, n);
}

void
dnd_init(Widget wlist)
{
	Display *dpy = XtDisplay(wlist);
	Widget shell;
	int rv;

	XA_TEXT_URI_LIST = XInternAtom(dpy, "text/uri-list", False);
	XA_FILE_NAME = XmInternAtom(dpy, XmSFILE_NAME, False);
	XA_XFILE_FILE_LIST = XInternAtom(dpy, CS_FILE_LIST, False);
	XA_UTF8_STRING = XInternAtom(dpy, "UTF8_STRING", False);
	XA_TARGETS = XInternAtom(dpy, "TARGETS", False);
	XA_TIMESTAMP = XInternAtom(dpy, "TIMESTAMP", False);
	XA_XFILE_DND_DATA = XInternAtom(dpy, "_XFILE_DND_DATA", False);

	dnd_import_targets[0] = XA_TEXT_URI_LIST;
	dnd_import_targets[1] = XA_FILE_NAME;
	dnd_import_targets[2] = XA_XFILE_FILE_LIST;
	dnd_import_targets[3] = XA_UTF8_STRING;

	wlist_ref = wlist;

	/* Walk up the widget hierarchy to find the ApplicationShell.
	 * XtParent(XtParent(wlist)) would only reach XmFrame, not the
	 * shell — the hierarchy is: shell > XmMainWindow > XmFrame >
	 * XmScrolledWindow > wlist. */
	shell = wlist;
	while (shell != NULL && !XtIsShell(shell))
		shell = XtParent(shell);
	rv = x11dnd_xt_init(shell);
	if(rv != 0) {
		stderr_msg("x11dnd_xt_init failed\n");
	}

	/* Cache XDnD action atoms */
	XA_XdndActionCopy = XInternAtom(dpy, "XdndActionCopy", False);
	XA_XdndActionMove = XInternAtom(dpy, "XdndActionMove", False);
	XA_XdndActionLink = XInternAtom(dpy, "XdndActionLink", False);

	/* Set up the X11DndClass callback table */
	memset(&dnd_callbacks, 0, sizeof(dnd_callbacks));
	dnd_callbacks.on_drag_begin = dnd_on_drag_begin;
	dnd_callbacks.on_drag_end = dnd_on_drag_end;
	dnd_callbacks.get_drag_data = dnd_get_drag_data;
	dnd_callbacks.status_received = dnd_status_received;
	dnd_callbacks.finished_received = dnd_finished_received;
	dnd_callbacks.on_enter = dnd_on_enter;
	dnd_callbacks.position_received = dnd_position_received;
	dnd_callbacks.on_leave = dnd_on_leave;
	dnd_callbacks.drop_received = dnd_drop_received;
	dnd_callbacks.action_ask = dnd_action_ask;
	dnd_callbacks.on_error = dnd_on_error;

	/* Register the shell as an XDnD drop target */
	rv = x11dnd_xt_register_target(shell, &dnd_callbacks);

	/* Also register as a Motif drop site for legacy compatibility */
	dnd_register_drop_site(wlist);

	/* Motif's XmDropSiteRegister sets XdndAware on the file list widget's
	 * window.  This confuses XDnD sources (GTK, Qt, Chromium) which walk
	 * the window tree looking for XdndAware and send ClientMessages to
	 * the first window they find — which would be the file list, not
	 * the shell where our event handler is registered.  Remove the
	 * property from the child window; our XDnD registration on the
	 * shell window is sufficient for protocol discovery. */
	if (XtIsRealized(wlist)) {
		XDeleteProperty(XtDisplay(wlist), XtWindow(wlist),
			XInternAtom(XtDisplay(wlist), "XdndAware", False));
	}
}

void
dnd_destroy(void)
{
	/* x11dnd_xt_destroy calls x11dnd_destroy internally */
	x11dnd_xt_destroy();
	wlist_ref = NULL;
	dnd_source_session = NULL;
}

void
dnd_start_drag(Widget w, XEvent *event)
{
	struct file_list_part *fl = FL_PART(w);
	XButtonEvent *bev;
	X11DndSourceSession *sess;
	unsigned char operation;

	if(!fl->drag_and_drop) {
		return;
	}

	/* Prevent re-entry: StartDrag is bound to <Btn1Motion> which
	 * fires on every motion event. Once we've started an XDnD drag
	 * (dnd_source_session != NULL), ignore further calls until the
	 * drag completes and dnd_on_drag_end clears the session. */
	if(dnd_source_session != NULL) {
		return;
	}

	if(file_list_get_selection(w) == NULL
		|| file_list_get_selection(w)->count == 0) {
		return;
	}

	/* Determine default operation */
	if(fl->dnd_copy_modifier) {
		operation = XfDROP_COPY;
	} else if(fl->dnd_move_modifier) {
		operation = XfDROP_MOVE;
	} else {
		operation = fl->dnd_default_op;
	}

	(void)operation; /* operation mapping will be used when libx11dnd
			    supports action selection in the future */

	/* Install BadAtom suppressor for Motif interop */
	dnd_active = True;
	dnd_prev_xerr = XSetErrorHandler(dnd_xerror_handler);

	/* Start the XDnD drag via libx11dnd */
	bev = &event->xbutton;
	sess = x11dnd_xt_start_drag(w, bev, &dnd_callbacks);
	if(sess == NULL) {
		dnd_active = False;
		if(dnd_prev_xerr != NULL) {
			XSetErrorHandler(dnd_prev_xerr);
			dnd_prev_xerr = NULL;
		}
		return;
	}

	dnd_source_session = sess;
	dnd_source_widget = w;
	fl->dnd_state = DND_DRAG;
}

Boolean
dnd_drag_active(void)
{
	return dnd_source_session != NULL;
}

void
dnd_end_drag(void)
{
	if (dnd_source_session == NULL) {
		return;
	}

	if (dnd_source_session->current_target != None
		&& dnd_source_session->state != X11DND_SOURCE_DROP_SENT
		&& dnd_source_session->state != X11DND_SOURCE_FINISHED) {
		x11dnd_source_send_drop(dnd_source_session,
			dnd_source_session->current_target,
			dnd_source_session->start_time);
		/* Stop tracking — the drop has been sent. XdndFinished
		 * or SelectionClear will clean up the session via the
		 * on_drag_end callback.  Remove the Xt work proc and
		 * timer so they stop calling track_motion. */
		x11dnd_xt_stop_tracking();
	} else if (dnd_source_session->current_target == None) {
		x11dnd_xt_cancel_drag();
	}
}