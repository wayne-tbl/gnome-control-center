/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* cc-wwan-mms-dialog.c
 *
 * Copyright 2025 Furi Labs
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author(s):
 *   Bardia Moshiri <bardia@furilabs.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "cc-wwan-mms-dialog"

#include <config.h>
#include <glib/gi18n.h>
#include <libmm-glib/libmm-glib.h>

#include "cc-wwan-mms-dialog.h"
#include "cc-wwan-resources.h"

struct _CcWwanMmsDialog
{
  GtkDialog          parent_instance;

  gchar             *port_name;
  gchar             *mms_context_path;
  GDBusProxy        *mmsd_proxy;
  GDBusProxy        *context_proxy;

  gchar             *current_proxy;
  gchar             *current_center;
  gchar             *current_apn;

  GtkButton         *save_button;
  AdwEntryRow       *message_proxy_row;
  AdwEntryRow       *message_center_row;
  AdwEntryRow       *access_point_row;

  gint               pending_operations;
};

G_DEFINE_TYPE (CcWwanMmsDialog, cc_wwan_mms_dialog, GTK_TYPE_DIALOG)

static char *
find_mms_context_path (const char *modem_path)
{
  g_autoptr(GDBusProxy) proxy = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) contexts = NULL;
  g_autoptr(GVariantIter) iter = NULL;
  g_autofree char *found_path = NULL;
  char *context_path;

  if (!modem_path || !g_variant_is_object_path (modem_path)) {
    g_warning ("Invalid modem path");
    return NULL;
  }

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         "org.ofono",
                                         modem_path,
                                         "org.ofono.ConnectionManager",
                                         NULL,
                                         &error);

  if (error) {
    g_warning ("Failed to create proxy for ConnectionManager: %s", error->message);
    return NULL;
  }

  contexts = g_dbus_proxy_call_sync (proxy,
                                     "GetContexts",
                                     NULL,
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     &error);

  if (error) {
    g_warning ("Failed to get contexts: %s", error->message);
    return NULL;
  }

  g_variant_get (contexts, "(a(oa{sv}))", &iter);

  while (g_variant_iter_loop (iter, "(&oa{sv})", &context_path, NULL)) {
    g_autoptr(GDBusProxy) context_proxy = NULL;
    g_autoptr(GVariant) type_value = NULL;
    g_autoptr(GVariant) properties = NULL;
    g_autoptr(GVariant) type_variant = NULL;
    const char *type;

    context_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                   G_DBUS_PROXY_FLAGS_NONE,
                                                   NULL,
                                                   "org.ofono",
                                                   context_path,
                                                   "org.ofono.ConnectionContext",
                                                   NULL,
                                                   NULL);

    if (!context_proxy)
      continue;

    type_value = g_dbus_proxy_call_sync (context_proxy,
                                         "GetProperties",
                                         NULL,
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         &error);
    if (!type_value) {
      g_clear_error (&error);
      continue;
    }

    properties = g_variant_get_child_value (type_value, 0);
    type_variant = g_variant_lookup_value (properties, "Type", G_VARIANT_TYPE_STRING);

    if (!type_variant)
      continue;

    type = g_variant_get_string (type_variant, NULL);
    if (g_strcmp0 (type, "mms") == 0) {
      found_path = g_strdup (context_path);
      break;
    }
  }

  return g_steal_pointer (&found_path);
}

static void
on_property_set (GObject      *source_object,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  CcWwanMmsDialog *self = CC_WWAN_MMS_DIALOG (user_data);
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) result = NULL;

  result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);
  if (error) {
    g_warning ("Failed to set MMS property: %s", error->message);

    g_autoptr(GtkAlertDialog) dialog = gtk_alert_dialog_new (_("Failed to save MMS settings"));
    gtk_alert_dialog_set_detail (GTK_ALERT_DIALOG (dialog),
                                 _("One or more settings could not be saved. Please check your input and try again."));
    gtk_alert_dialog_show (GTK_ALERT_DIALOG (dialog), GTK_WINDOW (self));
  }

  self->pending_operations--;
  if (self->pending_operations <= 0) {
    self->pending_operations = 0; /* Safety: ensure it never goes negative */
    gtk_widget_set_sensitive (GTK_WIDGET (self->save_button), TRUE);
    if (!error)
      gtk_window_close (GTK_WINDOW (self));
  }
}

