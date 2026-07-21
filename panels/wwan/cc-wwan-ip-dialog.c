/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* cc-wwan-ip-dialog.c
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
#define G_LOG_DOMAIN "cc-wwan-ip-dialog"

#include <config.h>
#include <glib/gi18n.h>

#include "cc-wwan-ip-dialog.h"
#include "cc-wwan-resources.h"

typedef enum {
  IP_PROTOCOL_IPV4 = 1,
  IP_PROTOCOL_IPV6 = 2
} IpProtocol;

struct _CcWwanIpDialog
{
  GtkDialog          parent_instance;

  CcWwanDevice      *device;
  GDBusProxy        *modem_proxy;
  GDBusProxy        *mmsd_proxy;

  GtkCheckButton    *ip_version_ipv4;
  GtkCheckButton    *ip_version_ipv6;
};

G_DEFINE_TYPE (CcWwanIpDialog, cc_wwan_ip_dialog, GTK_TYPE_DIALOG)

enum {
  PROP_0,
  PROP_DEVICE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
on_set_protocol_ready (GObject      *source_object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) ret = NULL;

  ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), result, &error);

  if (error)
    {
      g_warning ("Failed to set protocol: %s", error->message);
      return;
    }

  g_debug ("Protocol set successfully");
}

static void
on_set_mms_protocol_ready (GObject      *source_object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) ret = NULL;

  ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), result, &error);

  if (error)
    {
      g_warning ("Failed to set MMS context protocol: %s", error->message);
      return;
    }

  g_debug ("MMS context protocol set successfully");
}

static void
set_mms_context_protocol (CcWwanIpDialog *self,
                          IpProtocol      protocol)
{
  GVariantBuilder builder;
  GVariant *properties;
  const gchar *mms_protocol;

  if (!self->mmsd_proxy)
    {
      g_debug ("MMSD proxy is not available");
      return;
    }

  if (!g_dbus_proxy_get_name_owner (self->mmsd_proxy))
    {
      g_debug ("MMSD is not running");
      return;
    }

  if (protocol == IP_PROTOCOL_IPV4)
    mms_protocol = "ip";
  else if (protocol == IP_PROTOCOL_IPV6)
    mms_protocol = "dual";
  else
    {
      g_warning ("Unknown protocol value: %u", protocol);
      return;
    }

  g_debug ("Setting MMS context Protocol to %s", mms_protocol);

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&builder, "{ss}", "Protocol", mms_protocol);

  properties = g_variant_builder_end (&builder);

  g_dbus_proxy_call (self->mmsd_proxy,
                     "SetMMSContextProperties",
                     g_variant_new_tuple (&properties, 1),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     on_set_mms_protocol_ready,
                     self);
}

static void
sync_mms_context_protocol (CcWwanIpDialog *self)
{
  IpProtocol protocol;

  if (gtk_check_button_get_active (self->ip_version_ipv4))
    protocol = IP_PROTOCOL_IPV4;
  else if (gtk_check_button_get_active (self->ip_version_ipv6))
    protocol = IP_PROTOCOL_IPV6;
  else
    return;

  set_mms_context_protocol (self, protocol);
}

static void
on_ip_version_changed (GtkCheckButton *button,
                       gpointer        user_data)
{
  CcWwanIpDialog *self = CC_WWAN_IP_DIALOG (user_data);
  IpProtocol protocol;

  if (!gtk_check_button_get_active (button))
    return;

  if (gtk_check_button_get_active (self->ip_version_ipv4))
    {
      g_debug ("IPv4 selected");
      protocol = IP_PROTOCOL_IPV4;
    }
  else if (gtk_check_button_get_active (self->ip_version_ipv6))
    {
      g_debug ("IPv6 selected");
      protocol = IP_PROTOCOL_IPV6;
    }
  else
    {
      g_warning ("No IP version button is active");
      return;
    }

  g_dbus_proxy_call (self->modem_proxy,
                     "SetProtocol",
                     g_variant_new ("(u)", protocol),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     on_set_protocol_ready,
                     self);

  set_mms_context_protocol (self, protocol);
}

static void
on_mmsd_name_owner_changed (GDBusProxy     *proxy,
                            GParamSpec     *pspec,
                            CcWwanIpDialog *self)
{
  if (!g_dbus_proxy_get_name_owner (proxy))
    {
      g_debug ("MMSD is not running");
      return;
    }

  g_debug ("MMSD became available");

  sync_mms_context_protocol (self);
}

static void
on_mmsd_proxy_ready (GObject      *source_object,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  CcWwanIpDialog *self = CC_WWAN_IP_DIALOG (user_data);
  g_autoptr(GError) error = NULL;

  self->mmsd_proxy = g_dbus_proxy_new_for_bus_finish (result, &error);

  if (error)
    {
      g_debug ("Failed to create MMSD proxy: %s", error->message);
      return;
    }

  g_debug ("MMSD proxy created successfully");

  g_signal_connect (self->mmsd_proxy, "notify::g-name-owner",
                    G_CALLBACK (on_mmsd_name_owner_changed), self);

  if (!g_dbus_proxy_get_name_owner (self->mmsd_proxy))
    {
      g_debug ("MMSD is not running");
      return;
    }

  sync_mms_context_protocol (self);
}

