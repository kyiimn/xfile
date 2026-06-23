/*
 * x11dnd_source.c - Drag source state machine and XDnD protocol
 * message sending.
 *
 * Implements the source side of the XDnD v5 protocol: XdndEnter,
 * XdndPosition, XdndLeave, XdndDrop message construction and sending,
 * plus XdndStatus / XdndFinished reception and state transitions.
 *
 * Only one drag source session may be active at a time (enforced by
 * the static 'active_source' pointer).
 *
 * CRITICAL: XdndSelection ownership is taken at drag START (in
 * x11dnd_start_drag), NOT after XdndDrop. The old xdnd.c code took
 * ownership after XdndDrop which is a spec violation.
 */
#include "x11dnd_source.h"
#include "x11dnd_atoms.h"
#include "x11dnd_util.h"

#include <stdio.h>
#include <X11/Xatom.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static X11DndSourceSession *active_source = NULL;

X11DndSourceSession *
x11dnd_start_drag(Display *dpy, Window source_win, X11DndClass *callbacks,
	Time time, Atom *types, int n_types, Atom *actions, int n_actions,
	void *user_data)
{
	X11DndSourceSession *sess;
	const X11DndAtoms *atoms;

	if (dpy == NULL || source_win == None) {
		return NULL;
	}

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		if (callbacks && callbacks->on_error) {
			callbacks->on_error("x11dnd_start_drag: atoms not initialized", 2);
		}
		return NULL;
	}

	if (active_source != NULL) {
		if (callbacks && callbacks->on_error) {
			callbacks->on_error("x11dnd_start_drag: a drag is already active", 2);
		}
		return NULL;
	}

	sess = calloc(1, sizeof(*sess));
	if (sess == NULL) {
		if (callbacks && callbacks->on_error) {
			callbacks->on_error("x11dnd_start_drag: out of memory", 3);
		}
		return NULL;
	}

	sess->dpy = dpy;
	sess->source_win = source_win;
	sess->callbacks = callbacks;
	sess->user_data = user_data;
	sess->start_time = time;
	sess->state = X11DND_SOURCE_DRAGGING;

	sess->types = NULL;
	sess->n_types = 0;
	if (types && n_types > 0) {
		sess->types = malloc((size_t)n_types * sizeof(Atom));
		if (sess->types == NULL) {
			free(sess);
			if (callbacks && callbacks->on_error) {
				callbacks->on_error("x11dnd_start_drag: out of memory", 3);
			}
			return NULL;
		}
		memcpy(sess->types, types, (size_t)n_types * sizeof(Atom));
		sess->n_types = n_types;
	}

	sess->actions = NULL;
	sess->n_actions = 0;
	if (actions && n_actions > 0) {
		sess->actions = malloc((size_t)n_actions * sizeof(Atom));
		if (sess->actions == NULL) {
			free(sess->types);
			free(sess);
			if (callbacks && callbacks->on_error) {
				callbacks->on_error("x11dnd_start_drag: out of memory", 3);
			}
			return NULL;
		}
		memcpy(sess->actions, actions, (size_t)n_actions * sizeof(Atom));
		sess->n_actions = n_actions;
	}

	sess->current_target = None;
	sess->target_version = 0;
	sess->waiting_for_status = False;
	sess->last_sent_x = 0;
	sess->last_sent_y = 0;
	sess->last_position_time = 0;
	sess->last_action = None;
	sess->drop_completed = False;
	sess->performed_action = None;

	XSetSelectionOwner(dpy, atoms->XdndSelection, source_win, time);
	XFlush(dpy);

	/* Verify we actually own the selection. XSetSelectionOwner can
	 * silently fail if another client with higher priority grabbed it
	 * or if the server rejected the ownership change. */
	if (XGetSelectionOwner(dpy, atoms->XdndSelection) != source_win) {
		if (callbacks && callbacks->on_error) {
			callbacks->on_error(
				"x11dnd_start_drag: failed to acquire XdndSelection ownership",
				2);
		}
		free(sess->types);
		free(sess->actions);
		free(sess);
		return NULL;
	}

	active_source = sess;

	if (callbacks && callbacks->on_drag_begin) {
		callbacks->on_drag_begin(sess);
	}

	return sess;
}

