# libx11dnd

A standalone C99 library implementing the X Drag-and-Drop (XDnD) protocol version 5 for X11. The core library uses only Xlib and has no Xt or Motif dependency. An optional wrapper header adds convenience functions for Xt/Motif applications.

libx11dnd implements a complete drag-and-drop session lifecycle between X11 clients: drag sources, drop targets, action negotiation, type selection, incremental transfers, direct save, and proxy window support. It is built as a static archive and can be dropped into any X11 or Xt/Motif project.


## Features

- Pure Xlib core, no external dependencies
- Optional Xt/Motif integration wrapper
- XDnD version 5 awareness with backward compatibility
- Drag source and drop target APIs
- Action negotiation: Copy, Move, Link, Ask, Private, DirectSave
- Standard MIME targets: `text/uri-list`, `UTF8_STRING`, `STRING`, `text/plain`, `FILE_NAME`, and the custom `application/x-file-list`
- INCR (incremental) selection transfers for large data
- XdndDirectSave protocol for saving directly to a path
- XdndProxy support for relayed drops
- C99, single static archive (`libx11dnd.a`)


## Requirements

- A C99 compiler
- X11 development headers (`libX11`, and optionally `libXt` for the Xt wrapper)
- `pkg-config` with `x11.pc` (optional, used if available)
- GNU make or BSD make

No other libraries are required. The Xt wrapper is isolated in a single compilation unit, so projects that do not use Xt get no Xt dependency.


## Build Instructions

The library builds with plain `make`. The library Makefile uses `pkg-config` when available and falls back to `/usr/include` for include paths.

### Generic build

```sh
cd libx11dnd
make
```

This produces `libx11dnd.a` from all `.c` files in `src/`.

### Platform-specific build

The XFile project supports six platform variants. Select the one matching your OS:

```sh
make Linux
make FreeBSD
make NetBSD
make OpenBSD
make SunOS
make generic
```

Each target symlinks the matching platform Makefile in `mf/Makefile.<platform>` to `src/Makefile` and then builds through it. The libx11dnd library itself is compiled using the local `libx11dnd/Makefile`; the platform selection controls the wider XFile build.

### Clean

```sh
make clean
```

This removes object files and the static archive.


## Installation

libx11dnd is built as a static archive. To use it in your project, copy or install:

- `libx11dnd.a` to your library directory
- `include/x11dnd.h` (and optionally `include/x11dnd_xt.h`) to your include directory

Link with `-lX11` (and `-lXt` if you use the Xt wrapper).

There is no install target in the library Makefile. The XFile top-level `make install` installs the whole application, including libx11dnd if it is part of the build.


## API Overview

The public API is declared in `include/x11dnd.h`. The Xt wrapper is declared in `include/x11dnd_xt.h`. Internal headers (`x11dnd_action.h`, `x11dnd_dsave.h`, `x11dnd_incr.h`, `x11dnd_proxy.h`) describe the underlying protocol implementation but are not part of the public API.


### Library lifecycle

| Function | Description |
|----------|-------------|
| `int x11dnd_init(Display *dpy)` | Interns all XDnD atoms for a display. Call once per `Display` before any other library call. Returns 0 on success. |
| `void x11dnd_destroy(Display *dpy)` | Releases per-display state and cancels any active sessions. Safe to call multiple times. |


### Drag source functions

| Function | Description |
|----------|-------------|
| `X11DndSourceSession *x11dnd_start_drag(...)` | Begin a drag from `source_win`. Takes ownership of the `XdndSelection` and returns a new source session. |
| `void x11dnd_cancel_drag(X11DndSourceSession *sess)` | Cancel the drag, send `XdndLeave` to the current target, and destroy the session. |
| `void x11dnd_end_drag(X11DndSourceSession *sess)` | End the drag after `XdndFinished` has been received. |
| `int x11dnd_source_process_event(XEvent *ev)` | Dispatch an X event to the active source session. Returns 1 if consumed. |


