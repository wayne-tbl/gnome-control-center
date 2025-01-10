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
#include "cc-wwan-device.h"

#define MMSD_SERVICE  "mmsd.service"

typedef struct {
    CcWwanMmsDialog *dialog;
    char *proxy;
    char *center;
    char *apn;
    gboolean success;
    int pending_ops;
} SaveContext;

struct _CcWwanMmsDialog
{
  GtkDialog          parent_instance;

  CcWwanDevice      *device;
  gchar             *port_name;
  gchar             *mms_context_path;

  GtkButton         *save_button;
  AdwEntryRow       *message_proxy_row;
  AdwEntryRow       *message_center_row;
  AdwEntryRow       *access_point_row;
};

G_DEFINE_TYPE (CcWwanMmsDialog, cc_wwan_mms_dialog, GTK_TYPE_DIALOG)

static void
save_context_free (SaveContext *ctx)
{
    if (!ctx)
        return;
    g_free (ctx->proxy);
    g_free (ctx->center);
    g_free (ctx->apn);
    g_free (ctx);
}

static void
on_start_service_ready (GDBusConnection *connection,
                        GAsyncResult    *res,
                        gpointer         user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(GVariant) result = NULL;
  g_autoptr(GError) error = NULL;

  result = g_dbus_connection_call_finish (connection, res, &error);
  if (!result) {
    g_debug ("Failed to start service: %s", error->message);
  }

  /* Always return success to continue the operation */
  g_task_return_boolean (task, TRUE);
}

static void
cc_start_service_async (const char          *service,
                        GBusType             bus_type,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
  GTask *task;
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GError) error = NULL;

  task = g_task_new (NULL, cancellable, callback, user_data);

  connection = g_bus_get_sync (bus_type, cancellable, &error);
  if (!connection) {
    g_debug ("Failed connecting to D-Bus system bus: %s",
             error ? error->message : "unknown error");
    g_task_return_boolean (task, TRUE); /* Continue anyway */
    g_object_unref (task);
    return;
  }

  g_dbus_connection_call (connection,
                          "org.freedesktop.systemd1",
                          "/org/freedesktop/systemd1",
                          "org.freedesktop.systemd1.Manager",
                          "StartUnit",
                          g_variant_new ("(ss)", service, "replace"),
                          (GVariantType *) "(o)",
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          cancellable,
                          (GAsyncReadyCallback) on_start_service_ready,
                          task);
}

static gboolean
cc_start_service_finish (GAsyncResult  *res,
                         GError       **error)
{
  g_return_val_if_fail (g_task_is_valid (res, NULL), FALSE);
  return g_task_propagate_boolean (G_TASK (res), error);
}

static void
on_stop_service_ready (GDBusConnection *connection,
                       GAsyncResult    *res,
                       gpointer         user_data)
{
  g_autoptr(GTask) task = user_data;
  g_autoptr(GVariant) result = NULL;
  g_autoptr(GError) error = NULL;

  result = g_dbus_connection_call_finish (connection, res, &error);
  if (!result)
    g_debug ("Failed to stop service: %s", error->message);

  /* Always return success to continue the operation */
  g_task_return_boolean (task, TRUE);
}

static void
cc_stop_service_async (const char          *service,
                       GBusType             bus_type,
                       GCancellable        *cancellable,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
  GTask *task;
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GError) error = NULL;

  task = g_task_new (NULL, cancellable, callback, user_data);

  connection = g_bus_get_sync (bus_type, cancellable, &error);
  if (!connection) {
    g_debug ("Failed connecting to D-Bus system bus: %s",
             error ? error->message : "unknown error");
    g_task_return_boolean (task, TRUE); /* Continue anyway */
    g_object_unref (task);
    return;
  }

  g_dbus_connection_call (connection,
                          "org.freedesktop.systemd1",
                          "/org/freedesktop/systemd1",
                          "org.freedesktop.systemd1.Manager",
                          "StopUnit",
                          g_variant_new ("(ss)", service, "replace"),
                          (GVariantType *) "(o)",
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          cancellable,
                          (GAsyncReadyCallback) on_stop_service_ready,
                          task);
}

