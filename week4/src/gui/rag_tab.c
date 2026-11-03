/* src/gui/rag_tab.c - Menu 6: Resource Allocation Graph (GTK 3 + Cairo)
 *
 * Layout: processes (circles) in a top row, resources (rectangles with one
 * dot per instance) in a bottom row.  Positions are recomputed from the
 * current widget size on every draw, so the graph resizes with the window.
 *
 *   solid blue arrow   : assignment edge  R -> P  (label = instances held)
 *   dashed orange arrow: request edge     P -> R  (label = instances wanted)
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "rag_tab.h"
#include "../core/results.h"
#include "../core/detection.h"   /* detect_deadlock (Student 2) */

#define PROC_RADIUS 22.0
#define N_NODES     (MAX_PROC + MAX_RES)   /* graph nodes: processes + resources */
#define DOTS_PER_ROW 8

/* Everything the tab needs.  It is allocated once and intentionally lives as
 * long as the application: the state module keeps the pointer for callbacks
 * and has no "unsubscribe" function.  'root' is a weak pointer that becomes
 * NULL if the widget is ever destroyed, so the callback stays safe. */
typedef struct {
    SystemState    *state;
    GtkWidget      *root;
    GtkWidget      *area;     /* GtkDrawingArea */
    GtkWidget      *note;     /* cycle note under the graph */
    DetectionResult det;      /* cached result of detect_deadlock() */
} RagTab;

/* ------------------------------------------------------------------------ *
 *  Small helpers
 * ------------------------------------------------------------------------ */
