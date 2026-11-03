#include "setup_tabs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

typedef int Row[MAX_RES];

/* ================= helpers ================= */

static void destroy_cb(GtkWidget *w, gpointer unused)
{
    (void)unused;
    gtk_widget_destroy(w);
}

static void clear_container(GtkWidget *c)
{
    gtk_container_foreach(GTK_CONTAINER(c), destroy_cb, NULL);
}

/* Parses a non-negative whole number (0..max). Returns false for empty/non-numeric/out of range. */
static bool parse_int(const char *text, int max, int *out)
{
    while (*text == ' ' || *text == '\t')
        text++;
    if (*text == '\0')
        return false;
    char *end = NULL;
    errno = 0;
    long v = strtol(text, &end, 10);
    while (end && (*end == ' ' || *end == '\t'))
        end++;
    if (errno != 0 || !end || *end != '\0' || v < 0 || v > max)
        return false;
    *out = (int)v;
    return true;
}

/* Changes n_proc / n_res and clears every value that falls outside the new size. */
static void resize_state(SystemState *s, int np, int nr)
{
    for (int i = 0; i < MAX_PROC; i++)
        for (int j = 0; j < MAX_RES; j++)
            if (i >= np || j >= nr) {
                s->allocation[i][j] = 0;
                s->maximum[i][j] = 0;
                s->requests[i][j] = 0;
            }
    for (int j = nr; j < MAX_RES; j++)
        s->total[j] = 0;
    for (int i = np; i < MAX_PROC; i++)
        s->terminated[i] = false;
    s->n_proc = np;
    s->n_res = nr;
}

static GtkWidget *cell_label(const char *text, const char *css_class)
{
    GtkWidget *l = gtk_label_new(text);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), css_class);
    gtk_widget_set_hexpand(l, FALSE);
    return l;
}

static GtkWidget *make_grid(void)
{
    GtkWidget *g = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g), 2);
    gtk_grid_set_column_spacing(GTK_GRID(g), 2);
    gtk_widget_set_halign(g, GTK_ALIGN_START);
    return g;
}

static void on_sample_clicked(GtkButton *b, gpointer data)
{
    (void)b;
    state_load_sample((SystemState *)data);
    app_set_status_message("Sample data loaded (5 processes, 3 resources).");
    state_notify();
}

/* Standard tab frame: description text + "Load Sample Data" button on top. */
static GtkWidget *make_tab_box(const char *description, SystemState *s)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 16);

    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *desc = gtk_label_new(description);
    gtk_label_set_line_wrap(GTK_LABEL(desc), TRUE);
    gtk_label_set_xalign(GTK_LABEL(desc), 0.0f);
    gtk_style_context_add_class(gtk_widget_get_style_context(desc), "hint");
    gtk_box_pack_start(GTK_BOX(top), desc, TRUE, TRUE, 0);

    GtkWidget *sample = gtk_button_new_with_label("Load Sample Data");
    g_signal_connect(sample, "clicked", G_CALLBACK(on_sample_clicked), s);
    gtk_box_pack_end(GTK_BOX(top), sample, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), top, FALSE, FALSE, 0);
    return box;
}

/* ================= menu 1: Create Processes ================= */

typedef struct {
    SystemState *s;
    GtkWidget   *spin;
    GtkWidget   *names;
} ProcTab;

static void proc_refresh(void *data)
{
    ProcTab *t = data;
    char buf[256] = "";
    if (t->s->n_proc < 1) {
        snprintf(buf, sizeof buf, "No processes created yet.");
    } else {
        size_t len = (size_t)snprintf(buf, sizeof buf, "Current processes (%d):  ", t->s->n_proc);
        for (int i = 0; i < t->s->n_proc && len < sizeof buf - 8; i++) {
            char nm[8];
            state_proc_name(i, nm);
            len += (size_t)snprintf(buf + len, sizeof buf - len, "%s%s", nm, i + 1 < t->s->n_proc ? ", " : "");
        }
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(t->spin), t->s->n_proc);
    }
    gtk_label_set_text(GTK_LABEL(t->names), buf);
}

static void proc_create_clicked(GtkButton *b, gpointer data)
{
    (void)b;
    ProcTab *t = data;
    int n = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(t->spin));
    SystemState trial = *t->s;
    resize_state(&trial, n, trial.n_res);
    char err[200];
    if (trial.n_res > 0 && state_validate(&trial, err, sizeof err) != 0) {
        app_show_error(t->spin, err);
        return;
    }
    *t->s = trial;
    char msg[80];
    snprintf(msg, sizeof msg, "Created %d process(es).", n);
    app_set_status_message(msg);
    state_notify();
}

