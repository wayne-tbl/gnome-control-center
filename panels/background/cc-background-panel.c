/*
 * Copyright (C) 2010 Intel, Inc
 *
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Thomas Wood <thomas.wood@intel.com>
 *
 */

#include <config.h>

#include <string.h>
#include <glib.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include <gdesktop-enums.h>

#include "cc-background-panel.h"

#include "cc-background-chooser.h"
#include "cc-background-item.h"
#include "cc-background-preview.h"
#include "cc-background-resources.h"

#define WP_PATH_ID "org.gnome.desktop.background"
#define WP_LOCK_PATH_ID "org.gnome.desktop.screensaver"
#define WP_URI_KEY "picture-uri"
#define WP_URI_DARK_KEY "picture-uri-dark"
#define WP_OPTIONS_KEY "picture-options"
#define WP_SHADING_KEY "color-shading-type"
#define WP_PCOLOR_KEY "primary-color"
#define WP_SCOLOR_KEY "secondary-color"

#define INTERFACE_PATH_ID "org.gnome.desktop.interface"
#define INTERFACE_COLOR_SCHEME_KEY "color-scheme"
#define INTERFACE_ACCENT_COLOR_KEY "accent-color"


#define FURIOS_SHELL_SCHEMA_ID "io.furios.phosh.shell"

/* Screen recording and screenshots. Checked separately from the glass keys so
 * that a phosh with the glass look but not these still shows everything it
 * does support. */
#define FURIOS_SCREENSHOT_DELAY_KEY   "screenshot-delay"
#define FURIOS_REC_SPEED_KEY          "screen-recorder-speed"
#define FURIOS_REC_CODEC_KEY          "screen-recorder-codec"
#define FURIOS_REC_BITRATE_KEY        "screen-recorder-bitrate"
#define FURIOS_REC_AUDIO_KEY          "screen-recorder-audio"
#define FURIOS_REC_AUDIO_SOURCE_KEY   "screen-recorder-audio-source"
#define FURIOS_REC_TOUCHES_KEY        "screen-recorder-touches"

#define PHOSH_PLUGINS_SCHEMA_ID "mobi.phosh.shell.plugins"
#define PHOSH_STATUS_ICONS_KEY "status-icons"
#define WEATHER_STATUS_ICON "weather-status-icon"

/* Which quick settings the drawer loads. Phosh watches this key and rebuilds,
 * so adding or removing a tile takes effect without a restart -- which is why
 * the two switches below drive this rather than a flag of their own. */
#define PHOSH_QS_PLUGINS_SCHEMA_ID "sm.puri.phosh.plugins"
#define PHOSH_QUICK_SETTINGS_KEY  "quick-settings"
#define SCREEN_RECORDER_PLUGIN    "screen-recorder-quick-setting"
#define SCREENSHOT_PLUGIN         "screenshot-quick-setting"

struct _CcBackgroundPanel
{
  CcPanel parent_instance;

  GDBusConnection *connection;

  GSettings *settings;
  GSettings *lock_settings;
  GSettings *interface_settings;

  GDBusProxy *proxy;

  CcBackgroundItem *current_background;

  GtkWidget *accent_box;
  CcBackgroundChooser *background_chooser;
  CcBackgroundPreview *default_preview;
  CcBackgroundPreview *dark_preview;
  GtkToggleButton *default_toggle;
  GtkToggleButton *dark_toggle;

  GSettings *phosh_plugins_settings;
  AdwPreferencesGroup *status_indicator_group;
  AdwSwitchRow *weather_status_switch;
  gboolean updating_weather_status;


  GSettings *furios_shell_settings;

  GSettings           *qs_plugin_settings;
  AdwPreferencesGroup *recording_group;
  AdwPreferencesGroup *screenshot_group;
  AdwSwitchRow        *screen_recorder_switch;
  AdwSwitchRow        *screenshot_switch;
  AdwComboRow         *screenshot_delay_row;
  AdwExpanderRow      *recorder_expander;
  AdwComboRow         *recorder_speed_row;
  AdwComboRow         *recorder_codec_row;
  AdwComboRow         *recorder_bitrate_row;
  AdwSwitchRow        *recorder_audio_switch;
  AdwComboRow         *recorder_audio_source_row;
  AdwSwitchRow        *recorder_touches_switch;
  GStrv                audio_source_ids;
  GStrv                codec_ids;
  gboolean             updating_capture;
};

CC_PANEL_REGISTER (CcBackgroundPanel, cc_background_panel)

static void on_settings_changed (CcBackgroundPanel *self);

static void
load_custom_css (CcBackgroundPanel *self)
{
  g_autoptr(GtkCssProvider) provider = NULL;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider, "/org/gnome/control-center/background/preview.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
transition_screen (CcBackgroundPanel *self)
{
  g_autoptr (GError) error = NULL;

  if (!self->proxy)
    return;

  g_dbus_proxy_call_sync (self->proxy,
                          "ScreenTransition",
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          NULL,
                          &error);

  if (error)
    g_warning ("Couldn't transition screen: %s", error->message);
}

static void
on_accent_color_toggled_cb (CcBackgroundPanel *self,
                            GtkToggleButton   *toggle)
{
  GDesktopAccentColor accent_color_from_key;
  GDesktopAccentColor accent_color = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (toggle), "accent-color"));

  if (!gtk_toggle_button_get_active (toggle))
    return;

  accent_color_from_key = g_settings_get_enum (self->interface_settings,
                                               INTERFACE_ACCENT_COLOR_KEY);

  /* Don't unnecessarily set the key again */
  if (accent_color == accent_color_from_key)
    return;

  transition_screen (self);

  g_settings_set_enum (self->interface_settings,
                       INTERFACE_ACCENT_COLOR_KEY,
                       accent_color);
}

