
#include "banker_tab.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/bankers.h"
#include "../core/results.h"

#define MAX_AMOUNT 1000000L /* largest number accepted in an entry box */
#define PLAY_INTERVAL_MS 1000


static void ensure_fallback_css(void)
{
    static gboolean installed = FALSE;
    static const char *css =
        "label.state-safe { background-color: #2e7d32; color: #ffffff; }\n"
        "label.state-unsafe { background-color: #ef6c00; color: #ffffff; }\n"
        "label.state-deadlock { background-color: #c62828; color: #ffffff; }\n"
        "label.banker-idle { background-color: #e0e0e0; color: #424242; }\n"
        "label.banker-banner { padding: 10px 14px; border-radius: 6px; font-weight: bold; font-size: 13pt; }\n"
        "label.banker-big { padding: 10px 24px; border-radius: 8px; font-weight: bold; font-size: 30pt; }\n"
        "label.banker-hl { background-color: #fff176; color: #000000; }\n"
        "label.banker-cell { padding: 3px 8px; border: 1px solid #bdbdbd; }\n"
        "label.banker-hdr { padding: 3px 8px; font-weight: bold; }\n";
    if (installed) return;
    installed = TRUE;

    GdkScreen *screen = gdk_screen_get_default();
    if (screen == NULL) return;
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_FALLBACK);
    g_object_unref(provider);
}

static void add_class(GtkWidget *w, const char *name)
{
    gtk_style_context_add_class(gtk_widget_get_style_context(w), name);
}

static void remove_class(GtkWidget *w, const char *name)
{
    gtk_style_context_remove_class(gtk_widget_get_style_context(w), name);
}


static void set_state_class(GtkWidget *w, const char *cls)
{
    remove_class(w, "banker-idle");
    remove_class(w, "state-safe");
    remove_class(w, "state-unsafe");
    remove_class(w, "state-deadlock");
    add_class(w, cls);
}

/* Destroy every child of a container (used before rebuilding a grid). */
static void container_clear(GtkContainer *c)
{
    GList *kids = gtk_container_get_children(c);
    for (GList *l = kids; l != NULL; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);
}

/* "[3, 3, 2]" */
static void fmt_vec(char *buf, size_t len, const int *v, int n)
{
    size_t pos = 0;
    if (len == 0) return;
    buf[0] = '\0';
    for (int i = -1; i <= n; i++) {
        int w;
        if (pos + 1 >= len) break;
        if (i == -1) w = snprintf(buf + pos, len - pos, "[");
        else if (i == n) w = snprintf(buf + pos, len - pos, "]");
        else w = snprintf(buf + pos, len - pos, "%s%d", i > 0 ? ", " : "", v[i]);
        if (w < 0) break;
        pos += (size_t)w;
    }
}

/* Error / info pop-up. */
static void show_message(GtkWidget *anchor, GtkMessageType type, const char *text)
{
    GtkWidget *top = (anchor != NULL) ? gtk_widget_get_toplevel(anchor) : NULL;
    GtkWindow *parent = (top != NULL && GTK_IS_WINDOW(top)) ? GTK_WINDOW(top) : NULL;
    GtkWidget *dlg = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, type,
                                            GTK_BUTTONS_OK, "%s", text);
    gtk_window_set_title(GTK_WINDOW(dlg), type == GTK_MESSAGE_ERROR ? "Input error" : "Banker's Algorithm");
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

/* A label with optional CSS classes. */
static GtkWidget *make_label(const char *text, const char *cls1, const char *cls2)
{
    GtkWidget *l = gtk_label_new(text);
    if (cls1 != NULL) add_class(l, cls1);
    if (cls2 != NULL) add_class(l, cls2);
    return l;
}

/* A frame with a padded vertical box inside; returns the box. */
static GtkWidget *make_frame(const char *title, GtkWidget **frame_out)
{
    GtkWidget *frame = gtk_frame_new(title);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_container_add(GTK_CONTAINER(frame), box);
    if (frame_out != NULL) *frame_out = frame;
    return box;
}

static GtkWidget *make_log_view(void)
{
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 6);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 4);
    return view;
}

static GtkWidget *wrap_in_scroller(GtkWidget *child, int min_height)
{
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
    if (min_height > 0) gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(sw), min_height);
    gtk_container_add(GTK_CONTAINER(sw), child);
    return sw;
}

static int clamp_count(int n, int max)
{
    return n < 0 ? 0 : (n > max ? max : n);
}


typedef struct {
    int avail[MAX_RES];
    int alloc[MAX_RES];
    int need[MAX_RES];
} Snapshot;

