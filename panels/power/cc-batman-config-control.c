// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2024 Bardia Moshiri <fakeshell@bardia.tech>
// Copyright (C) 2024 Erik Inkinen <erik.inkinen@erikinkinen.fi>

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "cc-batman-config-control.h"

BatmanConfig batman_config;

void
read_batman_config ()
{
  GKeyFile *keyfile = g_key_file_new ();
  GError *error = NULL;

  if (!g_key_file_load_from_file (keyfile, BATMAN_CONFIG_FILE, G_KEY_FILE_NONE, &error)) {
    g_warning ("Error loading config file: %s\n", error->message);
    g_error_free (error);

    batman_config.offline = FALSE;
    batman_config.powersave = FALSE;
    batman_config.chargesave = FALSE;
    batman_config.bussave = FALSE;
    batman_config.gpusave = FALSE;
    batman_config.btsave = FALSE;
    batman_config.hybrissave = FALSE;
    batman_config.wifisave = FALSE;
  } else {
    batman_config.offline = g_key_file_get_boolean (keyfile, "Settings", "OFFLINE", NULL);
    batman_config.powersave = g_key_file_get_boolean (keyfile, "Settings", "POWERSAVE", NULL);
    batman_config.chargesave = g_key_file_get_boolean (keyfile, "Settings", "CHARGESAVE", NULL);
    batman_config.bussave = g_key_file_get_boolean (keyfile, "Settings", "BUSSAVE", NULL);
    batman_config.gpusave = g_key_file_get_boolean (keyfile, "Settings", "GPUSAVE", NULL);
    batman_config.btsave = g_key_file_get_boolean (keyfile, "Settings", "BTSAVE", NULL);
    batman_config.hybrissave = g_key_file_get_boolean (keyfile, "Settings", "HYBRIS", NULL);
    batman_config.wifisave = g_key_file_get_boolean (keyfile, "Settings", "WIFI", NULL);
  }

  g_key_file_free (keyfile);
}

int
update_config_value (const char* config_key, const char* config_value)
{
  FILE *src, *dst;
  char *line = NULL;
  size_t len = 0;
  ssize_t read;
  int found = 0;

  src = fopen (BATMAN_CONFIG_FILE, "r");
  if (src == NULL) {
    perror("Failed to open file");
    return -1;
  }

  dst = fopen (BATMAN_TEMP_FILE, "w");
  if (dst == NULL) {
    perror("Failed to open temp file");
    fclose(src);
    return -1;
  }

  while ((read = getline (&line, &len, src)) != -1) {
    if (strstr (line, config_key) == line) {
      fprintf (dst, "%s=%s\n", config_key, config_value);
      found = 1;
    } else {
    fprintf (dst, "%s", line);
    }
  }

  if (!found)
    fprintf (dst, "%s=%s\n", config_key, config_value);

  free (line);
  fclose (src);
  fclose (dst);

  rename (BATMAN_TEMP_FILE, BATMAN_CONFIG_FILE);

  return 0;
}

gboolean
powersave_switch_state_set (GtkSwitch *switch_widget, gboolean state, gpointer)
{
  int ret = update_config_value ("POWERSAVE", state ? "true" : "false");

  gtk_switch_set_state (GTK_SWITCH (switch_widget), state);
  gtk_switch_set_active (GTK_SWITCH (switch_widget), state);

  if (ret == 0)
    return TRUE;
  else
    return FALSE;
}

gboolean
offline_switch_state_set (GtkSwitch *switch_widget, gboolean state, gpointer)
{
  int ret = update_config_value ("OFFLINE", state ? "true" : "false");

  gtk_switch_set_state (GTK_SWITCH (switch_widget), state);
  gtk_switch_set_active (GTK_SWITCH (switch_widget), state);

  if (ret == 0)
    return TRUE;
  else
    return FALSE;
}

gboolean
gpusave_switch_state_set (GtkSwitch *switch_widget, gboolean state, gpointer)
{
  int ret = update_config_value ("GPUSAVE", state ? "true" : "false");

  gtk_switch_set_state (GTK_SWITCH (switch_widget), state);
  gtk_switch_set_active (GTK_SWITCH (switch_widget), state);

  if (ret == 0)
    return TRUE;
  else
    return FALSE;
}

gboolean
chargesave_switch_state_set (GtkSwitch *switch_widget, gboolean state, gpointer)
{
  int ret = update_config_value ("CHARGESAVE", state ? "true" : "false");

  gtk_switch_set_state (GTK_SWITCH (switch_widget), state);
  gtk_switch_set_active (GTK_SWITCH (switch_widget), state);

  if (ret == 0)
    return TRUE;
  else
    return FALSE;
}

gboolean
bussave_switch_state_set (GtkSwitch *switch_widget, gboolean state, gpointer)
{
  int ret = update_config_value ("BUSSAVE", state ? "true" : "false");

  gtk_switch_set_state (GTK_SWITCH (switch_widget), state);
  gtk_switch_set_active (GTK_SWITCH (switch_widget), state);

  if (ret == 0)
    return TRUE;
  else
    return FALSE;
}

gboolean
btsave_switch_state_set (GtkSwitch *switch_widget, gboolean state, gpointer)
{
  int ret = update_config_value ("BTSAVE", state ? "true" : "false");

  gtk_switch_set_state (GTK_SWITCH (switch_widget), state);
  gtk_switch_set_active (GTK_SWITCH (switch_widget), state);

  if (ret == 0)
    return TRUE;
  else
    return FALSE;
}

gboolean
hybrissave_switch_state_set (GtkSwitch *switch_widget, gboolean state, gpointer)
{
  int ret = update_config_value ("HYBRIS", state ? "true" : "false");

  gtk_switch_set_state (GTK_SWITCH (switch_widget), state);
  gtk_switch_set_active (GTK_SWITCH (switch_widget), state);

  if (ret == 0)
    return TRUE;
  else
    return FALSE;
}

gboolean
wifisave_switch_state_set (GtkSwitch *switch_widget, gboolean state, gpointer)
{
  int ret = update_config_value ("WIFI", state ? "true" : "false");

  gtk_switch_set_state (GTK_SWITCH (switch_widget), state);
  gtk_switch_set_active (GTK_SWITCH (switch_widget), state);

  if (ret == 0)
    return TRUE;
  else
    return FALSE;
}
