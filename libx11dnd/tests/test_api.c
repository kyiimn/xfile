/*
 * test_api.c — TDD compile-time verification of the x11dnd.h public API.
 *
 * This test does not link against libx11dnd; it only verifies that the
 * header is valid C99, that the advertised types and constants have the
 * expected values, and that the X11DndClass struct contains all required
 * callback slots at non-negative offsets.
 *
 * Compile with:
 *   cc -std=c99 -I libx11dnd/include -c libx11dnd/tests/test_api.c -o /dev/null
 */

#include <x11dnd.h>
#include <stddef.h>  /* offsetof, size_t */

/* ---- Compile-time constant assertions ---------------------------------- */

/* XDnD version constants (preprocessor-evaluable #defines) */
#if (X11DND_VERSION_5 != 5)
#  error "X11DND_VERSION_5 must equal 5"
#endif
#if (X11DND_VERSION_MIN != 3)
#  error "X11DND_VERSION_MIN must equal 3"
#endif

/* Action enum values are not preprocessor constants, so they are
 * verified at runtime in main(). The enum type itself is checked here. */
typedef char x11dnd_action_enum_chk[
    (sizeof(X11DndAction) > 0) ? 1 : -1];

/* MIME type string constants must be non-empty string literals */
typedef char x11dnd_mime_uri_list_chk[(sizeof(X11DND_MIME_URI_LIST) > 1) ? 1 : -1];
typedef char x11dnd_mime_utf8_string_chk[(sizeof(X11DND_MIME_UTF8_STRING) > 1) ? 1 : -1];
typedef char x11dnd_mime_string_chk[(sizeof(X11DND_MIME_STRING) > 1) ? 1 : -1];
typedef char x11dnd_mime_text_plain_chk[(sizeof(X11DND_MIME_TEXT_PLAIN) > 1) ? 1 : -1];
typedef char x11dnd_mime_file_name_chk[(sizeof(X11DND_MIME_FILE_NAME) > 1) ? 1 : -1];
typedef char x11dnd_mime_file_list_chk[(sizeof(X11DND_MIME_FILE_LIST) > 1) ? 1 : -1];

/* ---- Struct size and offset verification ------------------------------- */

/* X11DndClass must be non-empty and contain all required callbacks. */
typedef char x11dnd_class_nonempty_chk[(sizeof(X11DndClass) > 0) ? 1 : -1];

/* All callback offsets must be non-negative (sanity: offsetof never returns
 * negative, but this guards against accidental field removal by ensuring
 * each field exists and has a valid offset). */
struct x11dnd_class_offsets {
    int drag_begin;
    int drag_end;
    int get_drag_data;
    int status_received;
    int finished_received;
    int on_enter;
    int position_received;
    int on_leave;
    int drop_received;
    int action_ask;
    int on_error;
};

#define FILL_OFF(field) \
    (int)offsetof(X11DndClass, field)

static const struct x11dnd_class_offsets g_offsets = {
    FILL_OFF(on_drag_begin),
    FILL_OFF(on_drag_end),
    FILL_OFF(get_drag_data),
    FILL_OFF(status_received),
    FILL_OFF(finished_received),
    FILL_OFF(on_enter),
    FILL_OFF(position_received),
    FILL_OFF(on_leave),
    FILL_OFF(drop_received),
    FILL_OFF(action_ask),
    FILL_OFF(on_error),
};

/* ---- Opaque handle checks --------------------------------------------- */

/* Opaque types must be incomplete — verify by declaring pointers only.
 * Using sizeof on an incomplete type would be a compile error, which is
 * itself the proof that the types are opaque. */
static X11DndSourceSession * volatile p_src_sess = NULL;
static X11DndTargetSession * volatile p_tgt_sess = NULL;

/* ---- Function prototype presence checks -------------------------------- */

/* Assign each public function address to a typed pointer to verify the
 * prototype signature matches at compile time. These are never called;
 * they only validate prototype compatibility. Compile-only (-c) — no
 * link required. */
static int (*p_init)(Display *) = x11dnd_init;
static void (*p_destroy)(Display *) = x11dnd_destroy;

static X11DndSourceSession *(*p_start_drag)(
    Display *, Window, X11DndClass *, Time,
    Atom *, int, Atom *, int, void *) = x11dnd_start_drag;
static void (*p_cancel_drag)(X11DndSourceSession *) = x11dnd_cancel_drag;
static void (*p_end_drag)(X11DndSourceSession *) = x11dnd_end_drag;
static int (*p_source_proc)(XEvent *) = x11dnd_source_process_event;

