/*
 * x11dnd.h — Public API for libx11dnd
 *
 * A standalone C99 library implementing the X Drag-and-Drop (XDnD)
 * protocol version 5 for X11, with no Xt or Motif dependency.
 *
 * Copyright (C) 2025-2026 alx@fastestcode.org
 * This software is distributed under the terms of the X/MIT license.
 * See the included COPYING file for further information.
 */

#ifndef X11DND_H
#define X11DND_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Protocol version constants
 * ========================================================================= */

/**
 * @brief Maximum XDnD protocol version implemented by this library.
 *
 * The library advertises and negotiates up to this version with peers.
 * XDnD v5 is the latest version of the freedesktop.org specification.
 */
#define X11DND_VERSION_5  5

/**
 * @brief Minimum XDnD protocol version supported by this library.
 *
 * Peers advertising a version below this value will be rejected.
 * XDnD v3 is the lowest version with reasonable interoperability.
 */
#define X11DND_VERSION_MIN 3

/* =========================================================================
 * Action atoms (logical constants)
 *
 * These constants identify the standard XDnD actions. They are compared
 * against Atom values interned at runtime via XInternAtom(). The library
 * interns the corresponding "XdndAction<Name>" atoms during x11dnd_init().
 * ========================================================================= */

/**
 * @brief XDnD action enumeration.
 *
 * Logical identifiers for the standard XDnD actions. The library maps
 * these to the interned XdndAction* atoms at initialization time.
 */
typedef enum {
    X11DND_ACTION_COPY        = 0, /**< Copy action (XdndActionCopy) */
    X11DND_ACTION_MOVE        = 1, /**< Move action (XdndActionMove) */
    X11DND_ACTION_LINK        = 2, /**< Link action (XdndActionLink) */
    X11DND_ACTION_ASK         = 3, /**< Ask user action (XdndActionAsk) */
    X11DND_ACTION_PRIVATE     = 4, /**< Private action (XdndActionPrivate) */
    X11DND_ACTION_DIRECT_SAVE = 5  /**< Direct save action (XdndActionDirectSave) */
} X11DndAction;

/* =========================================================================
 * MIME type constants
 *
 * String constants for common XDnD transfer targets. Use these when
 * building the type list passed to x11dnd_start_drag() or when checking
 * the target atom in drag-data / drop-received callbacks.
 * ========================================================================= */

/** @brief URI list MIME type (RFC 2483). Primary file drag format. */
#define X11DND_MIME_URI_LIST     "text/uri-list"
/** @brief UTF-8 string MIME type. */
#define X11DND_MIME_UTF8_STRING  "UTF8_STRING"
/** @brief Latin-1 string MIME type. */
#define X11DND_MIME_STRING       "STRING"
/** @brief Plain text MIME type. */
#define X11DND_MIME_TEXT_PLAIN   "text/plain"
/** @brief FILE_NAME target (COMPOUND_TEXT file name, legacy). */
#define X11DND_MIME_FILE_NAME    "FILE_NAME"
/** @brief XFile-specific file list MIME type. */
#define X11DND_MIME_FILE_LIST    "application/x-file-list"

/* =========================================================================
 * Opaque session handles
 *
 * The source and target session structs are defined internally; the public
 * API only exposes pointers. This keeps the ABI stable and hides
 * implementation details.
 * ========================================================================= */

/**
 * @brief Opaque handle representing an active drag source session.
 *
 * Created by x11dnd_start_drag(), destroyed by x11dnd_end_drag() or
 * x11dnd_cancel_drag(). Carries the source window, negotiated types,
 * actions, callbacks, and user data across the drag lifecycle.
 */
typedef struct X11DndSourceSession X11DndSourceSession;

/**
 * @brief Opaque handle representing an active drop target session.
 *
 * A target session is created implicitly when an XdndEnter is received
 * for a window registered via x11dnd_register_target(), and destroyed
 * when XdndLeave is received or the window is unregistered.
 */
typedef struct X11DndTargetSession X11DndTargetSession;

/* =========================================================================
 * Callback function pointer types
 * ========================================================================= */