static int clamp_int(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Append text to buf (capacity cap) without ever overflowing it. */
static void append_text(char *buf, size_t cap, const char *txt)
{
    size_t len = strlen(buf);
    if (len < cap)
        snprintf(buf + len, cap - len, "%s", txt);
}

/* ------------------------------------------------------------------------ *
 *  Cycle detection (DFS) on the process/resource graph
 *
 *  Node numbering: 0 .. np-1 are processes, np .. np+nr-1 are resources.
 *  Edges: R_j -> P_i if allocation[i][j] > 0, P_i -> R_j if requests[i][j] > 0.
 *  Terminated processes are ignored (they no longer take part).
 * ------------------------------------------------------------------------ */
typedef struct {
    int color[N_NODES];       /* 0 = unvisited, 1 = on current path, 2 = done */
    int path[N_NODES + 1];    /* current DFS path                             */
    int depth;
    int cycle[N_NODES + 1];   /* nodes of the cycle that was found            */
    int cycle_len;
} CycleCtx;

static bool has_edge(const SystemState *s, int np, int u, int v)
{
    if (u >= np && v < np)    /* resource -> process (assignment) */
        return !s->terminated[v] && s->allocation[v][u - np] > 0;
    if (u < np && v >= np)    /* process -> resource (request)    */
        return !s->terminated[u] && s->requests[u][v - np] > 0;
    return false;             /* the graph is bipartite: no other edges */
}

static bool dfs_cycle(const SystemState *s, int np, int n, int u, CycleCtx *c)
{
    c->color[u] = 1;
    c->path[c->depth++] = u;

    for (int v = 0; v < n; v++) {
        if (!has_edge(s, np, u, v))
            continue;
        if (c->color[v] == 1) {            /* back edge -> cycle found */
            int k = 0;
            while (c->path[k] != v)        /* v is on the path, so this ends */
                k++;
            c->cycle_len = 0;
            for (int p = k; p < c->depth; p++)
                c->cycle[c->cycle_len++] = c->path[p];
            return true;
        }
        if (c->color[v] == 0 && dfs_cycle(s, np, n, v, c))
            return true;
    }
    c->color[u] = 2;
    c->depth--;
    return false;
}

/* Returns true and fills 'c' if the graph contains a directed cycle. */
static bool graph_find_cycle(const SystemState *s, CycleCtx *c)
{
    memset(c, 0, sizeof *c);
    int np = clamp_int(s->n_proc, 0, MAX_PROC);
    int nr = clamp_int(s->n_res, 0, MAX_RES);
    int n = np + nr;
    for (int u = 0; u < n; u++)
        if (c->color[u] == 0 && dfs_cycle(s, np, n, u, c))
            return true;
    return false;
}

/* ------------------------------------------------------------------------ *
 *  Geometry of a node (circle for processes, rectangle for resources)
 * ------------------------------------------------------------------------ */
typedef struct {
    double cx, cy;     /* centre                                  */
    double hw, hh;     /* half width / half height (circle: both = radius) */
    bool   circle;
} Node;

/* Point on the border of 'n' in the direction of (tx,ty), plus a 2 px gap. */
static void border_point(const Node *n, double tx, double ty,
                         double *ox, double *oy)
{
    double dx = tx - n->cx, dy = ty - n->cy;
    double len = hypot(dx, dy);
    if (len < 1e-6) { *ox = n->cx; *oy = n->cy; return; }

    double t;                                  /* distance from centre */
    if (n->circle) {
        t = n->hw + 2.0;
    } else {
        double sx = fabs(dx) > 1e-9 ? n->hw / fabs(dx) : 1e9;
        double sy = fabs(dy) > 1e-9 ? n->hh / fabs(dy) : 1e9;
        t = fmin(sx, sy) * len + 2.0;
    }
    *ox = n->cx + dx / len * t;
    *oy = n->cy + dy / len * t;
}

/* ------------------------------------------------------------------------ *
 *  Drawing primitives
 * ------------------------------------------------------------------------ */
static void draw_centered_text(cairo_t *cr, const char *txt, double cx, double cy)
{
    cairo_text_extents_t e;
    cairo_text_extents(cr, txt, &e);
    cairo_move_to(cr, cx - (e.width / 2.0 + e.x_bearing),
                      cy - (e.height / 2.0 + e.y_bearing));
    cairo_show_text(cr, txt);
}

/* Draw a slightly curved arrow from node 'from' to node 'to'.  Because the
 * curve bends to the LEFT of its direction of travel, an assignment edge
 * (R->P) and a request edge (P->R) between the same two nodes bend to
 * opposite sides and never overlap. */
static void draw_edge(cairo_t *cr, const Node *from, const Node *to,
                      double bend, bool dashed,
                      double r, double g, double b, const char *label)
{
    double dx = to->cx - from->cx, dy = to->cy - from->cy;
    double len = hypot(dx, dy);
    if (len < 1.0)
        return;

    /* Control point of a quadratic curve: midpoint pushed sideways. */
    double qx = (from->cx + to->cx) / 2.0 + (-dy / len) * bend;
    double qy = (from->cy + to->cy) / 2.0 + ( dx / len) * bend;

    double x0, y0, x3, y3;                       /* start / end on borders */
    border_point(from, qx, qy, &x0, &y0);
    border_point(to,   qx, qy, &x3, &y3);

    /* Convert the quadratic curve to the cubic form cairo needs. */
    double c1x = x0 + 2.0 / 3.0 * (qx - x0), c1y = y0 + 2.0 / 3.0 * (qy - y0);
    double c2x = x3 + 2.0 / 3.0 * (qx - x3), c2y = y3 + 2.0 / 3.0 * (qy - y3);

    cairo_set_source_rgb(cr, r, g, b);
    cairo_set_line_width(cr, 2.0);
    if (dashed) {
        const double dash[] = {7.0, 5.0};
        cairo_set_dash(cr, dash, 2, 0.0);
    } else {
        cairo_set_dash(cr, NULL, 0, 0.0);
    }
    cairo_move_to(cr, x0, y0);
    cairo_curve_to(cr, c1x, c1y, c2x, c2y, x3, y3);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0.0);            /* arrowhead is always solid */

    /* Arrowhead: direction of the curve at its end = (end - 2nd control pt). */
    double ang = atan2(y3 - c2y, x3 - c2x);
    const double head = 11.0, spread = 0.45;     /* length, half-angle (rad) */
    cairo_move_to(cr, x3, y3);
    cairo_line_to(cr, x3 - head * cos(ang - spread), y3 - head * sin(ang - spread));
    cairo_line_to(cr, x3 - head * cos(ang + spread), y3 - head * sin(ang + spread));
    cairo_close_path(cr);
    cairo_fill(cr);

    /* Label at the middle of the curve (cubic Bezier at t = 0.5). */
    double mx = 0.125 * x0 + 0.375 * c1x + 0.375 * c2x + 0.125 * x3;
    double my = 0.125 * y0 + 0.375 * c1y + 0.375 * c2y + 0.125 * y3;
    cairo_set_font_size(cr, 12.0);
    cairo_text_extents_t e;
    cairo_text_extents(cr, label, &e);
    cairo_set_source_rgb(cr, 1, 1, 1);           /* white box behind the text */
    cairo_rectangle(cr, mx - e.width / 2.0 - 3, my - e.height / 2.0 - 3,
                        e.width + 6, e.height + 6);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, r, g, b);
    draw_centered_text(cr, label, mx, my);
}

