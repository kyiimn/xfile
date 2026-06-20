/*
 * Copyright (C) 2025-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

/*
 * Drag-and-drop support for the file list widget
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <Xm/XmP.h>
#include <Xm/DragDrop.h>
#include <Xm/DragIcon.h>
#include <Xm/AtomMgr.h>
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

/* Widget reference for drag/drop callbacks */
static Widget wlist_ref = NULL;

/* Export targets: atoms the drag source can convert */
Atom dnd_export_targets[DND_NUM_EXPORT_TARGETS];

/* Import targets: atoms the drop site accepts */
Atom dnd_import_targets[DND_NUM_IMPORT_TARGETS];

#define FL_PART(w) &((struct file_list_rec*)w)->file_list
#define FL_REC(w) (struct file_list_rec*)w

/* Context passed through XmNclientData during a drag */
struct dnd_drag_context {
	Widget source_widget;
	unsigned int num_items;
	char **paths;
	unsigned char operation;
	Widget drag_icon;
	char *dir_path;
	char **names;
};

/* Data passed from dnd_drop_cb to dnd_transfer_cb via client_data */
struct dnd_transfer_data {
	char *dest_dir;
	unsigned char operation;  /* XmDROP_COPY, XmDROP_MOVE, etc. */
};

/* Local routines */
static Widget dnd_create_drag_icon(Widget w, Pixmap icon, Pixmap mask);
static void dnd_convert_cb(Widget w, XtPointer client_data,
	XtPointer call_data);
static void dnd_operation_changed_cb(Widget w, XtPointer client_data,
	XtPointer call_data);
static void dnd_drag_finish_cb(Widget w, XtPointer client_data,
	XtPointer call_data);
static void dnd_drag_end_cb(Widget w, XtPointer client_data,
	XtPointer call_data);
static void dnd_drag_site_cb(Widget w, XtPointer client, XtPointer call);
static void dnd_drop_cb(Widget w, XtPointer client, XtPointer call);
static void dnd_transfer_cb(Widget w, XtPointer client, XtPointer call);
static char *dnd_uri_to_path(const char *uri);
static void dnd_refresh_cb(XtPointer client_data, XtIntervalId *id);
static Boolean dnd_move_confirm_wp(XtPointer client_data);
static void dnd_perform_operation(const char *src_dir, char **names,
	unsigned int count, const char *dest_dir, unsigned char operation);

static Widget
dnd_create_drag_icon(Widget w, Pixmap icon, Pixmap mask)
{
	Arg args[4];
	Cardinal n = 0;
	Widget icon_widget;
	
	if(icon == None) return NULL;
	
	XtSetArg(args[n], XmNpixmap, icon); n++;
	if(mask != None) {
		XtSetArg(args[n], XmNmask, mask); n++;
	}
	XtSetArg(args[n], XmNdepth,
		DefaultDepthOfScreen(XtScreen(w))); n++;

	icon_widget = XmCreateDragIcon(w, "dragIcon", args, n);
	if(icon_widget == NULL) return NULL;

	return icon_widget;
}

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