### Drop target functions

| Function | Description |
|----------|-------------|
| `int x11dnd_register_target(...)` | Mark a window as XDnD-aware (version 5) and register callbacks for that window. |
| `void x11dnd_unregister_target(Display *dpy, Window win)` | Remove the `XdndAware` property and discard the target session. |
| `int x11dnd_target_process_event(XEvent *ev)` | Dispatch X events to registered targets. Returns 1 if consumed. |
| `Bool x11dnd_target_request_selection(...)` | Request drop data from the source after `XdndDrop`. |
| `int x11dnd_target_handle_selection_notify(XEvent *ev)` | Handle `SelectionNotify`, including INCR detection. Returns 1 if consumed. |


### Utility functions

| Function | Description |
|----------|-------------|
| `Bool x11dnd_set_aware(Display *dpy, Window win, int version)` | Set the `XdndAware` property on a window. |
| `int x11dnd_get_aware_version(Display *dpy, Window win)` | Read the `XdndAware` version, or -1 if not aware. |
| `Bool x11dnd_version_at_least(int negotiated, int required)` | Check whether a negotiated version meets a minimum. |
| `void x11dnd_send_status(...)` | Send an `XdndStatus` message to a source. |
| `void x11dnd_send_finished(...)` | Send an `XdndFinished` message to a source. |


### Session accessors

| Function | Description |
|----------|-------------|
| `void *x11dnd_source_get_user_data(X11DndSourceSession *sess)` | Retrieve the user data pointer passed to `x11dnd_start_drag()`. |
| `void *x11dnd_target_get_user_data(X11DndTargetSession *sess)` | Retrieve the user data pointer passed to `x11dnd_register_target()`. |
| `Window x11dnd_source_get_window(X11DndSourceSession *sess)` | Get the source window. |
| `Window x11dnd_target_get_window(X11DndTargetSession *sess)` | Get the target window. |
| `Display *x11dnd_source_get_display(X11DndSourceSession *sess)` | Get the source display. |
| `Display *x11dnd_target_get_display(X11DndTargetSession *sess)` | Get the target display. |
| `int x11dnd_target_get_negotiated_version(X11DndTargetSession *sess)` | Get the XDnD version negotiated with the source. |


### Xt wrapper functions

| Function | Description |
|----------|-------------|
| `int x11dnd_xt_init(Widget app_shell)` | Initialize libx11dnd on the display of `app_shell` and register Xt event handlers. |
| `void x11dnd_xt_destroy(void)` | Remove Xt handlers and release library resources. |
| `int x11dnd_xt_register_target(Widget w, X11DndClass *callbacks)` | Register the shell ancestor of a widget as a drop target. |
| `void x11dnd_xt_unregister_target(Widget w)` | Unregister the shell ancestor of a widget. |
| `X11DndSourceSession *x11dnd_xt_start_drag(Widget w, XButtonEvent *event, X11DndClass *callbacks)` | Begin a drag from an Xt widget and install a work proc for tracking. |
| `void x11dnd_xt_cancel_drag(void)` | Cancel the active drag started with `x11dnd_xt_start_drag()`. |
| `int x11dnd_xt_process_event(Widget w, XEvent *ev)` | Feed an event into libx11dnd from an Xt event loop. |


### Callback reference (`X11DndClass`)

`X11DndClass` is the callback table passed to `x11dnd_start_drag()` and `x11dnd_register_target()`. Unused slots may be left `NULL`.