/* ------------------------------------------------------------------------ *
 *  The "draw" signal handler
 * ------------------------------------------------------------------------ */
static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    RagTab *t = data;
    const SystemState *s = t->state;
    double W = gtk_widget_get_allocated_width(widget);
    double H = gtk_widget_get_allocated_height(widget);

    cairo_set_source_rgb(cr, 1, 1, 1);           /* white background */
    cairo_paint(cr);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);

    int np = clamp_int(s->n_proc, 0, MAX_PROC);
    int nr = clamp_int(s->n_res, 0, MAX_RES);
    if (np == 0 || nr == 0) {
        cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
        cairo_set_font_size(cr, 15.0);
        draw_centered_text(cr, "Nothing to draw yet - create processes and "
                               "resources first.", W / 2.0, H / 2.0);
        return FALSE;
    }

    /* ---- 1. Compute node positions from the current size ---- */
    Node procs[MAX_PROC], ress[MAX_RES];
    double py = H * 0.22, ry = H * 0.72;
    double slot_p = W / (np + 1), slot_r = W / (nr + 1);
    double max_hh = 0.0;

    for (int i = 0; i < np; i++) {
        procs[i] = (Node){ slot_p * (i + 1), py, PROC_RADIUS, PROC_RADIUS, true };
    }
    for (int j = 0; j < nr; j++) {
        int tot = clamp_int(s->total[j], 0, 64);
        int cols = clamp_int(tot, 1, DOTS_PER_ROW);
        int rows = (tot + DOTS_PER_ROW - 1) / DOTS_PER_ROW;
        if (rows < 1) rows = 1;
        double rw = cols * 14.0 + 16.0;
        if (rw < 64.0) rw = 64.0;
        if (rw > slot_r - 10.0) rw = slot_r - 10.0;
        if (rw < 44.0) rw = 44.0;
        double rh = 34.0 + rows * 14.0;
        ress[j] = (Node){ slot_r * (j + 1), ry, rw / 2.0, rh / 2.0, false };
        if (ress[j].hh > max_hh) max_hh = ress[j].hh;
    }

    /* Row captions */
    cairo_set_font_size(cr, 11.0);
    cairo_set_source_rgb(cr, 0.45, 0.45, 0.45);
    cairo_move_to(cr, 10, 16);
    cairo_show_text(cr, "PROCESSES");
    cairo_move_to(cr, 10, ry - max_hh - 10);
    cairo_show_text(cr, "RESOURCES  (filled dot = allocated instance, empty = free)");

    /* ---- 2. Edges (drawn first so the nodes sit on top of them) ---- */
    for (int i = 0; i < np; i++) {
        if (s->terminated[i])
            continue;
        for (int j = 0; j < nr; j++) {
            char label[16];
            if (s->allocation[i][j] > 0) {                 /* R_j -> P_i */
                snprintf(label, sizeof label, "%d", s->allocation[i][j]);
                draw_edge(cr, &ress[j], &procs[i], 26.0, false,
                          0.12, 0.40, 0.80, label);
            }
            if (s->requests[i][j] > 0) {                   /* P_i -> R_j */
                snprintf(label, sizeof label, "%d", s->requests[i][j]);
                draw_edge(cr, &procs[i], &ress[j], 26.0, true,
                          0.90, 0.50, 0.10, label);
            }
        }
    }

    /* ---- 3. Which processes are deadlocked? ---- */
    bool dead[MAX_PROC] = { false };
    for (int k = 0; k < t->det.n_deadlocked && k < MAX_PROC; k++) {
        int p = t->det.deadlocked_procs[k];
        if (p >= 0 && p < MAX_PROC)
            dead[p] = true;
    }

    /* ---- 4. Process circles ---- */
    for (int i = 0; i < np; i++) {
        char name[8];
        state_proc_name(i, name);

        cairo_arc(cr, procs[i].cx, procs[i].cy, PROC_RADIUS, 0, 2 * G_PI);
        if (s->terminated[i])      cairo_set_source_rgb(cr, 0.86, 0.86, 0.86);
        else if (dead[i])          cairo_set_source_rgb(cr, 0.85, 0.20, 0.20);
        else                       cairo_set_source_rgb(cr, 0.85, 0.92, 1.00);
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, 2.5);
        if (s->terminated[i])      cairo_set_source_rgb(cr, 0.60, 0.60, 0.60);
        else if (dead[i])          cairo_set_source_rgb(cr, 0.55, 0.05, 0.05);
        else                       cairo_set_source_rgb(cr, 0.15, 0.30, 0.55);
        cairo_stroke(cr);

        cairo_set_font_size(cr, 14.0);
        if (s->terminated[i])      cairo_set_source_rgb(cr, 0.55, 0.55, 0.55);
        else if (dead[i])          cairo_set_source_rgb(cr, 1, 1, 1);
        else                       cairo_set_source_rgb(cr, 0.10, 0.15, 0.30);
        draw_centered_text(cr, name, procs[i].cx, procs[i].cy);

        if (s->terminated[i]) {
            cairo_set_font_size(cr, 10.0);
            cairo_set_source_rgb(cr, 0.55, 0.55, 0.55);
            draw_centered_text(cr, "terminated", procs[i].cx,
                               procs[i].cy + PROC_RADIUS + 12);
        }
    }

    /* ---- 5. Resource rectangles with one dot per instance ---- */
    for (int j = 0; j < nr; j++) {
        char name[8];
        state_res_name(j, name);
        const Node *n = &ress[j];
        double rw = n->hw * 2.0, rh = n->hh * 2.0;

        cairo_rectangle(cr, n->cx - n->hw, n->cy - n->hh, rw, rh);
        cairo_set_source_rgb(cr, 0.95, 0.97, 0.92);
        cairo_fill_preserve(cr);
        cairo_set_line_width(cr, 2.5);
        cairo_set_source_rgb(cr, 0.25, 0.40, 0.20);
        cairo_stroke(cr);

        cairo_set_font_size(cr, 13.0);
        draw_centered_text(cr, name, n->cx, n->cy - n->hh + 14.0);

        int tot = clamp_int(s->total[j], 0, 64);
        int used = 0;                                   /* allocated instances */
        for (int i = 0; i < np; i++)
            used += s->allocation[i][j] > 0 ? s->allocation[i][j] : 0;
        if (used > tot) used = tot;

        int cols = clamp_int(tot, 1, DOTS_PER_ROW);
        double step = fmin(14.0, (rw - 12.0) / cols);
        double rad = fmax(1.5, fmin(4.5, step / 2.0 - 1.0));
        for (int k = 0; k < tot; k++) {
            int row = k / DOTS_PER_ROW, col = k % DOTS_PER_ROW;
            int in_row = tot - row * DOTS_PER_ROW;     /* dots left from this row on */
            if (in_row > DOTS_PER_ROW) in_row = DOTS_PER_ROW;
            double x = n->cx + (col - (in_row - 1) / 2.0) * step;
            double y = n->cy - n->hh + 30.0 + row * 14.0;
            cairo_arc(cr, x, y, rad, 0, 2 * G_PI);
            cairo_set_source_rgb(cr, 0.12, 0.40, 0.80);
            if (k < used) {
                cairo_fill(cr);                         /* allocated */
            } else {
                cairo_set_line_width(cr, 1.3);          /* free */
                cairo_stroke(cr);
            }
        }
    }
    return FALSE;
}

