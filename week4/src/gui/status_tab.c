/* src/gui/status_tab.c - Menu 11: Display System Status (GTK 3)
 *
 * The whole page is rebuilt from the current SystemState every time the state
 * changes (simple and beginner friendly: old widgets are destroyed, new ones
 * are created).  CSS classes .state-safe / .state-unsafe / .state-deadlock
 * are defined by the Team Lead in app.c.
 */
#include <stdio.h>
#include <string.h>
#include "status_tab.h"
#include "../core/analysis.h"
#include "../core/results.h"
#include "../core/bankers.h"      /* bankers_is_safe (Student 2) */
#include "../core/detection.h"    /* detect_deadlock (Student 2) */

/* Lives for the whole application (the state module cannot unsubscribe);
 * 'scroll' is a weak pointer, NULL once the widget is destroyed. */
typedef struct {
    SystemState *state;
    GtkWidget   *scroll;
    GtkWidget   *content;   /* vertical box that is emptied and refilled */
} StatusTab;

static int clamp_int(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ------------------------------------------------------------------------ *
 *  Small widget helpers
 * ------------------------------------------------------------------------ */
/* A label showing markup text. 'bold' header, otherwise monospace <tt>. */
static GtkWidget *cell_label(const char *text, bool header, const char *color)
{
    char buf[96];
    if (header)
        snprintf(buf, sizeof buf, "<b>%s</b>", text);
    else if (color)
        snprintf(buf, sizeof buf, "<tt><b><span foreground='%s'>%s</span></b></tt>",
                 color, text);
    else
        snprintf(buf, sizeof buf, "<tt>%s</tt>", text);
    GtkWidget *l = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(l), buf);
    gtk_label_set_xalign(GTK_LABEL(l), header && text[0] == 'P' ? 0.0f : 0.5f);
    gtk_widget_set_size_request(l, 36, -1);
    return l;
}

/* Frame with a title and some inner padding around 'child'. */
static GtkWidget *framed(const char *title, GtkWidget *child)
{
    GtkWidget *f = gtk_frame_new(title);
    gtk_widget_set_margin_start(child, 10);
    gtk_widget_set_margin_end(child, 10);
    gtk_widget_set_margin_top(child, 8);
    gtk_widget_set_margin_bottom(child, 10);
    gtk_container_add(GTK_CONTAINER(f), child);
    return f;
}

static GtkWidget *new_grid(void)
{
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(g), 10);
    gtk_grid_set_row_spacing(GTK_GRID(g), 4);
    return g;
}

/* Table for a matrix: header row R0.., header column P0..  Terminated
 * processes get "(done)" after their name.  Non-zero cells are drawn in
 * 'hl_color' (e.g. orange for pending requests) when it is not NULL. */
static GtkWidget *matrix_frame(const char *title, const SystemState *s,
                               const int m[MAX_PROC][MAX_RES],
                               const char *hl_color)
{
    int np = clamp_int(s->n_proc, 0, MAX_PROC);
    int nr = clamp_int(s->n_res, 0, MAX_RES);
    GtkWidget *grid = new_grid();
    char buf[32], nm[8];

    for (int j = 0; j < nr; j++) {
        state_res_name(j, nm);
        gtk_grid_attach(GTK_GRID(grid), cell_label(nm, true, NULL), j + 1, 0, 1, 1);
    }
    for (int i = 0; i < np; i++) {
        state_proc_name(i, nm);
        if (s->terminated[i]) snprintf(buf, sizeof buf, "%s (done)", nm);
        else                  snprintf(buf, sizeof buf, "%s", nm);
        gtk_grid_attach(GTK_GRID(grid), cell_label(buf, true, NULL), 0, i + 1, 1, 1);
        for (int j = 0; j < nr; j++) {
            snprintf(buf, sizeof buf, "%d", m[i][j]);
            gtk_grid_attach(GTK_GRID(grid),
                cell_label(buf, false, (hl_color && m[i][j] != 0) ? hl_color : NULL),
                j + 1, i + 1, 1, 1);
        }
    }
    return framed(title, grid);
}

