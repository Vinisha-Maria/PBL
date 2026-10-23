#ifndef DETECTION_TAB_H
#define DETECTION_TAB_H

#include <gtk/gtk.h>
#include "../core/state.h"
GtkWidget *detection_tab_new(SystemState *state);
GtkWidget *recovery_tab_new(SysyemState *state);

#endif
