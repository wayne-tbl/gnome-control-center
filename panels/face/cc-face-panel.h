/*
 * Copyright (C) 2026 Bardia Moshiri <bardia@furilabs.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <shell/cc-panel.h>

G_BEGIN_DECLS

#define CC_TYPE_FACE_PANEL (cc_face_panel_get_type ())
G_DECLARE_FINAL_TYPE (CcFacePanel, cc_face_panel, CC, FACE_PANEL, CcPanel)

CcFacePanel
*cc_face_panel_new (void);

G_END_DECLS