/* Adapted from adw-inspector-page.c */
static const char *
get_color_tooltip (GDesktopAccentColor color)
{
  switch (color)
    {
    case G_DESKTOP_ACCENT_COLOR_BLUE:
      return _("Blue");
    case G_DESKTOP_ACCENT_COLOR_TEAL:
      return _("Teal");
    case G_DESKTOP_ACCENT_COLOR_GREEN:
      return _("Green");
    case G_DESKTOP_ACCENT_COLOR_YELLOW:
      return _("Yellow");
    case G_DESKTOP_ACCENT_COLOR_ORANGE:
      return _("Orange");
    case G_DESKTOP_ACCENT_COLOR_RED:
      return _("Red");
    case G_DESKTOP_ACCENT_COLOR_PINK:
      return _("Pink");
    case G_DESKTOP_ACCENT_COLOR_PURPLE:
      return _("Purple");
    case G_DESKTOP_ACCENT_COLOR_SLATE:
      return _("Slate");
    default:
      g_assert_not_reached ();
    }
}

static const char *
get_untranslated_color (GDesktopAccentColor color)
{
  switch (color)
    {
    case G_DESKTOP_ACCENT_COLOR_BLUE:
      return "blue";
    case G_DESKTOP_ACCENT_COLOR_TEAL:
      return "teal";
    case G_DESKTOP_ACCENT_COLOR_GREEN:
      return "green";
    case G_DESKTOP_ACCENT_COLOR_YELLOW:
      return "yellow";
    case G_DESKTOP_ACCENT_COLOR_ORANGE:
      return "orange";
    case G_DESKTOP_ACCENT_COLOR_RED:
      return "red";
    case G_DESKTOP_ACCENT_COLOR_PINK:
      return "pink";
    case G_DESKTOP_ACCENT_COLOR_PURPLE:
      return "purple";
    case G_DESKTOP_ACCENT_COLOR_SLATE:
      return "slate";
    default:
      g_assert_not_reached ();
    }
}

static void
setup_accent_color_toggles (CcBackgroundPanel *self)
{
  GDesktopAccentColor accent_color = g_settings_get_enum (self->interface_settings, INTERFACE_ACCENT_COLOR_KEY);
  GDesktopAccentColor i;

  for (i = G_DESKTOP_ACCENT_COLOR_BLUE; i <= G_DESKTOP_ACCENT_COLOR_SLATE; i++)
    {
      GtkWidget *button = GTK_WIDGET (gtk_toggle_button_new ());
      GtkToggleButton *grouping_button = GTK_TOGGLE_BUTTON (gtk_widget_get_first_child (self->accent_box));

      gtk_widget_set_tooltip_text (button, get_color_tooltip (i));
      gtk_widget_add_css_class (button, "accent-button");
      gtk_widget_add_css_class (button, get_untranslated_color (i));
      g_object_set_data (G_OBJECT (button), "accent-color", GINT_TO_POINTER (i));
      g_signal_connect_object (button, "toggled",
                               G_CALLBACK (on_accent_color_toggled_cb),
                               self,
                               G_CONNECT_SWAPPED);

      if (grouping_button != NULL)
        gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (button), grouping_button);

      if (i == accent_color)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);

      gtk_box_append (GTK_BOX (self->accent_box), button);
    }
}

static void
reload_accent_color_toggles (CcBackgroundPanel *self)
{
  GDesktopAccentColor accent_color = g_settings_get_enum (self->interface_settings, INTERFACE_ACCENT_COLOR_KEY);
  GtkWidget *child;

  for (child = gtk_widget_get_first_child (self->accent_box);
       child;
       child = gtk_widget_get_next_sibling (child))
    {
      GDesktopAccentColor child_color = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (child), "accent-color"));

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (child), child_color == accent_color);
    }
}

static void
reload_color_scheme (CcBackgroundPanel *self)
{
  GDesktopColorScheme scheme;

  scheme = g_settings_get_enum (self->interface_settings, INTERFACE_COLOR_SCHEME_KEY);

  if (scheme == G_DESKTOP_COLOR_SCHEME_DEFAULT)
    {
      gtk_toggle_button_set_active (self->default_toggle, TRUE);
    }
  else if (scheme == G_DESKTOP_COLOR_SCHEME_PREFER_DARK)
    {
      gtk_toggle_button_set_active (self->dark_toggle, TRUE);
    }
  else
    {
      gtk_toggle_button_set_active (self->default_toggle, FALSE);
      gtk_toggle_button_set_active (self->dark_toggle, FALSE);
    }
}

