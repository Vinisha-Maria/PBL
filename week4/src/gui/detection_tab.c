#include "detection_tab.h"
#include "../core/detection.h"
#include "../core/recovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SystemState *state;
    GtkWidget *banner;
    GtkWidget *status_label;
    GtkWidget *log_textview;
    GtkEntry req_entries[MAX_PROC][MAX_RES];
} DetectionTabWidgets;

typedef struct {
    SystemState *state;
    GtkWidget *deadlocked_label;
    GtkRadioButton *radio_term_all;
    GtkRadioButton *radio_term_one;
    GtkRadioButton *radio_preempt;
    GtkComboBoxText *combo_victim;
    GtkWidget *banner;
    GtkWidget *log_textview;
} RecoveryTabWidgets;

static void detection_refresh_cb(void *user_data);
static void recovery_refresh_cb(void *user_data);

static void on_save_requests(GtkButton *btn, gpointer user_data) {
    DetectionTabWidgets *w = (DetectionTabWidgets *)user_data;
    SystemState *s = w->state;

    for (int i = 0; i < s->n_proc; i++) {
        for (int j = 0; j < s->n_res; j++) {
            const gchar *text = gtk_entry_get_text(GTK_ENTRY(&w->req_entries[i][j]));
            char *endptr;
            long val = strtol(text, &endptr, 10);
            if (*endptr != '\0' || val < 0) {
                GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                    "Invalid input at Request P%d, R%d: must be a non-negative integer.", i, j);
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
                return;
            }
            s->requests[i][j] = (int)val;
        }
    }
    state_notify();
}

static void on_load_example(GtkButton *btn, gpointer user_data) {
    DetectionTabWidgets *w = (DetectionTabWidgets *)user_data;
    SystemState *s = w->state;

    s->n_proc = 2;
    s->n_res = 2;
    s->total[0] = 1; s->total[1] = 1;
    for (int i = 0; i < MAX_PROC; i++) {
        for (int j = 0; j < MAX_RES; j++) {
            s->allocation[i][j] = 0;
            s->maximum[i][j] = 0;
            s->requests[i][j] = 0;
        }
        s->terminated[i] = false;
    }
    s->allocation[0][0] = 1; s->allocation[0][1] = 0;
    s->allocation[1][0] = 0; s->allocation[1][1] = 1;
    s->requests[0][0] = 0; s->requests[0][1] = 1;
    s->requests[1][0] = 1; s->requests[1][1] = 0;

    state_notify();
}

static void on_detect_clicked(GtkButton *btn, gpointer user_data) {
    DetectionTabWidgets *w = (DetectionTabWidgets *)user_data;
    SystemState *s = w->state;

    DetectionResult res = detect_deadlock(s);

    if (res.deadlocked) {
        gtk_label_set_text(GTK_LABEL(w->banner), "DEADLOCK DETECTED");
        gtk_style_context_remove_class(gtk_widget_get_style_context(w->banner), "state-safe");
        gtk_style_context_add_class(gtk_widget_get_style_context(w->banner), "state-deadlock");

        char buf[512] = "Deadlocked processes: ";
        for (int k = 0; k < res.n_deadlocked; k++) {
            char p_name[8];
            state_proc_name(res.deadlocked_procs[k], p_name);
            strcat(buf, p_name);
            if (k < res.n_deadlocked - 1) strcat(buf, ", ");
        }
        gtk_label_set_text(GTK_LABEL(w->status_label), buf);
    } else {
        gtk_label_set_text(GTK_LABEL(w->banner), "NO DEADLOCK");
        gtk_style_context_remove_class(gtk_widget_get_style_context(w->banner), "state-deadlock");
        gtk_style_context_add_class(gtk_widget_get_style_context(w->banner), "state-safe");
        gtk_label_set_text(GTK_LABEL(w->status_label), "All processes can complete.");
    }

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->log_textview));
    gtk_text_buffer_set_text(buffer, "", 0);
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(buffer, &iter);

    for (int i = 0; i < res.n_steps; i++) {
        gtk_text_buffer_insert(buffer, &iter, res.steps[i], -1);
        gtk_text_buffer_insert(buffer, &iter, "\n", -1);
    }
}

static void detection_refresh_cb(void *user_data) {
    DetectionTabWidgets *w = (DetectionTabWidgets *)user_data;
    SystemState *s = w->state;
    for (int i = 0; i < s->n_proc; i++) {
        for (int j = 0; j < s->n_res; j++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", s->requests[i][j]);
            gtk_entry_set_text(GTK_ENTRY(&w->req_entries[i][j]), buf);
        }
    }
}