static void
dnd_convert_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
	struct dnd_drag_context *ctx = (struct dnd_drag_context*)client_data;
	XmConvertCallbackStruct *cs = (XmConvertCallbackStruct*)call_data;
	unsigned int i;
	char *data;
	size_t len;

	(void)w;

	if(ctx == NULL || cs == NULL) return;

	if(cs->target == XA_TEXT_URI_LIST) {
		len = 0;
		for(i = 0; i < ctx->num_items; i++) {
			len += strlen("file://") + strlen(ctx->paths[i]) + 2;
		}
		if(len == 0) {
			cs->status = XmCONVERT_REFUSE;
			return;
		}

		data = XtMalloc(len + 1);
		if(!data) {
			cs->status = XmCONVERT_REFUSE;
			return;
		}
		data[0] = '\0';

		for(i = 0; i < ctx->num_items; i++) {
			if(i > 0) strcat(data, "\r\n");
			strcat(data, "file://");
			strcat(data, ctx->paths[i]);
		}

		cs->type = XA_TEXT_URI_LIST;
		cs->format = 8;
		cs->length = strlen(data);
		cs->value = data;
		cs->status = XmCONVERT_DONE;

	} else if(cs->target == XA_STRING || cs->target == XA_UTF8_STRING) {
		len = 0;
		for(i = 0; i < ctx->num_items; i++) {
			len += strlen(ctx->paths[i]) + 1;
		}
		if(len == 0) {
			cs->status = XmCONVERT_REFUSE;
			return;
		}

		data = XtMalloc(len + 1);
		if(!data) {
			cs->status = XmCONVERT_REFUSE;
			return;
		}
		data[0] = '\0';

		for(i = 0; i < ctx->num_items; i++) {
			if(i > 0) strcat(data, " ");
			strcat(data, ctx->paths[i]);
		}

		if(cs->target == XA_STRING)
			mbs_to_latin1(data, data);

		cs->type = cs->target;
		cs->format = 8;
		cs->length = strlen(data);
		cs->value = data;
		cs->status = XmCONVERT_DONE;

	} else if(cs->target == XA_FILE_NAME) {
		if(ctx->num_items == 0) {
			cs->status = XmCONVERT_REFUSE;
			return;
		}
		data = XtMalloc(strlen(ctx->paths[0]) + 1);
		if(!data) {
			cs->status = XmCONVERT_REFUSE;
			return;
		}
		strcpy(data, ctx->paths[0]);

		cs->type = XA_FILE_NAME;
		cs->format = 8;
		cs->length = strlen(data);
		cs->value = data;
		cs->status = XmCONVERT_DONE;

	} else if(cs->target == XA_XFILE_FILE_LIST) {
		char *path;
		char *pos;

		if(!ctx->dir_path || ctx->num_items == 0) {
			cs->status = XmCONVERT_REFUSE;
			return;
		}

		path = realpath(ctx->dir_path, NULL);
		if(!path) {
			cs->status = XmCONVERT_REFUSE;
			return;
		}

		len = strlen(path) + 1;
		for(i = 0; i < ctx->num_items; i++) {
			len += strlen(ctx->names[i]) + 1;
		}

		data = XtMalloc(len + 1);
		if(!data) {
			free(path);
			cs->status = XmCONVERT_REFUSE;
			return;
		}

		strcpy(data, path);
		pos = data + strlen(path) + 1;
		free(path);

		for(i = 0; i < ctx->num_items; i++) {
			strcpy(pos, ctx->names[i]);
			pos += strlen(ctx->names[i]);
			*pos = '\0';
			pos++;
		}
		*pos = '\0';

		cs->type = XA_XFILE_FILE_LIST;
		cs->format = 8;
		cs->length = len + 1;
		cs->value = data;
		cs->status = XmCONVERT_DONE;

	} else {
		cs->status = XmCONVERT_REFUSE;
	}
}

static void
dnd_operation_changed_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
	struct dnd_drag_context *ctx = (struct dnd_drag_context*)client_data;
	XmOperationChangedCallbackStruct *ocs =
		(XmOperationChangedCallbackStruct*)call_data;

	(void)w;

	if(ctx == NULL || ocs == NULL) return;

	ctx->operation = ocs->operation;
}

static void
dnd_drag_finish_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
	struct dnd_drag_context *ctx = (struct dnd_drag_context*)client_data;
	struct file_list_part *fl;

	(void)w;
	(void)call_data;

	if(ctx == NULL) return;

	if(ctx->drag_icon != NULL) {
		XtDestroyWidget(ctx->drag_icon);
		ctx->drag_icon = NULL;
	}

	if(ctx->paths != NULL) {
		unsigned int i;
		for(i = 0; i < ctx->num_items; i++) {
			if(ctx->paths[i] != NULL) XtFree(ctx->paths[i]);
		}
		XtFree((char*)ctx->paths);
	}

	if(ctx->names != NULL) {
		unsigned int i;
		for(i = 0; i < ctx->num_items; i++) {
			if(ctx->names[i] != NULL) XtFree(ctx->names[i]);
		}
		XtFree((char*)ctx->names);
	}

	if(ctx->dir_path != NULL) XtFree(ctx->dir_path);

	if(ctx->source_widget != NULL) {
		fl = FL_PART(ctx->source_widget);
		if(fl != NULL) fl->dnd_state = DND_NONE;
	}

	XtFree((char*)ctx);
}