static void
set_color_scheme (CcBackgroundPanel   *self,
                  GDesktopColorScheme  color_scheme)
{
  GDesktopColorScheme scheme;

  scheme = g_settings_get_enum (self->interface_settings,
                                INTERFACE_COLOR_SCHEME_KEY);

  /* We have to check the equality manually to avoid starting an unnecessary
   * screen transition */
  if (color_scheme == scheme)
    return;

  transition_screen (self);

  g_settings_set_enum (self->interface_settings,
                       INTERFACE_COLOR_SCHEME_KEY,
                       color_scheme);
}

/* Color schemes */

static void
on_color_scheme_toggle_active_cb (CcBackgroundPanel *self)
{
  if (gtk_toggle_button_get_active (self->default_toggle))
    set_color_scheme (self, G_DESKTOP_COLOR_SCHEME_DEFAULT);
  else if (gtk_toggle_button_get_active (self->dark_toggle))
    set_color_scheme (self, G_DESKTOP_COLOR_SCHEME_PREFER_DARK);
}

static void
got_transition_proxy_cb (GObject      *source_object,
                         GAsyncResult *res,
                         gpointer      data)
{
  g_autoptr(GError) error = NULL;
  CcBackgroundPanel *self = data;

  self->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);

  if (self->proxy == NULL)
    {
      g_warning ("Error creating proxy: %s", error->message);
      return;
    }
}

/* Background */

static void
update_preview (CcBackgroundPanel *self)
{
  CcBackgroundItem *current_background;

  current_background = self->current_background;
  cc_background_preview_set_item (self->default_preview, current_background);
  cc_background_preview_set_item (self->dark_preview, current_background);
}

static void
reload_current_bg (CcBackgroundPanel *self)
{
  CcBackgroundItem *configured;
  GSettings *settings = NULL;
  g_autofree gchar *uri = NULL;
  g_autofree gchar *dark_uri = NULL;
  g_autofree gchar *pcolor = NULL;
  g_autofree gchar *scolor = NULL;

  /* initalise the current background information from settings */
  settings = self->settings;
  uri = g_settings_get_string (settings, WP_URI_KEY);
  if (uri && *uri == '\0')
    g_clear_pointer (&uri, g_free);


  configured = cc_background_item_new (uri);

  dark_uri = g_settings_get_string (settings, WP_URI_DARK_KEY);
  pcolor = g_settings_get_string (settings, WP_PCOLOR_KEY);
  scolor = g_settings_get_string (settings, WP_SCOLOR_KEY);
  g_object_set (G_OBJECT (configured),
                "name", _("Current background"),
                "uri-dark", dark_uri,
                "placement", g_settings_get_enum (settings, WP_OPTIONS_KEY),
                "shading", g_settings_get_enum (settings, WP_SHADING_KEY),
                "primary-color", pcolor,
                "secondary-color", scolor,
                NULL);

  g_clear_object (&self->current_background);
  self->current_background = configured;
  cc_background_item_load (configured, NULL);

  cc_background_chooser_set_active_item (self->background_chooser, configured);
}

static void
reset_settings_if_defaults (CcBackgroundPanel *self,
                            GSettings         *settings,
                            gboolean           check_dark)
{
  gsize i;
  const char *keys[] = {
    WP_URI_KEY,       /* this key needs to be first */
    WP_URI_DARK_KEY,
    WP_OPTIONS_KEY,
    WP_SHADING_KEY,
    WP_PCOLOR_KEY,
    WP_SCOLOR_KEY,
    NULL
  };

  for (i = 0; keys[i] != NULL; i++)
    {
      g_autoptr (GVariant) default_value = NULL;
      g_autoptr (GVariant) user_value = NULL;
      gboolean setting_is_default;

      if (!check_dark && g_str_equal (keys[i], WP_URI_DARK_KEY))
        continue;

      default_value = g_settings_get_default_value (settings, keys[i]);
      user_value = g_settings_get_value (settings, keys[i]);

      setting_is_default = g_variant_equal (default_value, user_value);

      /* As a courtesy to distros that are a little lackadaisical about making sure
       * schema defaults match the settings in the background item with the default
       * picture, we only look at the URI to determine if we shouldn't clean out dconf.
       *
       * In otherwords, we still clean out the picture-uri key from dconf when a user
       * selects the default background in control-center, even if after selecting it
       * e.g., primary-color still mismatches with schema defaults.
       */
      if (g_str_equal (keys[i], WP_URI_KEY) && !setting_is_default)
        return;

      if (setting_is_default)
        g_settings_reset (settings, keys[i]);
    }

  g_settings_apply (settings);
}