typedef struct {
    SystemState *state;
    gboolean alive;          /* FALSE once the widget has been destroyed        */
    gboolean busy;           /* TRUE while bankers_request() runs (it notifies) */
    gboolean showing_result; /* the Before/After panel shows a finished request */
    int n_res_built;         /* how many entry boxes exist right now            */
    int n_proc_built;

    GtkWidget *root;
    GtkWidget *combo;
    GtkWidget *input_grid;
    GtkWidget *entries[MAX_RES];
    GtkWidget *avail_label;
    GtkWidget *banner;
    GtkWidget *log_view;
    GtkWidget *ba_grid;

    int shown_pid;
    Snapshot before, after;
    gboolean has_after;
} RequestTab;

static void on_combo_changed(GtkComboBox *combo, gpointer data);
static void on_submit(GtkWidget *widget, gpointer data);

static void take_snapshot(const SystemState *s, int pid, Snapshot *snap)
{
    int need[MAX_PROC][MAX_RES];
    memset(snap, 0, sizeof *snap);
    memset(need, 0, sizeof need);
    state_available(s, snap->avail);
    state_need(s, need);
    if (pid >= 0 && pid < s->n_proc) {
        for (int j = 0; j < s->n_res && j < MAX_RES; j++) {
            snap->alloc[j] = s->allocation[pid][j];
            snap->need[j] = need[pid][j];
        }
    }
}

/* One row of the Before/After table: name + one number per resource. */
static void ba_add_row(GtkWidget *grid, int row, const char *name, const int *vals, const int *compare_to,
                       int n_res, gboolean show_values)
{
    GtkWidget *row_name = make_label(name, "banker-hdr", NULL);
    gtk_label_set_xalign(GTK_LABEL(row_name), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), row_name, 0, row, 1, 1);
    for (int j = 0; j < n_res; j++) {
        char num[24];
        if (show_values) snprintf(num, sizeof num, "%d", vals[j]);
        else snprintf(num, sizeof num, "-");
        GtkWidget *cell = make_label(num, "banker-cell", NULL);
        gtk_label_set_width_chars(GTK_LABEL(cell), 4);
        if (show_values && compare_to != NULL && compare_to[j] != vals[j]) add_class(cell, "banker-hl");
        gtk_grid_attach(GTK_GRID(grid), cell, 1 + j, row, 1, 1);
    }
}

static void update_ba_panel(RequestTab *t)
{
    const int m = clamp_count(t->state->n_res, MAX_RES);
    char pname[8] = "-", name[64];
    if (t->shown_pid >= 0 && t->shown_pid < t->state->n_proc) state_proc_name(t->shown_pid, pname);

    container_clear(GTK_CONTAINER(t->ba_grid));
    gtk_grid_attach(GTK_GRID(t->ba_grid), make_label("", NULL, NULL), 0, 0, 1, 1);
    for (int j = 0; j < m; j++) {
        char rn[8];
        state_res_name(j, rn);
        gtk_grid_attach(GTK_GRID(t->ba_grid), make_label(rn, "banker-hdr", NULL), 1 + j, 0, 1, 1);
    }

    snprintf(name, sizeof name, "BEFORE  Available");
    ba_add_row(t->ba_grid, 1, name, t->before.avail, NULL, m, TRUE);
    snprintf(name, sizeof name, "BEFORE  %s Allocation", pname);
    ba_add_row(t->ba_grid, 2, name, t->before.alloc, NULL, m, TRUE);
    snprintf(name, sizeof name, "BEFORE  %s Need", pname);
    ba_add_row(t->ba_grid, 3, name, t->before.need, NULL, m, TRUE);

    gtk_grid_attach(GTK_GRID(t->ba_grid), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, 4, 1 + m, 1);

    /* AFTER rows: cells that differ from BEFORE are highlighted in yellow. */
    snprintf(name, sizeof name, "AFTER   Available");
    ba_add_row(t->ba_grid, 5, name, t->after.avail, t->before.avail, m, t->has_after);
    snprintf(name, sizeof name, "AFTER   %s Allocation", pname);
    ba_add_row(t->ba_grid, 6, name, t->after.alloc, t->before.alloc, m, t->has_after);
    snprintf(name, sizeof name, "AFTER   %s Need", pname);
    ba_add_row(t->ba_grid, 7, name, t->after.need, t->before.need, m, t->has_after);

    gtk_widget_show_all(t->ba_grid);
}

static void update_avail_label(RequestTab *t)
{
    int avail[MAX_RES];
    char vec[160], text[200];
    memset(avail, 0, sizeof avail);
    state_available(t->state, avail);
    fmt_vec(vec, sizeof vec, avail, clamp_count(t->state->n_res, MAX_RES));
    snprintf(text, sizeof text, "Available right now: %s", vec);
    gtk_label_set_text(GTK_LABEL(t->avail_label), text);
}


static void update_idle_panel(RequestTab *t)
{
    int pid = gtk_combo_box_get_active(GTK_COMBO_BOX(t->combo));
    t->shown_pid = pid;
    t->has_after = FALSE;
    take_snapshot(t->state, pid, &t->before);
    memset(&t->after, 0, sizeof t->after);
    update_ba_panel(t);
}


