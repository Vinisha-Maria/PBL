#ifndef APP_H
#define APP_H

#include <gtk/gtk.h>

int  app_run(int argc, char **argv);

void app_set_status_message(const char *msg);

void app_show_error(GtkWidget *parent, const char *msg);

#endif