static void
set_background (CcBackgroundPanel *self,
                GSettings         *settings,
                CcBackgroundItem  *item,
                gboolean           set_dark)
{
  GDesktopBackgroundStyle style;
  CcBackgroundItemFlags flags;
  g_autofree gchar *filename = NULL;
  const char *uri;

  if (item == NULL)
    return;

  uri = cc_background_item_get_uri (item);
  flags = cc_background_item_get_flags (item);

  g_settings_set_string (settings, WP_URI_KEY, uri);

  if (set_dark)
    {
      const char *uri_dark;

      uri_dark = cc_background_item_get_uri_dark (item);

      if (uri_dark && uri_dark[0])
        g_settings_set_string (settings, WP_URI_DARK_KEY, uri_dark);
      else
        g_settings_set_string (settings, WP_URI_DARK_KEY, uri);
    }

  /* Also set the placement if we have a URI and the previous value was none */
  if (flags & CC_BACKGROUND_ITEM_HAS_PLACEMENT)
    {
      g_settings_set_enum (settings, WP_OPTIONS_KEY, cc_background_item_get_placement (item));
    }
  else if (uri != NULL)
    {
      style = g_settings_get_enum (settings, WP_OPTIONS_KEY);
      if (style == G_DESKTOP_BACKGROUND_STYLE_NONE)
        g_settings_set_enum (settings, WP_OPTIONS_KEY, cc_background_item_get_placement (item));
    }

  if (flags & CC_BACKGROUND_ITEM_HAS_SHADING)
    g_settings_set_enum (settings, WP_SHADING_KEY, cc_background_item_get_shading (item));

  g_settings_set_string (settings, WP_PCOLOR_KEY, cc_background_item_get_pcolor (item));
  g_settings_set_string (settings, WP_SCOLOR_KEY, cc_background_item_get_scolor (item));

  /* Apply all changes */
  g_settings_apply (settings);

  /* Clean out dconf if the user went back to distro defaults */
  reset_settings_if_defaults (self, settings, set_dark);
}

static void
on_chooser_background_chosen_cb (CcBackgroundPanel *self,
                                 CcBackgroundItem  *item)
{
  g_signal_handlers_block_by_func (self->settings, on_settings_changed, self);

  set_background (self, self->settings, item, TRUE);
  set_background (self, self->lock_settings, item, FALSE);

  on_settings_changed (self);

  g_signal_handlers_unblock_by_func (self->settings, on_settings_changed, self);
}

static void
on_add_picture_button_clicked_cb (CcBackgroundPanel *self)
{
  cc_background_chooser_select_file (self->background_chooser);
}

/* Kept parallel to the strings in the .blp models */
static const int    screenshot_delays[]  = { 1, 2, 3, 4, 5 };
static const double recorder_speeds[]    = { 1.0, 1.5, 2.0, 4.0, 8.0 };
static const char * const recorder_bitrates[] = { "", "4M", "8M", "12M" };


static gboolean
plugin_list_contains (GStrv list, const char *plugin)
{
  return list != NULL && g_strv_contains ((const char * const *) list, plugin);
}


static void
set_plugin_enabled (CcBackgroundPanel *self, const char *plugin, gboolean enabled)
{
  g_auto (GStrv) current = NULL;
  g_autoptr (GStrvBuilder) builder = NULL;
  g_auto (GStrv) updated = NULL;

  if (self->qs_plugin_settings == NULL)
    return;

  current = g_settings_get_strv (self->qs_plugin_settings, PHOSH_QUICK_SETTINGS_KEY);

  if (plugin_list_contains (current, plugin) == enabled)
    return;

  builder = g_strv_builder_new ();

  for (guint i = 0; current != NULL && current[i]; i++) {
    if (!g_str_equal (current[i], plugin))
      g_strv_builder_add (builder, current[i]);
  }

  if (enabled)
    g_strv_builder_add (builder, plugin);

  updated = g_strv_builder_end (builder);
  g_settings_set_strv (self->qs_plugin_settings,
                       PHOSH_QUICK_SETTINGS_KEY,
                       (const char * const *) updated);
}


static void
reload_capture_switches (CcBackgroundPanel *self)
{
  g_auto (GStrv) plugins = NULL;

  if (self->qs_plugin_settings == NULL)
    return;

  plugins = g_settings_get_strv (self->qs_plugin_settings, PHOSH_QUICK_SETTINGS_KEY);

  /* Guarded because setting "active" here would otherwise come straight back
   * as a user edit and rewrite the very key being read. */
  self->updating_capture = TRUE;
  adw_switch_row_set_active (self->screen_recorder_switch,
                             plugin_list_contains (plugins, SCREEN_RECORDER_PLUGIN));
  adw_switch_row_set_active (self->screenshot_switch,
                             plugin_list_contains (plugins, SCREENSHOT_PLUGIN));
  self->updating_capture = FALSE;
}


static void
on_screen_recorder_switch_changed_cb (CcBackgroundPanel *self)
{
  if (self->updating_capture)
    return;

  set_plugin_enabled (self, SCREEN_RECORDER_PLUGIN,
                      adw_switch_row_get_active (self->screen_recorder_switch));
}


static void
on_screenshot_switch_changed_cb (CcBackgroundPanel *self)
{
  if (self->updating_capture)
    return;

  set_plugin_enabled (self, SCREENSHOT_PLUGIN,
                      adw_switch_row_get_active (self->screenshot_switch));
}


static void
on_screenshot_delay_changed_cb (CcBackgroundPanel *self)
{
  guint selected = adw_combo_row_get_selected (self->screenshot_delay_row);

  if (self->updating_capture || self->furios_shell_settings == NULL)
    return;
  if (selected >= G_N_ELEMENTS (screenshot_delays))
    return;

  g_settings_set_int (self->furios_shell_settings,
                      FURIOS_SCREENSHOT_DELAY_KEY,
                      screenshot_delays[selected]);
}


static void
on_recorder_speed_changed_cb (CcBackgroundPanel *self)
{
  guint selected = adw_combo_row_get_selected (self->recorder_speed_row);

  if (self->updating_capture || self->furios_shell_settings == NULL)
    return;
  if (selected >= G_N_ELEMENTS (recorder_speeds))
    return;

  g_settings_set_double (self->furios_shell_settings,
                         FURIOS_REC_SPEED_KEY,
                         recorder_speeds[selected]);
}