void
x11dnd_cancel_drag(X11DndSourceSession *sess)
{
	const X11DndAtoms *atoms;

	if (sess == NULL) {
		return;
	}

	atoms = x11dnd_get_atoms();

	if (sess->current_target != None && atoms != NULL) {
		x11dnd_source_send_leave(sess, sess->current_target);
	}

	if (atoms != NULL) {
		XSetSelectionOwner(sess->dpy, atoms->XdndSelection, None,
			sess->start_time);
		XFlush(sess->dpy);
	}

	sess->state = X11DND_SOURCE_FINISHED;

	if (sess->callbacks && sess->callbacks->on_drag_end) {
		sess->callbacks->on_drag_end(sess, False);
	}

	if (active_source == sess) {
		active_source = NULL;
	}

	free(sess->types);
	free(sess->actions);
	free(sess);
}

void
x11dnd_end_drag(X11DndSourceSession *sess)
{
	const X11DndAtoms *atoms;

	if (sess == NULL) {
		return;
	}

	atoms = x11dnd_get_atoms();

	if (atoms != NULL) {
		XSetSelectionOwner(sess->dpy, atoms->XdndSelection, None,
			sess->start_time);
		XFlush(sess->dpy);
	}

	sess->state = X11DND_SOURCE_FINISHED;

	if (sess->callbacks && sess->callbacks->on_drag_end) {
		sess->callbacks->on_drag_end(sess, True);
	}

	if (active_source == sess) {
		active_source = NULL;
	}

	free(sess->types);
	free(sess->actions);
	free(sess);
}

void
x11dnd_source_send_enter(X11DndSourceSession *sess, Window target)
{
	const X11DndAtoms *atoms;
	long data[5];
	int more_than_3;
	int i;

	if (sess == NULL || target == None) {
		return;
	}

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return;
	}

	data[0] = 0;

	more_than_3 = (sess->n_types > 3) ? 1 : 0;
	data[1] = ((long)X11DND_VERSION_5 << 24) | (more_than_3 & 0x1);

	if (sess->n_types <= 3) {
		for (i = 0; i < 3; i++) {
			data[2 + i] = (i < sess->n_types) ? (long)sess->types[i] : 0;
		}
	} else {
		data[2] = 0;
		data[3] = 0;
		data[4] = 0;

		XChangeProperty(sess->dpy, sess->source_win, atoms->XdndTypeList,
			XA_ATOM, 32, PropModeReplace,
			(unsigned char *)sess->types, sess->n_types);
		XFlush(sess->dpy);
	}

	x11dnd_send_client_message(sess->dpy, target, sess->source_win,
		atoms->XdndEnter, data, CurrentTime);

	sess->current_target = target;
	sess->state = X11DND_SOURCE_ENTERED;
}

void
x11dnd_source_send_position(X11DndSourceSession *sess, Window target,
	int x, int y, Time time, Atom action)
{
	const X11DndAtoms *atoms;
	long data[5];

	if (sess == NULL || target == None) {
		return;
	}

	if (sess->waiting_for_status) {
		return;
	}

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return;
	}

	data[0] = 0;
	data[1] = 0;
	data[2] = ((long)x << 16) | (y & 0xFFFF);
	data[3] = (long)time;
	data[4] = (long)action;

	x11dnd_send_client_message(sess->dpy, target, sess->source_win,
		atoms->XdndPosition, data, time);

	sess->waiting_for_status = True;
	sess->last_sent_x = x;
	sess->last_sent_y = y;
	sess->last_position_time = time;
	sess->last_action = action;
	sess->state = X11DND_SOURCE_WAITING_STATUS;
}