/**
 * @brief Source callback: provide drag data for a SelectionRequest.
 *
 * Called when the drag source receives a SelectionRequest from the target.
 * The source must fill in @p data_ret, @p length_ret, and @p format_ret.
 * The library frees @p *data_ret via XFree() after delivery.
 *
 * @param sess        Source session handle.
 * @param target      The requested target atom (e.g. XA_STRING, text/uri-list).
 * @param data_ret    [out] Pointer to newly-allocated data buffer (Xmalloc).
 * @param length_ret  [out] Number of 8-bit units in @p *data_ret.
 * @param format_ret  [out] Format: 8, 16, or 32.
 */
typedef void (*x11dnd_drag_data_cb)(
    X11DndSourceSession *sess,
    Atom target,
    unsigned char **data_ret,
    unsigned long *length_ret,
    int *format_ret
);

/**
 * @brief Target callback: drop data has been received.
 *
 * Called after the target has fetched the selection data corresponding to
 * the drop. @p data is owned by the library and freed after the callback
 * returns; copy it if you need to retain it.
 *
 * @param sess    Target session handle.
 * @param target  The target atom that was requested.
 * @param data    The received data buffer (read-only).
 * @param length  Number of 8-bit units in @p data.
 * @param format  Format: 8, 16, or 32.
 */
typedef void (*x11dnd_drop_received_cb)(
    X11DndTargetSession *sess,
    Atom target,
    unsigned char *data,
    unsigned long length,
    int format
);

/**
 * @brief Target callback: XdndActionAsk was negotiated.
 *
 * Called when the source advertises XdndActionAsk. The target should
 * present a menu of @p actions with @p descriptions and set
 * @p *chosen_action_ret to the atom the user picked.
 *
 * @param sess              Target session handle.
 * @param actions           Array of action atoms offered by the source.
 * @param n_actions         Number of entries in @p actions.
 * @param descriptions     Array of human-readable descriptions (may be NULL).
 * @param n_desc            Number of entries in @p descriptions.
 * @param chosen_action_ret [out] The action atom chosen by the user.
 */
typedef void (*x11dnd_action_ask_cb)(
    X11DndTargetSession *sess,
    Atom *actions,
    int n_actions,
    char **descriptions,
    int n_desc,
    Atom *chosen_action_ret
);

/**
 * @brief Target callback: XdndPosition received.
 *
 * Called whenever the source sends an XdndPosition message. The target
 * decides whether to accept the drop and which action to perform, and
 * optionally returns a status rectangle for visual feedback.
 *
 * @param sess         Target session handle.
 * @param x            Root-window x coordinate of the pointer.
 * @param y            Root-window y coordinate of the pointer.
 * @param time         Server timestamp from the XdndPosition message.
 * @param action       The action the source proposes.
 * @param accept_ret   [out] True to accept the drop at this position.
 * @param action_ret   [out] The action the target will perform.
 * @param rect_x       [out] Status rectangle x (0 = no rect).
 * @param rect_y       [out] Status rectangle y.
 * @param rect_w       [out] Status rectangle width.
 * @param rect_h       [out] Status rectangle height.
 */
typedef void (*x11dnd_position_cb)(
    X11DndTargetSession *sess,
    int x,
    int y,
    Time time,
    Atom action,
    Bool *accept_ret,
    Atom *action_ret,
    int *rect_x,
    int *rect_y,
    int *rect_w,
    int *rect_h
);

/**
 * @brief Source callback: XdndStatus received from the target.
 *
 * @param sess    Source session handle.
 * @param accept  True if the target accepts the drop.
 * @param x       Status rectangle x (0 if no rect).
 * @param y       Status rectangle y.
 * @param w       Status rectangle width.
 * @param h       Status rectangle height.
 * @param action  The action the target will perform.
 */
typedef void (*x11dnd_status_cb)(
    X11DndSourceSession *sess,
    Bool accept,
    int x,
    int y,
    int w,
    int h,
    Atom action
);

/**
 * @brief Source callback: XdndFinished received from the target.
 *
 * @param sess             Source session handle.
 * @param success          True if the drop completed successfully.
 * @param performed_action The action that was actually performed (None if
 *                         the target does not support v5 action reporting).
 */
typedef void (*x11dnd_finished_cb)(
    X11DndSourceSession *sess,
    Bool success,
    Atom performed_action
);

/* =========================================================================
 * X11DndClass — callback table (vtable)
 *
 * Inspired by Window Maker's DndClass but updated for XDnD v5. The
 * application fills in the function pointers it cares about; NULL entries
 * are skipped by the library.
 * ========================================================================= */