static void
on_recorder_bitrate_changed_cb (CcBackgroundPanel *self)
{
  guint selected = adw_combo_row_get_selected (self->recorder_bitrate_row);

  if (self->updating_capture || self->furios_shell_settings == NULL)
    return;
  if (selected >= G_N_ELEMENTS (recorder_bitrates))
    return;

  g_settings_set_string (self->furios_shell_settings,
                         FURIOS_REC_BITRATE_KEY,
                         recorder_bitrates[selected]);
}


static void
on_recorder_codec_changed_cb (CcBackgroundPanel *self)
{
  guint selected = adw_combo_row_get_selected (self->recorder_codec_row);

  if (self->updating_capture || self->furios_shell_settings == NULL)
    return;
  if (self->codec_ids == NULL || selected >= g_strv_length (self->codec_ids))
    return;

  g_settings_set_string (self->furios_shell_settings,
                         FURIOS_REC_CODEC_KEY,
                         self->codec_ids[selected]);
}


static void
on_recorder_audio_source_changed_cb (CcBackgroundPanel *self)
{
  guint selected = adw_combo_row_get_selected (self->recorder_audio_source_row);

  if (self->updating_capture || self->furios_shell_settings == NULL)
    return;
  if (self->audio_source_ids == NULL || selected >= g_strv_length (self->audio_source_ids))
    return;

  g_settings_set_string (self->furios_shell_settings,
                         FURIOS_REC_AUDIO_SOURCE_KEY,
                         self->audio_source_ids[selected]);
}


/*
 * Offer only encoders this ffmpeg actually has. The list is a candidate set
 * rather than everything ffmpeg reports: the full list runs to hundreds, most
 * of them irrelevant to a screen recording, and none of the hardware encoders
 * on this device work through wf-recorder anyway.
 */
static void
populate_codecs (CcBackgroundPanel *self)
{
  /* H.265 is deliberately absent. Measured on this device at 1078x2410 it
   * encodes about 5 frames a second even at preset=ultrafast, so ten seconds of
   * recording arrives as a handful of frames -- there is no setting that makes
   * it usable here, only a choice that looks broken. */
  static const struct { const char *id; const char *label; } candidates[] = {
    { "libx264",    N_("H.264 (fastest, most compatible)") },
    { "libvpx-vp9", N_("VP9 (smaller files, slower)") },
  };
  g_autoptr (GtkStringList) model = gtk_string_list_new (NULL);
  g_autoptr (GStrvBuilder) ids = g_strv_builder_new ();
  g_autofree char *out = NULL;
  g_autoptr (GError) err = NULL;

  if (!g_spawn_command_line_sync ("ffmpeg -hide_banner -encoders", &out, NULL, NULL, &err)) {
    g_debug ("Cannot list encoders: %s", err->message);
    out = NULL;
  }

  for (guint i = 0; i < G_N_ELEMENTS (candidates); i++) {
    /* With no ffmpeg to ask, offer the default alone rather than an empty list
     * the user cannot choose out of. */
    if (out != NULL && strstr (out, candidates[i].id) == NULL)
      continue;
    if (out == NULL && !g_str_equal (candidates[i].id, "libx264"))
      continue;

    gtk_string_list_append (model, _(candidates[i].label));
    g_strv_builder_add (ids, candidates[i].id);
  }

  g_clear_pointer (&self->codec_ids, g_strfreev);
  self->codec_ids = g_strv_builder_end (ids);

  adw_combo_row_set_model (self->recorder_codec_row, G_LIST_MODEL (model));
}


/*
 * Sources come from pactl. A monitor source is what the phone is playing, any
 * other is a capture device -- worth spelling out, because the raw names
 * ("sink.primary_output.monitor") do not say which is which to anyone who has
 * not met PulseAudio.
 */
static void
populate_audio_sources (CcBackgroundPanel *self)
{
  g_autoptr (GtkStringList) model = gtk_string_list_new (NULL);
  g_autoptr (GStrvBuilder) ids = g_strv_builder_new ();
  g_autofree char *out = NULL;
  g_auto (GStrv) lines = NULL;
  g_autoptr (GError) err = NULL;

  gtk_string_list_append (model, _("Default input"));
  g_strv_builder_add (ids, "");

  if (!g_spawn_command_line_sync ("pactl list short sources", &out, NULL, NULL, &err)) {
    g_debug ("Cannot list audio sources: %s", err->message);
  } else {
    lines = g_strsplit (out, "\n", -1);

    for (guint i = 0; lines[i]; i++) {
      g_auto (GStrv) fields = NULL;
      g_autofree char *label = NULL;

      if (*lines[i] == '\0')
        continue;

      fields = g_strsplit (lines[i], "\t", -1);
      if (g_strv_length (fields) < 2)
        continue;

      if (g_str_has_suffix (fields[1], ".monitor"))
        /* Translators: '%s' is an audio device name */
        label = g_strdup_printf (_("%s (internal audio)"), fields[1]);
      else
        /* Translators: '%s' is an audio device name */
        label = g_strdup_printf (_("%s (microphone)"), fields[1]);

      gtk_string_list_append (model, label);
      g_strv_builder_add (ids, fields[1]);
    }
  }

  g_clear_pointer (&self->audio_source_ids, g_strfreev);
  self->audio_source_ids = g_strv_builder_end (ids);

  adw_combo_row_set_model (self->recorder_audio_source_row, G_LIST_MODEL (model));
}