void
x11dnd_source_send_leave(X11DndSourceSession *sess, Window target)
{
	const X11DndAtoms *atoms;
	long data[5];

	if (sess == NULL || target == None) {
		return;
	}

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return;
	}

	data[0] = 0;
	data[1] = 0;
	data[2] = 0;
	data[3] = 0;
	data[4] = 0;

	x11dnd_send_client_message(sess->dpy, target, sess->source_win,
		atoms->XdndLeave, data, CurrentTime);

	sess->current_target = None;
	sess->state = X11DND_SOURCE_DRAGGING;
}

void
x11dnd_source_send_drop(X11DndSourceSession *sess, Window target, Time time)
{
	const X11DndAtoms *atoms;
	long data[5];

	if (sess == NULL || target == None) {
		return;
	}

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return;
	}

	data[0] = 0;
	data[1] = 0;
	data[2] = (long)time;
	data[3] = 0;
	data[4] = 0;

	x11dnd_send_client_message(sess->dpy, target, sess->source_win,
		atoms->XdndDrop, data, time);

	sess->state = X11DND_SOURCE_DROP_SENT;
}

int
x11dnd_source_handle_status(X11DndSourceSession *sess,
	XClientMessageEvent *ev)
{
	Bool accept;
	int x, y, w, h;
	Atom action;

	if (sess == NULL || ev == NULL) {
		return 0;
	}

	/* Ignore XdndStatus after XdndDrop has been sent or the drag
	 * has finished — stale messages must not regress the state. */
	if (sess->state == X11DND_SOURCE_DROP_SENT
		|| sess->state == X11DND_SOURCE_FINISHED) {
		return 1;
	}

	accept = (ev->data.l[1] & 0x1) ? True : False;
	x = (int)((ev->data.l[2] >> 16) & 0xFFFF);
	y = (int)(ev->data.l[2] & 0xFFFF);
	w = (int)((ev->data.l[3] >> 16) & 0xFFFF);
	h = (int)(ev->data.l[3] & 0xFFFF);
	action = (Atom)(ev->data.l[4] & 0xFFFFFFFFUL);

	sess->waiting_for_status = False;
	sess->state = X11DND_SOURCE_ENTERED;

	if (sess->callbacks && sess->callbacks->status_received) {
		sess->callbacks->status_received(sess, accept, x, y, w, h, action);
	}

	return 1;
}

int
x11dnd_source_handle_finished(X11DndSourceSession *sess,
	XClientMessageEvent *ev)
{
	Bool success;
	Atom performed_action;

	if (sess == NULL || ev == NULL) {
		return 0;
	}

	success = ev->data.l[1] ? True : False;
	performed_action = (Atom)(ev->data.l[2] & 0xFFFFFFFFUL);

	sess->state = X11DND_SOURCE_FINISHED;
	sess->drop_completed = success;
	sess->performed_action = performed_action;

	if (sess->callbacks && sess->callbacks->finished_received) {
		sess->callbacks->finished_received(sess, success, performed_action);
	}

	if (sess->callbacks && sess->callbacks->on_drag_end) {
		sess->callbacks->on_drag_end(sess, success);
	}

	if (active_source == sess) {
		active_source = NULL;
	}

	free(sess->types);
	free(sess->actions);
	free(sess);

	return 1;
}

/* ------------------------------------------------------------------ *
 * Data conversion helpers for SelectionRequest responses.
 * Each returns a malloc'd buffer and sets *len_ret. On failure returns
 * NULL and *len_ret = 0.
 * ------------------------------------------------------------------ */