static void populate_combo(RequestTab *t)
{
    GtkComboBoxText *c = GTK_COMBO_BOX_TEXT(t->combo);
    const int n = clamp_count(t->state->n_proc, MAX_PROC);
    int old = gtk_combo_box_get_active(GTK_COMBO_BOX(c));

    g_signal_handlers_block_by_func(c, G_CALLBACK(on_combo_changed), t);
    gtk_combo_box_text_remove_all(c);
    for (int i = 0; i < n; i++) {
        char pn[8], label[32];
        state_proc_name(i, pn);
        snprintf(label, sizeof label, "%s%s", pn, t->state->terminated[i] ? " (terminated)" : "");
        gtk_combo_box_text_append_text(c, label);
    }
    if (old < 0 || old >= n) old = (n > 0) ? 0 : -1;
    gtk_combo_box_set_active(GTK_COMBO_BOX(c), old);
    g_signal_handlers_unblock_by_func(c, G_CALLBACK(on_combo_changed), t);
}


{
    const int m = clamp_count(t->state->n_res, MAX_RES);
    container_clear(GTK_CONTAINER(t->input_grid));
    memset(t->entries, 0, sizeof t->entries);

    for (int j = 0; j < m; j++) {
        char rn[8];
        state_res_name(j, rn);
        GtkWidget *hdr = make_label(rn, "banker-hdr", NULL);
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_width_chars(GTK_ENTRY(entry), 5);
        gtk_entry_set_max_length(GTK_ENTRY(entry), 9);
        gtk_entry_set_alignment(GTK_ENTRY(entry), 0.5f);
        gtk_entry_set_text(GTK_ENTRY(entry), "0");
        g_signal_connect(entry, "activate", G_CALLBACK(on_submit), t); /* Enter submits */
        gtk_grid_attach(GTK_GRID(t->input_grid), hdr, j, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(t->input_grid), entry, j, 1, 1, 1);
        t->entries[j] = entry;
    }
    t->n_res_built = m;
    gtk_widget_show_all(t->input_grid);
}

/* Called by the shared state after ANY change. */
static void request_refresh(void *user_data)
{
    RequestTab *t = user_data;
    if (!t->alive || t->busy) return; /* busy: we caused this change ourselves */

    const int m = clamp_count(t->state->n_res, MAX_RES);
    const int n = clamp_count(t->state->n_proc, MAX_PROC);
    if (m != t->n_res_built) {
        rebuild_inputs(t);
        t->showing_result = FALSE;
    }
    if (n != t->n_proc_built) {
        t->n_proc_built = n;
        t->showing_result = FALSE;
    }
    populate_combo(t);
    update_avail_label(t);
    if (!t->showing_result) update_idle_panel(t);
}

static void on_combo_changed(GtkComboBox *combo, gpointer data)
{
    RequestTab *t = data;
    (void)combo;
    t->showing_result = FALSE;
    update_idle_panel(t);
}

/* Strict number parsing: whole non-negative number, nothing else. */
static gboolean parse_amount(const char *text, int *out, char *err, size_t err_len)
{
    while (isspace((unsigned char)*text)) text++;
    if (*text == '\0') {
        snprintf(err, err_len, "please enter a number (type 0 for none)");
        return FALSE;
    }
    errno = 0;
    char *end = NULL;
    long v = strtol(text, &end, 10);
    if (end == text) {
        snprintf(err, err_len, "\"%.20s\" is not a number", text);
        return FALSE;
    }
    while (isspace((unsigned char)*end)) end++;
    if (*end != '\0') {
        snprintf(err, err_len, "\"%.20s\" is not a whole number", text);
        return FALSE;
    }
    if (errno == ERANGE || v > MAX_AMOUNT) {
        snprintf(err, err_len, "the number is too large (maximum %ld)", MAX_AMOUNT);
        return FALSE;
    }
    if (v < 0) {
        snprintf(err, err_len, "negative numbers are not allowed");
        return FALSE;
    }
    *out = (int)v;
    return TRUE;
}