static void
on_modem_proxy_ready (GObject      *source_object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  CcWwanIpDialog *self = CC_WWAN_IP_DIALOG (user_data);
  g_autoptr(GError) error = NULL;
  GVariant *protocol_variant;
  guint32 protocol;

  self->modem_proxy = g_dbus_proxy_new_finish (result, &error);

  if (error)
    {
      g_warning ("Failed to create modem proxy: %s", error->message);
      return;
    }

  g_debug ("Modem proxy created successfully");

  protocol_variant = g_dbus_proxy_get_cached_property (self->modem_proxy, "Protocol");

  if (protocol_variant)
    {
      protocol = g_variant_get_uint32 (protocol_variant);
      g_debug ("Protocol: %u", protocol);

      if (protocol == IP_PROTOCOL_IPV4)
        {
          g_debug ("Setting IPv4 as active");
          gtk_check_button_set_active (self->ip_version_ipv4, TRUE);
        }
      else if (protocol == IP_PROTOCOL_IPV6)
        {
          g_debug ("Setting IPv6 as active");
          gtk_check_button_set_active (self->ip_version_ipv6, TRUE);
        }
      else
        {
          g_warning ("Unknown protocol value: %u, defaulting to IPv4", protocol);
          gtk_check_button_set_active (self->ip_version_ipv4, TRUE);
        }

      g_variant_unref (protocol_variant);
    }
  else
    {
      g_warning ("Failed to read Protocol property, defaulting to IPv4");
      gtk_check_button_set_active (self->ip_version_ipv4, TRUE);
    }

  g_signal_connect (self->ip_version_ipv4, "toggled",
                    G_CALLBACK (on_ip_version_changed), self);
  g_signal_connect (self->ip_version_ipv6, "toggled",
                    G_CALLBACK (on_ip_version_changed), self);

  sync_mms_context_protocol (self);
}

static void
cc_wwan_ip_dialog_constructed (GObject *object)
{
  CcWwanIpDialog *self = CC_WWAN_IP_DIALOG (object);

  G_OBJECT_CLASS (cc_wwan_ip_dialog_parent_class)->constructed (object);

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            "org.freedesktop.ModemManager1",
                            cc_wwan_device_get_path (self->device),
                            "org.freedesktop.ModemManager1.Modem",
                            NULL,
                            on_modem_proxy_ready,
                            self);

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                            NULL,
                            "org.ofono.mms",
                            "/org/ofono/mms",
                            "org.ofono.mms.Manager",
                            NULL,
                            on_mmsd_proxy_ready,
                            self);
}

static void
cc_wwan_ip_dialog_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  CcWwanIpDialog *self = CC_WWAN_IP_DIALOG (object);

  switch (prop_id)
    {
    case PROP_DEVICE:
      g_set_object (&self->device, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
cc_wwan_ip_dialog_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  CcWwanIpDialog *self = CC_WWAN_IP_DIALOG (object);

  switch (prop_id)
    {
    case PROP_DEVICE:
      g_value_set_object (value, self->device);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
cc_wwan_ip_dialog_dispose (GObject *object)
{
  CcWwanIpDialog *self = CC_WWAN_IP_DIALOG (object);

  g_clear_object (&self->device);
  g_clear_object (&self->modem_proxy);
  g_clear_object (&self->mmsd_proxy);

  G_OBJECT_CLASS (cc_wwan_ip_dialog_parent_class)->dispose (object);
}

static void
cc_wwan_ip_dialog_class_init (CcWwanIpDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_wwan_ip_dialog_dispose;
  object_class->get_property = cc_wwan_ip_dialog_get_property;
  object_class->set_property = cc_wwan_ip_dialog_set_property;
  object_class->constructed = cc_wwan_ip_dialog_constructed;

  properties[PROP_DEVICE] =
    g_param_spec_object ("device",
                         "Device",
                         "The WWAN Device",
                         CC_TYPE_WWAN_DEVICE,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/wwan/cc-wwan-ip-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, CcWwanIpDialog, ip_version_ipv4);
  gtk_widget_class_bind_template_child (widget_class, CcWwanIpDialog, ip_version_ipv6);
}

static void
cc_wwan_ip_dialog_init (CcWwanIpDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWindow *
cc_wwan_ip_dialog_new (GtkWindow    *parent_window,
                       CcWwanDevice *device)
{
  g_return_val_if_fail (GTK_IS_WINDOW (parent_window), NULL);
  g_return_val_if_fail (CC_IS_WWAN_DEVICE (device), NULL);

  return g_object_new (CC_TYPE_WWAN_IP_DIALOG,
                       "transient-for", parent_window,
                       "use-header-bar", 1,
                       "device", device,
                       NULL);
}