static void __attribute__((unused))
dnd_drag_end_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
	struct dnd_drag_context *ctx = (struct dnd_drag_context*)client_data;
	struct file_list_part *fl;

	(void)w;
	(void)call_data;

	if(ctx != NULL && ctx->source_widget != NULL) {
		fl = FL_PART(ctx->source_widget);
		if(fl != NULL) fl->dnd_state = DND_NONE;
	}
}

static void
dnd_realize_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
	Arg args[8];
	Cardinal n = 0;

	(void)client_data;
	(void)call_data;

	XtSetArg(args[n], XmNdropSiteOperations, DND_DROP_OPS); n++;
	XtSetArg(args[n], XmNimportTargets, dnd_import_targets); n++;
	XtSetArg(args[n], XmNnumImportTargets, DND_NUM_IMPORT_TARGETS); n++;
	XtSetArg(args[n], XmNdropProc, dnd_drop_cb); n++;
	XtSetArg(args[n], XmNdragProc, dnd_drag_site_cb); n++;

	XmDropSiteRegister(w, args, n);

	/* One-shot: remove callback after registration */
	XtRemoveCallback(w, XmNrealizeCallback, dnd_realize_cb, NULL);
}

void
dnd_register_drop_site(Widget wlist)
{
	if(XtIsRealized(wlist)) {
		dnd_realize_cb(wlist, NULL, NULL);
	} else {
		XtAddCallback(wlist, XmNrealizeCallback, dnd_realize_cb, NULL);
	}
}

void
dnd_init(Widget wlist)
{
	Display *dpy = XtDisplay(wlist);

	XA_TEXT_URI_LIST = XInternAtom(dpy, "text/uri-list", False);
	XA_FILE_NAME = XmInternAtom(dpy, XmSFILE_NAME, False);
	XA_XFILE_FILE_LIST = XInternAtom(dpy, CS_FILE_LIST, False);
	XA_UTF8_STRING = XInternAtom(dpy, "UTF8_STRING", False);

	dnd_export_targets[0] = XA_TEXT_URI_LIST;
	dnd_export_targets[1] = XA_FILE_NAME;
	dnd_export_targets[2] = XA_XFILE_FILE_LIST;
	dnd_export_targets[3] = XA_UTF8_STRING;

	dnd_import_targets[0] = XA_TEXT_URI_LIST;
	dnd_import_targets[1] = XA_FILE_NAME;
	dnd_import_targets[2] = XA_XFILE_FILE_LIST;
	dnd_import_targets[3] = XA_UTF8_STRING;

	wlist_ref = wlist;
	dnd_register_drop_site(wlist);
}

void
dnd_destroy(void)
{
	wlist_ref = NULL;
}

