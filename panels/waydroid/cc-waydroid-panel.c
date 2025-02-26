/*
 * Copyright (C) 2025 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-waydroid-panel.h"
#include "cc-waydroid-resources.h"
#include "cc-util.h"

#include "panels/nfc/cc-systemd-service.h"

#define WAYDROID_CONTAINER_DBUS_NAME          "id.waydro.Container"
#define WAYDROID_CONTAINER_DBUS_PATH          "/ContainerManager"
#define WAYDROID_CONTAINER_DBUS_INTERFACE     "id.waydro.ContainerManager"

#define WAYDROID_SESSION_DBUS_NAME          "id.waydro.Session"
#define WAYDROID_SESSION_DBUS_PATH          "/SessionManager"
#define WAYDROID_SESSION_DBUS_INTERFACE     "id.waydro.SessionManager"

#define WAYDROID_NOTIFICATION_SERVER_SERVICE  "waydroid-notification-server.service"

struct _CcWaydroidPanel {
  CcPanel            parent;
  AdwToastOverlay  *toast_overlay;
  GtkWidget        *waydroid_enabled_switch;
  GtkWidget        *waydroid_autostart_switch;
  GtkWidget        *waydroid_shared_folder_switch;
  GtkWidget        *waydroid_nfc_switch;
  GtkWidget        *waydroid_notification_switch;
  GtkWidget        *waydroid_ip_label;
  GtkWidget        *waydroid_version_label;
  AdwExpanderRow   *app_selector;
  GtkListBox       *app_list;
  GListStore       *app_list_store;
  GtkWidget        *launch_app_button;
  GtkWidget        *remove_app_button;
  GtkWidget        *install_app_button;
  GtkWidget        *store_button;
  GtkWidget        *refresh_app_list_button;
  GtkWidget        *factory_reset_button;
  GtkWidget        *clear_app_data_button;
  GtkWidget        *kill_app_button;
  AdwBottomSheet   *bottom_sheet;

  gchar            **apps;
  GList            *app_widgets;
  gchar            *selected_app_name;
  gchar            *selected_app_pkgname;

  gchar            *waydroid_ip_output;
  gchar            *waydroid_version_output;

  gboolean         refreshing;
  gboolean         waydroid_notification_active;
  gboolean         waydroid_nfc_active;
  gboolean         waydroid_shared_folder_enabled;
};

G_DEFINE_TYPE (CcWaydroidPanel, cc_waydroid_panel, CC_TYPE_PANEL)

static void
cc_waydroid_panel_finalize (GObject *object)
{
  CcWaydroidPanel *self = CC_WAYDROID_PANEL (object);

  g_list_free_full (self->app_widgets, g_object_unref);

  if (self->selected_app_name != NULL)
    g_free (self->selected_app_name);
  if (self->selected_app_pkgname != NULL)
    g_free (self->selected_app_pkgname);
  if (self->waydroid_ip_output != NULL)
    g_free (self->waydroid_ip_output);
  if (self->waydroid_version_output != NULL)
    g_free (self->waydroid_version_output);
  if (self->apps != NULL)
    g_strfreev (self->apps);

  self->app_widgets = NULL;

  G_OBJECT_CLASS (cc_waydroid_panel_parent_class)->finalize (object);
}

static void
show_toast (CcWaydroidPanel *self, const char *format, ...)
{
  va_list args;
  char *message;
  AdwToast *toast;

  va_start (args, format);
  message = g_strdup_vprintf (format, args);
  va_end (args);

  toast = adw_toast_new (message);
  adw_toast_set_timeout (toast, 3);

  adw_toast_overlay_add_toast (self->toast_overlay, toast);

  g_free (message);
}

static gchar *
waydroid_get_state (void)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;
  GVariant *result;
  gchar *state = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_CONTAINER_DBUS_NAME,
    WAYDROID_CONTAINER_DBUS_PATH,
    WAYDROID_CONTAINER_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s", error->message);
    g_clear_error (&error);
    return NULL;
  }

  result = g_dbus_proxy_call_sync(
    waydroid_proxy,
    "GetSession",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    G_MAXINT,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling GetSession: %s", error->message);
    g_clear_error (&error);
  } else {
    GVariant *inner_dict;
    GVariantIter iter;
    gchar *key, *value;

    inner_dict = g_variant_get_child_value (result, 0);

    g_variant_iter_init (&iter, inner_dict);

    while (g_variant_iter_next (&iter, "{ss}", &key, &value)) {
      if (g_strcmp0 (key, "state") == 0) {
        state = g_strdup (value);
        g_free (key);
        g_free (value);
        break;
      }
      g_free (key);
      g_free (value);
    }

    g_variant_unref (inner_dict);
    g_variant_unref (result);
  }

  g_object_unref (waydroid_proxy);

  return state;
}

static gboolean
waydroid_get_nfc_status (void)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;
  GVariant *result;
  gboolean nfc_status = FALSE;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_CONTAINER_DBUS_NAME,
    WAYDROID_CONTAINER_DBUS_PATH,
    WAYDROID_CONTAINER_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  result = g_dbus_proxy_call_sync(
    waydroid_proxy,
    "GetNfcStatus",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    G_MAXINT,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling GetNfcStatus: %s", error->message);
    g_clear_error (&error);
  } else {
    g_variant_get(result, "(b)", &nfc_status);
    g_variant_unref (result);
  }

  g_object_unref (waydroid_proxy);

  return nfc_status;
}

static void
waydroid_toggle_nfc (void)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_CONTAINER_DBUS_NAME,
    WAYDROID_CONTAINER_DBUS_PATH,
    WAYDROID_CONTAINER_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s", error->message);
    g_clear_error (&error);
    return;
  }

  g_dbus_proxy_call_sync(
    waydroid_proxy,
    "NfcToggle",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling NfcToggle: %s", error->message);
    g_clear_error (&error);
  }

  g_object_unref (waydroid_proxy);
}

static gchar *
waydroid_get_ip (void)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;
  GVariant *result;
  gchar *ip = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_SESSION_DBUS_NAME,
    WAYDROID_SESSION_DBUS_PATH,
    WAYDROID_SESSION_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s", error->message);
    g_clear_error (&error);
    return NULL;
  }

  result = g_dbus_proxy_call_sync(
    waydroid_proxy,
    "IpAddress",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    G_MAXINT,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling IpAddress: %s", error->message);
    g_clear_error (&error);
  } else {
    g_variant_get (result, "(s)", &ip);
    g_variant_unref (result);
  }

  g_object_unref (waydroid_proxy);

  return ip;
}

static gchar *
waydroid_get_version (void)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;
  GVariant *result;
  gchar *version = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_SESSION_DBUS_NAME,
    WAYDROID_SESSION_DBUS_PATH,
    WAYDROID_SESSION_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s", error->message);
    g_clear_error (&error);
    return NULL;
  }

  result = g_dbus_proxy_call_sync(
    waydroid_proxy,
    "LineageVersion",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    G_MAXINT,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling LineageVersion: %s", error->message);
    g_clear_error (&error);
  } else {
    g_variant_get (result, "(s)", &version);
    g_variant_unref (result);
  }

  g_object_unref (waydroid_proxy);

  return version;
}

static void
waydroid_mount_shared (void)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_CONTAINER_DBUS_NAME,
    WAYDROID_CONTAINER_DBUS_PATH,
    WAYDROID_CONTAINER_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s", error->message);
    g_clear_error (&error);
    return;
  }

  g_dbus_proxy_call_sync(
    waydroid_proxy,
    "MountSharedFolder",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling MountSharedFolder: %s", error->message);
    g_clear_error (&error);
  }

  g_object_unref (waydroid_proxy);
}

static void
waydroid_umount_shared (void)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_CONTAINER_DBUS_NAME,
    WAYDROID_CONTAINER_DBUS_PATH,
    WAYDROID_CONTAINER_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s", error->message);
    g_clear_error (&error);
    return;
  }

  g_dbus_proxy_call_sync(
    waydroid_proxy,
    "UnmountSharedFolder",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling UnmountSharedFolder: %s", error->message);
    g_clear_error (&error);
  }

  g_object_unref (waydroid_proxy);
}

static void
waydroid_remove_app (const gchar *package_name)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_SESSION_DBUS_NAME,
    WAYDROID_SESSION_DBUS_PATH,
    WAYDROID_SESSION_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s", error->message);
    g_clear_error (&error);
    return;
  }

  g_dbus_proxy_call_sync(
    waydroid_proxy,
    "RemoveApp",
    g_variant_new("(s)", package_name),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling RemoveApp: %s", error->message);
    g_clear_error (&error);
  }

  g_object_unref (waydroid_proxy);
}

static void
waydroid_install_app (const gchar *package_path)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_SESSION_DBUS_NAME,
    WAYDROID_SESSION_DBUS_PATH,
    WAYDROID_SESSION_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s", error->message);
    g_clear_error (&error);
    return;
  }

  g_dbus_proxy_call_sync(
    waydroid_proxy,
    "InstallApp",
    g_variant_new("(s)", package_path),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling InstallApp: %s", error->message);
    g_clear_error (&error);
  }

  g_object_unref (waydroid_proxy);
}

static void
waydroid_clear_app_data (const gchar *package_name)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_CONTAINER_DBUS_NAME,
    WAYDROID_CONTAINER_DBUS_PATH,
    WAYDROID_CONTAINER_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s", error->message);
    g_clear_error (&error);
    return;
  }

  g_dbus_proxy_call_sync(
    waydroid_proxy,
    "ClearAppData",
    g_variant_new("(s)", package_name),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling ClearAppData: %s", error->message);
    g_clear_error (&error);
  }

  g_object_unref (waydroid_proxy);
}

static void
waydroid_kill_app (const gchar *package_name)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_CONTAINER_DBUS_NAME,
    WAYDROID_CONTAINER_DBUS_PATH,
    WAYDROID_CONTAINER_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s", error->message);
    g_clear_error (&error);
    return;
  }

  g_dbus_proxy_call_sync(
    waydroid_proxy,
    "KillApp",
    g_variant_new("(s)", package_name),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling KillApp: %s", error->message);
    g_clear_error (&error);
  }

  g_object_unref (waydroid_proxy);
}

static gchar *
waydroid_name_to_package_name (const gchar *name)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;
  GVariant *result;
  gchar *app_name = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_SESSION_DBUS_NAME,
    WAYDROID_SESSION_DBUS_PATH,
    WAYDROID_SESSION_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s", error->message);
    g_clear_error (&error);
    return NULL;
  }

  result = g_dbus_proxy_call_sync(
    waydroid_proxy,
    "NameToPackageName",
    g_variant_new ("(s)", name),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling NameToPackageName: %s", error->message);
    g_clear_error (&error);
  } else {
    g_variant_get (result, "(s)", &app_name);
    g_variant_unref (result);
  }

  g_object_unref (waydroid_proxy);
  return app_name;
}

static gchar **
waydroid_get_all_names (void)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;
  GVariant *result;
  gchar **names = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_SESSION_DBUS_NAME,
    WAYDROID_SESSION_DBUS_PATH,
    WAYDROID_SESSION_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s", error->message);
    g_clear_error (&error);
    return NULL;
  }

  result = g_dbus_proxy_call_sync(
    waydroid_proxy,
    "GetAllNames",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling GetAllNames: %s", error->message);
    g_clear_error (&error);
  } else {
    g_variant_get (result, "(^as)", &names);
    g_variant_unref (result);
  }

  g_object_unref (waydroid_proxy);
  return names;
}

static void
waydroid_enable_notification_server (gboolean enable)
{
  GDBusProxy *waydroid_proxy;
  GError *error = NULL;

  waydroid_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    WAYDROID_CONTAINER_DBUS_NAME,
    WAYDROID_CONTAINER_DBUS_PATH,
    WAYDROID_CONTAINER_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s", error->message);
    g_clear_error (&error);
    return;
  }

  g_dbus_proxy_call_sync(
    waydroid_proxy,
    "EnableNotificationServer",
    g_variant_new("(b)", enable),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling EnableNotificationServer: %s", error->message);
    g_clear_error (&error);
  }

  g_object_unref (waydroid_proxy);
}

static int
is_mounted (const char *path)
{
  FILE *fp;
  char *line = NULL;
  size_t len = 0;
  int found = 0;

  fp = fopen ("/proc/mounts", "r");
  if (fp == NULL) {
    perror ("Failed to open /proc/mounts");
    return -1;
  }

  while (getline (&line, &len, fp) != -1) {
    char *mount_point;

    char *line_copy = strdup (line);
    if (line_copy == NULL) {
      perror ("Failed to allocate memory");
      free (line);
      fclose (fp);
      return -1;
    }

    strtok (line_copy, " ");
    mount_point = strtok (NULL, " ");

    if (mount_point != NULL && strcmp (mount_point, path) == 0) {
      found = 1;
      free (line_copy);
      break;
    }

    free (line_copy);
  }

  free (line);
  fclose (fp);

  return found;
}

static void
cc_waydroid_panel_nfc (GtkSwitch *widget, gboolean state, CcWaydroidPanel *self)
{
  waydroid_toggle_nfc ();
  gtk_switch_set_state (GTK_SWITCH (self->waydroid_nfc_switch), state);
  gtk_switch_set_active (GTK_SWITCH (self->waydroid_nfc_switch), state);
}

static void
cc_waydroid_panel_notification (GtkSwitch *widget, gboolean state, CcWaydroidPanel *self)
{
  waydroid_enable_notification_server (state);
  gtk_switch_set_state (GTK_SWITCH (self->waydroid_notification_switch), state);
  gtk_switch_set_active (GTK_SWITCH (self->waydroid_notification_switch), state);
}

static void
cc_waydroid_panel_autostart (GtkSwitch *widget, gboolean state, CcWaydroidPanel *self)
{
  const gchar *home_dir = g_get_home_dir ();
  gchar *filepath = g_build_filename (home_dir, ".android_enable", NULL);
  GFile *file = g_file_new_for_path (filepath);
  GError *error = NULL;

  g_signal_handlers_block_by_func (self->waydroid_autostart_switch, cc_waydroid_panel_autostart, self);

  if (state) {
    GFileOutputStream *output_stream = g_file_create (file, G_FILE_CREATE_NONE, NULL, &error);
    if (output_stream != NULL) {
      g_output_stream_close (G_OUTPUT_STREAM (output_stream), NULL, NULL);
      g_object_unref (output_stream);
    } else if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
      g_warning ("Error creating %s: %s", filepath, error->message);
      g_clear_error (&error);
    }
  } else {
    if (!g_file_delete (file, NULL, &error)) {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_warning ("Error deleting %s: %s", filepath, error->message);
      g_clear_error (&error);
    }
  }

  gtk_switch_set_state (GTK_SWITCH (self->waydroid_autostart_switch), state);
  gtk_switch_set_active (GTK_SWITCH (self->waydroid_autostart_switch), state);

  g_signal_handlers_unblock_by_func (self->waydroid_autostart_switch, cc_waydroid_panel_autostart, self);

  g_object_unref (file);
  g_free (filepath);
}

static void
cc_waydroid_panel_shared_folder (GtkSwitch *widget, gboolean state, CcWaydroidPanel *self)
{
  if (state) {
    waydroid_mount_shared ();
    gtk_switch_set_state (GTK_SWITCH (self->waydroid_shared_folder_switch), TRUE);
    gtk_switch_set_active (GTK_SWITCH (self->waydroid_shared_folder_switch), TRUE);
  } else {
    waydroid_umount_shared ();
    gtk_switch_set_state (GTK_SWITCH (self->waydroid_shared_folder_switch), FALSE);
    gtk_switch_set_active (GTK_SWITCH (self->waydroid_shared_folder_switch), FALSE);
  }
}

static gboolean
update_ip_idle (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;

  gtk_label_set_text (GTK_LABEL (self->waydroid_ip_label), self->waydroid_ip_output);

  return G_SOURCE_REMOVE;
}

static gpointer
update_waydroid_ip (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;

  self->waydroid_ip_output = waydroid_get_ip ();

  g_idle_add (update_ip_idle, self);

  return NULL;
}

static void
update_waydroid_ip_threaded (CcWaydroidPanel *self)
{
  g_thread_new ("update_waydroid_ip", update_waydroid_ip, self);
}

static GtkWidget *
create_app_row (const gchar *app_name)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 8);
  gtk_widget_set_margin_start (box, 16);
  gtk_widget_set_margin_end (box, 16);

  gchar *app_pkgname = waydroid_name_to_package_name (app_name);

  GtkWidget *icon;
  if (app_pkgname != NULL) {
    const gchar *home_dir = g_get_home_dir ();
    gchar *icon_path = g_strdup_printf ("%s/.local/share/waydroid/data/icons/%s.png", home_dir, app_pkgname);

    if (g_file_test (icon_path, G_FILE_TEST_EXISTS))
      icon = gtk_image_new_from_file (icon_path);
    else
      icon = gtk_image_new_from_icon_name ("application-x-executable");

    g_free (icon_path);
    g_free (app_pkgname);
  } else
    icon = gtk_image_new_from_icon_name ("application-x-executable");

  gtk_image_set_pixel_size (GTK_IMAGE (icon), 32);
  gtk_box_append (GTK_BOX (box), icon);

  GtkWidget *label = gtk_label_new (app_name);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_box_append (GTK_BOX (box), label);

  return box;
}

static void
on_app_activated (GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;

  GtkWidget *child = gtk_list_box_row_get_child (row);
  GtkWidget *label = gtk_widget_get_first_child (child);

  while (label && !GTK_IS_LABEL (label)) {
    label = gtk_widget_get_next_sibling (label);
  }

  if (GTK_IS_LABEL (label)) {
    const gchar *app_name = gtk_label_get_text (GTK_LABEL (label));

    self->selected_app_name = g_strdup (app_name);
    self->selected_app_pkgname = waydroid_name_to_package_name (app_name);

    g_debug ("Selected app: %s", self->selected_app_name);
    g_debug ("Package name: %s", self->selected_app_pkgname);

    adw_bottom_sheet_set_open (self->bottom_sheet, TRUE);
  }
}

static int
qsort_g_strcmp0 (const void *a, const void *b)
{
  return g_strcmp0 (*(const gchar **) a, *(const gchar **) b);
}

static gboolean
set_refresh_sensitive (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;
  gtk_widget_set_sensitive (GTK_WIDGET (self->refresh_app_list_button), !self->refreshing);
  gtk_widget_set_sensitive (GTK_WIDGET (self->app_selector), !self->refreshing);
  return G_SOURCE_REMOVE;
}

static gboolean
update_app_list_idle (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;
  AdwExpanderRow *expander_row = ADW_EXPANDER_ROW (self->app_selector);
  GtkListBox *list_box = self->app_list;

  adw_expander_row_set_expanded (self->app_selector, FALSE);

  if (self->apps == NULL) {
    g_debug ("self->apps became NULL, returning");
    return G_SOURCE_REMOVE;
  }

  if (!list_box) {
    list_box = GTK_LIST_BOX (gtk_list_box_new ());
    gtk_widget_set_margin_top (GTK_WIDGET (list_box), 8);
    gtk_widget_set_margin_bottom (GTK_WIDGET (list_box), 8);
    gtk_list_box_set_selection_mode (list_box, GTK_SELECTION_NONE);
    g_signal_connect (list_box, "row-activated", G_CALLBACK (on_app_activated), self);

    adw_expander_row_add_row (expander_row, GTK_WIDGET (list_box));
    self->app_list = list_box;
  } else {
    gtk_list_box_remove_all (list_box);
  }

  g_list_free_full (self->app_widgets, g_object_unref);
  self->app_widgets = NULL;
  qsort (self->apps, g_strv_length (self->apps), sizeof (gchar *), qsort_g_strcmp0);

  for (gchar **app = self->apps; *app; app++) {
    if ((*app)[0] != '\0') {
      GtkWidget *row = create_app_row (*app);
      gtk_list_box_append (list_box, row);
      self->app_widgets = g_list_append (self->app_widgets, g_object_ref (row));
      g_debug ("Added row for app: %s", *app);
    }
  }

  self->refreshing = FALSE;
  g_idle_add (set_refresh_sensitive, self);

  return G_SOURCE_REMOVE;
}

static gpointer
update_app_list (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;

  self->refreshing = TRUE;
  g_idle_add (set_refresh_sensitive, self);
  self->apps = waydroid_get_all_names ();
  if (self->apps == NULL) {
    g_debug ("Failed to get app names");
    self->refreshing = FALSE;
    g_idle_add (set_refresh_sensitive, self);
    return NULL;
  }

  g_idle_add (update_app_list_idle, self);

  return NULL;
}

static void
update_app_list_threaded (CcWaydroidPanel *self)
{
  g_thread_new ("update_app_list", update_app_list, self);
}

static void
set_widgets_state (CcWaydroidPanel *self, gboolean state)
{
 gtk_widget_set_sensitive (GTK_WIDGET (self->app_selector), state);
 gtk_widget_set_sensitive (GTK_WIDGET (self->remove_app_button), state);
 gtk_widget_set_sensitive (GTK_WIDGET (self->install_app_button), state);
 gtk_widget_set_sensitive (GTK_WIDGET (self->refresh_app_list_button), state);
 gtk_widget_set_sensitive (GTK_WIDGET (self->store_button), state);
 gtk_widget_set_sensitive (GTK_WIDGET (self->clear_app_data_button), state);
 gtk_widget_set_sensitive (GTK_WIDGET (self->kill_app_button), state);
 gtk_widget_set_sensitive (GTK_WIDGET (self->launch_app_button), state);
}

static gboolean
close_bottom_sheet_and_refresh (CcWaydroidPanel *self)
{
  set_widgets_state (self, TRUE);
  adw_bottom_sheet_set_open (self->bottom_sheet, FALSE);
  return G_SOURCE_REMOVE;
}

static void
show_confirmation_alert_dialog (CcWaydroidPanel *self,
                                const gchar *title,
                                const gchar *description,
                                const gchar *confirm_text,
                                GCallback confirm_callback)
{
  AdwDialog *dialog;
  GtkWindow *parent;

  parent = GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (self)));

  dialog = ADW_DIALOG (adw_alert_dialog_new (title, description));
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "cancel", ("Cancel"));
  adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "confirm", confirm_text);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog), "confirm", ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "cancel");

  g_signal_connect (dialog, "response::confirm", confirm_callback, self);

  adw_dialog_present (dialog, GTK_WIDGET (parent));
}

static void
on_remove_app_confirm (AdwAlertDialog *dialog, gchar *response, CcWaydroidPanel *self)
{
  if (g_strcmp0 (response, "confirm") == 0) {
    gchar *pkgname = self->selected_app_pkgname;
    if (pkgname != NULL) {
      set_widgets_state (self, FALSE);

      gchar *stripped_pkgname = g_strstrip (pkgname);
      waydroid_remove_app (stripped_pkgname);

      g_timeout_add_seconds (5, (GSourceFunc) close_bottom_sheet_and_refresh, self);
      update_app_list_threaded (self);
    }
  }
}

static void
cc_waydroid_panel_uninstall_app (GtkWidget *widget, CcWaydroidPanel *self)
{
  if (self->selected_app_pkgname != NULL) {
    gchar *title = g_strdup_printf (("Uninstall %s?"), self->selected_app_name);
    gchar *description = g_strdup_printf (("This will completely remove %s from your device. This action cannot be undone."), self->selected_app_name);

    show_confirmation_alert_dialog (self,
                                    title,
                                    description,
                                    ("Uninstall"),
                                    G_CALLBACK (on_remove_app_confirm));

    g_free (title);
    g_free (description);
  } else {
    show_toast (self, "No app selected");
  }
}

static gpointer
cc_waydroid_panel_launch_app (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;

  if (self->selected_app_pkgname != NULL && g_strstrip (self->selected_app_pkgname)[0] != '\0') {
    g_debug ("Launching Android application: %s", self->selected_app_pkgname);

    const gchar *home_dir = g_get_home_dir ();
    gchar *desktop_file_path = g_strdup_printf ("%s/.local/share/applications/waydroid.%s.desktop", home_dir, g_strstrip (self->selected_app_pkgname));
    gchar *launch_command = g_strdup_printf ("dex \"%s\"", desktop_file_path);

    g_spawn_command_line_async (launch_command, NULL);

    g_free (launch_command);
    g_free (desktop_file_path);
  }

  return NULL;
}

static void
cc_waydroid_panel_launch_app_threaded (GtkWidget *widget, CcWaydroidPanel *self)
{
  if (self->selected_app_pkgname != NULL)
    g_thread_new ("cc_waydroid_panel_launch_app", cc_waydroid_panel_launch_app, self);
  else
    show_toast (self, "No app selected");
}

static gboolean
update_version_idle (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;

  gtk_label_set_text (GTK_LABEL (self->waydroid_version_label), self->waydroid_version_output);

  return G_SOURCE_REMOVE;
}

static gpointer
update_waydroid_version (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;

  self->waydroid_version_output = waydroid_get_version ();

  g_idle_add (update_version_idle, self);

  return NULL;
}

static void
update_waydroid_version_threaded (CcWaydroidPanel *self)
{
  g_thread_new ("update_waydroid_version", update_waydroid_version, self);
}

static gboolean
check_nfc_idle (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;

  g_signal_handlers_block_by_func (self->waydroid_nfc_switch, cc_waydroid_panel_nfc, self);
  gtk_switch_set_state (GTK_SWITCH (self->waydroid_nfc_switch), self->waydroid_nfc_active);
  gtk_switch_set_active (GTK_SWITCH (self->waydroid_nfc_switch), self->waydroid_nfc_active);
  g_signal_handlers_unblock_by_func (self->waydroid_nfc_switch, cc_waydroid_panel_nfc, self);

  return G_SOURCE_REMOVE;
}

static gpointer
check_waydroid_nfc (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;

  self->waydroid_nfc_active = waydroid_get_nfc_status ();

  g_idle_add (check_nfc_idle, self);

  return NULL;
}

static void
check_waydroid_nfc_threaded (CcWaydroidPanel *self)
{
  g_thread_new ("check_waydroid_nfc", check_waydroid_nfc, self);
}

static gboolean
check_notification_idle (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;

  g_signal_handlers_block_by_func (self->waydroid_notification_switch, cc_waydroid_panel_notification, self);
  gtk_switch_set_state (GTK_SWITCH (self->waydroid_notification_switch), self->waydroid_notification_active);
  gtk_switch_set_active (GTK_SWITCH (self->waydroid_notification_switch), self->waydroid_notification_active);
  g_signal_handlers_unblock_by_func (self->waydroid_notification_switch, cc_waydroid_panel_notification, self);

  return G_SOURCE_REMOVE;
}

static gpointer
check_waydroid_notification (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;

  self->waydroid_notification_active = cc_is_service_active (WAYDROID_NOTIFICATION_SERVER_SERVICE, G_BUS_TYPE_SYSTEM);

  g_idle_add (check_notification_idle, self);

  return NULL;
}

static void
check_waydroid_notification_threaded (CcWaydroidPanel *self)
{
  g_thread_new ("check_waydroid_notification", check_waydroid_notification, self);
}

static gboolean
check_shared_folder_idle (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;

  g_signal_handlers_block_by_func (self->waydroid_shared_folder_switch, cc_waydroid_panel_shared_folder, self);
  gtk_switch_set_state (GTK_SWITCH (self->waydroid_shared_folder_switch), self->waydroid_shared_folder_enabled);
  gtk_switch_set_active (GTK_SWITCH (self->waydroid_shared_folder_switch), self->waydroid_shared_folder_enabled);
  g_signal_handlers_unblock_by_func (self->waydroid_shared_folder_switch, cc_waydroid_panel_shared_folder, self);

  return G_SOURCE_REMOVE;
}

static gpointer
check_waydroid_shared_folder (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;

  const gchar *home_dir = g_get_home_dir ();
  gchar *android_dir_path = g_strdup_printf ("%s/Android", home_dir);
  self->waydroid_shared_folder_enabled = is_mounted (android_dir_path);
  g_free (android_dir_path);

  g_idle_add (check_shared_folder_idle, self);

  return NULL;
}

static void
check_waydroid_shared_folder_threaded (CcWaydroidPanel *self)
{
  g_thread_new ("check_waydroid_shared_folder", check_waydroid_shared_folder, self);
}

static void
update_waydroid_info (CcWaydroidPanel *self)
{
  update_waydroid_ip_threaded (self);
  update_waydroid_version_threaded (self);
  update_app_list_threaded (self);
  check_waydroid_notification_threaded (self);
  check_waydroid_nfc_threaded (self);
  check_waydroid_shared_folder_threaded (self);
}

static gboolean
update_waydroid_info_idle (gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;
  update_waydroid_info (self);
  return G_SOURCE_REMOVE;
}

static void
on_clear_app_data_confirm (AdwAlertDialog *dialog, gchar *response, CcWaydroidPanel *self)
{
  if (g_strcmp0 (response, "confirm") == 0) {
    gchar *pkgname = self->selected_app_pkgname;
    if (pkgname != NULL) {
      set_widgets_state (self, FALSE);

      gchar *stripped_pkgname = g_strstrip (pkgname);
      waydroid_clear_app_data (stripped_pkgname);

      g_timeout_add_seconds (2, (GSourceFunc) close_bottom_sheet_and_refresh, self);
    }
  }
}

static void
on_kill_app_confirm (AdwAlertDialog *dialog, gchar *response, CcWaydroidPanel *self)
{
  if (g_strcmp0 (response, "confirm") == 0) {
    gchar *pkgname = self->selected_app_pkgname;
    if (pkgname != NULL) {
      set_widgets_state (self, FALSE);

      gchar *stripped_pkgname = g_strstrip (pkgname);
      waydroid_kill_app (stripped_pkgname);

      g_timeout_add_seconds (2, (GSourceFunc) close_bottom_sheet_and_refresh, self);
    }
  }
}

static void
cc_waydroid_refresh_button (GtkButton *button, gpointer user_data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) user_data;
  g_idle_add (update_waydroid_info_idle, self);
}

static void
install_app (CcWaydroidPanel *self, GFile *file)
{
  gchar *file_path = g_file_get_path (file);
  waydroid_install_app (file_path);

  g_free (file_path);

  update_app_list_threaded (self);
}

static void
on_file_chosen (GtkFileChooserNative *native, gint response_id, CcWaydroidPanel *self)
{
  if (response_id == GTK_RESPONSE_ACCEPT) {
    GFile *file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (native));
    if (file) {
      install_app (self, file);
      g_object_unref (file);
    }
  }

  gtk_native_dialog_destroy (GTK_NATIVE_DIALOG (native));
}

static void
cc_waydroid_panel_install_app (GtkWidget *widget, CcWaydroidPanel *self)
{
  GtkFileChooserNative *native = gtk_file_chooser_native_new ("Choose an APK",
                                                              GTK_WINDOW (gtk_widget_get_root (widget)),
                                                              GTK_FILE_CHOOSER_ACTION_OPEN,
                                                              "Open",
                                                              "Cancel");

  GtkFileFilter *filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "APK files");
  gtk_file_filter_add_pattern (filter, "*.apk");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (native), filter);

  g_signal_connect (native, "response", G_CALLBACK (on_file_chosen), self);

  gtk_native_dialog_show (GTK_NATIVE_DIALOG (native));
}

static void
cc_waydroid_panel_open_store (GtkButton *button, gpointer user_data)
{
  const gchar *home_dir = g_get_home_dir ();
  gchar *desktop_file_path = g_strdup_printf ("%s/.local/share/applications/waydroid.org.fdroid.fdroid.desktop", home_dir);
  gchar *launch_command = g_strdup_printf ("dex \"%s\"", desktop_file_path);

  g_spawn_command_line_async (launch_command, NULL);

  g_free (launch_command);
  g_free (desktop_file_path);
}

static void
cc_waydroid_panel_clear_app_data (GtkWidget *widget, CcWaydroidPanel *self)
{
  if (self->selected_app_pkgname != NULL) {
    gchar *title = g_strdup_printf (("Clear data for %s?"), self->selected_app_name);
    gchar *description = g_strdup_printf (("This will permanently delete all app data for %s. This action cannot be undone."), self->selected_app_name);

    show_confirmation_alert_dialog (self,
                                    title,
                                    description,
                                    ("Clear Data"),
                                    G_CALLBACK (on_clear_app_data_confirm));
    g_free (title);
    g_free (description);
  } else {
    show_toast (self, "No app selected");
  }
}

static void
cc_waydroid_panel_kill_app (GtkWidget *widget, CcWaydroidPanel *self)
{
  if (self->selected_app_pkgname != NULL) {
    gchar *title = g_strdup_printf (("Kill %s?"), self->selected_app_name);
    gchar *description = g_strdup_printf (("This will forcefully stop %s. Any unsaved data may be lost."), self->selected_app_name);

    show_confirmation_alert_dialog (self,
                                    title,
                                    description,
                                    ("Kill App"),
                                    G_CALLBACK (on_kill_app_confirm));

    g_free (title);
    g_free (description);
  } else {
    show_toast (self, "No app selected");
  }
}

static void
cc_waydroid_factory_reset_threaded (GtkWidget *widget, CcWaydroidPanel *self)
{
  GError *error = NULL;
  gchar *command = "rm -rf $HOME/.local/share/waydroid";
  gchar *home_env = g_strdup_printf ("HOME=%s", g_get_home_dir ());
  gchar *argv[] = {"pkexec", "env", home_env, "/bin/sh", "-c", command, NULL};

  if (!g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, NULL, &error)) {
    g_warning ("Error running command: %s", error->message);
    g_clear_error (&error);
  } else {
    gtk_widget_set_sensitive (GTK_WIDGET (self->factory_reset_button), FALSE);
    g_timeout_add_seconds (10, (GSourceFunc) gtk_widget_set_sensitive, self->factory_reset_button);
  }

  g_free (home_env);
}

static void
set_widgets_sensitive (gboolean sensitive, ...)
{
  va_list args;
  GtkWidget *widget;

  va_start (args, sensitive);
  while ((widget = va_arg (args, GtkWidget *))) {
    gtk_widget_set_sensitive (widget, sensitive);
  }
  va_end (args);
}

static gboolean
enable_idle (gpointer data)
{
  CcWaydroidPanel *self = (CcWaydroidPanel *) data;

  set_widgets_sensitive (TRUE,
                         GTK_WIDGET (self->waydroid_enabled_switch),
                         GTK_WIDGET (self->launch_app_button),
                         GTK_WIDGET (self->remove_app_button),
                         GTK_WIDGET (self->install_app_button),
                         GTK_WIDGET (self->app_selector),
                         GTK_WIDGET (self->store_button),
                         GTK_WIDGET (self->refresh_app_list_button),
                         GTK_WIDGET (self->waydroid_nfc_switch),
                         GTK_WIDGET (self->waydroid_notification_switch),
                         GTK_WIDGET (self->clear_app_data_button),
                         GTK_WIDGET (self->kill_app_button),
                         NULL);

  set_widgets_sensitive (FALSE,
                         GTK_WIDGET (self->factory_reset_button),
                         NULL);

  g_signal_connect (G_OBJECT (self->launch_app_button), "clicked", G_CALLBACK (cc_waydroid_panel_launch_app_threaded), self);
  g_signal_connect (G_OBJECT (self->remove_app_button), "clicked", G_CALLBACK (cc_waydroid_panel_uninstall_app), self);
  g_signal_connect (G_OBJECT (self->install_app_button), "clicked", G_CALLBACK (cc_waydroid_panel_install_app), self);
  g_signal_connect (G_OBJECT (self->store_button), "clicked", G_CALLBACK (cc_waydroid_panel_open_store), self);
  g_signal_connect (self->refresh_app_list_button, "clicked", G_CALLBACK (cc_waydroid_refresh_button), self);
  g_signal_connect (G_OBJECT (self->clear_app_data_button), "clicked", G_CALLBACK (cc_waydroid_panel_clear_app_data), self);
  g_signal_connect (G_OBJECT (self->kill_app_button), "clicked", G_CALLBACK (cc_waydroid_panel_kill_app), self);

  g_idle_add (update_waydroid_info_idle, self);

  return G_SOURCE_REMOVE;
}

static gboolean
cc_waydroid_panel_enable_waydroid (GtkSwitch *widget, gboolean state, CcWaydroidPanel *self)
{
  GError *error = NULL;

  if (state) {
    gchar *argv[] = { "waydroid", "session", "start", NULL };
    GPid child_pid;
    gint stdout_fd;

    if (!g_spawn_async_with_pipes (NULL, argv, NULL,
                                   G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                   NULL, NULL, &child_pid,
                                   NULL, &stdout_fd, NULL, &error)) {
      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);
      return FALSE;
    }

    gtk_widget_set_sensitive (GTK_WIDGET (self->waydroid_enabled_switch), FALSE);

    g_timeout_add_seconds (10, enable_idle, self);
  } else {
    gchar *argv[] = { "waydroid", "session", "stop", NULL };
    gint exit_status = 0;

    if (!g_spawn_sync (NULL, argv, NULL,
                       G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                       NULL, NULL, NULL, NULL, &exit_status, &error)) {
      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);
    }

    gtk_label_set_text (GTK_LABEL (self->waydroid_ip_label), "");
    gtk_label_set_text (GTK_LABEL (self->waydroid_version_label), "");

    set_widgets_sensitive (FALSE,
                           GTK_WIDGET (self->launch_app_button),
                           GTK_WIDGET (self->remove_app_button),
                           GTK_WIDGET (self->install_app_button),
                           GTK_WIDGET (self->app_selector),
                           GTK_WIDGET (self->store_button),
                           GTK_WIDGET (self->refresh_app_list_button),
                           GTK_WIDGET (self->waydroid_nfc_switch),
                           GTK_WIDGET (self->waydroid_notification_switch),
                           GTK_WIDGET (self->clear_app_data_button),
                           GTK_WIDGET (self->kill_app_button),
                           NULL);

    set_widgets_sensitive (TRUE,
                           GTK_WIDGET (self->factory_reset_button),
                           NULL);

    adw_expander_row_set_expanded (self->app_selector, FALSE);
  }

  return FALSE;
}

static void
cc_waydroid_panel_class_init (CcWaydroidPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_waydroid_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/waydroid/cc-waydroid-panel.ui");

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        toast_overlay);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        bottom_sheet);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_enabled_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_autostart_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_shared_folder_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_nfc_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_notification_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_ip_label);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        waydroid_version_label);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        app_selector);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        launch_app_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        remove_app_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        install_app_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        store_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        refresh_app_list_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        factory_reset_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        clear_app_data_button);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcWaydroidPanel,
                                        kill_app_button);
}

static void
cc_waydroid_panel_init (CcWaydroidPanel *self)
{
  g_resources_register (cc_waydroid_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  self->app_list_store = g_list_store_new (G_TYPE_APP_INFO);

  self->selected_app_name = NULL;
  self->selected_app_pkgname = NULL;
  self->waydroid_ip_output = NULL;
  self->waydroid_version_output = NULL;
  self->apps = NULL;
  self->waydroid_notification_active = FALSE;
  self->waydroid_nfc_active = FALSE;
  self->waydroid_shared_folder_enabled = FALSE;
  self->refreshing = FALSE;

  if (g_file_test ("/usr/bin/waydroid", G_FILE_TEST_EXISTS)) {
    g_signal_connect (G_OBJECT (self->waydroid_enabled_switch), "state-set", G_CALLBACK (cc_waydroid_panel_enable_waydroid), self);
    g_signal_connect (G_OBJECT (self->waydroid_autostart_switch), "state-set", G_CALLBACK (cc_waydroid_panel_autostart), self);
    g_signal_connect (G_OBJECT (self->waydroid_shared_folder_switch), "state-set", G_CALLBACK (cc_waydroid_panel_shared_folder), self);
    g_signal_connect (G_OBJECT (self->waydroid_nfc_switch), "state-set", G_CALLBACK (cc_waydroid_panel_nfc), self);
    g_signal_connect (G_OBJECT (self->waydroid_notification_switch), "state-set", G_CALLBACK (cc_waydroid_panel_notification), self);
    g_signal_connect (G_OBJECT (self->factory_reset_button), "clicked", G_CALLBACK (cc_waydroid_factory_reset_threaded), self);

    gchar *file_path = g_build_filename (g_get_home_dir (), ".android_enable", NULL);
    if (g_file_test (file_path, G_FILE_TEST_EXISTS)) {
      g_signal_handlers_block_by_func (self->waydroid_autostart_switch, cc_waydroid_panel_autostart, self);
      gtk_switch_set_state (GTK_SWITCH (self->waydroid_autostart_switch), TRUE);
      gtk_switch_set_active (GTK_SWITCH (self->waydroid_autostart_switch), TRUE);
      g_signal_handlers_unblock_by_func (self->waydroid_autostart_switch, cc_waydroid_panel_autostart, self);
    } else {
      g_signal_handlers_block_by_func (self->waydroid_autostart_switch, cc_waydroid_panel_autostart, self);
      gtk_switch_set_state (GTK_SWITCH (self->waydroid_autostart_switch), FALSE);
      gtk_switch_set_active (GTK_SWITCH (self->waydroid_autostart_switch), FALSE);
      g_signal_handlers_unblock_by_func (self->waydroid_autostart_switch, cc_waydroid_panel_autostart, self);
    }

    g_free (file_path);

    gchar *current_state = waydroid_get_state ();

    if (current_state != NULL && g_strcmp0 (current_state, "RUNNING") == 0) {
      g_signal_handlers_block_by_func (self->waydroid_enabled_switch, cc_waydroid_panel_enable_waydroid, self);
      gtk_switch_set_state (GTK_SWITCH (self->waydroid_enabled_switch), TRUE);
      gtk_switch_set_active (GTK_SWITCH (self->waydroid_enabled_switch), TRUE);
      g_signal_handlers_unblock_by_func (self->waydroid_enabled_switch, cc_waydroid_panel_enable_waydroid, self);

      set_widgets_sensitive (FALSE,
                             GTK_WIDGET (self->factory_reset_button),
                             NULL);

      g_signal_connect (G_OBJECT (self->launch_app_button), "clicked", G_CALLBACK (cc_waydroid_panel_launch_app_threaded), self);
      g_signal_connect (G_OBJECT (self->remove_app_button), "clicked", G_CALLBACK (cc_waydroid_panel_uninstall_app), self);
      g_signal_connect (G_OBJECT (self->install_app_button), "clicked", G_CALLBACK (cc_waydroid_panel_install_app), self);
      g_signal_connect (G_OBJECT (self->store_button), "clicked", G_CALLBACK (cc_waydroid_panel_open_store), self);
      g_signal_connect (self->refresh_app_list_button, "clicked", G_CALLBACK (cc_waydroid_refresh_button), self);
      g_signal_connect (G_OBJECT (self->clear_app_data_button), "clicked", G_CALLBACK (cc_waydroid_panel_clear_app_data), self);
      g_signal_connect (G_OBJECT (self->kill_app_button), "clicked", G_CALLBACK (cc_waydroid_panel_kill_app), self);

      g_idle_add (update_waydroid_info_idle, self);
    } else {
      g_signal_handlers_block_by_func (self->waydroid_enabled_switch, cc_waydroid_panel_enable_waydroid, self);
      gtk_switch_set_state (GTK_SWITCH (self->waydroid_enabled_switch), FALSE);
      gtk_switch_set_active (GTK_SWITCH (self->waydroid_enabled_switch), FALSE);
      g_signal_handlers_unblock_by_func (self->waydroid_enabled_switch, cc_waydroid_panel_enable_waydroid, self);
      gtk_label_set_text (GTK_LABEL (self->waydroid_version_label), "");

      set_widgets_sensitive (FALSE,
                             GTK_WIDGET (self->waydroid_nfc_switch),
                             GTK_WIDGET (self->waydroid_notification_switch),
                             GTK_WIDGET (self->launch_app_button),
                             GTK_WIDGET (self->remove_app_button),
                             GTK_WIDGET (self->install_app_button),
                             GTK_WIDGET (self->app_selector),
                             GTK_WIDGET (self->store_button),
                             GTK_WIDGET (self->refresh_app_list_button),
                             GTK_WIDGET (self->clear_app_data_button),
                             GTK_WIDGET (self->kill_app_button),
                             NULL);
    }

    g_free (current_state);
  } else {
    gtk_switch_set_state (GTK_SWITCH (self->waydroid_enabled_switch), FALSE);
    gtk_switch_set_active (GTK_SWITCH (self->waydroid_enabled_switch), FALSE);

    set_widgets_sensitive (FALSE,
                           GTK_WIDGET (self->waydroid_enabled_switch),
                           GTK_WIDGET (self->waydroid_autostart_switch),
                           GTK_WIDGET (self->waydroid_shared_folder_switch),
                           GTK_WIDGET (self->waydroid_nfc_switch),
                           GTK_WIDGET (self->waydroid_notification_switch),
                           GTK_WIDGET (self->launch_app_button),
                           GTK_WIDGET (self->remove_app_button),
                           GTK_WIDGET (self->install_app_button),
                           GTK_WIDGET (self->app_selector),
                           GTK_WIDGET (self->store_button),
                           GTK_WIDGET (self->refresh_app_list_button),
                           GTK_WIDGET (self->factory_reset_button),
                           GTK_WIDGET (self->clear_app_data_button),
                           GTK_WIDGET (self->kill_app_button),
                           NULL);

    gtk_label_set_text (GTK_LABEL (self->waydroid_version_label), "");
  }
}

CcWaydroidPanel *
cc_waydroid_panel_new (void)
{
  return CC_WAYDROID_PANEL (g_object_new (CC_TYPE_WAYDROID_PANEL, NULL));
}