static void show_request_result(RequestTab *t, int pid, const int *req, const RequestResult *res)
{
    const int m = clamp_count(t->state->n_res, MAX_RES);
    char pn[8], vec[160], head[LINE_LEN + 32];
    state_proc_name(pid, pn);

    /* coloured banner */
    const char *cls = "state-safe";
    const char *title = "GRANTED";
    switch (bankers_outcome(res)) {
    case BANKERS_GRANTED: cls = "state-safe"; title = "GRANTED"; break;
    case BANKERS_WAIT: cls = "state-unsafe"; title = "WAIT"; break;
    case BANKERS_DENIED: cls = "state-unsafe"; title = "DENIED"; break;
    case BANKERS_ERROR: cls = "state-deadlock"; title = "ERROR"; break;
    }
    snprintf(head, sizeof head, "%s\n%s", title, res->message);
    gtk_label_set_text(GTK_LABEL(t->banner), head);
    set_state_class(t->banner, cls);

    /* step log */
    GString *log = g_string_new(NULL);
    fmt_vec(vec, sizeof vec, req, m);
    g_string_append_printf(log, "%s requests %s\n", pn, vec);
    fmt_vec(vec, sizeof vec, t->before.avail, m);
    g_string_append_printf(log, "Available before: %s\n", vec);
    g_string_append(log, "------------------------------------------------------------\n");
    for (int k = 0; k < res->safety.n_steps; k++) g_string_append_printf(log, "%s\n", res->safety.steps[k]);
    g_string_append(log, "------------------------------------------------------------\n");
    fmt_vec(vec, sizeof vec, t->after.avail, m);
    g_string_append_printf(log, "Available after:  %s\n", vec);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(t->log_view)), log->str, -1);
    g_string_free(log, TRUE);
}

static void on_submit(GtkWidget *widget, gpointer data)
{
    RequestTab *t = data;
    SystemState *s = t->state;
    (void)widget;

    if (s->n_proc <= 0 || s->n_res <= 0) {
        show_message(t->root, GTK_MESSAGE_ERROR, "There are no processes or resources yet.\nCreate them first (menu options 1 and 2).");
        return;
    }
    const int pid = gtk_combo_box_get_active(GTK_COMBO_BOX(t->combo));
    if (pid < 0 || pid >= s->n_proc) {
        show_message(t->root, GTK_MESSAGE_ERROR, "Please choose a process.");
        return;
    }

   
    int req[MAX_RES];
    memset(req, 0, sizeof req);
    gboolean any_positive = FALSE;
    for (int j = 0; j < s->n_res && j < MAX_RES; j++) {
        char err[96], rn[8], msg[160];
        if (t->entries[j] == NULL || !parse_amount(gtk_entry_get_text(GTK_ENTRY(t->entries[j])), &req[j], err, sizeof err)) {
            state_res_name(j, rn);
            snprintf(msg, sizeof msg, "Resource %s: %s.", rn, t->entries[j] ? err : "no input box");
            show_message(t->root, GTK_MESSAGE_ERROR, msg);
            if (t->entries[j] != NULL) gtk_widget_grab_focus(t->entries[j]);
            return;
        }
        if (req[j] > 0) any_positive = TRUE;
    }
    if (!any_positive) {
        show_message(t->root, GTK_MESSAGE_ERROR, "The request is all zeros.\nEnter at least one amount greater than 0.");
        return;
    }

    
    take_snapshot(s, pid, &t->before);
    t->busy = TRUE;
    RequestResult res = bankers_request(s, pid, req);
    t->busy = FALSE;
    take_snapshot(s, pid, &t->after);

    t->shown_pid = pid;
    t->has_after = TRUE;
    t->showing_result = TRUE;
    show_request_result(t, pid, req, &res);
    update_ba_panel(t);
    update_avail_label(t);
}

static void on_request_destroy(GtkWidget *widget, gpointer data)
{
    RequestTab *t = data;
    (void)widget;
    t->alive = FALSE; /* the struct itself stays: the state still holds a pointer to it */
}