/* One-row table for the Available vector. */
static GtkWidget *vector_frame(const char *title, const SystemState *s,
                               const int v[MAX_RES])
{
    int nr = clamp_int(s->n_res, 0, MAX_RES);
    GtkWidget *grid = new_grid();
    char buf[32], nm[8];
    for (int j = 0; j < nr; j++) {
        state_res_name(j, nm);
        gtk_grid_attach(GTK_GRID(grid), cell_label(nm, true, NULL), j, 0, 1, 1);
        snprintf(buf, sizeof buf, "%d", v[j]);
        gtk_grid_attach(GTK_GRID(grid), cell_label(buf, false, NULL), j, 1, 1, 1);
    }
    return framed(title, grid);
}

/* A progress bar showing "xx.x%". The value is clamped to 0..100 for the bar. */
static GtkWidget *percent_bar(double percent)
{
    char buf[32];
    double frac = percent / 100.0;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    GtkWidget *bar = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar), frac);
    snprintf(buf, sizeof buf, "%.1f%%", percent);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(bar), buf);
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(bar), TRUE);
    gtk_widget_set_hexpand(bar, TRUE);
    return bar;
}

/* ------------------------------------------------------------------------ *
 *  Sections of the page
 * ------------------------------------------------------------------------ */
static void add_banner(GtkWidget *box, StateClass cls)
{
    const char *css, *sub;
    switch (cls) {
    case STATE_SAFE:
        css = "state-safe";
        sub = "All processes can eventually complete.";
        break;
    case STATE_UNSAFE:
        css = "state-unsafe";
        sub = "No safe sequence exists: completion of all processes is not guaranteed.";
        break;
    default:
        css = "state-deadlock";
        sub = "Some processes are permanently waiting for resources held by each other.";
        break;
    }
    char buf[256];
    snprintf(buf, sizeof buf,
             "<span size='xx-large' weight='bold'>System state: %s</span>\n"
             "<span size='medium'>%s</span>", analysis_class_name(cls), sub);
    GtkWidget *banner = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(banner), buf);
    gtk_label_set_justify(GTK_LABEL(banner), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(GTK_LABEL(banner), TRUE);
    gtk_widget_set_size_request(banner, -1, 80);
    gtk_style_context_add_class(gtk_widget_get_style_context(banner), css);
    gtk_box_pack_start(GTK_BOX(box), banner, FALSE, FALSE, 0);
}

static void add_matrices(GtkWidget *box, const SystemState *s)
{
    int avail[MAX_RES] = {0};
    int need[MAX_PROC][MAX_RES] = {{0}};
    state_available(s, avail);
    state_need(s, need);

    gtk_box_pack_start(GTK_BOX(box), vector_frame("Available Vector", s, avail),
                       FALSE, FALSE, 0);

    /* The four matrices flow side by side and wrap on narrow windows. */
    GtkWidget *flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), FALSE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 10);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 10);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 4);
    gtk_widget_set_valign(flow, GTK_ALIGN_START);
    gtk_container_add(GTK_CONTAINER(flow), matrix_frame("Allocation Matrix", s, s->allocation, NULL));
    gtk_container_add(GTK_CONTAINER(flow), matrix_frame("Maximum Matrix", s, s->maximum, NULL));
    gtk_container_add(GTK_CONTAINER(flow), matrix_frame("Need Matrix (Max - Alloc)", s, need, NULL));
    gtk_container_add(GTK_CONTAINER(flow), matrix_frame("Resource Requests (pending)", s, s->requests, "#d35400"));
    gtk_box_pack_start(GTK_BOX(box), flow, FALSE, FALSE, 0);
}

