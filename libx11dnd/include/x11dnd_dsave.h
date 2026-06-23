/*
 * x11dnd_dsave.h - Internal header for XdndDirectSave (XDS) protocol.
 *
 * This is an INTERNAL header for the libx11dnd library, NOT the public API.
 * It declares helpers for the XDS (Direct Save) protocol, which allows a
 * drop target to request the drag source to save data directly to a file
 * path instead of transferring data via the X selection mechanism.
 *
 * Protocol summary:
 *   1. Target includes XdndDirectSave in the offered type list (XdndEnter).
 *   2. Target sends XdndPosition with action=XdndActionDirectSave.
 *   3. Target sends XdndDrop.
 *   4. Target sets the desired save path on its own XdndDirectSave0 property.
 *   5. Target calls XConvertSelection(XdndSelection, XdndDirectSave,
 *      XdndDirectSave0, target_win, time).
 *   6. Source receives SelectionRequest for XdndDirectSave:
 *      - Reads save path from requestor's XdndDirectSave0 property.
 *      - Calls get_drag_data callback to obtain the file data.
 *      - Writes the data to the save path.
 *      - Sets the XdndDirectSave0 property on the requestor to the result:
 *        "file:///path/to/saved/file" on success, "!" on failure.
 *   7. Source sends SelectionNotify.
 *   8. Target reads the result from the XdndDirectSave0 property.
 *   9. Target sends XdndFinished.
 */
#ifndef X11DND_DSAVE_H
#define X11DND_DSAVE_H

#include <X11/Xlib.h>

/*
 * Check if a target atom is XdndDirectSave.
 * Returns True if @p target matches the interned XdndDirectSave atom,
 * False otherwise (including if atoms are not initialized).
 */
Bool x11dnd_is_direct_save(Atom target);

/*
 * Target-side: set the save path on the XdndDirectSave0 property.
 * Stores @p path as an XA_STRING (format 8) property on @p win.
 * Returns 0 on success, non-zero on failure.
 */
int x11dnd_dsave_set_path(Display *dpy, Window win, const char *path);

/*
 * Target-side: get the save path from the XdndDirectSave0 property.
 * Reads the XdndDirectSave0 property (XA_STRING, format 8) from @p win.
 * Returns a malloc'd null-terminated string (caller must free), or NULL
 * on failure.
 */
char *x11dnd_dsave_get_path(Display *dpy, Window win);

/*
 * Source-side: handle an XdndDirectSave SelectionRequest.
 *
 * Reads the save path from the requestor's XdndDirectSave0 property,
 * calls the source's get_drag_data callback to obtain the file data,
 * writes the data to the save path, and sets the result property on
 * the requestor window:
 *   - "file://<save_path>" on success
 *   - "!" on failure
 *
 * @p source_sess is the X11DndSourceSession pointer (cast to void* to
 * avoid a circular include dependency on x11dnd_source.h).
 *
 * Returns 0 on success (property set), non-zero on internal error.
 * Note: a return of 0 means the protocol completed (the result property
 * was set); the result itself may be "!" if the file save failed.
 */
int x11dnd_dsave_handle_request(Display *dpy, Window requestor,
    Atom property, Time time, void *source_sess);

/*
 * Target-side: read the result from a SelectionNotify for XdndDirectSave.
 *
 * Reads the @p property (XdndDirectSave0) from the target window @p win
 * after the source has set the result.
 *
 * Returns the result string (malloc'd, caller must free):
 *   - "file:///..." means success
 *   - "!" means failure
 * Returns NULL on error (property missing, wrong type, etc.).
 */
char *x11dnd_dsave_get_result(Display *dpy, Window win, Atom property);

#endif /* X11DND_DSAVE_H */