GtkWidget *banker_request_tab_new(SystemState *state)
{
    ensure_fallback_css();

    RequestTab *t = g_new0(RequestTab, 1);
    t->state = state;
    t->alive = TRUE;
    t->shown_pid = -1;

    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(page), 14);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span size='x-large' weight='bold'>Request Resources</span>");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_box_pack_start(GTK_BOX(page), title, FALSE, FALSE, 0);

    /* --- frame: the request form --- */
    GtkWidget *form_frame;
    GtkWidget *form = make_frame("New request", &form_frame);
    GtkWidget *row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(row1), gtk_label_new("Process:"), FALSE, FALSE, 0);
    t->combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(row1), t->combo, FALSE, FALSE, 0);
    t->avail_label = gtk_label_new("");
    gtk_box_pack_end(GTK_BOX(row1), t->avail_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(form), row1, FALSE, FALSE, 0);

    GtkWidget *hint = gtk_label_new("Request vector (how many units of each resource):");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_box_pack_start(GTK_BOX(form), hint, FALSE, FALSE, 0);
    t->input_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(t->input_grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(t->input_grid), 4);
    gtk_box_pack_start(GTK_BOX(form), t->input_grid, FALSE, FALSE, 0);

    GtkWidget *submit = gtk_button_new_with_label("Submit Request");
    gtk_widget_set_halign(submit, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(form), submit, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), form_frame, FALSE, FALSE, 0);

    /* --- result banner --- */
    t->banner = make_label("Submit a request to see the result here.", "banker-banner", "banker-idle");
    gtk_label_set_line_wrap(GTK_LABEL(t->banner), TRUE);
    gtk_label_set_justify(GTK_LABEL(t->banner), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(page), t->banner, FALSE, FALSE, 0);

    /* --- frame: Before vs After --- */
    GtkWidget *ba_frame;
    GtkWidget *ba_box = make_frame("Before vs After", &ba_frame);
    t->ba_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(t->ba_grid), 4);
    gtk_grid_set_row_spacing(GTK_GRID(t->ba_grid), 4);
    gtk_box_pack_start(GTK_BOX(ba_box), t->ba_grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), ba_frame, FALSE, FALSE, 0);

    /* --- frame: step log --- */
    GtkWidget *log_frame;
    GtkWidget *log_box = make_frame("Step log", &log_frame);
    t->log_view = make_log_view();
    GtkWidget *log_scroll = wrap_in_scroller(t->log_view, 150);
    gtk_widget_set_vexpand(log_scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(log_box), log_scroll, TRUE, TRUE, 0);
    gtk_widget_set_vexpand(log_frame, TRUE);
    gtk_box_pack_start(GTK_BOX(page), log_frame, TRUE, TRUE, 0);

    /* The tab's top-level widget is a scroller so it also fits small screens. */
    t->root = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(t->root), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(t->root), page);

    g_signal_connect(t->combo, "changed", G_CALLBACK(on_combo_changed), t);
    g_signal_connect(submit, "clicked", G_CALLBACK(on_submit), t);
    g_signal_connect(t->root, "destroy", G_CALLBACK(on_request_destroy), t);

    rebuild_inputs(t);
    t->n_proc_built = clamp_count(state->n_proc, MAX_PROC);
    populate_combo(t);
    update_avail_label(t);
    update_idle_panel(t);

    state_subscribe(request_refresh, t);
    gtk_widget_show_all(t->root);
    return t->root;
}


typedef struct {
    int pid;
    int before[MAX_RES]; /* Work before this process runs        */
    int after[MAX_RES];  /* Work after it finishes and releases  */
} RunStep;

typedef struct {
    SystemState *state;
    gboolean alive;
    guint timer_id; /* 0 = not playing */

    GtkWidget *root;
    GtkWidget *run_btn, *play_btn;
    GtkWidget *verdict, *seq_label, *work_label;
    GtkListStore *store;
    GtkWidget *tree;
    GtkWidget *log_view;
    GtkWidget *grids[4]; /* Available, Allocation, Maximum, Need */
    GtkWidget *row_hdr[3][MAX_PROC];
    GtkWidget *cell[3][MAX_PROC][MAX_RES];

    gboolean has_result;
    SafetyResult result;
    RunStep steps[MAX_PROC];
    int n_steps;
    int blocked[MAX_PROC];
    int n_blocked;
    int play_pos; /* how many steps are revealed while playing */
} RunTab;

static void stop_play(RunTab *t)
{
    if (t->timer_id != 0) {
        g_source_remove(t->timer_id);
        t->timer_id = 0;
    }
    if (t->alive) gtk_button_set_label(GTK_BUTTON(t->play_btn), "Play step-by-step");
}

/* Light up (or un-light) the rows of process `pid` in the three matrices. */
static void highlight_proc(RunTab *t, int pid)
{
    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < MAX_PROC; i++) {
            const gboolean on = (i == pid);
            if (t->row_hdr[k][i] != NULL) {
                if (on) add_class(t->row_hdr[k][i], "banker-hl");
                else remove_class(t->row_hdr[k][i], "banker-hl");
            }
            for (int j = 0; j < MAX_RES; j++) {
                if (t->cell[k][i][j] == NULL) continue;
                if (on) add_class(t->cell[k][i][j], "banker-hl");
                else remove_class(t->cell[k][i][j], "banker-hl");
            }
        }
    }
}

/* Header row "R0 R1 R2 ..." at row 0, starting at column `first_col`. */
static void attach_res_header(GtkWidget *grid, int n_res, int first_col)
{
    for (int j = 0; j < n_res; j++) {
        char rn[8];
        state_res_name(j, rn);
        gtk_grid_attach(GTK_GRID(grid), make_label(rn, "banker-hdr", NULL), first_col + j, 0, 1, 1);
    }
}

static void build_matrix(RunTab *t, int k, int data[MAX_PROC][MAX_RES])
{
    GtkWidget *grid = t->grids[k + 1];
    const int n = clamp_count(t->state->n_proc, MAX_PROC);
    const int m = clamp_count(t->state->n_res, MAX_RES);

    container_clear(GTK_CONTAINER(grid));
    memset(t->row_hdr[k], 0, sizeof t->row_hdr[k]);
    memset(t->cell[k], 0, sizeof t->cell[k]);

    gtk_grid_attach(GTK_GRID(grid), make_label("", NULL, NULL), 0, 0, 1, 1);
    attach_res_header(grid, m, 1);
    for (int i = 0; i < n; i++) {
        char pn[8], num[24];
        state_proc_name(i, pn);
        t->row_hdr[k][i] = make_label(pn, "banker-hdr", NULL);
        gtk_grid_attach(GTK_GRID(grid), t->row_hdr[k][i], 0, 1 + i, 1, 1);
        for (int j = 0; j < m; j++) {
            snprintf(num, sizeof num, "%d", data[i][j]);
            t->cell[k][i][j] = make_label(num, "banker-cell", NULL);
            gtk_label_set_width_chars(GTK_LABEL(t->cell[k][i][j]), 3);
            gtk_grid_attach(GTK_GRID(grid), t->cell[k][i][j], 1 + j, 1 + i, 1, 1);
        }
    }
    gtk_widget_show_all(grid);
}