static void add_results(GtkWidget *box, const SystemState *s,
                        const SafetyResult *safety, const DetectionResult *det)
{
    char seq[256] = "", dead[256] = "", nm[8];
    size_t len;

    if (safety->is_safe && safety->seq_len > 0) {
        for (int k = 0; k < safety->seq_len && k < MAX_PROC; k++) {
            state_proc_name(safety->safe_sequence[k], nm);
            len = strlen(seq);
            snprintf(seq + len, sizeof seq - len, "%s%s", k ? "  \xe2\x86\x92  " : "", nm);
        }
    } else {
        snprintf(seq, sizeof seq, "No safe sequence exists");
    }

    if (det->deadlocked && det->n_deadlocked > 0) {
        for (int k = 0; k < det->n_deadlocked && k < MAX_PROC; k++) {
            state_proc_name(det->deadlocked_procs[k], nm);
            len = strlen(dead);
            snprintf(dead + len, sizeof dead - len, "%s%s", k ? ", " : "", nm);
        }
    } else {
        snprintf(dead, sizeof dead, "None");
    }
    (void)s;

    GtkWidget *grid = new_grid();
    GtkWidget *l1 = gtk_label_new(NULL), *l2 = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(l1), "<b>Safe sequence:</b>");
    gtk_label_set_markup(GTK_LABEL(l2), "<b>Deadlocked processes:</b>");
    gtk_label_set_xalign(GTK_LABEL(l1), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(l2), 0.0f);
    GtkWidget *v1 = gtk_label_new(seq), *v2 = gtk_label_new(dead);
    gtk_label_set_xalign(GTK_LABEL(v1), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(v2), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(v1), TRUE);
    gtk_label_set_selectable(GTK_LABEL(v1), TRUE);
    gtk_grid_attach(GTK_GRID(grid), l1, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), v1, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), l2, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), v2, 1, 1, 1, 1);
    gtk_box_pack_start(GTK_BOX(box), framed("Analysis Results", grid), FALSE, FALSE, 0);
}

static void add_utilization(GtkWidget *box, const SystemState *s)
{
    Utilization u = analysis_utilization(s);
    GtkWidget *grid = new_grid();
    char buf[64], nm[8];

    for (int j = 0; j < u.n_res; j++) {
        state_res_name(j, nm);
        snprintf(buf, sizeof buf, "%s  (%d / %d in use)", nm, u.used[j], u.total[j]);
        GtkWidget *l = gtk_label_new(buf);
        gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
        gtk_grid_attach(GTK_GRID(grid), l, 0, j, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), percent_bar(u.percent[j]), 1, j, 1, 1);
    }
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_grid_attach(GTK_GRID(grid), sep, 0, u.n_res, 2, 1);
    GtkWidget *ol = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(ol), "<b>Overall</b>");
    gtk_label_set_xalign(GTK_LABEL(ol), 0.0f);
    gtk_grid_attach(GTK_GRID(grid), ol, 0, u.n_res + 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), percent_bar(u.overall), 1, u.n_res + 1, 1, 1);
    gtk_box_pack_start(GTK_BOX(box), framed("Resource Utilization", grid), FALSE, FALSE, 0);
}

/* One row of the four-conditions table. */
static void condition_row(GtkWidget *grid, int row, const char *name,
                          const char *explain, bool holds)
{
    GtkWidget *ind = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(ind), holds
        ? "<span foreground='#c0392b' weight='bold'>\xe2\x97\x8f HOLDS</span>"
        : "<span foreground='#1e8e3e' weight='bold'>\xe2\x97\x8b not holding</span>");
    gtk_label_set_xalign(GTK_LABEL(ind), 0.0f);

    GtkWidget *nl = gtk_label_new(NULL);
    char buf[96];
    snprintf(buf, sizeof buf, "<b>%s</b>", name);
    gtk_label_set_markup(GTK_LABEL(nl), buf);
    gtk_label_set_xalign(GTK_LABEL(nl), 0.0f);

    GtkWidget *el = gtk_label_new(explain);
    gtk_label_set_xalign(GTK_LABEL(el), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(el), TRUE);
    gtk_widget_set_hexpand(el, TRUE);

    gtk_grid_attach(GTK_GRID(grid), ind, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), nl,  1, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), el,  2, row, 1, 1);
}

