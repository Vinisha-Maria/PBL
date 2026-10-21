#ifndef SETUP_TABS_H
#define SETUP_TABS_H

/* Menu 1-5: setup screens.  Owner: Team Lead. */
#include <gtk/gtk.h>
#include "state.h"

GtkWidget *create_processes_tab_new(SystemState *state);   /* menu 1 */
GtkWidget *create_resources_tab_new(SystemState *state);   /* menu 2 */
GtkWidget *allocation_tab_new(SystemState *state);         /* menu 3 */
GtkWidget *maximum_tab_new(SystemState *state);            /* menu 4 */
GtkWidget *need_tab_new(SystemState *state);               /* menu 5 */

#endif