static guint
index_of_int (const int *values, guint n, int wanted)
{
  for (guint i = 0; i < n; i++) {
    if (values[i] == wanted)
      return i;
  }

  return 0;
}


static void
reload_capture_rows (CcBackgroundPanel *self)
{
  g_autofree char *codec = NULL;
  g_autofree char *bitrate = NULL;
  g_autofree char *source = NULL;
  double speed;
  guint selected;
  gboolean found;

  if (self->furios_shell_settings == NULL)
    return;

  self->updating_capture = TRUE;

  adw_combo_row_set_selected (self->screenshot_delay_row,
                              index_of_int (screenshot_delays,
                                            G_N_ELEMENTS (screenshot_delays),
                                            g_settings_get_int (self->furios_shell_settings,
                                                                FURIOS_SCREENSHOT_DELAY_KEY)));

  /* Doubles do not compare exactly, so match on the nearest offered speed */
  speed = g_settings_get_double (self->furios_shell_settings, FURIOS_REC_SPEED_KEY);
  selected = 0;
  for (guint i = 0; i < G_N_ELEMENTS (recorder_speeds); i++) {
    if (ABS (recorder_speeds[i] - speed) < ABS (recorder_speeds[selected] - speed))
      selected = i;
  }
  adw_combo_row_set_selected (self->recorder_speed_row, selected);

  bitrate = g_settings_get_string (self->furios_shell_settings, FURIOS_REC_BITRATE_KEY);
  selected = 0;
  for (guint i = 0; i < G_N_ELEMENTS (recorder_bitrates); i++) {
    if (g_strcmp0 (recorder_bitrates[i], bitrate) == 0) {
      selected = i;
      break;
    }
  }
  adw_combo_row_set_selected (self->recorder_bitrate_row, selected);

  codec = g_settings_get_string (self->furios_shell_settings, FURIOS_REC_CODEC_KEY);
  selected = 0;
  found = FALSE;
  for (guint i = 0; self->codec_ids != NULL && self->codec_ids[i]; i++) {
    if (g_strcmp0 (self->codec_ids[i], codec) == 0) {
      selected = i;
      found = TRUE;
      break;
    }
  }
  adw_combo_row_set_selected (self->recorder_codec_row, selected);
  /* A codec that is no longer offered -- H.265 was dropped, and ffmpeg may be
   * built without one -- would otherwise leave the row showing the first entry
   * while the recorder went on using the old key. Setting the row does not
   * write it back, because selecting what is already selected notifies nothing.
   */
  if (!found && self->codec_ids != NULL && self->codec_ids[0] != NULL) {
    g_settings_set_string (self->furios_shell_settings, FURIOS_REC_CODEC_KEY,
                           self->codec_ids[0]);
  }

  source = g_settings_get_string (self->furios_shell_settings, FURIOS_REC_AUDIO_SOURCE_KEY);
  selected = 0;
  for (guint i = 0; self->audio_source_ids != NULL && self->audio_source_ids[i]; i++) {
    if (g_strcmp0 (self->audio_source_ids[i], source) == 0) {
      selected = i;
      break;
    }
  }
  adw_combo_row_set_selected (self->recorder_audio_source_row, selected);

  self->updating_capture = FALSE;
}


static gboolean
weather_status_icon_is_enabled (CcBackgroundPanel *self)
{
  g_auto(GStrv) status_icons = NULL;

  if (self->phosh_plugins_settings == NULL)
    return FALSE;

  status_icons = g_settings_get_strv (self->phosh_plugins_settings,
                                      PHOSH_STATUS_ICONS_KEY);

  return g_strv_contains ((const gchar * const *) status_icons,
                          WEATHER_STATUS_ICON);
}


static void
reload_weather_status_switch (CcBackgroundPanel *self)
{
  gboolean enabled;

  if (self->phosh_plugins_settings == NULL)
    return;

  enabled = weather_status_icon_is_enabled (self);

  self->updating_weather_status = TRUE;
  adw_switch_row_set_active (self->weather_status_switch, enabled);
  self->updating_weather_status = FALSE;
}


