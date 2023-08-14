/*
 * Copyright (C) 2023 Eugenio "g7" Paolantonio <me@medesimo.eu>
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <shell/cc-panel.h>

G_BEGIN_DECLS

typedef enum {
  ENCRYPTION_SERVICE_STATUS_UNKNOWN = 0,
  ENCRYPTION_SERVICE_STATUS_UNSUPPORTED,
  ENCRYPTION_SERVICE_STATUS_UNCONFIGURED,
  ENCRYPTION_SERVICE_STATUS_CONFIGURING,
  ENCRYPTION_SERVICE_STATUS_CONFIGURED,
  ENCRYPTION_SERVICE_STATUS_ENCRYPTING,
  ENCRYPTION_SERVICE_STATUS_ENCRYPTED,
  ENCRYPTION_SERVICE_STATUS_FAILED,
} EncryptionServiceStatus;

#define CC_TYPE_CRYPTED_PANEL (cc_crypted_panel_get_type ())
G_DECLARE_FINAL_TYPE (CcCryptedPanel, cc_crypted_panel, CC, CRYPTED_PANEL, CcPanel)

CcCryptedPanel *cc_crypted_panel_new (void);

G_END_DECLS