static unsigned char *
convert_to_uri_list(char **paths, int n_paths, unsigned long *len_ret)
{
	size_t cap = 256;
	size_t used = 0;
	unsigned char *buf;
	int i;

	if (len_ret) {
		*len_ret = 0;
	}
	if (paths == NULL || n_paths <= 0) {
		return NULL;
	}

	buf = malloc(cap);
	if (buf == NULL) {
		return NULL;
	}

	for (i = 0; i < n_paths; i++) {
		const char *p = paths[i];
		size_t plen;
		size_t need;
		char *enc;
		size_t j, k;

		if (p == NULL) {
			continue;
		}
		plen = strlen(p);

		/* Worst case: every char needs %XX (3x) + "file://" + "\r\n" */
		need = plen * 3 + 16;
		if (used + need + 1 > cap) {
			while (used + need + 1 > cap) {
				cap *= 2;
			}
			{
				unsigned char *nb = realloc(buf, cap);
				if (nb == NULL) {
					free(buf);
					return NULL;
				}
				buf = nb;
			}
		}

		memcpy(buf + used, "file://", 7);
		used += 7;

		/* URL-encode: space -> %20, other unreserved chars pass through.
		 * Basic encoding per RFC 3986 — encode chars that are not
		 * unreserved (alphanumeric or -._~). Keep '/' as-is for paths. */
		enc = (char *)(buf + used);
		k = 0;
		for (j = 0; j < plen; j++) {
			unsigned char c = (unsigned char)p[j];
			if (isalnum(c) || c == '-' || c == '.' || c == '_' ||
			    c == '~' || c == '/') {
				enc[k++] = (char)c;
			} else {
				k += (size_t)snprintf(enc + k, 4, "%%%02X", c);
			}
		}
		used += k;

		buf[used++] = '\r';
		buf[used++] = '\n';
	}

	buf[used] = '\0';
	if (len_ret) {
		*len_ret = (unsigned long)used;
	}
	return buf;
}

static unsigned char *
convert_to_utf8_string(char **paths, int n_paths, unsigned long *len_ret)
{
	size_t cap = 256;
	size_t used = 0;
	unsigned char *buf;
	int i;

	if (len_ret) {
		*len_ret = 0;
	}
	if (paths == NULL || n_paths <= 0) {
		return NULL;
	}

	buf = malloc(cap);
	if (buf == NULL) {
		return NULL;
	}

	for (i = 0; i < n_paths; i++) {
		const char *p = paths[i];
		size_t plen;
		size_t sep;

		if (p == NULL) {
			continue;
		}
		plen = strlen(p);
		sep = (i < n_paths - 1) ? 1 : 0;

		if (used + plen + sep + 1 > cap) {
			while (used + plen + sep + 1 > cap) {
				cap *= 2;
			}
			{
				unsigned char *nb = realloc(buf, cap);
				if (nb == NULL) {
					free(buf);
					return NULL;
				}
				buf = nb;
			}
		}

		memcpy(buf + used, p, plen);
		used += plen;
		if (sep) {
			buf[used++] = '\n';
		}
	}

	buf[used] = '\0';
	if (len_ret) {
		*len_ret = (unsigned long)used;
	}
	return buf;
}

static unsigned char *
convert_to_plain_text(char **paths, int n_paths, unsigned long *len_ret)
{
	return convert_to_utf8_string(paths, n_paths, len_ret);
}

static unsigned char *
convert_to_string(char **paths, int n_paths, unsigned long *len_ret)
{
	unsigned char *buf;
	unsigned long len;
	unsigned long i;

	buf = convert_to_utf8_string(paths, n_paths, &len);
	if (buf == NULL) {
		if (len_ret) {
			*len_ret = 0;
		}
		return NULL;
	}

	for (i = 0; i < len; i++) {
		if (buf[i] >= 0x80) {
			buf[i] = '?';
		}
	}

	if (len_ret) {
		*len_ret = len;
	}
	return buf;
}

static unsigned char *
convert_to_file_name(char **paths, int n_paths, unsigned long *len_ret)
{
	if (paths == NULL || n_paths <= 0 || paths[0] == NULL) {
		if (len_ret) {
			*len_ret = 0;
		}
		return NULL;
	}
	{
		char *single[1];
		single[0] = paths[0];
		return convert_to_string(single, 1, len_ret);
	}
}