/* ------------------------------------------------------------------------ *
 *  Refresh: recompute deadlock info + cycle note, then redraw
 * ------------------------------------------------------------------------ */
static void rag_refresh(void *user_data)
{
    RagTab *t = user_data;
    if (t == NULL || t->root == NULL)       /* widget already destroyed */
        return;

    const SystemState *s = t->state;
    memset(&t->det, 0, sizeof t->det);
    if (s->n_proc > 0 && s->n_res > 0)
        t->det = detect_deadlock(s);

    /* Build the note about cycles. */
    char text[512];
    CycleCtx cyc;
    if (s->n_proc <= 0 || s->n_res <= 0) {
        snprintf(text, sizeof text, "No graph yet.");
    } else if (!graph_find_cycle(s, &cyc)) {
        snprintf(text, sizeof text,
                 "<span foreground='#1e8e3e' weight='bold'>No cycle in the graph</span>"
                 " - without a cycle no deadlock is possible.");
    } else {
        /* Cycle as text: P0 -> R1 -> P1 -> R0 -> P0 */
        char path[200] = "";
        int np = clamp_int(s->n_proc, 0, MAX_PROC);
        for (int k = 0; k <= cyc.cycle_len; k++) {
            int node = cyc.cycle[k % cyc.cycle_len];   /* last step closes the loop */
            char nm[8];
            if (node < np) state_proc_name(node, nm);
            else           state_res_name(node - np, nm);
            if (k > 0) append_text(path, sizeof path, " \xe2\x86\x92 ");
            append_text(path, sizeof path, nm);
        }
        if (t->det.deadlocked) {
            snprintf(text, sizeof text,
                     "<span foreground='#c0392b' weight='bold'>Cycle found:</span> %s."
                     " The detection algorithm confirms a <b>DEADLOCK</b>.", path);
        } else {
            snprintf(text, sizeof text,
                     "<span foreground='#d35400' weight='bold'>Cycle found:</span> %s."
                     " but the detection algorithm finds <b>no deadlock</b>: a cycle is"
                     " only sufficient when every resource has a single instance.", path);
        }
    }
    gtk_label_set_markup(GTK_LABEL(t->note), text);
    gtk_widget_queue_draw(t->area);
}