/**
 * @brief Callback table for a drag source or drop target.
 *
 * The application populates this struct and passes it to
 * x11dnd_start_drag() (source) or x11dnd_register_target() (target).
 * Unused callbacks may be left NULL; the library checks before calling.
 */
typedef struct X11DndClass {
    /* --- Lifecycle ----------------------------------------------------- */

    /**
     * @brief Called when a drag operation begins (after XdndEnter is sent).
     * @param sess Source session handle.
     */
    void (*on_drag_begin)(X11DndSourceSession *sess);

    /**
     * @brief Called when a drag operation ends (after XdndFinished or cancel).
     * @param sess       Source session handle.
     * @param completed  True if the drop completed successfully.
     */
    void (*on_drag_end)(X11DndSourceSession *sess, Bool completed);

    /* --- Source callbacks ---------------------------------------------- */

    /** @brief Provide drag data for SelectionRequest. */
    x11dnd_drag_data_cb get_drag_data;

    /** @brief XdndStatus received from target. */
    x11dnd_status_cb status_received;

    /** @brief XdndFinished received from target. */
    x11dnd_finished_cb finished_received;

    /* --- Target callbacks --------------------------------------------- */

    /**
     * @brief XdndEnter received: source is beginning a drag over this target.
     * @param sess     Target session handle.
     * @param source   Source window.
     * @param version  Negotiated XDnD version.
     * @param types    Array of offered type atoms (NULL if >3 types; fetch
     *                 via XGetWindowProperty on XdndTypeList).
     * @param n_types  Number of types in @p types (0 if fetched separately).
     */
    void (*on_enter)(X11DndTargetSession *sess, Window source,
                     int version, Atom *types, int n_types);

    /** @brief XdndPosition received. */
    x11dnd_position_cb position_received;

    /**
     * @brief XdndLeave received: source has left the target window.
     * @param sess Target session handle.
     */
    void (*on_leave)(X11DndTargetSession *sess);

    /** @brief Drop data received (after SelectionNotify). */
    x11dnd_drop_received_cb drop_received;

    /** @brief XdndActionAsk negotiated — present a menu. */
    x11dnd_action_ask_cb action_ask;

    /* --- Error --------------------------------------------------------- */

    /**
     * @brief Called when an error occurs inside the library.
     * @param message   Human-readable error message (read-only).
     * @param severity   0 = info, 1 = warning, 2 = error, 3 = fatal.
     */
    void (*on_error)(const char *message, int severity);
} X11DndClass;

/* =========================================================================
 * Library lifecycle
 * ========================================================================= */

/**
 * @brief Initialize the libx11dnd library for a display.
 *
 * Interns all XDnD atoms (XdndAware, XdndEnter, XdndPosition, XdndStatus,
 * XdndLeave, XdndDrop, XdndFinished, XdndActionCopy, …, XdndTypeList,
 * XdndActionList, XdndActionDescription, XdndDirectSave, etc.).
 * Must be called once per Display before any other libx11dnd function
 * using that display.
 *
 * @param dpy The X11 display connection.
 * @return 0 on success, non-zero on failure (e.g. atom interning failed).
 */
int x11dnd_init(Display *dpy);

/**
 * @brief Release library resources for a display.
 *
 * Frees any per-display internal state. Sessions still active on @p dpy
 * are cancelled. Safe to call multiple times.
 *
 * @param dpy The X11 display connection previously passed to x11dnd_init().
 */
void x11dnd_destroy(Display *dpy);

/* =========================================================================
 * Drag source functions
 * ========================================================================= */

/**
 * @brief Begin a drag operation from @p source_win.
 *
 * Takes ownership of the XDnD selection (XdndSelection), sets up the
 * source session with the given @p callbacks, types, and actions, and
 * is ready to dispatch XdndStatus / XdndFinished / SelectionRequest
 * events via x11dnd_source_process_event().
 *
 * @param dpy         The X11 display.
 * @param source_win  The window initiating the drag.
 * @param callbacks   Callback table (must remain valid for the drag's
 *                    lifetime; at least get_drag_data should be set).
 * @param time        Server timestamp of the drag-initiating event
 *                    (ButtonPress time).
 * @param types       Array of offered target type atoms (e.g. URI list).
 * @param n_types     Number of entries in @p types.
 * @param actions     Array of action atoms offered (may be NULL for
 *                    copy-only).
 * @param n_actions    Number of entries in @p actions (0 if @p actions NULL).
 * @param user_data   Opaque pointer stored in the session, retrievable
 *                    via x11dnd_source_get_user_data().
 * @return New source session handle, or NULL on failure.
 */