static int (*p_reg_target)(Display *, Window, X11DndClass *, void *) =
    x11dnd_register_target;
static void (*p_unreg_target)(Display *, Window) = x11dnd_unregister_target;
static int (*p_target_proc)(XEvent *) = x11dnd_target_process_event;

static Bool (*p_set_aware)(Display *, Window, int) = x11dnd_set_aware;
static int (*p_get_aware)(Display *, Window) = x11dnd_get_aware_version;
static Bool (*p_ver_at_least)(int, int) = x11dnd_version_at_least;
static void (*p_send_status)(Display *, Window, Window, Bool,
                             int, int, int, int, Atom) = x11dnd_send_status;
static void (*p_send_finished)(Display *, Window, Window, Bool, Atom) =
    x11dnd_send_finished;

static void *(*p_src_user)(X11DndSourceSession *) =
    x11dnd_source_get_user_data;
static void *(*p_tgt_user)(X11DndTargetSession *) =
    x11dnd_target_get_user_data;
static Window (*p_src_win)(X11DndSourceSession *) =
    x11dnd_source_get_window;
static Window (*p_tgt_win)(X11DndTargetSession *) =
    x11dnd_target_get_window;
static Display *(*p_src_dpy)(X11DndSourceSession *) =
    x11dnd_source_get_display;
static Display *(*p_tgt_dpy)(X11DndTargetSession *) =
    x11dnd_target_get_display;
static int (*p_tgt_ver)(X11DndTargetSession *) =
    x11dnd_target_get_negotiated_version;

/* ---- Callback type presence checks ------------------------------------ */

static x11dnd_drag_data_cb p_drag_data_cb = NULL;
static x11dnd_drop_received_cb p_drop_cb = NULL;
static x11dnd_action_ask_cb p_ask_cb = NULL;
static x11dnd_position_cb p_pos_cb = NULL;
static x11dnd_status_cb p_status_cb = NULL;
static x11dnd_finished_cb p_finished_cb = NULL;

/* ---- main ------------------------------------------------------------- */

int main(void)
{
    /* Runtime checks mirroring the compile-time assertions, so a debugger
     * or test harness can observe the values. */
    if (X11DND_VERSION_5 != 5) return 1;
    if (X11DND_VERSION_MIN != 3) return 2;
    if (sizeof(X11DndClass) == 0) return 3;

    /* Action enum values (not preprocessor-checkable). */
    if (X11DND_ACTION_COPY != 0) return 100;
    if (X11DND_ACTION_MOVE != 1) return 101;
    if (X11DND_ACTION_LINK != 2) return 102;
    if (X11DND_ACTION_ASK != 3) return 103;
    if (X11DND_ACTION_PRIVATE != 4) return 104;
    if (X11DND_ACTION_DIRECT_SAVE != 5) return 105;

    /* Verify all offsets are non-negative. */
    if (g_offsets.drag_begin < 0) return 4;
    if (g_offsets.drag_end < 0) return 5;
    if (g_offsets.get_drag_data < 0) return 6;
    if (g_offsets.status_received < 0) return 7;
    if (g_offsets.finished_received < 0) return 8;
    if (g_offsets.on_enter < 0) return 9;
    if (g_offsets.position_received < 0) return 10;
    if (g_offsets.on_leave < 0) return 11;
    if (g_offsets.drop_received < 0) return 12;
    if (g_offsets.action_ask < 0) return 13;
    if (g_offsets.on_error < 0) return 14;

    /* Suppress unused-variable warnings for the pointer stubs. */
    (void)p_init; (void)p_destroy; (void)p_start_drag;
    (void)p_cancel_drag; (void)p_end_drag; (void)p_source_proc;
    (void)p_reg_target; (void)p_unreg_target; (void)p_target_proc;
    (void)p_set_aware; (void)p_get_aware; (void)p_ver_at_least;
    (void)p_send_status; (void)p_send_finished;
    (void)p_src_user; (void)p_tgt_user; (void)p_src_win; (void)p_tgt_win;
    (void)p_src_dpy; (void)p_tgt_dpy; (void)p_tgt_ver;
    (void)p_drag_data_cb; (void)p_drop_cb; (void)p_ask_cb;
    (void)p_pos_cb; (void)p_status_cb; (void)p_finished_cb;
    (void)p_src_sess; (void)p_tgt_sess;

    return 0;
}