static void add_conditions(GtkWidget *box, const SystemState *s,
                           const DetectionResult *det)
{
    int np = clamp_int(s->n_proc, 0, MAX_PROC);
    int nr = clamp_int(s->n_res, 0, MAX_RES);

    /* Hold and Wait: a (running) process holds something AND is waiting. */
    bool hold_wait = false;
    for (int i = 0; i < np && !hold_wait; i++) {
        if (s->terminated[i]) continue;
        int held = 0, wanted = 0;
        for (int j = 0; j < nr; j++) {
            held   += s->allocation[i][j] > 0 ? s->allocation[i][j] : 0;
            wanted += s->requests[i][j]   > 0 ? s->requests[i][j]   : 0;
        }
        hold_wait = (held > 0 && wanted > 0);
    }

    GtkWidget *grid = new_grid();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    condition_row(grid, 0, "Mutual Exclusion",
        "A resource instance can be used by only one process at a time (always true in this model).", true);
    condition_row(grid, 1, "Hold and Wait",
        "A process holds some resources while waiting for more.", hold_wait);
    condition_row(grid, 2, "No Preemption",
        "Resources cannot be taken away; only the holder releases them (always true in this model).", true);
    condition_row(grid, 3, "Circular Wait",
        "A closed chain of processes, each waiting for a resource held by the next.", det->deadlocked);
    gtk_box_pack_start(GTK_BOX(box),
        framed("Four Necessary Conditions for Deadlock", grid), FALSE, FALSE, 0);

    GtkWidget *hint = gtk_label_new("A deadlock can only exist when all four conditions hold at the same time.");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0f);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);
}

/* ------------------------------------------------------------------------ *
 *  Rebuild
 * ------------------------------------------------------------------------ */
static void destroy_child(GtkWidget *child, gpointer unused)
{
    (void)unused;
    gtk_widget_destroy(child);
}

static void status_refresh(void *user_data)
{
    StatusTab *t = user_data;
    if (t == NULL || t->scroll == NULL)          /* widget already destroyed */
        return;

    gtk_container_foreach(GTK_CONTAINER(t->content), destroy_child, NULL);

    const SystemState *s = t->state;
    if (s->n_proc <= 0 || s->n_res <= 0) {
        GtkWidget *msg = gtk_label_new(
            "No system defined yet.\nUse \"Create Processes\" and \"Create Resources\" first.");
        gtk_box_pack_start(GTK_BOX(t->content), msg, FALSE, FALSE, 20);
        gtk_widget_show_all(t->content);
        return;
    }

    /* Run the analysis once and show everything from these results. */
    StateClass cls = analysis_classify(s);
    SafetyResult safety = bankers_is_safe(s);
    DetectionResult det = detect_deadlock(s);

    add_banner(t->content, cls);
    add_matrices(t->content, s);
    add_results(t->content, s, &safety, &det);
    add_utilization(t->content, s);
    add_conditions(t->content, s, &det);
    gtk_widget_show_all(t->content);
}

/* ------------------------------------------------------------------------ *
 *  Public constructor
 * ------------------------------------------------------------------------ */
GtkWidget *status_tab_new(SystemState *state)
{
    StatusTab *t = g_new0(StatusTab, 1);
    t->state = state;

    t->scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(t->scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    g_object_add_weak_pointer(G_OBJECT(t->scroll), (gpointer *)&t->scroll);

    t->content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(t->content, 12);
    gtk_widget_set_margin_end(t->content, 12);
    gtk_widget_set_margin_top(t->content, 12);
    gtk_widget_set_margin_bottom(t->content, 12);
    gtk_container_add(GTK_CONTAINER(t->scroll), t->content);

    state_subscribe(status_refresh, t);
    status_refresh(t);
    return t->scroll;
}
