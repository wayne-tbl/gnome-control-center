/*
 * Copyright (C) 2023 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-usb-panel.h"
#include "cc-usb-resources.h"
#include "cc-util.h"

#include "panels/nfc/cc-systemd-service.h"

#define USBCONFIG_DBUS_NAME            "io.FuriOS.USBConfig"
#define USBCONFIG_DBUS_PATH            "/io/FuriOS/USBConfig"
#define USBCONFIG_DBUS_INTERFACE       "io.FuriOS.USBConfig"

#define POWERCONFIG_DBUS_NAME          "io.FuriOS.BatmanPowerConfig"
#define POWERCONFIG_DBUS_PATH          "/io/FuriOS/BatmanPowerConfig"
#define POWERCONFIG_DBUS_INTERFACE     "io.FuriOS.BatmanPowerConfig"

#define MTP_SERVER_SERVICE             "mtp-server.service"

struct _CcUsbPanel {
  CcPanel            parent;
  GtkWidget        *cdrom_enabled_switch;
  GtkWidget        *iso_selection_switch;
  GtkWidget        *iso_label;
  GtkWidget        *usb_state_mtp;
  GtkWidget        *usb_state_rndis;
  GtkWidget        *usb_state_none;
  GtkWidget        *power_role_sink;
  GtkWidget        *power_role_source;
  char             *path;
};

G_DEFINE_TYPE (CcUsbPanel, cc_usb_panel, CC_TYPE_PANEL)

static void
cc_usb_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_usb_panel_parent_class)->finalize (object);
}

static void
usb_set_mode (const char *mode)
{
  GDBusProxy *usbconfig_proxy;
  GError *error = NULL;

  usbconfig_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    USBCONFIG_DBUS_NAME,
    USBCONFIG_DBUS_PATH,
    USBCONFIG_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return;
  }

  g_dbus_proxy_call(
    usbconfig_proxy,
    "SetUSBMode",
    g_variant_new("(s)", mode),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    NULL,
    NULL
  );

  g_object_unref (usbconfig_proxy);
}

static char *
usb_get_current_state (void)
{
  GDBusProxy *usbconfig_proxy;
  GError *error = NULL;
  GVariant *result;
  char *current_state = NULL;

  usbconfig_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    USBCONFIG_DBUS_NAME,
    USBCONFIG_DBUS_PATH,
    USBCONFIG_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return NULL;
  }

  result = g_dbus_proxy_call_sync(
    usbconfig_proxy,
    "org.freedesktop.DBus.Properties.Get",
    g_variant_new ("(ss)", USBCONFIG_DBUS_INTERFACE, "CurrentState"),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling method: %s\n", error->message);
    g_clear_error (&error);
    g_object_unref (usbconfig_proxy);
    return NULL;
  }

  if (result) {
    GVariant *state_variant;
    g_variant_get (result, "(v)", &state_variant);
    current_state = g_strdup (g_variant_get_string (state_variant, NULL));
    g_variant_unref (state_variant);
    g_variant_unref (result);
  }

  g_object_unref (usbconfig_proxy);
  return current_state;
}

static void
powerconfig_set (const char *method, const char *mode)
{
  GDBusProxy *powerconfig_proxy;
  GError *error = NULL;

  powerconfig_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    POWERCONFIG_DBUS_NAME,
    POWERCONFIG_DBUS_PATH,
    POWERCONFIG_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return;
  }

  g_dbus_proxy_call(
    powerconfig_proxy,
    method,
    g_variant_new("(s)", mode),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    NULL,
    NULL
  );

  g_object_unref (powerconfig_proxy);
}

static char *
powerconfig_get (const char *prop)
{
  GDBusProxy *powerconfig_proxy;
  GError *error = NULL;
  GVariant *result;
  char *power_role = NULL;

  powerconfig_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    POWERCONFIG_DBUS_NAME,
    POWERCONFIG_DBUS_PATH,
    POWERCONFIG_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return NULL;
  }

  result = g_dbus_proxy_call_sync(
    powerconfig_proxy,
    "org.freedesktop.DBus.Properties.Get",
    g_variant_new ("(ss)", POWERCONFIG_DBUS_INTERFACE, prop),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling method: %s\n", error->message);
    g_clear_error (&error);
    g_object_unref (powerconfig_proxy);
    return NULL;
  }

  if (result) {
    GVariant *role_variant;
    g_variant_get (result, "(v)", &role_variant);
    power_role = g_strdup (g_variant_get_string (role_variant, NULL));
    g_variant_unref (role_variant);
    g_variant_unref (result);
  }

  g_object_unref (powerconfig_proxy);
  return power_role;
}

static void
cc_usb_panel_enable_mtp (CcUsbPanel *self, gboolean state)
{
  GError *error = NULL;
  const gchar *home_dir = g_get_home_dir ();
  gchar *filepath = g_build_filename (home_dir, ".mtp_disable", NULL);
  GFile *file = g_file_new_for_path (filepath);

  if (state) {
    if (!g_file_delete (file, NULL, &error)) {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_warning ("Error deleting %s: %s", filepath, error->message);
      g_clear_error (&error);
    }
    cc_start_service (MTP_SERVER_SERVICE, G_BUS_TYPE_SESSION, &error);
  } else {
    cc_stop_service (MTP_SERVER_SERVICE, G_BUS_TYPE_SESSION, &error);
    if (!g_file_query_exists (file, NULL)) {
      GFileOutputStream *output_stream = g_file_create (file, G_FILE_CREATE_NONE, NULL, &error);
      if (output_stream != NULL) {
        g_output_stream_close (G_OUTPUT_STREAM (output_stream), NULL, NULL);
        g_object_unref (output_stream);
      } else {
        g_warning ("Error creating %s: %s", filepath, error->message);
        g_clear_error (&error);
      }
    }
  }

  g_object_unref (file);
  g_free (filepath);

  if (error != NULL) {
    g_warning ("Failed to toggle mtp server service: %s", error->message);
    g_error_free (error);
  }
}

static void
cc_usb_panel_usb_state_changed (GtkCheckButton *button, CcUsbPanel *self)
{
  const gchar *selected_mode;
  gboolean mtp_enabled = FALSE;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (self->usb_state_mtp))) {
    selected_mode = "mtp";
    mtp_enabled = TRUE;
  } else if (gtk_check_button_get_active (GTK_CHECK_BUTTON (self->usb_state_rndis))) {
    selected_mode = "rndis";
    mtp_enabled = FALSE;
  } else {
    selected_mode = "none";
    mtp_enabled = FALSE;
  }

  g_debug ("Selected USB state: %s", selected_mode);
  usb_set_mode (selected_mode);
  cc_usb_panel_enable_mtp (self, mtp_enabled);
}

static void
cc_usb_panel_power_role_changed (GtkCheckButton *button, CcUsbPanel *self)
{
  const gchar *selected_role;

  if (gtk_check_button_get_active (GTK_CHECK_BUTTON (self->power_role_sink)))
    selected_role = "sink";
  else if (gtk_check_button_get_active (GTK_CHECK_BUTTON (self->power_role_source)))
    selected_role = "source";
  else
    return;

  g_debug ("Selected USB Power Role: %s", selected_role);
  powerconfig_set ("SetPowerRole", selected_role);
  powerconfig_set ("SetPreferredRole", selected_role);
}

static void
cc_usb_panel_enable_cdrom (GtkSwitch *widget, gboolean state, CcUsbPanel *self)
{
  if (state) {
    g_debug ("Mounting cdrom: %s", self->path);
    gchar *argv[] = {"pkexec", "isodrive", self->path, "-cdrom", NULL};
    g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, NULL, NULL);
  } else {
    g_debug ("Unmounting cdrom");
    gchar *argv[] = {"pkexec", "isodrive", "umount", NULL};
    g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, NULL, NULL);
  }

  gtk_switch_set_state (GTK_SWITCH (self->cdrom_enabled_switch), state);
  gtk_switch_set_active (GTK_SWITCH (self->cdrom_enabled_switch), state);
}

static void
on_file_chosen (GtkFileChooserNative *native, gint response_id, CcUsbPanel *self)
{
  if (response_id == GTK_RESPONSE_ACCEPT) {
    GFile *file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (native));
    char *path = g_file_get_path (file);
    if (file) {
      g_free (self->path);
      g_strchomp (path);
      char *basename = g_path_get_basename (path);
      self->path = g_strdup (path);
      gtk_widget_set_sensitive (GTK_WIDGET (self->cdrom_enabled_switch), TRUE);
      gtk_label_set_text (GTK_LABEL (self->iso_label), basename);
      g_free (path);
      g_free (basename);
    }
    g_object_unref (file);
  }

  gtk_native_dialog_destroy (GTK_NATIVE_DIALOG (native));
}

static void
cc_usb_panel_select_iso (GtkWidget *widget, CcUsbPanel *self)
{
  GtkFileChooserNative *native = gtk_file_chooser_native_new ("Choose an ISO",
                                                              GTK_WINDOW (gtk_widget_get_root (widget)),
                                                              GTK_FILE_CHOOSER_ACTION_OPEN,
                                                              "Open",
                                                              "Cancel");

  GtkFileFilter *filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "ISO files");
  gtk_file_filter_add_mime_type (filter, "application/vnd.efi.iso");
  gtk_file_filter_add_mime_type (filter, "application/vnd.efi.img");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (native), filter);

  g_signal_connect (native, "response", G_CALLBACK (on_file_chosen), self);

  gtk_native_dialog_show (GTK_NATIVE_DIALOG (native));
}

static void
cc_usb_panel_class_init (CcUsbPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_usb_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/usb/cc-usb-panel.ui");

  gtk_widget_class_bind_template_child (widget_class,
                                        CcUsbPanel,
                                        cdrom_enabled_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcUsbPanel,
                                        iso_selection_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcUsbPanel,
                                        iso_label);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcUsbPanel,
                                        usb_state_mtp);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcUsbPanel,
                                        usb_state_rndis);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcUsbPanel,
                                        usb_state_none);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcUsbPanel,
                                        power_role_sink);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcUsbPanel,
                                        power_role_source);
}

static void
cc_usb_panel_init (CcUsbPanel *self)
{
  g_resources_register (cc_usb_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  gboolean mtp_supported = g_file_test ("/usr/lib/droidian/device/mtp-supported", G_FILE_TEST_EXISTS);

  if (!mtp_supported) {
    gtk_widget_set_sensitive (GTK_WIDGET (self->usb_state_mtp), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->usb_state_rndis), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->usb_state_none), FALSE);
  }

  char *current_state = usb_get_current_state ();
  if (current_state) {
    g_signal_connect (G_OBJECT (self->usb_state_mtp), "toggled", G_CALLBACK (cc_usb_panel_usb_state_changed), self);
    g_signal_connect (G_OBJECT (self->usb_state_rndis), "toggled", G_CALLBACK (cc_usb_panel_usb_state_changed), self);
    g_signal_connect (G_OBJECT (self->usb_state_none), "toggled", G_CALLBACK (cc_usb_panel_usb_state_changed), self);

    g_signal_handlers_block_by_func (self->usb_state_mtp, cc_usb_panel_usb_state_changed, self);
    g_signal_handlers_block_by_func (self->usb_state_rndis, cc_usb_panel_usb_state_changed, self);
    g_signal_handlers_block_by_func (self->usb_state_none, cc_usb_panel_usb_state_changed, self);

    if (g_strcmp0 (current_state, "mtp") == 0)
      gtk_check_button_set_active (GTK_CHECK_BUTTON (self->usb_state_mtp), TRUE);
    else if (g_strcmp0 (current_state, "rndis") == 0)
      gtk_check_button_set_active (GTK_CHECK_BUTTON (self->usb_state_rndis), TRUE);
    else
      gtk_check_button_set_active (GTK_CHECK_BUTTON (self->usb_state_none), TRUE);

    g_signal_handlers_unblock_by_func (self->usb_state_mtp, cc_usb_panel_usb_state_changed, self);
    g_signal_handlers_unblock_by_func (self->usb_state_rndis, cc_usb_panel_usb_state_changed, self);
    g_signal_handlers_unblock_by_func (self->usb_state_none, cc_usb_panel_usb_state_changed, self);

    g_free (current_state);
  } else {
    g_debug ("Failed to get CurrentState from USBConfig, marking as unavailable");
    gtk_widget_set_sensitive (GTK_WIDGET (self->usb_state_mtp), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->usb_state_rndis), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->usb_state_none), FALSE);
  }

  char *preferred_role = powerconfig_get ("PreferredRole");

  if (preferred_role) {
    g_signal_connect (G_OBJECT (self->power_role_sink), "toggled", G_CALLBACK (cc_usb_panel_power_role_changed), self);
    g_signal_connect (G_OBJECT (self->power_role_source), "toggled", G_CALLBACK (cc_usb_panel_power_role_changed), self);

    g_signal_handlers_block_by_func (self->power_role_sink, cc_usb_panel_power_role_changed, self);
    g_signal_handlers_block_by_func (self->power_role_source, cc_usb_panel_power_role_changed, self);

    if (g_strcmp0 (preferred_role, "sink") == 0)
      gtk_check_button_set_active (GTK_CHECK_BUTTON (self->power_role_sink), TRUE);
    else if (g_strcmp0 (preferred_role, "source") == 0)
      gtk_check_button_set_active (GTK_CHECK_BUTTON (self->power_role_source), TRUE);

    g_signal_handlers_unblock_by_func (self->power_role_sink, cc_usb_panel_power_role_changed, self);
    g_signal_handlers_unblock_by_func (self->power_role_source, cc_usb_panel_power_role_changed, self);

    g_free (preferred_role);
  } else {
    g_debug ("Failed to get PreferredRole from PowerConfig, marking as unavailable");
    gtk_widget_set_sensitive (GTK_WIDGET (self->power_role_sink), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->power_role_source), FALSE);
  }

  gtk_widget_set_sensitive (GTK_WIDGET (self->cdrom_enabled_switch), FALSE);
  if (g_file_test ("/usr/bin/isodrive", G_FILE_TEST_EXISTS)) {
    g_signal_connect (G_OBJECT (self->cdrom_enabled_switch), "state-set", G_CALLBACK (cc_usb_panel_enable_cdrom), self);
    g_signal_connect (G_OBJECT (self->iso_selection_switch), "clicked", G_CALLBACK (cc_usb_panel_select_iso), self);
    if (g_file_test ("/sys/kernel/config/usb_gadget/g1/functions/mass_storage.0/lun.0/cdrom", G_FILE_TEST_EXISTS)) {
      gchar *content = NULL;
      if (g_file_get_contents ("/sys/kernel/config/usb_gadget/g1/functions/mass_storage.0/lun.0/cdrom", &content, NULL, NULL)) {
        int cdrom_status = atoi (content);
        g_free (content);
        if (cdrom_status == 1) {
          g_signal_handlers_block_by_func (self->cdrom_enabled_switch, cc_usb_panel_enable_cdrom, self);
          gtk_switch_set_state (GTK_SWITCH (self->cdrom_enabled_switch), TRUE);
          gtk_switch_set_active (GTK_SWITCH (self->cdrom_enabled_switch), TRUE);
          gtk_widget_set_sensitive (GTK_WIDGET (self->cdrom_enabled_switch), TRUE);
          g_signal_handlers_unblock_by_func (self->cdrom_enabled_switch, cc_usb_panel_enable_cdrom, self);
          if (g_file_test ("/sys/kernel/config/usb_gadget/g1/functions/mass_storage.0/lun.0/file", G_FILE_TEST_EXISTS)) {
            gchar *content = NULL;
            if (g_file_get_contents ("/sys/kernel/config/usb_gadget/g1/functions/mass_storage.0/lun.0/file", &content, NULL, NULL)) {
              g_strchomp (content);
              char *basename = g_path_get_basename (content);
              gtk_label_set_text (GTK_LABEL (self->iso_label), basename);
              self->path = g_strdup (basename);
              g_free (content);
              g_free (basename);
            }
          }
        }
      }
    }
  } else
    gtk_widget_set_sensitive (GTK_WIDGET (self->iso_selection_switch), FALSE);
}

CcUsbPanel *
cc_usb_panel_new (void)
{
  return CC_USB_PANEL (g_object_new (CC_TYPE_USB_PANEL, NULL));
}
