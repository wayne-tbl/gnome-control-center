/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* cc-wwan-ims.c
 *
 * Copyright 2024 Furi Labs
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

#include <gio/gio.h>
#include "cc-wwan-ims.h"

static GDBusProxy *
get_ims_proxy_for_port (const gchar *port_name, GError **error)
{
  GDBusConnection *connection = NULL;
  GDBusProxy *ims_proxy = NULL;
  GVariant *result = NULL;
  gchar *matched_path = NULL;

  g_return_val_if_fail (port_name != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
  if (!connection)
    return NULL;

  result = g_dbus_connection_call_sync (connection,
                                        "org.ofono",
                                        "/",
                                        "org.ofono.Manager",
                                        "GetModems",
                                        NULL,
                                        G_VARIANT_TYPE ("(a(oa{sv}))"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        error);

  if (!result)
    {
      g_object_unref (connection);
      return NULL;
    }

  GVariantIter iter;
  const gchar *path;
  GVariant *properties;
  GVariant *modems = g_variant_get_child_value (result, 0);

  if (!modems)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Invalid response from GetModems");
      g_variant_unref (result);
      g_object_unref (connection);
      return NULL;
    }

  g_variant_iter_init (&iter, modems);
  while (g_variant_iter_next (&iter, "(&oa{sv})", &path, &properties))
    {
      if (g_str_has_suffix (path, port_name))
        {
          matched_path = g_strdup (path);
          g_variant_unref (properties);
          break;
        }
      g_variant_unref (properties);
    }

  g_variant_unref (modems);
  g_variant_unref (result);

  if (matched_path)
    {
      if (g_variant_is_object_path (matched_path))
        {
          ims_proxy = g_dbus_proxy_new_sync (connection,
                                             G_DBUS_PROXY_FLAGS_NONE,
                                             NULL,
                                             "org.ofono",
                                             matched_path,
                                             "org.ofono.IpMultimediaSystem",
                                             NULL,
                                             error);
        }
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                       "Invalid object path: %s", matched_path);
        }
      g_free (matched_path);
    }
  else
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "No modem found for port %s", port_name);
    }

  g_object_unref (connection);

  return ims_proxy;
}

static gboolean
get_ims_property_boolean (const gchar *port_name,
                          const gchar *property_name,
                          gboolean *value,
                          GError **error)
{
  GDBusProxy *proxy;
  GVariant *result;
  GVariant *properties;
  gboolean success = FALSE;

  g_return_val_if_fail (port_name != NULL, FALSE);
  g_return_val_if_fail (property_name != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  proxy = get_ims_proxy_for_port (port_name, error);
  if (!proxy)
    return FALSE;

  result = g_dbus_proxy_call_sync (proxy,
                                   "GetProperties",
                                   NULL,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   NULL,
                                   error);

  if (result)
    {
      properties = g_variant_get_child_value (result, 0);
      success = g_variant_lookup (properties, property_name, "b", value);
      if (!success)
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                     "Property '%s' not found", property_name);
      g_variant_unref (properties);
      g_variant_unref (result);
    }
  else
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to get IMS properties");
    }

  g_object_unref (proxy);
  return success;
}

gboolean
cc_wwan_ims_check_registered (const gchar *port_name)
{
  gboolean registered = FALSE;
  GError *error = NULL;

  g_return_val_if_fail (port_name != NULL, FALSE);

  if (!get_ims_property_boolean (port_name, "Registered", &registered, &error))
    {
      if (error)
        {
          g_warning ("Failed to get IMS registration status: %s", error->message);
          g_error_free (error);
        }
      return FALSE;
    }

  return registered;
}

gboolean
cc_wwan_ims_check_voice_capable (const gchar *port_name)
{
  gboolean voice_capable = FALSE;
  GError *error = NULL;

  if (!get_ims_property_boolean (port_name, "VoiceCapable", &voice_capable, &error))
    {
      g_warning ("Failed to get IMS voice capability status: %s", error->message);
      g_error_free (error);
      return FALSE;
    }

  return voice_capable;
}

gboolean
cc_wwan_ims_check_sms_capable (const gchar *port_name)
{
  gboolean sms_capable = FALSE;
  GError *error = NULL;

  if (!get_ims_property_boolean (port_name, "SmsCapable", &sms_capable, &error))
    {
      g_warning ("Failed to get IMS SMS capability status: %s", error->message);
      g_error_free (error);
      return FALSE;
    }

  return sms_capable;
}
