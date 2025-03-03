// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024 Bardia Moshiri <fakeshell@bardia.tech>
// Copyright (C) 2024 Erik Inkinen <erik.inkinen@erikinkinen.fi>

#pragma once

#include <gtk/gtk.h>
#include <adwaita.h>

#define BATMAN_TEMP_FILE "/var/lib/batman/config.tmp"
#define BATMAN_CONFIG_FILE "/var/lib/batman/config"

typedef struct {
    gboolean offline;
    gboolean powersave;
    gboolean chargesave;
    gboolean bussave;
    gboolean gpusave;
    gboolean btsave;
    gboolean hybrissave;
    gboolean wifisave;
} BatmanConfig;

extern BatmanConfig batman_config;

void
read_batman_config ();

int
update_config_value (const char* config_key, const char* config_value);

int
update_batman_config_value (const char* config_key, const char* config_value);

gboolean
powersave_switch_state_set (GtkSwitch* sender, gboolean state, gpointer data);

gboolean
offline_switch_state_set (GtkSwitch* sender, gboolean state, gpointer data);

gboolean
gpusave_switch_state_set (GtkSwitch* sender, gboolean state, gpointer data);

gboolean
chargesave_switch_state_set (GtkSwitch* sender, gboolean state, gpointer data);

gboolean
bussave_switch_state_set (GtkSwitch* sender, gboolean state, gpointer data);

gboolean
btsave_switch_state_set (GtkSwitch* sender, gboolean state, gpointer data);

gboolean
hybrissave_switch_state_set (GtkSwitch* sender, gboolean state, gpointer data);

gboolean
wifisave_switch_state_set (GtkSwitch* sender, gboolean state, gpointer data);