static gboolean
cc_stop_service_finish (GAsyncResult  *res,
                        GError       **error)
{
  g_return_val_if_fail (g_task_is_valid (res, NULL), FALSE);
  return g_task_propagate_boolean (G_TASK (res), error);
}

static void
on_property_set (GDBusProxy    *proxy,
                 GAsyncResult  *res,
                 gpointer       user_data)
{
    GTask *task = G_TASK (user_data);
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) result = NULL;

    result = g_dbus_proxy_call_finish (proxy, res, &error);
    if (error) {
        g_debug ("Failed to set property: %s", error->message);
        g_task_return_boolean (task, FALSE);
    } else {
        g_task_return_boolean (task, TRUE);
    }
}

static void
on_proxy_ready (GObject       *source_object,
                GAsyncResult  *res,
                gpointer       user_data)
{
    GTask *task = G_TASK (user_data);
    GVariant *value = g_task_get_task_data (task);
    const char *property = g_object_get_data (G_OBJECT (task), "property");
    g_autoptr(GError) error = NULL;
    g_autoptr(GDBusProxy) proxy = NULL;

    proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
    if (!proxy) {
        g_debug ("Failed to create proxy: %s", error->message);
        g_task_return_boolean (task, FALSE);
        return;
    }

    g_dbus_proxy_call (proxy,
                       "SetProperty",
                       g_variant_new ("(sv)", property, value),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       g_task_get_cancellable (task),
                       (GAsyncReadyCallback) on_property_set,
                       task);
}

static void
set_mms_context_property_async (CcWwanMmsDialog     *self,
                                const char          *property,
                                GVariant            *value,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
    GTask *task;

    task = g_task_new (self, cancellable, callback, user_data);

    if (!self || !self->mms_context_path) {
        g_debug ("Cannot set property %s: invalid context path", property);
        g_task_return_boolean (task, FALSE);
        g_object_unref (task);
        return;
    }

    g_object_set_data_full (G_OBJECT (task), "property", g_strdup (property), g_free);
    g_task_set_task_data (task, g_variant_ref (value), (GDestroyNotify) g_variant_unref);

    g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                              G_DBUS_PROXY_FLAGS_NONE,
                              NULL,
                              "org.ofono",
                              self->mms_context_path,
                              "org.ofono.ConnectionContext",
                              cancellable,
                              on_proxy_ready,
                              task);
}

static gboolean
set_mms_context_property_finish (GAsyncResult  *res,
                                 GError       **error)
{
    return g_task_propagate_boolean (G_TASK (res), error);
}

static void
on_final_activate (SaveContext *ctx)
{
    CcWwanMmsDialog *self = ctx->dialog;

    if (!ctx->success) {
        g_autoptr(GtkAlertDialog) dialog = NULL;
        dialog = gtk_alert_dialog_new ("Failed to save MMS settings");
        gtk_alert_dialog_set_detail (GTK_ALERT_DIALOG (dialog),
                                     "One or more settings could not be saved. Please check your input and try again.");
        gtk_alert_dialog_show (GTK_ALERT_DIALOG (dialog), GTK_WINDOW (self));
    }

    gtk_window_close (GTK_WINDOW (self));
    save_context_free (ctx);
}

static void
on_service_restarted (GObject       *source_object,
                      GAsyncResult  *res,
                      gpointer       user_data)
{
    SaveContext *ctx = user_data;
    g_autoptr(GError) error = NULL;

    if (!cc_start_service_finish (res, &error)) {
        g_debug ("Failed to restart MMSD service: %s", error->message);
        ctx->success = FALSE;
    }

    on_final_activate (ctx);
}