static unsigned char *
convert_to_targets(Display *dpy, unsigned long *len_ret, int *format_ret)
{
	const X11DndAtoms *atoms;
	Atom *list;

	if (len_ret) {
		*len_ret = 0;
	}
	if (format_ret) {
		*format_ret = 0;
	}
	if (dpy == NULL) {
		return NULL;
	}

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return NULL;
	}

	list = malloc(7 * sizeof(Atom));
	if (list == NULL) {
		return NULL;
	}

	list[0] = atoms->text_uri_list;
	list[1] = atoms->UTF8_STRING;
	list[2] = atoms->STRING;
	list[3] = atoms->FILE_NAME;
	list[4] = atoms->text_plain;
	list[5] = atoms->TARGETS;
	list[6] = atoms->TIMESTAMP;

	if (len_ret) {
		*len_ret = 7;
	}
	if (format_ret) {
		*format_ret = 32;
	}
	return (unsigned char *)list;
}

static unsigned char *
convert_to_timestamp(X11DndSourceSession *sess, unsigned long *len_ret)
{
	long *val;

	if (len_ret) {
		*len_ret = 0;
	}
	if (sess == NULL) {
		return NULL;
	}

	val = malloc(sizeof(long));
	if (val == NULL) {
		return NULL;
	}

	*val = (long)sess->start_time;

	if (len_ret) {
		*len_ret = 1;
	}
	return (unsigned char *)val;
}

static void
send_selection_notify(Display *dpy, Window requestor, Atom selection,
	Atom target, Atom property, Time time, Bool success)
{
	XSelectionEvent notify;

	memset(&notify, 0, sizeof(notify));
	notify.type = SelectionNotify;
	notify.requestor = requestor;
	notify.selection = selection;
	notify.target = target;
	notify.property = success ? property : None;
	notify.time = time;

	XSendEvent(dpy, requestor, False, NoEventMask, (XEvent *)&notify);
	XFlush(dpy);
}

