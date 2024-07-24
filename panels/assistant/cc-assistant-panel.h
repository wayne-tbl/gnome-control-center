/*
 * Copyright (C) 2024 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <shell/cc-panel.h>

G_BEGIN_DECLS

#define CC_TYPE_ASSISTANT_PANEL (cc_assistant_panel_get_type ())
G_DECLARE_FINAL_TYPE (CcAssistantPanel, cc_assistant_panel, CC, ASSISTANT_PANEL, CcPanel)

CcAssistantPanel *cc_assistant_panel_new (void);

G_END_DECLS
