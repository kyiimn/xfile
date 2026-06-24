## libx11dnd Makefile.am — convenience (non-installed) static library
## Copyright (C) 2023-2026 alx@fastestcode.org

noinst_LIBRARIES = libx11dnd.a

libx11dnd_a_SOURCES = \
	src/x11dnd_atoms.c \
	src/x11dnd_source.c \
	src/x11dnd_target.c \
	src/x11dnd_action.c \
	src/x11dnd_reply.c \
	src/x11dnd_incr.c \
	src/x11dnd_dsave.c \
	src/x11dnd_proxy.c \
	src/x11dnd_util.c \
	src/x11dnd_xt.c \
	src/x11dnd_stub.c

libx11dnd_a_CPPFLAGS = \
	-I$(srcdir)/include \
	$(X11_CFLAGS) \
	$(XT_CFLAGS) \
	$(XEXT_CFLAGS)

# Public and internal headers — distributed but not installed.
noinst_HEADERS = \
	include/x11dnd.h \
	include/x11dnd_xt.h \
	include/x11dnd_source.h \
	include/x11dnd_target.h \
	include/x11dnd_action.h \
	include/x11dnd_atoms.h \
	include/x11dnd_incr.h \
	include/x11dnd_dsave.h \
	include/x11dnd_proxy.h \
	include/x11dnd_util.h

dist_man3_MANS = man/x11dnd.3 \
	man/x11dnd_init.3 \
	man/x11dnd_destroy.3 \
	man/x11dnd_start_drag.3 \
	man/x11dnd_cancel_drag.3 \
	man/x11dnd_end_drag.3 \
	man/x11dnd_source_process_event.3 \
	man/x11dnd_register_target.3 \
	man/x11dnd_unregister_target.3 \
	man/x11dnd_target_process_event.3 \
	man/x11dnd_target_request_selection.3 \
	man/x11dnd_target_handle_selection_notify.3 \
	man/x11dnd_set_aware.3 \
	man/x11dnd_get_aware_version.3 \
	man/x11dnd_version_at_least.3 \
	man/x11dnd_send_status.3 \
	man/x11dnd_send_finished.3 \
	man/x11dnd_source_get_user_data.3 \
	man/x11dnd_target_get_user_data.3 \
	man/x11dnd_source_get_window.3 \
	man/x11dnd_target_get_window.3 \
	man/x11dnd_target_get_source_window.3 \
	man/x11dnd_source_get_display.3 \
	man/x11dnd_target_get_display.3 \
	man/x11dnd_target_get_negotiated_version.3 \
	man/x11dnd_target_get_action.3 \
	man/x11dnd_xt_init.3 \
	man/x11dnd_xt_destroy.3 \
	man/x11dnd_xt_register_target.3 \
	man/x11dnd_xt_unregister_target.3 \
	man/x11dnd_xt_start_drag.3 \
	man/x11dnd_xt_cancel_drag.3 \
	man/x11dnd_xt_set_drag_icon.3 \
	man/x11dnd_xt_set_poll_interval.3 \
	man/x11dnd_xt_stop_tracking.3 \
	man/x11dnd_xt_process_event.3

EXTRA_DIST = README.md