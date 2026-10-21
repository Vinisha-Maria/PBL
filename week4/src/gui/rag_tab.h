/* src/gui/rag_tab.h - Menu 6: Display Resource Allocation Graph */
#ifndef RAG_TAB_H
#define RAG_TAB_H

#include <gtk/gtk.h>
#include "../core/state.h"

/* Create the tab. It subscribes to state changes and redraws itself. */
GtkWidget *rag_tab_new(SystemState *state);

#endif /* RAG_TAB_H */
