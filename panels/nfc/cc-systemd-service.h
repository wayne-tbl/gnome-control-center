/*
 * Copyright (C) 2022 Red Hat, Inc
 * Copyright (C) 2024 FuriLabs

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Authors: Bardia Moshiri <bardia@furilabs.com>
 */

#pragma once

#include <gio/gio.h>

gboolean cc_is_service_active (const char  *service,
                               GBusType     bus_type);

gboolean cc_start_service (const char  *service,
                           GBusType     bus_type,
                           GError     **error);

gboolean cc_stop_service (const char  *service,
                          GBusType     bus_type,
                          GError     **error);