static void on_refresh_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    rag_refresh(data);
}

/* ------------------------------------------------------------------------ *
 *  Public constructor
 * ------------------------------------------------------------------------ */
GtkWidget *rag_tab_new(SystemState *state)
{
    RagTab *t = g_new0(RagTab, 1);
    t->state = state;

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(root, 12);
    gtk_widget_set_margin_end(root, 12);
    gtk_widget_set_margin_top(root, 12);
    gtk_widget_set_margin_bottom(root, 12);
    t->root = root;
    g_object_add_weak_pointer(G_OBJECT(root), (gpointer *)&t->root);

    /* Title */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='x-large' weight='bold'>Resource Allocation Graph</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(root), title, FALSE, FALSE, 0);

    /* Legend + Refresh button */
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *legend = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(legend),
        "<span foreground='#1f66cc' weight='bold'>\xe2\x94\x81\xe2\x96\xb6</span> assignment (R\xe2\x86\x92P)   "
        "<span foreground='#e6801a' weight='bold'>\xe2\x95\x8c\xe2\x95\x8c\xe2\x96\xb6</span> request (P\xe2\x86\x92R)   "
        "<span foreground='#d93333'>\xe2\x97\x8f</span> deadlocked process   "
        "<span foreground='#999999'>\xe2\x97\x8f</span> terminated process");
    gtk_widget_set_halign(legend, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(legend), TRUE);
    gtk_box_pack_start(GTK_BOX(bar), legend, TRUE, TRUE, 0);
    GtkWidget *btn = gtk_button_new_with_label("Refresh");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_refresh_clicked), t);
    gtk_box_pack_end(GTK_BOX(bar), btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), bar, FALSE, FALSE, 0);

    /* Drawing area inside a labelled frame */
    GtkWidget *frame = gtk_frame_new("Graph");
    t->area = gtk_drawing_area_new();
    gtk_widget_set_size_request(t->area, 640, 340);
    g_signal_connect(t->area, "draw", G_CALLBACK(on_draw), t);
    gtk_container_add(GTK_CONTAINER(frame), t->area);
    gtk_box_pack_start(GTK_BOX(root), frame, TRUE, TRUE, 0);

    /* Cycle note */
    t->note = gtk_label_new(NULL);
    gtk_widget_set_halign(t->note, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(t->note), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(t->note), TRUE);
    gtk_box_pack_start(GTK_BOX(root), t->note, FALSE, FALSE, 0);

    state_subscribe(rag_refresh, t);   /* redraw whenever the state changes */
    rag_refresh(t);                    /* initial content */
    return root;
}