GtkWidget *detection_tab_new(SystemState *state) {
    DetectionTabWidgets *w = g_new0(DetectionTabWidgets, 1);
    w->state = state;

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    GtkWidget *hbox_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<b>Deadlock Detection</b>");
    gtk_box_pack_start(GTK_BOX(hbox_top), title, FALSE, FALSE, 0);

    GtkWidget *btn_example = gtk_button_new_with_label("Load Deadlock Example");
    g_signal_connect(btn_example, "clicked", G_CALLBACK(on_load_example), w);
    gtk_box_pack_end(GTK_BOX(hbox_top), btn_example, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_top, FALSE, FALSE, 0);

    w->banner = gtk_label_new("STATUS: UNCHECKED");
    gtk_box_pack_start(GTK_BOX(vbox), w->banner, FALSE, FALSE, 0);

    w->status_label = gtk_label_new("Click 'Detect Deadlock' to analyze.");
    gtk_box_pack_start(GTK_BOX(vbox), w->status_label, FALSE, FALSE, 0);

    GtkWidget *frame_req = gtk_frame_new("Resource Requests Matrix");
    GtkWidget *grid_req = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid_req), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid_req), 5);
    gtk_container_set_border_width(GTK_CONTAINER(grid_req), 8);

    for (int j = 0; j < state->n_res; j++) {
        char r_name[8];
        state_res_name(j, r_name);
        gtk_grid_attach(GTK_GRID(grid_req), gtk_label_new(r_name), j + 1, 0, 1, 1);
    }
    for (int i = 0; i < state->n_proc; i++) {
        char p_name[8];
        state_proc_name(i, p_name);
        gtk_grid_attach(GTK_GRID(grid_req), gtk_label_new(p_name), 0, i + 1, 1, 1);

        for (int j = 0; j < state->n_res; j++) {
            w->req_entries[i][j] = *GTK_ENTRY(gtk_entry_new());
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", state->requests[i][j]);
            gtk_entry_set_text(&w->req_entries[i][j], buf);
            gtk_widget_set_size_request(GTK_WIDGET(&w->req_entries[i][j]), 50, -1);
            gtk_grid_attach(GTK_GRID(grid_req), GTK_WIDGET(&w->req_entries[i][j]), j + 1, i + 1, 1, 1);
        }
    }
    gtk_container_add(GTK_CONTAINER(frame_req), grid_req);
    gtk_box_pack_start(GTK_BOX(vbox), frame_req, FALSE, FALSE, 0);

    GtkWidget *hbox_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *btn_save = gtk_button_new_with_label("Save Requests");
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_save_requests), w);
    gtk_box_pack_start(GTK_BOX(hbox_btns), btn_save, FALSE, FALSE, 0);

    GtkWidget *btn_detect = gtk_button_new_with_label("Detect Deadlock");
    g_signal_connect(btn_detect, "clicked", G_CALLBACK(on_detect_clicked), w);
    gtk_box_pack_start(GTK_BOX(hbox_btns), btn_detect, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox_btns, FALSE, FALSE, 0);

    GtkWidget *frame_log = gtk_frame_new("Trace Log");
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scrolled, -1, 150);
    w->log_textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(w->log_textview), FALSE);
    gtk_container_add(GTK_CONTAINER(scrolled), w->log_textview);
    gtk_container_add(GTK_CONTAINER(frame_log), scrolled);
    gtk_box_pack_start(GTK_BOX(vbox), frame_log, TRUE, TRUE, 0);

    state_subscribe(detection_refresh_cb, w);
    return vbox;
}

static void on_radio_toggled(GtkToggleButton *btn, gpointer user_data) {
    RecoveryTabWidgets *w = (RecoveryTabWidgets *)user_data;
    gboolean is_preempt = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->radio_preempt));
    gtk_widget_set_sensitive(GTK_WIDGET(w->combo_victim), is_preempt);
}

static void on_recover_clicked(GtkButton *btn, gpointer user_data) {
    RecoveryTabWidgets *w = (RecoveryTabWidgets *)user_data;
    SystemState *s = w->state;
    SystemState before_state = *s;

    RecoveryResult res;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->radio_term_all))) {
        res = recover_terminate_all(s);
    } else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->radio_term_one))) {
        res = recover_terminate_one_by_one(s);
    } else {
        char *active_id = gtk_combo_box_text_get_active_id(w->combo_victim);
        int victim = active_id ? atoi(active_id) : 0;
        if (active_id) g_free(active_id);
        res = recover_preempt(s, victim);
    }

    if (res.success) {
        gtk_label_set_text(GTK_LABEL(w->banner), "RECOVERY SUCCESSFUL");
    } else {
        gtk_label_set_text(GTK_LABEL(w->banner), "RECOVERY FAILED");
    }

    char summary_buf[512];
    int avail_before[MAX_RES], avail_after[MAX_RES];
    state_available(&before_state, avail_before);
    state_available(s, avail_after);

    snprintf(summary_buf, sizeof(summary_buf),
        "Before Available: R0=%d, R1=%d\nAfter Available: R0=%d, R1=%d\n\nActions:\n",
        avail_before[0], avail_before[1], avail_after[0], avail_after[1]
    );

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->log_textview));
    gtk_text_buffer_set_text(buffer, summary_buf, -1);
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(buffer, &iter);

    for (int i = 0; i < res.n_actions; i++) {
        gtk_text_buffer_insert(buffer, &iter, res.actions[i], -1);
        gtk_text_buffer_insert(buffer, &iter, "\n", -1);
    }
}