GtkWidget *create_processes_tab_new(SystemState *state)
{
    ProcTab *t = g_malloc0(sizeof *t);   /* lives as long as the application */
    t->s = state;
    GtkWidget *box = make_tab_box("Choose how many processes the system has (1 to 10). "
                                  "Processes are named P0, P1, ...", state);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "card");
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new("Number of processes:"), FALSE, FALSE, 0);
    t->spin = gtk_spin_button_new_with_range(1, MAX_PROC, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(t->spin), 3);
    gtk_box_pack_start(GTK_BOX(row), t->spin, FALSE, FALSE, 0);
    GtkWidget *btn = gtk_button_new_with_label("Create");
    g_signal_connect(btn, "clicked", G_CALLBACK(proc_create_clicked), t);
    gtk_box_pack_start(GTK_BOX(row), btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), row, FALSE, FALSE, 0);

    t->names = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(t->names), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(t->names), TRUE);
    gtk_box_pack_start(GTK_BOX(card), t->names, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), card, FALSE, FALSE, 0);

    state_subscribe(proc_refresh, t);
    proc_refresh(t);
    return box;
}

/* ================= menu 2: Create Resources ================= */

typedef struct {
    SystemState *s;
    GtkWidget   *spin;
    GtkWidget   *grid;
    GtkWidget   *entries[MAX_RES];
    int          shown;
} ResTab;

static void res_rebuild(ResTab *t)
{
    clear_container(t->grid);
    t->shown = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(t->spin));
    for (int j = 0; j < MAX_RES; j++)
        t->entries[j] = NULL;
    for (int j = 0; j < t->shown; j++) {
        char nm[8], lbl[48], val[16];
        state_res_name(j, nm);
        snprintf(lbl, sizeof lbl, "%s  total instances:", nm);
        gtk_grid_attach(GTK_GRID(t->grid), cell_label(lbl, "cell-head"), 0, j, 1, 1);
        t->entries[j] = gtk_entry_new();
        gtk_entry_set_width_chars(GTK_ENTRY(t->entries[j]), 6);
        snprintf(val, sizeof val, "%d", j < t->s->n_res ? t->s->total[j] : 1);
        gtk_entry_set_text(GTK_ENTRY(t->entries[j]), val);
        gtk_grid_attach(GTK_GRID(t->grid), t->entries[j], 1, j, 1, 1);
    }
    gtk_widget_show_all(t->grid);
}

static void res_spin_changed(GtkSpinButton *sp, gpointer data)
{
    (void)sp;
    res_rebuild(data);
}

static void res_refresh(void *data)
{
    ResTab *t = data;
    if (t->s->n_res > 0)
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(t->spin), t->s->n_res);
    res_rebuild(t);
}

static void res_save_clicked(GtkButton *b, gpointer data)
{
    (void)b;
    ResTab *t = data;
    SystemState trial = *t->s;
    resize_state(&trial, trial.n_proc, t->shown);
    for (int j = 0; j < t->shown; j++) {
        int v;
        if (!parse_int(gtk_entry_get_text(GTK_ENTRY(t->entries[j])), 1000, &v) || v < 1) {
            char msg[100];
            snprintf(msg, sizeof msg, "Total instances of R%d must be a whole number from 1 to 1000.", j);
            app_show_error(t->grid, msg);
            return;
        }
        trial.total[j] = v;
    }
    char err[200];
    if (trial.n_proc > 0 && state_validate(&trial, err, sizeof err) != 0) {
        app_show_error(t->grid, err);
        return;
    }
    *t->s = trial;
    char msg[80];
    snprintf(msg, sizeof msg, "Saved %d resource type(s).", t->shown);
    app_set_status_message(msg);
    state_notify();
}

GtkWidget *create_resources_tab_new(SystemState *state)
{
    ResTab *t = g_malloc0(sizeof *t);
    t->s = state;
    GtkWidget *box = make_tab_box("Choose how many resource types exist (1 to 10) and how many instances "
                                  "of each. Resources are named R0, R1, ...", state);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "card");
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new("Number of resource types:"), FALSE, FALSE, 0);
    t->spin = gtk_spin_button_new_with_range(1, MAX_RES, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(t->spin), 3);
    g_signal_connect(t->spin, "value-changed", G_CALLBACK(res_spin_changed), t);
    gtk_box_pack_start(GTK_BOX(row), t->spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(card), row, FALSE, FALSE, 0);

    t->grid = make_grid();
    gtk_box_pack_start(GTK_BOX(card), t->grid, FALSE, FALSE, 0);

    GtkWidget *save = gtk_button_new_with_label("Save");
    gtk_widget_set_halign(save, GTK_ALIGN_START);
    g_signal_connect(save, "clicked", G_CALLBACK(res_save_clicked), t);
    gtk_box_pack_start(GTK_BOX(card), save, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), card, FALSE, FALSE, 0);

    state_subscribe(res_refresh, t);
    res_rebuild(t);
    return box;
}