static void
on_properties_received (GObject      *source_object,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  CcWwanMmsDialog *self = CC_WWAN_MMS_DIALOG (user_data);
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) properties = NULL;
  g_autoptr(GVariant) dict = NULL;
  g_autoptr(GVariant) value = NULL;
  const char *str_value;

  properties = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);
  if (!properties) {
    g_warning ("Failed to get properties: %s", error->message);
    return;
  }

  dict = g_variant_get_child_value (properties, 0);

  value = g_variant_lookup_value (dict, "MessageProxy", G_VARIANT_TYPE_STRING);
  if (value) {
    str_value = g_variant_get_string (value, NULL);
    gtk_editable_set_text (GTK_EDITABLE (self->message_proxy_row), str_value);
    g_free (self->current_proxy);
    self->current_proxy = g_strdup (str_value);
    g_clear_pointer (&value, g_variant_unref);
  }

  value = g_variant_lookup_value (dict, "MessageCenter", G_VARIANT_TYPE_STRING);
  if (value) {
    str_value = g_variant_get_string (value, NULL);
    gtk_editable_set_text (GTK_EDITABLE (self->message_center_row), str_value);
    g_free (self->current_center);
    self->current_center = g_strdup (str_value);
    g_clear_pointer (&value, g_variant_unref);
  }

  value = g_variant_lookup_value (dict, "AccessPointName", G_VARIANT_TYPE_STRING);
  if (value) {
    str_value = g_variant_get_string (value, NULL);
    gtk_editable_set_text (GTK_EDITABLE (self->access_point_row), str_value);
    g_free (self->current_apn);
    self->current_apn = g_strdup (str_value);
  }
}

static void
on_context_proxy_acquired (GObject      *source_object,
                           GAsyncResult *res,
                           gpointer      user_data)
{
  CcWwanMmsDialog *self = CC_WWAN_MMS_DIALOG (user_data);
  g_autoptr(GError) error = NULL;

  self->context_proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
  if (error) {
    g_warning ("Failed to create context proxy: %s", error->message);
    return;
  }

  /* Get the initial properties */
  g_dbus_proxy_call (self->context_proxy,
                     "GetProperties",
                     NULL,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     on_properties_received,
                     self);
}

static void
on_mmsd_proxy_acquired (GObject      *source_object,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  CcWwanMmsDialog *self = CC_WWAN_MMS_DIALOG (user_data);
  g_autoptr(GError) error = NULL;

  self->mmsd_proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
  if (error) {
    g_warning ("Failed to create MMSD proxy: %s", error->message);
    return;
  }
}

static void
setup_context_proxy (CcWwanMmsDialog *self)
{
  if (!self || !self->mms_context_path || !g_variant_is_object_path (self->mms_context_path)) {
    g_warning ("Invalid state for context proxy setup");
    return;
  }

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            "org.ofono",
                            self->mms_context_path,
                            "org.ofono.ConnectionContext",
                            NULL,
                            on_context_proxy_acquired,
                            self);
}

static void
setup_mmsd_proxy (CcWwanMmsDialog *self)
{
  if (!self->mms_context_path) {
    g_warning ("No MMS context path available");
    return;
  }

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            "org.ofono.mms",
                            "/org/ofono/mms",
                            "org.ofono.mms.Manager",
                            NULL,
                            on_mmsd_proxy_acquired,
                            self);
}

static void
set_mms_context_properties_async (CcWwanMmsDialog *self,
                                  GVariant        *properties)
{
  if (!self->mmsd_proxy)
    return;

  self->pending_operations = 1;

  g_dbus_proxy_call (self->mmsd_proxy,
                     "SetMMSContextProperties",
                     g_variant_new_tuple (&properties, 1),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     on_property_set,
                     self);
}

static void
on_save_clicked (GtkButton       *button,
                 CcWwanMmsDialog *self)
{
  const char *proxy, *center, *apn;
  gboolean changes_made = FALSE;
  GVariantBuilder builder;

  self->pending_operations = 0;
  gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);

  proxy = gtk_editable_get_text (GTK_EDITABLE (self->message_proxy_row));
  center = gtk_editable_get_text (GTK_EDITABLE (self->message_center_row));
  apn = gtk_editable_get_text (GTK_EDITABLE (self->access_point_row));

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));

  if (g_strcmp0 (proxy, self->current_proxy) != 0) {
    g_variant_builder_add (&builder, "{ss}", "MessageProxy", proxy);
    changes_made = TRUE;
  }

  if (g_strcmp0 (center, self->current_center) != 0) {
    g_variant_builder_add (&builder, "{ss}", "MessageCenter", center);
    changes_made = TRUE;
  }

  if (g_strcmp0 (apn, self->current_apn) != 0) {
    g_variant_builder_add (&builder, "{ss}", "AccessPointName", apn);
    changes_made = TRUE;
  }

  if (changes_made) {
    GVariant *properties = g_variant_builder_end (&builder);
    set_mms_context_properties_async (self, properties);
    g_variant_unref (properties);
  } else {
    gtk_widget_set_sensitive (GTK_WIDGET (button), TRUE);
    gtk_window_close (GTK_WINDOW (self));
    g_variant_builder_clear (&builder);
  }
}

