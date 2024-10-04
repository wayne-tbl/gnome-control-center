/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* cc-wwan-ims.h
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

#ifndef CC_WWAN_IMS_H
#define CC_WWAN_IMS_H

#include <glib.h>

G_BEGIN_DECLS

gboolean cc_wwan_ims_check_registered (const gchar *port_name);
gboolean cc_wwan_ims_check_voice_capable (const gchar *port_name);
gboolean cc_wwan_ims_check_sms_capable (const gchar *port_name);

G_END_DECLS

#endif /* CC_WWAN_IMS_H */
