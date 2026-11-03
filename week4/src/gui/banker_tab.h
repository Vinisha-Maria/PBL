
#ifndef BANKER_TAB_H
#define BANKER_TAB_H

#include <gtk/gtk.h>

#include "../core/state.h"

GtkWidget *banker_request_tab_new(SystemState *state);
GtkWidget *banker_run_tab_new(SystemState *state);

#endif