static void
cc_wwan_mms_dialog_dispose (GObject *object)
{
  CcWwanMmsDialog *self = CC_WWAN_MMS_DIALOG (object);

  g_clear_object (&self->mmsd_proxy);
  g_clear_object (&self->context_proxy);
  g_clear_pointer (&self->current_proxy, g_free);
  g_clear_pointer (&self->current_center, g_free);
  g_clear_pointer (&self->current_apn, g_free);
  g_clear_pointer (&self->port_name, g_free);
  g_clear_pointer (&self->mms_context_path, g_free);

  G_OBJECT_CLASS (cc_wwan_mms_dialog_parent_class)->dispose (object);
}

static void
cc_wwan_mms_dialog_finalize (GObject *object)
{
  CcWwanMmsDialog *self = CC_WWAN_MMS_DIALOG (object);

  /* Ensure we're fully cleaned up */
  g_clear_object (&self->mmsd_proxy);
  g_clear_object (&self->context_proxy);
  g_clear_pointer (&self->current_proxy, g_free);
  g_clear_pointer (&self->current_center, g_free);
  g_clear_pointer (&self->current_apn, g_free);
  g_clear_pointer (&self->port_name, g_free);
  g_clear_pointer (&self->mms_context_path, g_free);

  G_OBJECT_CLASS (cc_wwan_mms_dialog_parent_class)->finalize (object);
}

static void
cc_wwan_mms_dialog_class_init (CcWwanMmsDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_wwan_mms_dialog_dispose;
  object_class->finalize = cc_wwan_mms_dialog_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/wwan/cc-wwan-mms-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, CcWwanMmsDialog, save_button);
  gtk_widget_class_bind_template_child (widget_class, CcWwanMmsDialog, message_proxy_row);
  gtk_widget_class_bind_template_child (widget_class, CcWwanMmsDialog, message_center_row);
  gtk_widget_class_bind_template_child (widget_class, CcWwanMmsDialog, access_point_row);
}

static void
cc_wwan_mms_dialog_init (CcWwanMmsDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->current_proxy = NULL;
  self->current_center = NULL;
  self->current_apn = NULL;
  self->mmsd_proxy = NULL;
  self->context_proxy = NULL;
  self->port_name = NULL;
  self->mms_context_path = NULL;
  self->pending_operations = 0;

  g_signal_connect (self->save_button, "clicked", G_CALLBACK (on_save_clicked), self);
}

static void
cc_wwan_mms_dialog_setup (CcWwanMmsDialog *self,
                          const gchar     *port_name)
{
  g_return_if_fail (CC_IS_WWAN_MMS_DIALOG (self));
  g_return_if_fail (port_name != NULL);

  self->port_name = g_strdup (port_name);
  if (!g_variant_is_object_path (self->port_name)) {
    g_warning ("Invalid modem path: %s", self->port_name);
    return;
  }

  /* Then find the MMS context path */
  self->mms_context_path = find_mms_context_path (self->port_name);
  if (!self->mms_context_path) {
    g_warning ("Failed to find MMS context path");
    return;
  }

  /* Set up both proxies */
  setup_context_proxy (self);
  setup_mmsd_proxy (self);
}

GtkWindow *
cc_wwan_mms_dialog_new (GtkWindow   *parent_window,
                        const gchar *port_name)
{
  CcWwanMmsDialog *dialog;

  g_return_val_if_fail (GTK_IS_WINDOW (parent_window), NULL);
  g_return_val_if_fail (port_name != NULL, NULL);

  dialog = g_object_new (CC_TYPE_WWAN_MMS_DIALOG,
                         "transient-for", parent_window,
                         "use-header-bar", 1,
                         NULL);

  cc_wwan_mms_dialog_setup (dialog, port_name);

  return GTK_WINDOW (dialog);
}