X11DndSourceSession *x11dnd_start_drag(Display *dpy, Window source_win,
                                        X11DndClass *callbacks, Time time,
                                        Atom *types, int n_types,
                                        Atom *actions, int n_actions,
                                        void *user_data);

/**
 * @brief Cancel an in-progress drag operation.
 *
 * Sends XdndLeave to the current target and tears down the session
 * without waiting for XdndFinished. The session handle is invalid
 * after this call.
 *
 * @param sess Source session handle (may be NULL — no-op).
 */
void x11dnd_cancel_drag(X11DndSourceSession *sess);

/**
 * @brief End a drag operation normally.
 *
 * Called after XdndFinished has been received (or the drop is otherwise
 * complete). Releases the session. The handle is invalid after this call.
 *
 * @param sess Source session handle (may be NULL — no-op).
 */
void x11dnd_end_drag(X11DndSourceSession *sess);

/**
 * @brief Dispatch an X event to the active source session.
 *
 * Feed ClientMessage (XdndStatus, XdndFinished), SelectionRequest, and
 * SelectionClear events to this function from your event loop.
 *
 * @param ev The X event.
 * @return 1 if the event was consumed by a source session, 0 otherwise.
 */
int x11dnd_source_process_event(XEvent *ev);

/* =========================================================================
 * Drop target functions
 * ========================================================================= */

/**
 * @brief Register a window as an XDnD drop target.
 *
 * Sets the XdndAware property (version 5) on @p win and records the
 * callback table for dispatching XdndEnter/Position/Drop/Leave events.
 *
 * @param dpy        The X11 display.
 * @param win        The window to register.
 * @param callbacks  Callback table (must remain valid while registered).
 * @param user_data  Opaque pointer retrievable via
 *                   x11dnd_target_get_user_data().
 * @return 0 on success, non-zero on failure.
 */
int x11dnd_register_target(Display *dpy, Window win,
                           X11DndClass *callbacks, void *user_data);

/**
 * @brief Unregister a window as a drop target.
 *
 * Removes the XdndAware property and discards any pending target session
 * for @p win.
 *
 * @param dpy The X11 display.
 * @param win The window previously registered.
 */
void x11dnd_unregister_target(Display *dpy, Window win);

/**
 * @brief Dispatch an X event to registered drop targets.
 *
 * Feed ClientMessage (XdndEnter, XdndPosition, XdndDrop, XdndLeave) and
 * SelectionNotify events to this function from your event loop.
 *
 * @param ev The X event.
 * @return 1 if the event was consumed by a target session, 0 otherwise.
 */
int x11dnd_target_process_event(XEvent *ev);

/**
 * @brief Request selection data from the drag source after XdndDrop.
 *
 * The application should call this from its event loop after receiving
 * XdndDrop (the library does not auto-request). Selects the best type
 * from the source's type list (prefers text/uri-list, then UTF8_STRING,
 * STRING, text/plain, FILE_NAME). The resulting SelectionNotify event
 * should be fed to x11dnd_target_process_event() or
 * x11dnd_target_handle_selection_notify().
 *
 * @param sess     Target session handle (must be in DROP_PENDING state).
 * @param property The property atom to use for the transfer (e.g.
 *                 XInternAtom(dpy, "Xdnd_DATA", False)).
 * @return True if the request was sent, False on error.
 */
Bool x11dnd_target_request_selection(X11DndTargetSession *sess,
                                     Atom property);

/**
 * @brief Handle a SelectionNotify event for XdndSelection.
 *
 * Detects INCR transfers and starts incremental collection if needed,
 * or delivers immediate data to the drop_received callback.
 * Feed SelectionNotify events to this function from your event loop.
 *
 * @param ev The X event (must be SelectionNotify type).
 * @return 1 if the event was consumed, 0 otherwise.
 */
int x11dnd_target_handle_selection_notify(XEvent *ev);

/* =========================================================================
 * Utility functions
 * ========================================================================= */