static void rebuild_matrices(RunTab *t)
{
    int need[MAX_PROC][MAX_RES], avail[MAX_RES];
    const int m = clamp_count(t->state->n_res, MAX_RES);
    memset(need, 0, sizeof need);
    memset(avail, 0, sizeof avail);
    state_need(t->state, need);
    state_available(t->state, avail);

   
    container_clear(GTK_CONTAINER(t->grids[0]));
    attach_res_header(t->grids[0], m, 0);
    for (int j = 0; j < m; j++) {
        char num[24];
        snprintf(num, sizeof num, "%d", avail[j]);
        GtkWidget *c = make_label(num, "banker-cell", NULL);
        gtk_label_set_width_chars(GTK_LABEL(c), 3);
        gtk_grid_attach(GTK_GRID(t->grids[0]), c, j, 1, 1, 1);
    }
    gtk_widget_show_all(t->grids[0]);

    build_matrix(t, 0, t->state->allocation);
    build_matrix(t, 1, t->state->maximum);
    build_matrix(t, 2, need);
}


static void clear_result(RunTab *t)
{
    stop_play(t);
    t->has_result = FALSE;
    t->n_steps = 0;
    t->n_blocked = 0;
    gtk_list_store_clear(t->store);
    gtk_label_set_text(GTK_LABEL(t->verdict), "NOT RUN");
    set_state_class(t->verdict, "banker-idle");
    gtk_label_set_text(GTK_LABEL(t->seq_label), "Press \"Run Banker's Algorithm\" to check the current state.");
    gtk_label_set_text(GTK_LABEL(t->work_label), "");
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(t->log_view)), "", -1);
    highlight_proc(t, -1);
}

static void run_refresh(void *user_data)
{
    RunTab *t = user_data;
    if (!t->alive) return;
    rebuild_matrices(t);
    clear_result(t); /* the old answer may no longer be true */
}


static void compute_steps(RunTab *t)
{
    const SystemState *s = t->state;
    const int n = clamp_count(s->n_proc, MAX_PROC);
    const int m = clamp_count(s->n_res, MAX_RES);
    int work[MAX_RES];
    gboolean in_seq[MAX_PROC] = {FALSE};

    memset(work, 0, sizeof work);
    state_available(s, work);
    t->n_steps = 0;
    for (int k = 0; k < t->result.seq_len && k < MAX_PROC; k++) {
        const int pid = t->result.safe_sequence[k];
        if (pid < 0 || pid >= n) continue;
        RunStep *st = &t->steps[t->n_steps++];
        st->pid = pid;
        memset(st->before, 0, sizeof st->before);
        memset(st->after, 0, sizeof st->after);
        for (int j = 0; j < m; j++) {
            st->before[j] = work[j];
            work[j] += s->allocation[pid][j];
            st->after[j] = work[j];
        }
        in_seq[pid] = TRUE;
    }
    t->n_blocked = 0;
    for (int i = 0; i < n; i++)
        if (!in_seq[i] && !s->terminated[i]) t->blocked[t->n_blocked++] = i;
}

static void append_step_row(RunTab *t, int k)
{
    const int m = clamp_count(t->state->n_res, MAX_RES);
    const RunStep *st = &t->steps[k];
    char num[16], pn[8], b[160], a[160];
    GtkTreeIter it;

    snprintf(num, sizeof num, "%d", k + 1);
    state_proc_name(st->pid, pn);
    fmt_vec(b, sizeof b, st->before, m);
    fmt_vec(a, sizeof a, st->after, m);
    gtk_list_store_append(t->store, &it);
    gtk_list_store_set(t->store, &it, 0, num, 1, pn, 2, b, 3, a, -1);

    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(t->tree));
    gtk_tree_selection_select_iter(sel, &it);
    GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(t->store), &it);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(t->tree), path, NULL, FALSE, 0.0f, 0.0f);
    gtk_tree_path_free(path);
}