static void
on_weather_status_switch_active_changed_cb (AdwSwitchRow      *row,
                                            GParamSpec        *pspec,
                                            CcBackgroundPanel *self)
{
  g_auto(GStrv) status_icons = NULL;
  g_autoptr(GPtrArray) updated_status_icons = NULL;
  gboolean active;
  gboolean currently_enabled;
  guint i;

  if (self->updating_weather_status)
    return;

  if (self->phosh_plugins_settings == NULL)
    return;

  active = adw_switch_row_get_active (row);

  status_icons = g_settings_get_strv (self->phosh_plugins_settings,
                                      PHOSH_STATUS_ICONS_KEY);

  currently_enabled = g_strv_contains ((const gchar * const *) status_icons,
                                       WEATHER_STATUS_ICON);

  if (active == currently_enabled)
    return;

  updated_status_icons = g_ptr_array_new_with_free_func (g_free);

  for (i = 0; status_icons[i] != NULL; i++) {
    if (!g_str_equal (status_icons[i], WEATHER_STATUS_ICON))
      g_ptr_array_add (updated_status_icons, g_strdup (status_icons[i]));
  }

  if (active)
    g_ptr_array_add (updated_status_icons, g_strdup (WEATHER_STATUS_ICON));

  g_ptr_array_add (updated_status_icons, NULL);

  if (!g_settings_set_strv (self->phosh_plugins_settings,
                            PHOSH_STATUS_ICONS_KEY,
                            (const gchar * const *) updated_status_icons->pdata)) {
    g_warning ("Failed to update %s %s",
               PHOSH_PLUGINS_SCHEMA_ID,
               PHOSH_STATUS_ICONS_KEY);

    reload_weather_status_switch (self);
    return;
  }
}


static void
setup_weather_status_switch (CcBackgroundPanel *self)
{
  GSettingsSchemaSource *schema_source;
  g_autoptr(GSettingsSchema) schema = NULL;

  schema_source = g_settings_schema_source_get_default ();

  if (schema_source != NULL)
    schema = g_settings_schema_source_lookup (schema_source,
                                              PHOSH_PLUGINS_SCHEMA_ID,
                                              TRUE);

  if (schema == NULL ||
      !g_settings_schema_has_key (schema, PHOSH_STATUS_ICONS_KEY)) {
    gtk_widget_set_visible (GTK_WIDGET (self->status_indicator_group), FALSE);
    return;
  }

  gtk_widget_set_visible (GTK_WIDGET (self->status_indicator_group), TRUE);

  self->phosh_plugins_settings = g_settings_new_full (schema, NULL, NULL);

  reload_weather_status_switch (self);

  g_signal_connect_object (self->phosh_plugins_settings,
                           "changed::" PHOSH_STATUS_ICONS_KEY,
                           G_CALLBACK (reload_weather_status_switch),
                           self,
                           G_CONNECT_SWAPPED);
}


/*
 * The shell's own settings, shared by the groups that read them.
 *
 * Looked up rather than opened: the schema is only present where a phosh that
 * has these keys is installed, and asking GSettings for one that is not there
 * aborts the process. %NULL means no such phosh, and each group hides itself.
 */
static GSettings *
ensure_furios_shell_settings (CcBackgroundPanel *self)
{
  GSettingsSchemaSource *schema_source;
  g_autoptr (GSettingsSchema) schema = NULL;

  if (self->furios_shell_settings != NULL)
    return self->furios_shell_settings;

  schema_source = g_settings_schema_source_get_default ();
  if (schema_source != NULL)
    schema = g_settings_schema_source_lookup (schema_source, FURIOS_SHELL_SCHEMA_ID, TRUE);

  if (schema == NULL)
    return NULL;

  self->furios_shell_settings = g_settings_new_full (schema, NULL, NULL);

  return self->furios_shell_settings;
}


static void
setup_capture_group (CcBackgroundPanel *self)
{
  GSettingsSchemaSource *schema_source;
  g_autoptr (GSettingsSchema) schema = NULL;
  const char * const keys[] = {
    FURIOS_SCREENSHOT_DELAY_KEY,
    FURIOS_REC_SPEED_KEY,
    FURIOS_REC_CODEC_KEY,
    FURIOS_REC_BITRATE_KEY,
    FURIOS_REC_AUDIO_KEY,
    FURIOS_REC_AUDIO_SOURCE_KEY,
    FURIOS_REC_TOUCHES_KEY,
  };

  /* Checked on their own: a phosh that predates these keys should still show
   * everything else it does support, and asking for a key a schema lacks
   * aborts the process. */
  if (ensure_furios_shell_settings (self) == NULL) {
    gtk_widget_set_visible (GTK_WIDGET (self->recording_group), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->screenshot_group), FALSE);
    return;
  }

  schema_source = g_settings_schema_source_get_default ();
  if (schema_source != NULL)
    schema = g_settings_schema_source_lookup (schema_source, FURIOS_SHELL_SCHEMA_ID, TRUE);

  for (guint i = 0; schema != NULL && i < G_N_ELEMENTS (keys); i++) {
    if (!g_settings_schema_has_key (schema, keys[i])) {
      g_clear_pointer (&schema, g_settings_schema_unref);
      break;
    }
  }

  if (schema == NULL) {
    gtk_widget_set_visible (GTK_WIDGET (self->recording_group), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->screenshot_group), FALSE);
    return;
  }

  gtk_widget_set_visible (GTK_WIDGET (self->recording_group), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (self->screenshot_group), TRUE);

  g_settings_bind (self->furios_shell_settings, FURIOS_REC_AUDIO_KEY,
                   self->recorder_audio_switch, "active", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->furios_shell_settings, FURIOS_REC_TOUCHES_KEY,
                   self->recorder_touches_switch, "active", G_SETTINGS_BIND_DEFAULT);

  /* Choosing a source is meaningless while nothing is being recorded */
  g_object_bind_property (self->recorder_audio_switch, "active",
                          self->recorder_audio_source_row, "visible",
                          G_BINDING_SYNC_CREATE);

  populate_codecs (self);
  populate_audio_sources (self);
  reload_capture_rows (self);

  g_signal_connect_object (self->furios_shell_settings, "changed",
                           G_CALLBACK (reload_capture_rows), self, G_CONNECT_SWAPPED);

  /* The plugin list lives in phosh's own schema, which a non-phosh system will
   * not have -- hence a separate lookup rather than assuming it is there. */
  schema = NULL;
  if (schema_source != NULL)
    schema = g_settings_schema_source_lookup (schema_source, PHOSH_QS_PLUGINS_SCHEMA_ID, TRUE);

  if (schema == NULL || !g_settings_schema_has_key (schema, PHOSH_QUICK_SETTINGS_KEY)) {
    gtk_widget_set_visible (GTK_WIDGET (self->screen_recorder_switch), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->screenshot_switch), FALSE);
    return;
  }

  self->qs_plugin_settings = g_settings_new_full (schema, NULL, NULL);

  reload_capture_switches (self);
  g_signal_connect_object (self->qs_plugin_settings,
                           "changed::" PHOSH_QUICK_SETTINGS_KEY,
                           G_CALLBACK (reload_capture_switches), self, G_CONNECT_SWAPPED);
}


