/*
 * x11dnd_reply.c - XdndStatus and XdndFinished reply message construction.
 *
 * Builds and sends the two XDnD v5 messages that a drop target sends
 * back to a drag source:
 *
 *   XdndStatus  — sent in response to XdndPosition; tells the source
 *                 whether the target accepts the drop, an optional
 *                 no-motion rectangle, and the action the target
 *                 will perform.
 *
 *   XdndFinished — sent in response to XdndDrop after the drop data has
 *                 been processed; reports success/failure and the
 *                 action that was performed (v5 only; v4 callers
 *                 should pass None as the action).
 *
 * Message format reference (XDnD v5 spec):
 *
 *   XdndStatus:
 *     data.l[0] = target window (the window receiving the drop)
 *     data.l[1] = flags:  bit 0 = accept (1/0)
 *                         bit 1 = want position messages for entire
 *                                 window (set when accept=1 and no
 *                                 no-motion rectangle is supplied)
 *     data.l[2] = x<<16 | y   (no-motion rectangle, root-relative)
 *     data.l[3] = w<<16 | h
 *     data.l[4] = action atom (None if not accepting)
 *
 *   XdndFinished (v5):
 *     data.l[0] = target window
 *     data.l[1] = success flag (1 if drop completed, 0 if failed)
 *     data.l[2] = performed action atom (None if failed or v4)
 *     data.l[3] = 0 (reserved)
 *     data.l[4] = 0 (reserved)
 *
 *   XdndFinished (v4 and below):
 *     data.l[2] = 0  (no action field; caller passes None)
 *
 * The version check for v4 compatibility happens at the call site
 * (the target handler passes None as the action for v4 sessions);
 * this builder just sends what it is given.
 */
#include "x11dnd_atoms.h"
#include "x11dnd_util.h"

#include <X11/Xlib.h>

/*
 * Send an XdndStatus reply to the drag source.
 *
 * Per XDnD v5 spec:
 *   data.l[0] = target window (ourselves)
 *   data.l[1] = flags: bit 0 = accept, bit 1 = send position for whole window
 *   data.l[2] = x<<16 | y (no-motion rectangle, root-relative)
 *   data.l[3] = w<<16 | h
 *   data.l[4] = action atom (the action the target will perform)
 *
 * When @p accept is True and no rectangle is specified (x=y=w=h=0),
 * bit 1 is set to indicate "accept anywhere" — the source should keep
 * sending XdndPosition for the entire window, not just a rectangle.
 * When @p accept is True and a rectangle is supplied, bit 1 is cleared
 * and the rectangle is encoded in data.l[2]/data.l[3].
 */
void
x11dnd_send_status(Display *dpy, Window source, Window target,
    Bool accept, int x, int y, int w, int h, Atom action)
{
    const X11DndAtoms *atoms;
    long data[5];
    long flags;

    atoms = x11dnd_get_atoms();
    if (atoms == NULL) {
        return;
    }

    /*
     * x11dnd_send_client_message() places its 'source' argument into
     * ev.data.l[0], so we pass 'target' as the source to set data.l[0]
     * correctly. The remaining fields data[1..4] are passed through.
     */
    data[0] = (long)target;

    /* Build flags: bit 0 = accept, bit 1 = whole window (no rect) */
    flags = 0;
    if (accept) {
        flags |= 0x1;
    }
    if (accept && x == 0 && y == 0 && w == 0 && h == 0) {
        /*
         * No rectangle specified + accept = accept anywhere.
         * Set bit 1 so the source keeps sending XdndPosition for the
         * entire window rather than only inside a rectangle.
         */
        flags |= 0x2;
    }
    data[1] = flags;

    /* Encode rectangle: x<<16 | y, w<<16 | h */
    data[2] = ((long)x << 16) | (y & 0xFFFF);
    data[3] = ((long)w << 16) | (h & 0xFFFF);

    data[4] = (long)action;

    x11dnd_send_client_message(dpy, source, target,
        atoms->XdndStatus, data, CurrentTime);
}

/*
 * Send an XdndFinished reply to the drag source.
 *
 * v5 format: data.l[2] = performed action atom.
 * v4 format: data.l[2] = 0 (caller passes None as the action for v4
 *            sessions; the version check is at the call site, not
 *            here — this builder just sends what it is given).
 *
 *   data.l[0] = target window
 *   data.l[1] = success flag (1 if drop completed, 0 if failed)
 *   data.l[2] = performed action atom (v5; None/0 for v4 or failure)
 *   data.l[3] = 0 (reserved)
 *   data.l[4] = 0 (reserved)
 */
void
x11dnd_send_finished(Display *dpy, Window source, Window target,
    Bool success, Atom action)
{
    const X11DndAtoms *atoms;
    long data[5];

    atoms = x11dnd_get_atoms();
    if (atoms == NULL) {
        return;
    }

    /*
     * x11dnd_send_client_message() places its 'source' argument into
     * ev.data.l[0], so we pass 'target' as the source to set data.l[0]
     * correctly.
     */
    data[0] = (long)target;
    data[1] = success ? 1 : 0;
    data[2] = (long)action;
    data[3] = 0;
    data[4] = 0;

    x11dnd_send_client_message(dpy, source, target,
        atoms->XdndFinished, data, CurrentTime);
}