/* Final row for an UNSAFE state: who is stuck. */
static void append_blocked_row(RunTab *t)
{
    const int m = clamp_count(t->state->n_res, MAX_RES);
    int work[MAX_RES];
    char names[160] = "", vec[160];
    GtkTreeIter it;

    memset(work, 0, sizeof work);
    state_available(t->state, work);
    if (t->n_steps > 0) memcpy(work, t->steps[t->n_steps - 1].after, sizeof work);
    for (int k = 0; k < t->n_blocked; k++) {
        char pn[8];
        state_proc_name(t->blocked[k], pn);
        size_t used = strlen(names);
        snprintf(names + used, sizeof names - used, "%s%s", k > 0 ? ", " : "", pn);
    }
    fmt_vec(vec, sizeof vec, work, m);
    gtk_list_store_append(t->store, &it);
    gtk_list_store_set(t->store, &it, 0, "-", 1, names, 2, vec, 3, "blocked: Need > Work", -1);
}

static void show_verdict(RunTab *t)
{
    char text[LINE_LEN * 2];
    gtk_label_set_text(GTK_LABEL(t->verdict), t->result.is_safe ? "SAFE" : "UNSAFE");
    set_state_class(t->verdict, t->result.is_safe ? "state-safe" : "state-unsafe");

    if (t->result.is_safe) {
        GString *seq = g_string_new("Safe sequence:  ");
        for (int k = 0; k < t->n_steps; k++) {
            char pn[8];
            state_proc_name(t->steps[k].pid, pn);
            g_string_append_printf(seq, "%s%s", k > 0 ? " -> " : "", pn);
        }
        if (t->n_steps == 0) g_string_append(seq, "(no active processes)");
        gtk_label_set_text(GTK_LABEL(t->seq_label), seq->str);
        g_string_free(seq, TRUE);
    } else {
        GString *names = g_string_new(NULL);
        for (int k = 0; k < t->n_blocked; k++) {
            char pn[8];
            state_proc_name(t->blocked[k], pn);
            g_string_append_printf(names, "%s%s", k > 0 ? ", " : "", pn);
        }
        snprintf(text, sizeof text, "No safe sequence exists. Cannot finish: %s", names->str);
        gtk_label_set_text(GTK_LABEL(t->seq_label), text);
        g_string_free(names, TRUE);
    }

    GString *log = g_string_new(NULL);
    for (int k = 0; k < t->result.n_steps; k++) g_string_append_printf(log, "%s\n", t->result.steps[k]);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(t->log_view)), log->str, -1);
    g_string_free(log, TRUE);
}


{
    stop_play(t);
    if (t->state->n_proc <= 0 || t->state->n_res <= 0) {
        show_message(t->root, GTK_MESSAGE_ERROR, "There are no processes or resources yet.\nCreate them first (menu options 1 and 2).");
        return FALSE;
    }
    t->result = bankers_is_safe(t->state);
    t->has_result = TRUE;
    compute_steps(t);
    show_verdict(t);
    gtk_label_set_text(GTK_LABEL(t->work_label), "");
    highlight_proc(t, -1);
    return TRUE;
}

static void on_run(GtkWidget *widget, gpointer data)
{
    RunTab *t = data;
    (void)widget;
    if (!do_run(t)) return;

    /* show all steps at once */
    gtk_list_store_clear(t->store);
    for (int k = 0; k < t->n_steps; k++) append_step_row(t, k);
    if (!t->result.is_safe) append_blocked_row(t);
}

/* One tick of the animation: reveal the next step. */
static gboolean play_tick(gpointer data)
{
    RunTab *t = data;
    const int m = clamp_count(t->state->n_res, MAX_RES);
    char vec[160], text[220];

    if (!t->alive) {
        t->timer_id = 0;
        return G_SOURCE_REMOVE;
    }
    if (t->play_pos < t->n_steps) {
        const RunStep *st = &t->steps[t->play_pos];
        char pn[8];
        state_proc_name(st->pid, pn);
        append_step_row(t, t->play_pos);
        highlight_proc(t, st->pid);
        fmt_vec(vec, sizeof vec, st->after, m);
        snprintf(text, sizeof text, "%s runs and releases its resources.  Work = %s", pn, vec);
        gtk_label_set_text(GTK_LABEL(t->work_label), text);
        t->play_pos++;
        return G_SOURCE_CONTINUE;
    }

    /* finished */
    highlight_proc(t, -1);
    if (!t->result.is_safe) {
        append_blocked_row(t);
        gtk_label_set_text(GTK_LABEL(t->work_label), "No other process can run: the state is UNSAFE.");
    } else {
        gtk_label_set_text(GTK_LABEL(t->work_label), "All processes finished: the state is SAFE.");
    }
    t->timer_id = 0;
    gtk_button_set_label(GTK_BUTTON(t->play_btn), "Play step-by-step");
    return G_SOURCE_REMOVE;
}