static void
on_property_changed (GObject       *source_object,
                     GAsyncResult  *res,
                     gpointer       user_data)
{
    SaveContext *ctx = user_data;
    g_autoptr(GError) error = NULL;

    if (!set_mms_context_property_finish (res, &error)) {
        g_debug ("Failed to set property: %s", error->message);
        ctx->success = FALSE;
    }

    ctx->pending_ops--;
    if (ctx->pending_ops == 0) {
        cc_start_service_async (MMSD_SERVICE,
                                G_BUS_TYPE_SESSION,
                                NULL,
                                on_service_restarted,
                                ctx);
    }
}

static char *
find_mms_context_path (const char *modem_path)
{
  g_autoptr(GDBusProxy) proxy = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) contexts = NULL;
  g_autoptr(GVariantIter) iter = NULL;
  g_autofree char *found_path = NULL;
  char *context_path;

  if (!modem_path)
    return NULL;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         "org.ofono",
                                         modem_path,
                                         "org.ofono.ConnectionManager",
                                         NULL,
                                         &error);

  if (error) {
    g_debug ("Failed to create proxy for ConnectionManager: %s", error->message);
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
    g_debug ("Failed to get contexts: %s", error->message);
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
save_settings_and_restart (SaveContext *ctx)
{
    CcWwanMmsDialog *self = ctx->dialog;
    g_autoptr(GVariant) value = NULL;

    if (!self) {
        g_debug ("Invalid dialog context");
        on_final_activate (ctx);
        return;
    }

    ctx->pending_ops = 0;

    if (ctx->proxy && *ctx->proxy) {
        value = g_variant_new_string (ctx->proxy);
        ctx->pending_ops++;
        set_mms_context_property_async (self, "MessageProxy", value,
                                        NULL, on_property_changed, ctx);
    }

    if (ctx->center && *ctx->center) {
        value = g_variant_new_string (ctx->center);
        ctx->pending_ops++;
        set_mms_context_property_async (self, "MessageCenter", value,
                                        NULL, on_property_changed, ctx);
    }

    if (ctx->apn && *ctx->apn) {
        value = g_variant_new_string (ctx->apn);
        ctx->pending_ops++;
        set_mms_context_property_async (self, "AccessPointName", value,
                                        NULL, on_property_changed, ctx);
    }

    /* If no properties to set, continue directly */
    if (ctx->pending_ops == 0) {
        cc_start_service_async (MMSD_SERVICE,
                                G_BUS_TYPE_SESSION,
                                NULL,
                                on_service_restarted,
                                ctx);
    }
}

static void
on_context_deactivated (GObject       *source_object,
                        GAsyncResult  *res,
                        gpointer       user_data)
{
    SaveContext *ctx = user_data;
    g_autoptr(GError) error = NULL;

    if (!set_mms_context_property_finish (res, &error)) {
        g_debug ("Failed to deactivate context: %s", error->message);
        ctx->success = FALSE;
    }

    save_settings_and_restart (ctx);
}

static void
on_service_stopped (GObject      *source_object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
    SaveContext *ctx = user_data;
    CcWwanMmsDialog *self = ctx->dialog;
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) value = NULL;

    if (!cc_stop_service_finish (res, &error)) {
        g_debug ("Failed to stop MMSD service: %s", error->message);
        ctx->success = FALSE;
    }

    value = g_variant_new_boolean (FALSE);
    set_mms_context_property_async (self, "Active", value,
                                    NULL, on_context_deactivated, ctx);
}

static void
on_save_clicked (GtkButton       *button,
                 CcWwanMmsDialog *self)
{
    SaveContext *ctx;

    ctx = g_new0 (SaveContext, 1);
    ctx->dialog = self;
    ctx->success = TRUE;
    ctx->proxy = g_strdup (gtk_editable_get_text (GTK_EDITABLE (self->message_proxy_row)));
    ctx->center = g_strdup (gtk_editable_get_text (GTK_EDITABLE (self->message_center_row)));
    ctx->apn = g_strdup (gtk_editable_get_text (GTK_EDITABLE (self->access_point_row)));

    cc_stop_service_async (MMSD_SERVICE,
                           G_BUS_TYPE_SESSION,
                           NULL,
                           on_service_stopped,
                           ctx);
}

