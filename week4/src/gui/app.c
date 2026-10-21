#include "app.h"

#include <stdio.h>
#include <string.h>

#include "analysis.h"
#include "banker_tab.h"
#include "detection_tab.h"
#include "rag_tab.h"
#include "setup_tabs.h"
#include "state.h"
#include "status_tab.h"

#define N_MENU 12

static SystemState    g_state;                 /* the one shared system state */
static GtkApplication *g_app;
static GtkWidget      *g_window;
static GtkWidget      *g_stack;
static GtkWidget      *g_status_label;
static GtkWidget      *g_buttons[N_MENU];
static char            g_message[200] = "Welcome! Create processes and resources, or load the sample data.";

static const char *MENU[N_MENU] = {
    "Create Processes", "Create Resources", "Enter Allocation Matrix", "Enter Maximum Matrix",
    "Display Need Matrix", "Display Resource Allocation Graph", "Request Resources",
    "Run Banker's Algorithm", "Detect Deadlock", "Recover from Deadlock",
    "Display System Status", "Exit"
};
static const char *PAGE[N_MENU - 1] = {
    "p1", "p2", "p3", "p4", "p5", "p6", "p7", "p8", "p9", "p10", "p11"
};

static const char *CSS =
    ".sidebar { background: #263238; }"
    ".sidebar button { color: #cfd8dc; background: transparent; border: none; border-radius: 0;"
    "                  padding: 10px 14px; box-shadow: none; text-shadow: none; }"
    ".sidebar button:hover { background: #2f3f46; }"
    ".sidebar button.active { background: #37474f; color: #ffffff; font-weight: bold;"
    "                         border-left: 4px solid #4fc3f7; }"
    ".sidebar-title { color: #ffffff; font-weight: bold; font-size: 15px; padding: 14px; }"
    ".title { font-size: 20px; font-weight: bold; margin: 14px 16px 2px 16px; }"
    ".hint { color: #607d8b; }"
    ".card { background: #fafafa; color: #212121; border: 1px solid #cfd8dc;"
    "        border-radius: 8px; padding: 12px; }"
    ".cell { background: #ffffff; color: #212121; border: 1px solid #b0bec5; padding: 4px 10px; }"
    ".cell-head { background: #eceff1; color: #37474f; font-weight: bold;"
    "             border: 1px solid #b0bec5; padding: 4px 10px; }"
    ".cell-bad { background: #ffcdd2; color: #b71c1c; border: 1px solid #b0bec5; padding: 4px 10px; }"
    ".state-safe, .state-unsafe, .state-deadlock { color: #ffffff; font-weight: bold;"
    "        padding: 8px 14px; border-radius: 6px; }"
    ".state-safe { background: #2e7d32; }"
    ".state-unsafe { background: #ef6c00; }"
    ".state-deadlock { background: #c62828; }"
    ".status-bar { background: #eceff1; color: #37474f; padding: 6px 14px; }";

/* ---------- public helpers ---------- */

void app_set_status_message(const char *msg)
{
    snprintf(g_message, sizeof g_message, "%s", msg ? msg : "");
}

void app_show_error(GtkWidget *parent, const char *msg)
{
    GtkWindow *w = GTK_WINDOW(g_window);
    if (parent) {
        GtkWidget *top = gtk_widget_get_toplevel(parent);
        if (GTK_IS_WINDOW(top))
            w = GTK_WINDOW(top);
    }
    GtkWidget *d = gtk_message_dialog_new(w, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                          GTK_BUTTONS_OK, "%s", msg);
    gtk_window_set_title(GTK_WINDOW(d), "Error");
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

/* ---------- status bar ---------- */

static void refresh_status(void *unused)
{
    (void)unused;
    GtkStyleContext *ctx = gtk_widget_get_style_context(g_status_label);
    char buf[600];
    char err[200];

    gtk_style_context_remove_class(ctx, "state-safe");
    gtk_style_context_remove_class(ctx, "state-unsafe");
    gtk_style_context_remove_class(ctx, "state-deadlock");

    if (g_state.n_proc < 1 || g_state.n_res < 1) {
        snprintf(buf, sizeof buf, "System: not defined yet   |   %s", g_message);
    } else if (state_validate(&g_state, err, sizeof err) != 0) {
        snprintf(buf, sizeof buf, "System: invalid data (%s)   |   %s", err, g_message);
    } else {
        StateClass c = analysis_classify(&g_state);
        const char *cls = c == STATE_SAFE ? "state-safe"
                        : c == STATE_UNSAFE ? "state-unsafe" : "state-deadlock";
        gtk_style_context_add_class(ctx, cls);
        snprintf(buf, sizeof buf, "System state: %s   |   %s", analysis_class_name(c), g_message);
    }
    gtk_label_set_text(GTK_LABEL(g_status_label), buf);
}

/* ---------- navigation ---------- */

static void select_page(int idx)
{
    for (int i = 0; i < N_MENU - 1; i++) {
        GtkStyleContext *ctx = gtk_widget_get_style_context(g_buttons[i]);
        if (i == idx)
            gtk_style_context_add_class(ctx, "active");
        else
            gtk_style_context_remove_class(ctx, "active");
    }
    gtk_stack_set_visible_child_name(GTK_STACK(g_stack), PAGE[idx]);
}

static void confirm_exit(void)
{
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(g_window), GTK_DIALOG_MODAL,
                                          GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                          "Are you sure you want to exit?");
    gtk_window_set_title(GTK_WINDOW(d), "Exit");
    int r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    if (r == GTK_RESPONSE_YES)
        g_application_quit(G_APPLICATION(g_app));
}

