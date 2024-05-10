/*
 * Copyright (C) 2024 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <shell/cc-panel.h>

G_BEGIN_DECLS

#define CC_TYPE_GPS_PANEL (cc_gps_panel_get_type ())
G_DECLARE_FINAL_TYPE (CcGpsPanel, cc_gps_panel, CC, GPS_PANEL, CcPanel)

CcGpsPanel *cc_gps_panel_new (void);

G_END_DECLS
