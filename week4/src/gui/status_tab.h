/* src/gui/status_tab.h - Menu 11: Display System Status */
#ifndef STATUS_TAB_H
#define STATUS_TAB_H

#include <gtk/gtk.h>
#include "../core/state.h"

/* Create the tab (a scrolled window). It rebuilds itself on state changes. */
GtkWidget *status_tab_new(SystemState *state);

#endif /* STATUS_TAB_H */