static void recovery_refresh_cb(void *user_data) {
    RecoveryTabWidgets *w = (RecoveryTabWidgets *)user_data;
    SystemState *s = w->state;
    DetectionResult det = detect_deadlock(s);
    char buf[256] = "Deadlocked: ";
    if (det.deadlocked) {
        for (int k = 0; k < det.n_deadlocked; k++) {
            char p_name[8];
            state_proc_name(det.deadlocked_procs[k], p_name);
            strcat(buf, p_name);
            if (k < det.n_deadlocked - 1) strcat(buf, ", ");
        }
    } else {
        strcat(buf, "None");
    }
    gtk_label_set_text(GTK_LABEL(w->deadlocked_label), buf);

    gtk_combo_box_text_remove_all(w->combo_victim);
    for (int k = 0; k < det.n_deadlocked; k++) {
        int p = det.deadlocked_procs[k];
        char p_name[8], id_buf[8];
        state_proc_name(p, p_name);
        snprintf(id_buf, sizeof(id_buf), "%d", p);
        gtk_combo_box_text_append(w->combo_victim, id_buf, p_name);
    }
    if (det.n_deadlocked > 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(w->combo_victim), 0);
    }
}

GtkWidget *recovery_tab_new(SystemState *state) {
    RecoveryTabWidgets *w = g_new0(RecoveryTabWidgets, 1);
    w->state = state;

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new("Deadlock Recovery"), FALSE, FALSE, 0);
    w->banner = gtk_label_new("READY");
    gtk_box_pack_start(GTK_BOX(vbox), w->banner, FALSE, FALSE, 0);

    w->deadlocked_label = gtk_label_new("Checking...");
    gtk_box_pack_start(GTK_BOX(vbox), w->deadlocked_label, FALSE, FALSE, 0);

    GtkWidget *frame_strat = gtk_frame_new("Strategy");
    GtkWidget *vbox_strat = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    w->radio_term_all = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(NULL, "Terminate All"));
    w->radio_term_one = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(w->radio_term_all, "Terminate One by One"));
    w->radio_preempt = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(w->radio_term_all, "Preempt"));

    g_signal_connect(w->radio_term_all, "toggled", G_CALLBACK(on_radio_toggled), w);
    g_signal_connect(w->radio_term_one, "toggled", G_CALLBACK(on_radio_toggled), w);
    g_signal_connect(w->radio_preempt, "toggled", G_CALLBACK(on_radio_toggled), w);

    gtk_box_pack_start(GTK_BOX(vbox_strat), GTK_WIDGET(w->radio_term_all), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_strat), GTK_WIDGET(w->radio_term_one), FALSE, FALSE, 0);

    GtkWidget *hbox_p = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(hbox_p), GTK_WIDGET(w->radio_preempt), FALSE, FALSE, 0);
    w->combo_victim = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_widget_set_sensitive(GTK_WIDGET(w->combo_victim), FALSE);
    gtk_box_pack_start(GTK_BOX(hbox_p), GTK_WIDGET(w->combo_victim), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_strat), hbox_p, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame_strat), vbox_strat);
    gtk_box_pack_start(GTK_BOX(vbox), frame_strat, FALSE, FALSE, 0);

    GtkWidget *btn_recover = gtk_button_new_with_label("Execute Recovery");
    g_signal_connect(btn_recover, "clicked", G_CALLBACK(on_recover_clicked), w);
    gtk_box_pack_start(GTK_BOX(vbox), btn_recover, FALSE, FALSE, 0);

    GtkWidget *frame_log = gtk_frame_new("Log");
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    w->log_textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(w->log_textview), FALSE);
    gtk_container_add(GTK_CONTAINER(scrolled), w->log_textview);
    gtk_container_add(GTK_CONTAINER(frame_log), scrolled);
    gtk_box_pack_start(GTK_BOX(vbox), frame_log, TRUE, TRUE, 0);

    state_subscribe(recovery_refresh_cb, w);
    recovery_refresh_cb(w);
    return vbox;
}