static void on_play(GtkWidget *widget, gpointer data)
{
    RunTab *t = data;
    (void)widget;

    if (t->timer_id != 0) { /* second click = stop */
        stop_play(t);
        return;
    }
    if (!do_run(t)) return;

    gtk_list_store_clear(t->store);
    t->play_pos = 0;
    gtk_button_set_label(GTK_BUTTON(t->play_btn), "Stop");

    int avail[MAX_RES];
    char vec[160], text[220];
    memset(avail, 0, sizeof avail);
    state_available(t->state, avail);
    fmt_vec(vec, sizeof vec, avail, clamp_count(t->state->n_res, MAX_RES));
    snprintf(text, sizeof text, "Start.  Work = Available = %s", vec);
    gtk_label_set_text(GTK_LABEL(t->work_label), text);

    t->timer_id = g_timeout_add(PLAY_INTERVAL_MS, play_tick, t);
}

static void on_run_destroy(GtkWidget *widget, gpointer data)
{
    RunTab *t = data;
    (void)widget;
    stop_play(t); /* alive is still TRUE here, but the button is going away: harmless */
    t->alive = FALSE;
    if (t->store != NULL) {
        g_object_unref(t->store);
        t->store = NULL;
    }
}

static GtkWidget *make_tree(RunTab *t)
{
    static const char *titles[4] = {"Step", "Process", "Work Before", "Work After"};
    t->store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(t->store));
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(tree), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
    for (int c = 0; c < 4; c++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(titles[c], r, "text", c, NULL);
        gtk_tree_view_column_set_expand(col, c >= 2);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);
    }
    return tree;
}

GtkWidget *banker_run_tab_new(SystemState *state)
{
    ensure_fallback_css();

    RunTab *t = g_new0(RunTab, 1);
    t->state = state;
    t->alive = TRUE;

    /* ---------- left column: controls, verdict, steps ---------- */
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_hexpand(left, TRUE);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span size='x-large' weight='bold'>Run Banker's Algorithm</span>");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_box_pack_start(GTK_BOX(left), title, FALSE, FALSE, 0);

    GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    t->run_btn = gtk_button_new_with_label("Run Banker's Algorithm");
    t->play_btn = gtk_button_new_with_label("Play step-by-step");
    gtk_box_pack_start(GTK_BOX(buttons), t->run_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(buttons), t->play_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), buttons, FALSE, FALSE, 0);

    t->verdict = make_label("NOT RUN", "banker-big", "banker-idle");
    gtk_box_pack_start(GTK_BOX(left), t->verdict, FALSE, FALSE, 0);

    t->seq_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(t->seq_label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(t->seq_label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(t->seq_label), TRUE);
    gtk_box_pack_start(GTK_BOX(left), t->seq_label, FALSE, FALSE, 0);

    t->work_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(t->work_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(left), t->work_label, FALSE, FALSE, 0);

    GtkWidget *steps_frame;
    GtkWidget *steps_box = make_frame("Steps", &steps_frame);
    t->tree = make_tree(t);
    GtkWidget *tree_scroll = wrap_in_scroller(t->tree, 170);
    gtk_widget_set_vexpand(tree_scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(steps_box), tree_scroll, TRUE, TRUE, 0);
    gtk_widget_set_vexpand(steps_frame, TRUE);
    gtk_box_pack_start(GTK_BOX(left), steps_frame, TRUE, TRUE, 0);

    GtkWidget *log_frame;
    GtkWidget *log_box = make_frame("Explanation", &log_frame);
    t->log_view = make_log_view();
    gtk_box_pack_start(GTK_BOX(log_box), wrap_in_scroller(t->log_view, 110), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(left), log_frame, FALSE, FALSE, 0);

    /* ---------- right column: the four tables ---------- */
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    static const char *grid_titles[4] = {"Available Vector", "Allocation Matrix", "Maximum Matrix", "Need Matrix"};
    for (int k = 0; k < 4; k++) {
        GtkWidget *frame;
        GtkWidget *box = make_frame(grid_titles[k], &frame);
        t->grids[k] = gtk_grid_new();
        gtk_grid_set_column_spacing(GTK_GRID(t->grids[k]), 2);
        gtk_grid_set_row_spacing(GTK_GRID(t->grids[k]), 2);
        gtk_box_pack_start(GTK_BOX(box), t->grids[k], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(right), frame, FALSE, FALSE, 0);
    }
    GtkWidget *right_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(right_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(right_scroll), 330);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(right_scroll), TRUE);
    gtk_container_add(GTK_CONTAINER(right_scroll), right);

    
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(page), 14);
    gtk_box_pack_start(GTK_BOX(page), left, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), right_scroll, FALSE, TRUE, 0);
    t->root = page;

    g_signal_connect(t->run_btn, "clicked", G_CALLBACK(on_run), t);
    g_signal_connect(t->play_btn, "clicked", G_CALLBACK(on_play), t);
    g_signal_connect(t->root, "destroy", G_CALLBACK(on_run_destroy), t);

    rebuild_matrices(t);
    clear_result(t);
    state_subscribe(run_refresh, t);
    gtk_widget_show_all(t->root);
    return t->root;
}