/* ================= menu 3 and 4: Allocation / Maximum matrix ================= */

typedef enum { KIND_ALLOC, KIND_MAX } MatKind;

typedef struct {
    SystemState *s;
    MatKind      kind;
    GtkWidget   *grid;
    GtkWidget   *save_btn;
    GtkWidget   *entries[MAX_PROC][MAX_RES];
} MatTab;

static Row *matrix_of(SystemState *s, MatKind k)
{
    return k == KIND_ALLOC ? s->allocation : s->maximum;
}

static void mat_rebuild(void *data)
{
    MatTab *t = data;
    clear_container(t->grid);
    memset(t->entries, 0, sizeof t->entries);

    if (t->s->n_proc < 1 || t->s->n_res < 1) {
        gtk_grid_attach(GTK_GRID(t->grid),
                        gtk_label_new("Create processes (menu 1) and resources (menu 2) first, "
                                      "or load the sample data."), 0, 0, 1, 1);
        gtk_widget_set_sensitive(t->save_btn, FALSE);
        gtk_widget_show_all(t->grid);
        return;
    }
    gtk_widget_set_sensitive(t->save_btn, TRUE);

    Row *m = matrix_of(t->s, t->kind);
    gtk_grid_attach(GTK_GRID(t->grid), cell_label("", "cell-head"), 0, 0, 1, 1);
    for (int j = 0; j < t->s->n_res; j++) {
        char nm[8];
        state_res_name(j, nm);
        gtk_grid_attach(GTK_GRID(t->grid), cell_label(nm, "cell-head"), j + 1, 0, 1, 1);
    }
    for (int i = 0; i < t->s->n_proc; i++) {
        char nm[8];
        state_proc_name(i, nm);
        gtk_grid_attach(GTK_GRID(t->grid), cell_label(nm, "cell-head"), 0, i + 1, 1, 1);
        for (int j = 0; j < t->s->n_res; j++) {
            char val[16];
            GtkWidget *e = gtk_entry_new();
            gtk_entry_set_width_chars(GTK_ENTRY(e), 5);
            gtk_entry_set_alignment(GTK_ENTRY(e), 0.5f);
            snprintf(val, sizeof val, "%d", m[i][j]);
            gtk_entry_set_text(GTK_ENTRY(e), val);
            t->entries[i][j] = e;
            gtk_grid_attach(GTK_GRID(t->grid), e, j + 1, i + 1, 1, 1);
        }
    }
    gtk_widget_show_all(t->grid);
}

static void mat_save_clicked(GtkButton *b, gpointer data)
{
    (void)b;
    MatTab *t = data;
    SystemState trial = *t->s;
    Row *m = matrix_of(&trial, t->kind);
    const char *what = t->kind == KIND_ALLOC ? "Allocation" : "Maximum";

    for (int i = 0; i < trial.n_proc; i++)
        for (int j = 0; j < trial.n_res; j++) {
            int v;
            if (!t->entries[i][j] ||
                !parse_int(gtk_entry_get_text(GTK_ENTRY(t->entries[i][j])), 100000, &v)) {
                char msg[120];
                snprintf(msg, sizeof msg,
                         "%s of P%d / R%d must be a whole number (0 or more).", what, i, j);
                app_show_error(t->grid, msg);
                return;
            }
            m[i][j] = v;
        }

    char err[200];
    if (state_validate(&trial, err, sizeof err) != 0) {
        app_show_error(t->grid, err);
        return;
    }
    *t->s = trial;
    char msg[80];
    snprintf(msg, sizeof msg, "%s matrix saved.", what);
    app_set_status_message(msg);
    state_notify();
}

