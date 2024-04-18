/*
 * Copyright (C) 2025 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <shell/cc-panel.h>

G_BEGIN_DECLS

#define CC_TYPE_ANDROMEDA_PANEL (cc_andromeda_panel_get_type ())
G_DECLARE_FINAL_TYPE (CcAndromedaPanel, cc_andromeda_panel, CC, ANDROMEDA_PANEL, CcPanel)

CcAndromedaPanel *cc_andromeda_panel_new (void);

G_END_DECLS