static void on_menu_clicked(GtkButton *b, gpointer data)
{
    (void)b;
    int idx = GPOINTER_TO_INT(data);
    if (idx == N_MENU - 1)
        confirm_exit();
    else
        select_page(idx);
}

/* ---------- window construction ---------- */

static GtkWidget *wrap_page(const char *title, GtkWidget *content)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *lbl = gtk_label_new(title);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "title");
    gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), content, TRUE, TRUE, 0);
    return box;
}

static void load_css(void)
{
    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p, CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(p),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

static void on_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;
    state_init(&g_state);
    load_css();

    g_window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(g_window), "Deadlock Simulation System");
    gtk_window_set_default_size(GTK_WINDOW(g_window), 1100, 700);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(g_window), root);

    GtkWidget *middle = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(root), middle, TRUE, TRUE, 0);

    /* sidebar */
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(sidebar), "sidebar");
    gtk_widget_set_size_request(sidebar, 270, -1);
    GtkWidget *brand = gtk_label_new("Deadlock Simulator");
    gtk_widget_set_halign(brand, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(brand), "sidebar-title");
    gtk_box_pack_start(GTK_BOX(sidebar), brand, FALSE, FALSE, 0);
    for (int i = 0; i < N_MENU; i++) {
        char text[80];
        snprintf(text, sizeof text, "%d.  %s", i + 1, MENU[i]);
        g_buttons[i] = gtk_button_new_with_label(text);
        gtk_button_set_relief(GTK_BUTTON(g_buttons[i]), GTK_RELIEF_NONE);
        gtk_widget_set_halign(gtk_bin_get_child(GTK_BIN(g_buttons[i])), GTK_ALIGN_START);
        g_signal_connect(g_buttons[i], "clicked", G_CALLBACK(on_menu_clicked), GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(sidebar), g_buttons[i], FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(middle), sidebar, FALSE, FALSE, 0);

    /* pages */
    g_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(g_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    GtkWidget *pages[N_MENU - 1] = {
        create_processes_tab_new(&g_state),
        create_resources_tab_new(&g_state),
        allocation_tab_new(&g_state),
        maximum_tab_new(&g_state),
        need_tab_new(&g_state),
        rag_tab_new(&g_state),
        banker_request_tab_new(&g_state),
        banker_run_tab_new(&g_state),
        detection_tab_new(&g_state),
        recovery_tab_new(&g_state),
        status_tab_new(&g_state)
    };
    for (int i = 0; i < N_MENU - 1; i++)
        gtk_stack_add_named(GTK_STACK(g_stack), wrap_page(MENU[i], pages[i]), PAGE[i]);
    gtk_box_pack_start(GTK_BOX(middle), g_stack, TRUE, TRUE, 0);

    /* status bar */
    gtk_box_pack_start(GTK_BOX(root), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    g_status_label = gtk_label_new("");
    gtk_widget_set_halign(g_status_label, GTK_ALIGN_FILL);
    gtk_label_set_xalign(GTK_LABEL(g_status_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(g_status_label), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(g_status_label), "status-bar");
    gtk_box_pack_start(GTK_BOX(root), g_status_label, FALSE, FALSE, 0);

    state_subscribe(refresh_status, NULL);

    gtk_widget_show_all(g_window);
    select_page(0);
    state_notify();
}

int app_run(int argc, char **argv)
{
#if GLIB_CHECK_VERSION(2, 74, 0)
    g_app = gtk_application_new("edu.pbl.deadlocksim", G_APPLICATION_DEFAULT_FLAGS);
#else
    g_app = gtk_application_new("edu.pbl.deadlocksim", G_APPLICATION_FLAGS_NONE);
#endif
    g_signal_connect(g_app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(g_app), argc, argv);
    g_object_unref(g_app);
    return status;
}
