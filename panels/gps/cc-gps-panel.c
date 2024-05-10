/*
 * Copyright (C) 2024 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-gps-panel.h"
#include "cc-gps-resources.h"
#include "cc-util.h"

#include <adwaita.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>

struct _CcGpsPanel {
  CcPanel            parent;
  GtkWidget          *gps_supl_enabled_switch;
  GtkWidget          *supl_server_url_entry;
};

G_DEFINE_TYPE (CcGpsPanel, cc_gps_panel, CC_TYPE_PANEL)

static void
cc_gps_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_gps_panel_parent_class)->finalize (object);
}

static gboolean
get_supl_enabled_status ()
{
  gchar *contents = NULL;
  gsize length = 0;
  gboolean result = FALSE;

  if (g_file_get_contents ("/etc/geoclue/conf.d/supl.conf", &contents, &length, NULL)) {
    gchar **lines = g_strsplit (contents, "\n", -1);
    for (gchar **line = lines; *line != NULL; line++) {
      if (g_str_has_prefix (*line, "supl-enabled=")) {
        gchar **tokens = g_strsplit (*line, "=", 2);
        if (tokens[1] && g_ascii_strcasecmp (tokens[1], "true") == 0)
          result = TRUE;

        g_strfreev (tokens);
        break;
      }
    }
    g_strfreev (lines);
    g_free (contents);
  }
  return result;
}

static void
set_supl_enabled_status (gboolean enabled)
{
  GError *error = NULL;
  gchar *contents;
  gsize length;

  if (g_file_get_contents ("/etc/geoclue/conf.d/supl.conf", &contents, &length, &error)) {
    gchar **lines = g_strsplit (contents, "\n", -1);
    GString *new_contents = g_string_new (NULL);
    gboolean found = FALSE;

    for (gchar **line = lines; *line != NULL; line++) {
      if (g_str_has_prefix (*line, "supl-enabled=")) {
        g_string_append_printf (new_contents, "supl-enabled=%s\n", enabled ? "true" : "false");
        found = TRUE;
      } else if (**line != '\0') {
        g_string_append_printf (new_contents, "%s\n", *line);
      }
    }

    if (!found)
      g_string_append_printf (new_contents, "supl-enabled=%s\n", enabled ? "true" : "false");

    g_file_set_contents ("/etc/geoclue/conf.d/supl.conf", new_contents->str, -1, &error);
    g_string_free (new_contents, TRUE);
    g_strfreev (lines);
    g_free (contents);
  }

  if (error) {
    g_warning ("Failed to write SUPL enabled status: %s", error->message);
    g_error_free (error);
  }
}

static gchar*
get_supl_server_url ()
{
  gchar *contents = NULL;
  gsize length = 0;

  if (g_file_get_contents ("/etc/geoclue/conf.d/supl.conf", &contents, &length, NULL)) {
    gchar **lines = g_strsplit (contents, "\n", -1);
    for (gchar **line = lines; *line != NULL; line++) {
      if (g_str_has_prefix (*line, "supl-server=")) {
        gchar **tokens = g_strsplit (*line, "=", 2);
        gchar *url = g_strdup (tokens[1]);

        g_strfreev (tokens);
        g_strfreev (lines);
        g_free (contents);
        return url;
      }
    }
    g_strfreev (lines);
  }
  g_free (contents);
  return NULL;
}

static void
set_supl_server_url (const gchar *new_url)
{
  gchar *contents;
  gsize length;
  GError *error = NULL;

  if (g_file_get_contents ("/etc/geoclue/conf.d/supl.conf", &contents, &length, &error)) {
    gchar **lines = g_strsplit (contents, "\n", -1);
    GString *new_contents = g_string_new (NULL);
    gboolean found = FALSE;

    for (gchar **line = lines; *line != NULL; line++) {
      if (g_str_has_prefix (*line, "supl-server=") && !found) {
        g_string_append_printf (new_contents, "supl-server=%s\n", new_url);
        found = TRUE;
      } else {
        g_string_append_printf (new_contents, "%s\n", *line);
      }
    }

    if (!found)
      g_string_append_printf (new_contents, "supl-server=%s\n", new_url);

    g_file_set_contents ("/etc/geoclue/conf.d/supl.conf", new_contents->str, -1, &error);
    g_string_free (new_contents, TRUE);
    g_strfreev (lines);
    g_free (contents);
  }

  if (error) {
    g_warning ("Failed to write SUPL server URL: %s", error->message);
    g_error_free (error);
  }
}

static void
cc_gps_panel_enable_supl (GtkSwitch *widget, gboolean state, CcGpsPanel *self)
{
  g_signal_handlers_block_by_func (self->gps_supl_enabled_switch, cc_gps_panel_enable_supl, self);
  gtk_switch_set_state (GTK_SWITCH (self->gps_supl_enabled_switch), state);
  set_supl_enabled_status (state);
  g_signal_handlers_unblock_by_func (self->gps_supl_enabled_switch, cc_gps_panel_enable_supl, self);
}

static void
cc_gps_panel_set_supl_server_url (GtkEditable *editable, CcGpsPanel *self)
{
  const gchar *url = gtk_editable_get_text (GTK_EDITABLE (editable));
  set_supl_server_url (url);
//  g_debug ("SUPL Server URL changed to: %s", url);
}

static void
cc_gps_panel_class_init (CcGpsPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_gps_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/gps/cc-gps-panel.ui");
  gtk_widget_class_bind_template_child (widget_class,
                                        CcGpsPanel,
                                        gps_supl_enabled_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcGpsPanel,
                                        supl_server_url_entry);
}

static void
cc_gps_panel_init (CcGpsPanel *self)
{
  g_resources_register (cc_gps_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  if (g_file_test ("/usr/libexec/geoclue", G_FILE_TEST_EXISTS)) {
    g_signal_connect (G_OBJECT (self->gps_supl_enabled_switch), "state-set", G_CALLBACK (cc_gps_panel_enable_supl), self);
    g_signal_connect (G_OBJECT (self->supl_server_url_entry), "changed", G_CALLBACK (cc_gps_panel_set_supl_server_url), self);

    if (g_file_test ("/etc/geoclue/conf.d/supl.conf", G_FILE_TEST_EXISTS)) {
      gboolean supl_enabled = get_supl_enabled_status ();
      gtk_switch_set_active (GTK_SWITCH (self->gps_supl_enabled_switch), supl_enabled);
      gtk_switch_set_state (GTK_SWITCH (self->gps_supl_enabled_switch), supl_enabled);

      gchar *supl_server_url = get_supl_server_url ();
      if (supl_server_url) {
        gtk_editable_set_text (GTK_EDITABLE (self->supl_server_url_entry), supl_server_url);
        g_free (supl_server_url);
      }
    } else {
      GError *error = NULL;
      const gchar *default_content = "[hybris]\nsupl-enabled=false\nsupl-server=\n";
      g_file_set_contents ("/etc/geoclue/conf.d/supl.conf", default_content, -1, &error);

      if (error) {
        g_warning ("Failed to create default supl.conf: %s", error->message);
        gtk_widget_set_sensitive (GTK_WIDGET (self->gps_supl_enabled_switch), FALSE);
        gtk_widget_set_sensitive (GTK_WIDGET (self->supl_server_url_entry), FALSE);
        g_error_free (error);
      }
    }
  } else {
    gtk_widget_set_sensitive (GTK_WIDGET (self->gps_supl_enabled_switch), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->supl_server_url_entry), FALSE);
  }
}

CcGpsPanel *
cc_gps_panel_new (void)
{
  return CC_GPS_PANEL (g_object_new (CC_TYPE_GPS_PANEL, NULL));
}