| Slot | Signature | Description |
|------|-----------|-------------|
| `on_drag_begin` | `void (*)(X11DndSourceSession *sess)` | Called when a drag begins (after `XdndEnter` is sent). |
| `on_drag_end` | `void (*)(X11DndSourceSession *sess, Bool completed)` | Called when a drag ends (after `XdndFinished` or cancel). |
| `get_drag_data` | `x11dnd_drag_data_cb` | Source callback: allocate and return data for a `SelectionRequest`. |
| `status_received` | `x11dnd_status_cb` | Source callback: `XdndStatus` received from the target. |
| `finished_received` | `x11dnd_finished_cb` | Source callback: `XdndFinished` received from the target. |
| `on_enter` | `void (*)(X11DndTargetSession *sess, Window source, int version, Atom *types, int n_types)` | Target callback: `XdndEnter` received. |
| `position_received` | `x11dnd_position_cb` | Target callback: `XdndPosition` received. Set `accept_ret` and `action_ret`. |
| `on_leave` | `void (*)(X11DndTargetSession *sess)` | Target callback: `XdndLeave` received. |
| `drop_received` | `x11dnd_drop_received_cb` | Target callback: selection data has been delivered. |
| `action_ask` | `x11dnd_action_ask_cb` | Target callback: `XdndActionAsk` negotiated; present an action menu. |
| `on_error` | `void (*)(const char *message, int severity)` | Called when the library reports an error. Severity: 0 info, 1 warning, 2 error, 3 fatal. |


### Callback signatures

| Typedef | Description |
|---------|-------------|
| `x11dnd_drag_data_cb` | Provide drag data for a requested target atom. Fill `data_ret`, `length_ret`, and `format_ret`. The library frees `*data_ret` with `XFree()`. |
| `x11dnd_drop_received_cb` | Receive the data buffer from a drop. The buffer is read-only and freed after the callback returns. |
| `x11dnd_action_ask_cb` | Choose an action from the list of atoms and descriptions offered by the source. Set `*chosen_action_ret`. |
| `x11dnd_position_cb` | Decide whether to accept a drop and return an action and optional status rectangle. |
| `x11dnd_status_cb` | Receive the target's response to an `XdndPosition`. |
| `x11dnd_finished_cb` | Receive the final `XdndFinished` message, including the performed action if available. |


### Constants

- `X11DND_VERSION_5` (5) — maximum protocol version implemented
- `X11DND_VERSION_MIN` (3) — minimum supported protocol version

### MIME type strings

| Constant | Value |
|----------|-------|
| `X11DND_MIME_URI_LIST` | `"text/uri-list"` |
| `X11DND_MIME_UTF8_STRING` | `"UTF8_STRING"` |
| `X11DND_MIME_STRING` | `"STRING"` |
| `X11DND_MIME_TEXT_PLAIN` | `"text/plain"` |
| `X11DND_MIME_FILE_NAME` | `"FILE_NAME"` |
| `X11DND_MIME_FILE_LIST` | `"application/x-file-list"` |

### Actions

The `X11DndAction` enum provides logical identifiers for the standard XDnD actions:

| Enum | Atom |
|------|------|
| `X11DND_ACTION_COPY` | `XdndActionCopy` |
| `X11DND_ACTION_MOVE` | `XdndActionMove` |
| `X11DND_ACTION_LINK` | `XdndActionLink` |
| `X11DND_ACTION_ASK` | `XdndActionAsk` |
| `X11DND_ACTION_PRIVATE` | `XdndActionPrivate` |
| `X11DND_ACTION_DIRECT_SAVE` | `XdndActionDirectSave` |


## Protocol Compliance

libx11dnd targets the freedesktop.org XDnD version 5 specification. The following protocol elements are supported:

- `XdndEnter` — source announces drag, types, and version
- `XdndPosition` — source reports pointer position and proposed action
- `XdndStatus` — target reports acceptance, action, and optional rectangle
- `XdndLeave` — source cancels or leaves the target
- `XdndDrop` — source requests the drop
- `XdndFinished` — target reports completion and performed action
- `XdndAware` version 5 registration on drop targets
- Action negotiation for Copy, Move, Link, Ask, Private, and DirectSave
- `XdndActionList` and `XdndActionDescription` property handling
- Standard MIME targets: `text/uri-list`, `UTF8_STRING`, `STRING`, `FILE_NAME`, `text/plain`, `application/x-file-list`
- `TARGETS` and `TIMESTAMP` selection targets
- INCR incremental transfer for large selections
- `XdndDirectSave` direct-save protocol
- `XdndProxy` proxy window support
- `XdndActionAsk` callback-driven action selection


