#ifndef APP_H
#define APP_H

/* Main window, sidebar, status bar, shared CSS.  Owner: Team Lead. */
#include <gtk/gtk.h>

int  app_run(int argc, char **argv);

/* Sets the "last message" shown in the bottom status bar (shown after the next state_notify()). */
void app_set_status_message(const char *msg);

/* Shows a modal error dialog. parent can be any widget inside the window (or NULL). */
void app_show_error(GtkWidget *parent, const char *msg);

/* CSS classes available to every tab (defined once in app.c):
 *   .state-safe (green)  .state-unsafe (orange)  .state-deadlock (red)   - coloured banners
 *   .title  .card  .cell  .cell-head  .cell-bad  .hint                   - layout / tables
 */
#endif