void
dnd_start_drag(Widget w, XEvent *event)
{
	struct file_list_part *fl = FL_PART(w);
	struct file_list_selection *sel;
	struct dnd_drag_context *ctx;
	Arg args[16];
	Cardinal n = 0;
	unsigned int i;
	unsigned char operation;
	Widget drag_context;
	Widget drag_icon = NULL;

	if(!fl->drag_and_drop) return;

	sel = file_list_get_selection(w);
	if(sel == NULL || sel->count == 0) return;

	ctx = (struct dnd_drag_context*)XtMalloc(
		sizeof(struct dnd_drag_context));
	if(!ctx) return;

	memset(ctx, 0, sizeof(struct dnd_drag_context));
	ctx->source_widget = w;
	ctx->num_items = sel->count;
	ctx->dir_path = dnd_dir_path_from_widget(w);

	ctx->names = (char**)XtMalloc(sizeof(char*) * sel->count);
	ctx->paths = (char**)XtMalloc(sizeof(char*) * sel->count);
	if(!ctx->names || !ctx->paths) goto fail;

	for(i = 0; i < sel->count; i++) {
		ctx->names[i] = NULL;
		ctx->paths[i] = NULL;
	}

	for(i = 0; i < sel->count; i++) {
		ctx->names[i] = XtMalloc(strlen(sel->names[i]) + 1);
		if(!ctx->names[i]) goto fail;
		strcpy(ctx->names[i], sel->names[i]);

		ctx->paths[i] = dnd_make_absolute_path(ctx->dir_path, sel->names[i]);
		if(!ctx->paths[i]) goto fail;
	}

	if(fl->dnd_copy_modifier) {
		operation = XmDROP_COPY;
	} else if(fl->dnd_move_modifier) {
		operation = XmDROP_MOVE;
	} else {
		operation = fl->dnd_default_op;
	}
	ctx->operation = operation;

	if(sel->item.icon != None) {
		drag_icon = dnd_create_drag_icon(w, sel->item.icon,
			sel->item.icon_mask);
	}
	ctx->drag_icon = drag_icon;

	XtSetArg(args[n], XmNexportTargets, dnd_export_targets); n++;
	XtSetArg(args[n], XmNnumExportTargets, DND_NUM_EXPORT_TARGETS); n++;
	XtSetArg(args[n], XmNdragOperations, DND_DRAG_OPS); n++;
	XtSetArg(args[n], XmNconvertCallback, dnd_convert_cb); n++;
	XtSetArg(args[n], XmNdragDropFinishCallback, dnd_drag_finish_cb); n++;
	XtSetArg(args[n], XmNclientData, (XtPointer)ctx); n++;
	if(drag_icon != NULL) {
		XtSetArg(args[n], XmNsourceCursorIcon, drag_icon); n++;
	}

	drag_context = XmDragStart(w, event, args, n);
	if(drag_context == NULL) goto fail;

	XtAddCallback(drag_context, XmNoperationChangedCallback,
		dnd_operation_changed_cb, (XtPointer)ctx);

	fl->dnd_state = DND_DRAG;
	return;

fail:
	if(ctx != NULL) {
		if(ctx->paths != NULL) {
			for(i = 0; i < ctx->num_items; i++) {
				if(ctx->paths[i] != NULL) XtFree(ctx->paths[i]);
			}
			XtFree((char*)ctx->paths);
		}
		if(ctx->names != NULL) {
			for(i = 0; i < ctx->num_items; i++) {
				if(ctx->names[i] != NULL) XtFree(ctx->names[i]);
			}
			XtFree((char*)ctx->names);
		}
		if(ctx->dir_path != NULL) XtFree(ctx->dir_path);
		if(drag_icon != NULL) XtDestroyWidget(drag_icon);
		XtFree((char*)ctx);
	}
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
	char *source_dir;
	Atom target;
	struct dnd_drag_context *src_ctx;
	struct dnd_transfer_data *td;
	Widget transfer;

	(void)client;

	if(dropInfo == NULL) return;

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

	src_ctx = NULL;
	if(dropInfo->dragContext != NULL
		&& XtDisplay(dropInfo->dragContext) == XtDisplay(w)) {
		XtPointer cd = NULL;
		XtSetArg(args[0], XmNclientData, &cd);
		XtGetValues(dropInfo->dragContext, args, 1);
		src_ctx = (struct dnd_drag_context*)cd;
	}

	source_dir = NULL;
	if(src_ctx != NULL && src_ctx->source_widget != NULL
		&& XtIsWidget(src_ctx->source_widget)
		&& src_ctx->source_widget == w
		&& src_ctx->dir_path != NULL) {
		source_dir = realpath(src_ctx->dir_path, NULL);
	}

	if(source_dir != NULL && dest_dir != NULL &&
		!strcmp(source_dir, dest_dir)) {
		XtSetArg(args[0], XmNtransferStatus, XmTRANSFER_FAILURE);
		XtSetValues(w, args, 1);
		free(dest_dir);
		free(source_dir);
		return;
	}

	if(source_dir != NULL) free(source_dir);

	if(src_ctx != NULL && src_ctx->source_widget != NULL
		&& XtIsWidget(src_ctx->source_widget)
		&& src_ctx->source_widget == w)
		target = XA_XFILE_FILE_LIST;
	else
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

	{
		XmDropTransferEntryRec entry;

		entry.client_data = (XtPointer)td;
		entry.target = target;

		XtSetArg(args[n], XmNdropTransfers, &entry); n++;
		XtSetArg(args[n], XmNnumDropTransfers, 1); n++;
		XtSetArg(args[n], XmNtransferProc, dnd_transfer_cb); n++;
		XtSetArg(args[n], XmNtransferStatus, XmTRANSFER_SUCCESS); n++;
	}

	transfer = XmDropTransferStart(dropInfo->dragContext, args, n);
	if(transfer == NULL) {
		free(td->dest_dir);
		free(td);
		XtSetArg(args[0], XmNtransferStatus, XmTRANSFER_FAILURE);
		XtSetValues(w, args, 1);
	}
}