int
x11dnd_source_handle_selection_request(XEvent *ev)
{
	const X11DndAtoms *atoms;
	XSelectionRequestEvent *req;
	X11DndSourceSession *sess;
	unsigned char *data;
	unsigned long length;
	int format;
	Atom prop_target;
	Bool success;
	char **paths = NULL;
	int n_paths = 0;

	if (ev == NULL) {
		return 0;
	}
	if (ev->type != SelectionRequest) {
		return 0;
	}

	sess = active_source;
	if (sess == NULL) {
		return 0;
	}

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return 0;
	}

	req = &ev->xselectionrequest;

	fprintf(stderr, "XDND SOURCE: SelectionRequest selection=%ld XdndSel=%ld target=%ld property=%ld requestor=0x%lx time=%lu\n",
		(long)req->selection, (long)atoms->XdndSelection,
		(long)req->target, (long)req->property,
		(unsigned long)req->requestor, (unsigned long)req->time);

	if (req->selection != atoms->XdndSelection) {
		return 0;
	}

	data = NULL;
	length = 0;
	format = 8;
	success = False;

	/* Fetch paths from the get_drag_data callback if available.
	 * If the callback provides data in the exact requested format,
	 * use it directly. Otherwise, split into paths and convert. */
	if (sess->callbacks && sess->callbacks->get_drag_data) {
		unsigned char *raw = NULL;
		unsigned long raw_len = 0;
		int raw_fmt = 8;

		sess->callbacks->get_drag_data(sess, req->target,
			&raw, &raw_len, &raw_fmt);
		if (raw && raw_len > 0) {
			/* If the callback produced data for the exact target type
			 * we were asked for, use it as-is without re-encoding.
			 * Jump past the path-splitting and conversion to the
			 * XChangeProperty + SelectionNotify section. */
			if (req->target == atoms->text_uri_list
				|| req->target == atoms->UTF8_STRING
				|| req->target == atoms->STRING
				|| req->target == atoms->FILE_NAME
				|| req->target == atoms->text_plain) {
				data = raw;
				length = raw_len;
				format = raw_fmt;
				success = True;
				goto send_notify;
			}
			/* The callback provides paths as a NUL-separated or
			 * newline-separated list. Split into an array.
			 * Handle \r\n and \n as line terminators per
			 * text/uri-list spec; skip empty lines. */
			char *p = (char *)raw;
			unsigned long pos;
			size_t cap;

			cap = 8;
			paths = malloc(cap * sizeof(char *));
			if (paths == NULL) {
				free(raw);
				goto done;
			}
			n_paths = 0;

			pos = 0;
			while (pos < raw_len) {
				unsigned long line_start, line_end;
				size_t slen;
				char *dup;

				line_start = pos;

				/* Find end of this line */
				while (pos < raw_len && p[pos] != '\n'
					&& p[pos] != '\0') {
					pos++;
				}
				line_end = pos;

				/* Strip trailing \r */
				if (line_end > line_start
					&& p[line_end - 1] == '\r') {
					line_end--;
				}

				/* Skip past delimiter */
				if (pos < raw_len && p[pos] == '\n') {
					pos++;
				} else if (pos < raw_len && p[pos] == '\0') {
					pos++;
				}

				slen = (size_t)(line_end - line_start);
				if (slen == 0) {
					continue;
				}

				if ((size_t)n_paths >= cap) {
					cap *= 2;
					{
						char **np = realloc(paths,
							cap * sizeof(char *));
						if (np == NULL) {
							/* Free individual
							 * entries first */
							int j;
							for (j = 0; j < n_paths;
								j++) {
								free(paths[j]);
							}
							free(paths);
							paths = NULL;
							n_paths = 0;
							free(raw);
							goto done;
						}
						paths = np;
					}
				}

				dup = malloc(slen + 1);
				if (dup) {
					memcpy(dup, p + line_start, slen);
					dup[slen] = '\0';
					paths[n_paths++] = dup;
				}
			}
			free(raw);
		} else {
			if (raw) {
				free(raw);
			}
		}
	}

	/* Convert based on requested target */
	if (req->target == atoms->text_uri_list) {
		data = convert_to_uri_list(paths, n_paths, &length);
		format = 8;
		success = (data != NULL);
	} else if (req->target == atoms->UTF8_STRING) {
		data = convert_to_utf8_string(paths, n_paths, &length);
		format = 8;
		success = (data != NULL);
	} else if (req->target == atoms->STRING) {
		data = convert_to_string(paths, n_paths, &length);
		format = 8;
		success = (data != NULL);
	} else if (req->target == atoms->FILE_NAME) {
		data = convert_to_file_name(paths, n_paths, &length);
		format = 8;
		success = (data != NULL);
	} else if (req->target == atoms->text_plain) {
		data = convert_to_plain_text(paths, n_paths, &length);
		format = 8;
		success = (data != NULL);
	} else if (req->target == atoms->TARGETS) {
		data = convert_to_targets(sess->dpy, &length, &format);
		success = (data != NULL);
	} else if (req->target == atoms->TIMESTAMP) {
		data = convert_to_timestamp(sess, &length);
		format = 32;
		success = (data != NULL);
	} else {
		success = False;
	}

send_notify:
	prop_target = success ? req->property : None;

	if (success && data != NULL && req->property != None) {
		XChangeProperty(sess->dpy, req->requestor, req->property,
			req->target, format, PropModeReplace, data,
			(int)length);
		XFlush(sess->dpy);
	}

	send_selection_notify(sess->dpy, req->requestor, req->selection,
		req->target, prop_target, req->time, success);

	if (data) {
		free(data);
	}

done:
	if (paths != NULL) {
		int i;
		for (i = 0; i < n_paths; i++) {
			free(paths[i]);
		}
		free(paths);
	}

	return 1;
}