static void
load_current_settings (CcWwanMmsDialog *self)
{
  g_autoptr(GDBusProxy) proxy = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) properties = NULL;
  g_autoptr(GVariant) value = NULL;
  const char *str_value;

  if (!self->mms_context_path)
    return;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         "org.ofono",
                                         self->mms_context_path,
                                         "org.ofono.ConnectionContext",
                                         NULL,
                                         &error);
  if (!proxy) {
    g_debug ("Failed to create proxy: %s", error ? error->message : "unknown error");
    return;
  }

  properties = g_dbus_proxy_call_sync (proxy,
                                       "GetProperties",
                                       NULL,
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1,
                                       NULL,
                                       &error);
  if (!properties) {
    g_debug ("Failed to get properties: %s", error ? error->message : "unknown error");
    return;
  }

  g_autoptr(GVariant) dict = g_variant_get_child_value (properties, 0);

  value = g_variant_lookup_value (dict, "MessageProxy", G_VARIANT_TYPE_STRING);
  if (value) {
    str_value = g_variant_get_string (value, NULL);
    gtk_editable_set_text (GTK_EDITABLE (self->message_proxy_row), str_value);
    g_clear_pointer (&value, g_variant_unref);
  }

  value = g_variant_lookup_value (dict, "MessageCenter", G_VARIANT_TYPE_STRING);
  if (value) {
    str_value = g_variant_get_string (value, NULL);
    gtk_editable_set_text (GTK_EDITABLE (self->message_center_row), str_value);
    g_clear_pointer (&value, g_variant_unref);
  }

  value = g_variant_lookup_value (dict, "AccessPointName", G_VARIANT_TYPE_STRING);
  if (value) {
    str_value = g_variant_get_string (value, NULL);
    gtk_editable_set_text (GTK_EDITABLE (self->access_point_row), str_value);
  }
}

static void
cc_wwan_mms_dialog_dispose (GObject *object)
{
  CcWwanMmsDialog *self = CC_WWAN_MMS_DIALOG (object);

  g_clear_pointer (&self->port_name, g_free);
  g_clear_pointer (&self->mms_context_path, g_free);
  g_clear_object (&self->device);

  G_OBJECT_CLASS (cc_wwan_mms_dialog_parent_class)->dispose (object);
}

static void
cc_wwan_mms_dialog_class_init (CcWwanMmsDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = cc_wwan_mms_dialog_dispose;

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
  g_signal_connect (self->save_button, "clicked", G_CALLBACK (on_save_clicked), self);
}

static void
cc_wwan_mms_dialog_setup (CcWwanMmsDialog *self)
{
  self->port_name = cc_wwan_device_get_primary_port (self->device);
  if (!self->port_name)
    return;

  self->mms_context_path = find_mms_context_path (self->port_name);
  if (self->mms_context_path)
    load_current_settings (self);
}

GtkWindow *
cc_wwan_mms_dialog_new (GtkWindow    *parent_window,
                        CcWwanDevice *device)
{
  CcWwanMmsDialog *dialog;

  g_return_val_if_fail (GTK_IS_WINDOW (parent_window), NULL);
  g_return_val_if_fail (CC_IS_WWAN_DEVICE (device), NULL);

  dialog = g_object_new (CC_TYPE_WWAN_MMS_DIALOG,
                         "transient-for", parent_window,
                         "use-header-bar", 1,
                         NULL);

  dialog->device = g_object_ref (device);
  cc_wwan_mms_dialog_setup (dialog);

  return GTK_WINDOW (dialog);
}