static void
dnd_transfer_cb(Widget w, XtPointer client, XtPointer call)
{
	XmSelectionCallbackStruct *ts = (XmSelectionCallbackStruct*)call;
	struct dnd_transfer_data *td = (struct dnd_transfer_data*)client;
	char *dest_dir;
	unsigned char operation;

	(void)w;

	if(ts == NULL) {
		if(td) {
			free(td->dest_dir);
			free(td);
		}
		return;
	}

	dest_dir = td->dest_dir;
	operation = td->operation;
	free(td);

	if(ts->target == XA_TEXT_URI_LIST) {
		char *data;
		char *line;
		char *next;
		char **paths;
		unsigned int count;
		unsigned int i;
		char *src_dir;

		if(ts->value == NULL || ts->length == 0) {
			free(dest_dir);
			return;
		}
		data = XtMalloc(ts->length + 1);
		if(!data) {
			free(dest_dir);
			return;
		}
		memcpy(data, ts->value, ts->length);
		data[ts->length] = '\0';

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

		dnd_perform_operation(src_dir, paths, count, dest_dir, operation);

		free(src_dir);
		for(i = 0; i < count; i++) free(paths[i]);
		XtFree((char*)paths);
		XtFree(data);

	} else if(ts->target == XA_XFILE_FILE_LIST) {
		char *value;
		char *ptr;
		char *sb;
		char **list;
		unsigned int n, i;
		size_t len;
		char *src_dir;

		if(ts->value == NULL || ts->length == 0) {
			free(dest_dir);
			return;
		}
		value = (char*)ts->value;

		len = ts->length;
		ptr = value;
		for(n = 0; ; ) {
			if((size_t)(ptr - value) >= len || ptr[0] == '\0') {
				n++;
				if((size_t)(ptr - value) + 1 >= len || ptr[1] == '\0')
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

		ptr = value;
		sb = ptr;
		for(i = 0; ; ) {
			if((size_t)(ptr - value) >= len || ptr[0] == '\0') {
				list[i] = sb;
				i++;
				sb = ptr + 1;
				if((size_t)(ptr - value) + 1 >= len || ptr[1] == '\0')
					break;
			}
			ptr++;
		}

		n--;
		src_dir = realpath(list[0], NULL);
		if(!src_dir) src_dir = strdup(list[0]);

		if(src_dir) {
			dnd_perform_operation(src_dir, list + 1, n, dest_dir,
				operation);
			free(src_dir);
		}

		XtFree((char*)list);
	}

	free(dest_dir);
}

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

	if(operation == XmDROP_LINK) {
		set_status_text("Symbolic links not supported");
		XtAppAddTimeOut(XtWidgetToApplicationContext(app_inst.wshell),
			3000, dnd_status_clear_cb, NULL);
		message_box(app_inst.wshell, MB_WARN, APP_TITLE,
			"Symbolic links are not supported in drag-and-drop.");
		free(wd);
		free(dest);
		return;
	}

	if(operation == XmDROP_COPY) {
		do_copy = True;
	} else if(operation == XmDROP_MOVE) {
		do_move = True;
	} else {
		/* Fallback: operation unclear (e.g. XmDROP default).
		 * Default to copy for safety — move is destructive. */
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