int
x11dnd_source_handle_selection_clear(XEvent *ev)
{
	const X11DndAtoms *atoms;
	X11DndSourceSession *sess;

	if (ev == NULL) {
		return 0;
	}
	if (ev->type != SelectionClear) {
		return 0;
	}

	sess = active_source;

	if (sess == NULL) {
		return 0;
	}

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return 0;
	}

	if (ev->xselectionclear.selection != atoms->XdndSelection) {
		return 0;
	}

	/* Ignore stale SelectionClear events: if the event was sent to a
	 * different window or predates our drag start, it belongs to a
	 * previous session and must not cancel our active drag. */
	if (ev->xselectionclear.window != sess->source_win) {
		return 0;
	}
	if (ev->xselectionclear.time <= sess->start_time) {
		return 0;
	}

	if (sess->callbacks && sess->callbacks->on_error) {
		sess->callbacks->on_error("Lost XdndSelection ownership", 2);
	}

	x11dnd_cancel_drag(sess);

	return 1;
}

int
x11dnd_source_process_event(XEvent *ev)
{
	const X11DndAtoms *atoms;
	XClientMessageEvent *cm;

	if (ev == NULL) {
		return 0;
	}

	if (active_source == NULL) {
		return 0;
	}

	if (ev->type == SelectionRequest) {
		return x11dnd_source_handle_selection_request(ev);
	}

	if (ev->type == SelectionClear) {
		return x11dnd_source_handle_selection_clear(ev);
	}

	if (ev->type != ClientMessage) {
		return 0;
	}

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return 0;
	}

	cm = &ev->xclient;

	if (cm->message_type == atoms->XdndStatus) {
		return x11dnd_source_handle_status(active_source, cm);
	}

	if (cm->message_type == atoms->XdndFinished) {
		return x11dnd_source_handle_finished(active_source, cm);
	}

	return 0;
}

void
x11dnd_source_track_motion(X11DndSourceSession *sess, int x, int y,
	Time time)
{
	const X11DndAtoms *atoms;
	Window root_return, child, target, proxy;
	int dest_x, dest_y;
	unsigned int mask;
	Window root;

	if (sess == NULL) {
		return;
	}

	if (sess->state == X11DND_SOURCE_DROP_SENT ||
		sess->state == X11DND_SOURCE_FINISHED) {
		return;
	}

	atoms = x11dnd_get_atoms();
	if (atoms == NULL) {
		return;
	}

	root = DefaultRootWindow(sess->dpy);

	/* Find the deepest window under the pointer */
	child = None;
	if (!XQueryPointer(sess->dpy, root,
		&root_return, &child, &x, &y, &dest_x, &dest_y, &mask)) {
		return;
	}

	if (child == None) {
		/* No window under the pointer */
	}

	/* Descend into the child window tree to find the deepest window */
	if (child != None) {
		child = x11dnd_find_window_at_point(sess->dpy, root, x, y);
	}

	/* Find the nearest XdndAware ancestor */
	if (child != None) {
		target = x11dnd_find_aware_ancestor(sess->dpy, child);
	} else {
		target = None;
	}

	/* Check for XdndProxy */
	if (target != None) {
		proxy = x11dnd_validate_proxy(sess->dpy, target);
		if (proxy != None) {
			target = proxy;
		}
	}

	/* Prevent self-drag: skip if the target is our own source window */
	if (target == sess->source_win) {
		target = None;
	}

	/* Target changed: send Leave to old, Enter to new */
	if (target != sess->current_target) {
		if (sess->current_target != None) {
			x11dnd_source_send_leave(sess, sess->current_target);
		}
		if (target != None) {
			x11dnd_source_send_enter(sess, target);
		}
	}

	/* Send XdndPosition if we have a target and not waiting for status */
	if (target != None && !sess->waiting_for_status) {
		Atom action = sess->actions[0];
		x11dnd_source_send_position(sess, target, x, y, time, action);
	}
}

void *
x11dnd_source_get_user_data(X11DndSourceSession *sess)
{
	if (sess == NULL) {
		return NULL;
	}
	return sess->user_data;
}

Window
x11dnd_source_get_window(X11DndSourceSession *sess)
{
	if (sess == NULL) {
		return None;
	}
	return sess->source_win;
}

Display *
x11dnd_source_get_display(X11DndSourceSession *sess)
{
	if (sess == NULL) {
		return NULL;
	}
	return sess->dpy;
}