/**
 * @brief Set the XdndAware property on a window.
 *
 * @param dpy     The X11 display.
 * @param win     The window to mark as XDnD-aware.
 * @param version The XDnD version to advertise (use X11DND_VERSION_5).
 * @return True on success, False on failure.
 */
Bool x11dnd_set_aware(Display *dpy, Window win, int version);

/**
 * @brief Read the XdndAware property of a window.
 *
 * @param dpy The X11 display.
 * @param win The window to query.
 * @return The XDnD version advertised (≥1), or -1 if the window is not
 *         XDnD-aware or the property could not be read.
 */
int x11dnd_get_aware_version(Display *dpy, Window win);

/**
 * @brief Check whether a negotiated version meets a requirement.
 *
 * @param negotiated The version reported by the peer.
 * @param required   The minimum version needed.
 * @return True if @p negotiated >= @p required, False otherwise.
 */
Bool x11dnd_version_at_least(int negotiated, int required);

/**
 * @brief Send an XdndStatus message to a source.
 *
 * Typically called from within the position_received callback.
 *
 * @param dpy     The X11 display.
 * @param source  The source window to notify.
 * @param target  The target window (ourselves).
 * @param accept  True if the target accepts the drop.
 * @param x       Status rectangle x (0 if no rectangle).
 * @param y       Status rectangle y.
 * @param w       Status rectangle width.
 * @param h       Status rectangle height.
 * @param action  The action the target will perform (None if not accepting).
 */
void x11dnd_send_status(Display *dpy, Window source, Window target,
                        Bool accept, int x, int y, int w, int h, Atom action);

/**
 * @brief Send an XdndFinished message to a source.
 *
 * Typically called after processing the drop data.
 *
 * @param dpy     The X11 display.
 * @param source  The source window to notify.
 * @param target  The target window (ourselves).
 * @param success True if the drop completed successfully.
 * @param action  The action that was performed (None if failed).
 */
void x11dnd_send_finished(Display *dpy, Window source, Window target,
                          Bool success, Atom action);

/* =========================================================================
 * Session accessors
 * ========================================================================= */

/**
 * @brief Retrieve the user data pointer stored in a source session.
 * @param sess Source session handle.
 * @return The user_data pointer passed to x11dnd_start_drag(), or NULL.
 */
void *x11dnd_source_get_user_data(X11DndSourceSession *sess);

/**
 * @brief Retrieve the user data pointer stored in a target session.
 * @param sess Target session handle.
 * @return The user_data pointer passed to x11dnd_register_target(), or NULL.
 */
void *x11dnd_target_get_user_data(X11DndTargetSession *sess);

/**
 * @brief Get the source window of a drag session.
 * @param sess Source session handle.
 * @return The Window passed to x11dnd_start_drag().
 */
Window x11dnd_source_get_window(X11DndSourceSession *sess);

/**
 * @brief Get the target window of a drop session.
 * @param sess Target session handle.
 * @return The Window registered with x11dnd_register_target().
 */
Window x11dnd_target_get_window(X11DndTargetSession *sess);

/**
 * @brief Get the source window of a drop target session.
 * @param sess Target session handle.
 * @return The drag source window that sent XdndEnter, or None.
 */
Window x11dnd_target_get_source_window(X11DndTargetSession *sess);

/**
 * @brief Get the Display of a drag source session.
 * @param sess Source session handle.
 * @return The Display passed to x11dnd_start_drag().
 */
Display *x11dnd_source_get_display(X11DndSourceSession *sess);

/**
 * @brief Get the Display of a drop target session.
 * @param sess Target session handle.
 * @return The Display passed to x11dnd_register_target().
 */
Display *x11dnd_target_get_display(X11DndTargetSession *sess);

/**
 * @brief Get the XDnD version negotiated with the source.
 * @param sess Target session handle.
 * @return The negotiated version (e.g. 5), or 0 if no XdndEnter received.
 */
int x11dnd_target_get_negotiated_version(X11DndTargetSession *sess);

/**
 * @brief Get the negotiated action from the last XdndPosition message.
 * @param sess Target session handle.
 * @return The action Atom (e.g., XdndActionCopy, XdndActionMove)
 *         or None if no action was negotiated.
 */
Atom x11dnd_target_get_action(X11DndTargetSession *sess);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* X11DND_H */