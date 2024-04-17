/*
 * Copyright (C) 2024 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-nfc-panel.h"
#include "cc-nfc-resources.h"
#include "cc-util.h"

#include "cc-systemd-service.h"
#include "panels/system/cc-systemd-service.h"

#define ANDROMEDA_SESSION_DBUS_NAME          "io.furios.Andromeda.Session"
#define ANDROMEDA_SESSION_DBUS_PATH          "/SessionManager"
#define ANDROMEDA_SESSION_DBUS_INTERFACE     "io.furios.Andromeda.SessionManager"

#define NFCD_SERVICE                        "nfcd.service"

struct _CcNfcPanel {
  CcPanel            parent;
  GtkStack          *stack;
  GtkWidget         *nfc_enabled_switch;
  GtkWidget         *content_box;
  AdwStatusPage     *status_page;
};

G_DEFINE_TYPE (CcNfcPanel, cc_nfc_panel, CC_TYPE_PANEL)

static void
cc_nfc_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_nfc_panel_parent_class)->finalize (object);
}

static gboolean
cc_nfc_panel_enable_nfc (CcNfcPanel *self, gboolean state, GtkSwitch *widget)
{
  GError *error = NULL;

  const gchar *home_dir = g_get_home_dir ();
  gchar *filepath = g_strdup_printf ("%s/.nfc_disable", home_dir);

  g_signal_handlers_block_by_func (self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);

  if (state) {
    if (unlink (filepath) != 0)
      g_printerr ("Error deleting ~/.nfc_disable");

    cc_start_service (NFCD_SERVICE, G_BUS_TYPE_SYSTEM, &error);
  } else {
    FILE *file = fopen (filepath, "w");
    if (file != NULL)
      fclose (file);
    else
      g_printerr ("Error creating ~/.nfc_disable");

    cc_stop_service (NFCD_SERVICE, G_BUS_TYPE_SYSTEM, &error);
  }

  gtk_switch_set_state (GTK_SWITCH (self->nfc_enabled_switch), state);
  gtk_switch_set_active (GTK_SWITCH (self->nfc_enabled_switch), state);

  g_signal_handlers_unblock_by_func (self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);

  g_free (filepath);

  if (error != NULL) {
    g_printerr ("Failed to toggle NFC service: %s\n", error->message);
    g_error_free (error);

    return FALSE;
  }

  return TRUE;
}

static gboolean
ping_andromeda (void)
{
  GDBusProxy *andromeda_proxy;
  GError *error = NULL;

  andromeda_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    ANDROMEDA_SESSION_DBUS_NAME,
    ANDROMEDA_SESSION_DBUS_PATH,
    ANDROMEDA_SESSION_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  g_dbus_proxy_call_sync(
    andromeda_proxy,
    "Ping",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_clear_error (&error);
    g_object_unref (andromeda_proxy);
    return FALSE;
  }

  g_object_unref (andromeda_proxy);

  return TRUE;
}

static void
cc_nfc_panel_class_init (CcNfcPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_nfc_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/nfc/cc-nfc-panel.ui");

  gtk_widget_class_bind_template_child (widget_class,
                                        CcNfcPanel,
                                        stack);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcNfcPanel,
                                        nfc_enabled_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcNfcPanel,
                                        content_box);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcNfcPanel,
                                        status_page);
}

static void
cc_nfc_panel_init (CcNfcPanel *self)
{
  g_resources_register (cc_nfc_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  gboolean andromeda_active = ping_andromeda ();
  gboolean nfcd_installed = g_file_test ("/usr/sbin/nfcd", G_FILE_TEST_EXISTS);

  if (andromeda_active) {
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "status");
    adw_status_page_set_icon_name (self->status_page, "dialog-warning-symbolic");
    adw_status_page_set_title (self->status_page, ("NFC Unavailable"));
    adw_status_page_set_description (self->status_page, ("NFC is not available while Android is running"));
  } else if (!nfcd_installed) {
    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "status");
    adw_status_page_set_icon_name (self->status_page, "dialog-warning-symbolic");
    adw_status_page_set_title (self->status_page, ("NFC Not Installed"));
    adw_status_page_set_description (self->status_page, ("The NFC service is not installed on this system"));
  } else {
    g_signal_connect_swapped (G_OBJECT (self->nfc_enabled_switch), "state-set", G_CALLBACK (cc_nfc_panel_enable_nfc), self);

    gboolean active = cc_is_service_active (NFCD_SERVICE, G_BUS_TYPE_SYSTEM);
    g_signal_handlers_block_by_func (self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);
    gtk_switch_set_state (GTK_SWITCH (self->nfc_enabled_switch), active);
    gtk_switch_set_active (GTK_SWITCH (self->nfc_enabled_switch), active);
    g_signal_handlers_unblock_by_func (self->nfc_enabled_switch, cc_nfc_panel_enable_nfc, self);
  }
}

CcNfcPanel *
cc_nfc_panel_new (void)
{
  return CC_NFC_PANEL (g_object_new (CC_TYPE_NFC_PANEL, NULL));
}