static const char *
cc_background_panel_get_help_uri (CcPanel *panel)
{
  return "help:gnome-help/look-background";
}

static void
cc_background_panel_dispose (GObject *object)
{
  CcBackgroundPanel *self = CC_BACKGROUND_PANEL (object);

  g_clear_object (&self->phosh_plugins_settings);
  g_clear_object (&self->furios_shell_settings);
  g_clear_object (&self->qs_plugin_settings);
  g_clear_pointer (&self->audio_source_ids, g_strfreev);
  g_clear_pointer (&self->codec_ids, g_strfreev);
  g_clear_object (&self->settings);
  g_clear_object (&self->lock_settings);
  g_clear_object (&self->interface_settings);
  g_clear_object (&self->proxy);

  G_OBJECT_CLASS (cc_background_panel_parent_class)->dispose (object);
}

static void
cc_background_panel_finalize (GObject *object)
{
  CcBackgroundPanel *self = CC_BACKGROUND_PANEL (object);

  g_clear_object (&self->current_background);

  G_OBJECT_CLASS (cc_background_panel_parent_class)->finalize (object);
}

static void
cc_background_panel_class_init (CcBackgroundPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  CcPanelClass *panel_class = CC_PANEL_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  g_type_ensure (CC_TYPE_BACKGROUND_CHOOSER);
  g_type_ensure (CC_TYPE_BACKGROUND_PREVIEW);

  panel_class->get_help_uri = cc_background_panel_get_help_uri;

  object_class->dispose = cc_background_panel_dispose;
  object_class->finalize = cc_background_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/background/cc-background-panel.ui");

  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, status_indicator_group);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, weather_status_switch);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, recording_group);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, screenshot_group);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, screen_recorder_switch);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, screenshot_switch);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, screenshot_delay_row);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, recorder_expander);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, recorder_speed_row);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, recorder_codec_row);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, recorder_bitrate_row);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, recorder_audio_switch);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, recorder_audio_source_row);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, recorder_touches_switch);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, accent_box);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, background_chooser);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, default_preview);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, dark_preview);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, default_toggle);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, dark_toggle);

  gtk_widget_class_bind_template_callback (widget_class, on_color_scheme_toggle_active_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_chooser_background_chosen_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_add_picture_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_weather_status_switch_active_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_screen_recorder_switch_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_screenshot_switch_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_screenshot_delay_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_recorder_speed_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_recorder_codec_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_recorder_bitrate_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_recorder_audio_source_changed_cb);
}

static void
on_settings_changed (CcBackgroundPanel *self)
{
  reload_current_bg (self);
  update_preview (self);
}

static void
cc_background_panel_init (CcBackgroundPanel *self)
{
  g_resources_register (cc_background_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (self));

  self->connection = g_application_get_dbus_connection (g_application_get_default ());

  self->settings = g_settings_new (WP_PATH_ID);
  g_settings_delay (self->settings);
 
  self->lock_settings = g_settings_new (WP_LOCK_PATH_ID);
  g_settings_delay (self->lock_settings);

  self->interface_settings = g_settings_new (INTERFACE_PATH_ID);

  /* Load the background */
  reload_current_bg (self);
  update_preview (self);

  /* Background settings */
  g_signal_connect_object (self->settings, "changed", G_CALLBACK (on_settings_changed), self, G_CONNECT_SWAPPED);

  /* Interface settings */
  reload_color_scheme (self);
  setup_accent_color_toggles (self);

  g_signal_connect_object (self->interface_settings,
                           "changed::" INTERFACE_COLOR_SCHEME_KEY,
                           G_CALLBACK (reload_color_scheme),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->interface_settings,
                           "changed::" INTERFACE_ACCENT_COLOR_KEY,
                           G_CALLBACK (reload_accent_color_toggles),
                           self,
                           G_CONNECT_SWAPPED);

  setup_weather_status_switch (self);
  setup_capture_group (self);

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            "org.gnome.Shell",
                            "/org/gnome/Shell",
                            "org.gnome.Shell",
                            NULL,
                            got_transition_proxy_cb,
                            self);

  load_custom_css (self);
}