## Xt Integration Example

This example shows a typical Xt/Motif program using the Xt wrapper.

```c
#include <Xm/Xm.h>
#include <Xm/Form.h>
#include "x11dnd_xt.h"

static X11DndClass dnd_class;

static void on_drop_received(X11DndTargetSession *sess, Atom target,
                             unsigned char *data, unsigned long length,
                             int format)
{
    /* Process the dropped data; the buffer is freed after this returns. */
    if (target == XInternAtom(x11dnd_target_get_display(sess),
                              X11DND_MIME_URI_LIST, False)) {
        /* Handle URI list... */
    }
}

static void on_position(X11DndTargetSession *sess, int x, int y, Time time,
                        Atom action, Bool *accept_ret, Atom *action_ret,
                        int *rect_x, int *rect_y, int *rect_w, int *rect_h)
{
    *accept_ret = True;
    *action_ret = action;
    *rect_x = *rect_y = *rect_w = *rect_h = 0;
}

static void start_drag_cb(Widget w, XEvent *event, String *args,
                          Cardinal *n_args)
{
    x11dnd_xt_start_drag(w, &event->xbutton, &dnd_class);
}

int main(int argc, char **argv)
{
    XtAppContext app;
    Widget shell, form;

    shell = XtVaOpenApplication(&app, "Demo", NULL, 0,
                               &argc, argv, NULL,
                               sessionShellWidgetClass,
                               NULL);
    form = XmCreateForm(shell, "form", NULL, 0);
    XtManageChild(form);
    XtRealizeWidget(shell);

    /* 1. Initialize libx11dnd for this display. */
    x11dnd_xt_init(shell);

    /* 2. Register the shell window as a drop target. */
    dnd_class.position_received = on_position;
    dnd_class.drop_received     = on_drop_received;
    x11dnd_xt_register_target(form, &dnd_class);

    /* 3. Run the application; the Xt handler feeds events automatically. */
    XtAppMainLoop(app);

    /* 4. Cleanup before exit. */
    x11dnd_xt_unregister_target(form);
    x11dnd_xt_destroy();

    return 0;
}
```

If you prefer to feed events manually from an Xt application:

```c
XEvent ev;
while (1) {
    XtAppNextEvent(app, &ev);

    if (!x11dnd_xt_process_event(NULL, &ev)) {
        XtDispatchEvent(&ev);
    }
}
```


## Xlib-only usage pattern

For non-Xt programs, the typical flow is:

1. `x11dnd_init(dpy)` — intern atoms.
2. `x11dnd_register_target(dpy, win, &callbacks, user_data)` — become a drop target.
3. In your event loop, feed `ClientMessage`, `SelectionNotify`, and `PropertyNotify` events to `x11dnd_target_process_event()` and `x11dnd_target_handle_selection_notify()`.
4. To drag, call `x11dnd_start_drag()` and feed source events to `x11dnd_source_process_event()`.
5. Request drop data from `drop_received` or by calling `x11dnd_target_request_selection()`.
6. Clean up with `x11dnd_destroy(dpy)`.


## Tests

libx11dnd includes a test suite in `tests/`. To build and run the tests:

```sh
cd libx11dnd
cd tests
make
bash test_runner.sh
```

The test runner starts an Xvfb instance when available, then executes each test binary. You can also build the library first, then run the tests:

```sh
cd libx11dnd
make
cd tests
make
bash test_runner.sh
```


## License

libx11dnd is distributed under the X/MIT license. See the `COPYING` file in the source distribution for the full license text.
