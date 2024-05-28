/*
 * Copyright (C) 2024 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <shell/cc-panel.h>

G_BEGIN_DECLS

#define CC_TYPE_DISPLAY_CUSTOMIZATION (cc_display_customization_get_type ())
G_DECLARE_FINAL_TYPE (CcDisplayCustomization, cc_display_customization, CC, DISPLAY_CUSTOMIZATION, CcPanel)

CcDisplayCustomization *cc_display_customization_new (void);

G_END_DECLS