static GtkWidget *matrix_tab_new(SystemState *state, MatKind kind)
{
    MatTab *t = g_malloc0(sizeof *t);
    t->s = state;
    t->kind = kind;
    GtkWidget *box = make_tab_box(
        kind == KIND_ALLOC
            ? "Enter how many instances of each resource every process currently HOLDS. "
              "Column totals cannot exceed the total instances of that resource."
            : "Enter the MAXIMUM number of instances of each resource every process may ever need. "
              "A maximum cannot exceed the total instances of that resource.",
        state);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "card");
    t->grid = make_grid();
    gtk_box_pack_start(GTK_BOX(card), t->grid, FALSE, FALSE, 0);
    t->save_btn = gtk_button_new_with_label("Save");
    gtk_widget_set_halign(t->save_btn, GTK_ALIGN_START);
    g_signal_connect(t->save_btn, "clicked", G_CALLBACK(mat_save_clicked), t);
    gtk_box_pack_start(GTK_BOX(card), t->save_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), card, FALSE, FALSE, 0);

    state_subscribe(mat_rebuild, t);
    mat_rebuild(t);
    return box;
}

GtkWidget *allocation_tab_new(SystemState *state) { return matrix_tab_new(state, KIND_ALLOC); }
GtkWidget *maximum_tab_new(SystemState *state)    { return matrix_tab_new(state, KIND_MAX); }

/* ================= menu 5: Need matrix ================= */

typedef struct {
    SystemState *s;
    GtkWidget   *avail_grid;
    GtkWidget   *need_grid;
    GtkWidget   *note;
} NeedTab;

static void need_refresh(void *data)
{
    NeedTab *t = data;
    clear_container(t->avail_grid);
    clear_container(t->need_grid);

    if (t->s->n_proc < 1 || t->s->n_res < 1) {
        gtk_label_set_text(GTK_LABEL(t->note), "Nothing to show yet: create processes and resources first.");
        return;
    }
    gtk_label_set_text(GTK_LABEL(t->note), "");

    int av[MAX_RES], need[MAX_PROC][MAX_RES];
    state_available(t->s, av);
    state_need(t->s, need);

    for (int j = 0; j < t->s->n_res; j++) {
        char nm[8], val[16];
        state_res_name(j, nm);
        snprintf(val, sizeof val, "%d", av[j]);
        gtk_grid_attach(GTK_GRID(t->avail_grid), cell_label(nm, "cell-head"), j, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(t->avail_grid), cell_label(val, av[j] < 0 ? "cell-bad" : "cell"), j, 1, 1, 1);
    }

    gtk_grid_attach(GTK_GRID(t->need_grid), cell_label("", "cell-head"), 0, 0, 1, 1);
    for (int j = 0; j < t->s->n_res; j++) {
        char nm[8];
        state_res_name(j, nm);
        gtk_grid_attach(GTK_GRID(t->need_grid), cell_label(nm, "cell-head"), j + 1, 0, 1, 1);
    }
    for (int i = 0; i < t->s->n_proc; i++) {
        char nm[8];
        state_proc_name(i, nm);
        gtk_grid_attach(GTK_GRID(t->need_grid), cell_label(nm, "cell-head"), 0, i + 1, 1, 1);
        for (int j = 0; j < t->s->n_res; j++) {
            char val[16];
            snprintf(val, sizeof val, "%d", need[i][j]);
            gtk_grid_attach(GTK_GRID(t->need_grid), cell_label(val, need[i][j] < 0 ? "cell-bad" : "cell"),
                            j + 1, i + 1, 1, 1);
        }
    }
    gtk_widget_show_all(t->avail_grid);
    gtk_widget_show_all(t->need_grid);
}

GtkWidget *need_tab_new(SystemState *state)
{
    NeedTab *t = g_malloc0(sizeof *t);
    t->s = state;
    GtkWidget *box = make_tab_box("Need = Maximum - Allocation. Available = Total - sum of Allocation. "
                                  "Values shown in red are invalid (negative).", state);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "card");

    t->note = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(t->note), 0.0f);
    gtk_box_pack_start(GTK_BOX(card), t->note, FALSE, FALSE, 0);

    GtkWidget *h1 = gtk_label_new("Available Vector");
    gtk_label_set_xalign(GTK_LABEL(h1), 0.0f);
    gtk_box_pack_start(GTK_BOX(card), h1, FALSE, FALSE, 0);
    t->avail_grid = make_grid();
    gtk_box_pack_start(GTK_BOX(card), t->avail_grid, FALSE, FALSE, 0);

    GtkWidget *h2 = gtk_label_new("Need Matrix");
    gtk_label_set_xalign(GTK_LABEL(h2), 0.0f);
    gtk_widget_set_margin_top(h2, 10);
    gtk_box_pack_start(GTK_BOX(card), h2, FALSE, FALSE, 0);
    t->need_grid = make_grid();
    gtk_box_pack_start(GTK_BOX(card), t->need_grid, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), card, FALSE, FALSE, 0);

    state_subscribe(need_refresh, t);
    need_refresh(t);
    return box;
}
