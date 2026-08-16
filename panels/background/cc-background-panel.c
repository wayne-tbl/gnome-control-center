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

/* The shell's glass look. Only present when a phosh new enough to understand
 * these is installed, so the group hides itself otherwise. */
#define FURIOS_SHELL_SCHEMA_ID "io.furios.phosh.shell"
#define FURIOS_GLASS_THEME_KEY "glass-theme"
#define FURIOS_GLASS_BLUR_RADIUS_KEY "glass-blur-radius"
#define FURIOS_GLASS_OPACITY_KEY "glass-opacity"
#define FURIOS_GLASS_LIGHTNESS_KEY "glass-lightness"
#define FURIOS_GLASS_ACCENT_WASH_KEY "glass-accent-wash"
#define FURIOS_ACCENT_COLOR_CUSTOM_KEY "accent-color-custom"
#define FURIOS_GLASS_TEXT_COLOR_KEY "glass-text-color"
#define FURIOS_GLASS_TEXT_OPACITY_KEY "glass-text-opacity"
#define FURIOS_GLASS_ACCENT_TEXT_KEY "glass-accent-text-color"
#define FURIOS_GLASS_TEXT_SHADOW_KEY "glass-text-shadow"
#define FURIOS_GLASS_SHADOW_COLOR_KEY "glass-text-shadow-color"
#define FURIOS_WALLPAPER_FOLDER_KEY   "wallpaper-folder"
#define FURIOS_LOCKSCREEN_TINT_KEY    "lockscreen-tint"
#define PHOSH_PLUGINS_SCHEMA_ID "mobi.phosh.shell.plugins"
#define PHOSH_STATUS_ICONS_KEY "status-icons"
#define WEATHER_STATUS_ICON "weather-status-icon"

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
  AdwPreferencesGroup *glass_theme_group;
  AdwPreferencesGroup *glass_group;
  AdwPreferencesGroup *text_glass_group;
  GtkWidget           *accent_custom_row;
  GtkWidget           *accent_text_row;
  AdwSwitchRow        *glass_theme_switch;
  GtkWidget           *wallpaper_folder_row;
  AdwButtonContent    *wallpaper_folder_content;
  AdwSwitchRow        *lockscreen_tint_switch;
  AdwComboRow         *fit_row;
  gboolean             updating_fit;
  AdwComboRow         *wallpaper_mode_row;
  GtkWidget           *add_picture_button;
  GtkWidget           *chooser_bin;
  gboolean             updating_mode;
  GtkAdjustment *glass_blur_adjustment;
  GtkAdjustment *glass_opacity_adjustment;
  GtkAdjustment *glass_lightness_adjustment;
  GtkAdjustment *glass_accent_wash_adjustment;
  GtkColorDialogButton *custom_accent_button;
  GtkColorDialogButton *text_color_button;
  GtkColorDialogButton *accent_text_button;
  GtkAdjustment *glass_text_opacity_adjustment;
  GtkColorDialogButton *shadow_color_button;
  GtkAdjustment *glass_text_shadow_adjustment;
  gboolean updating_custom_accent;
  gboolean updating_text_color;
  gboolean updating_accent_text;
  gboolean updating_shadow_color;
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

/* glass-blur-radius is a uint, GtkAdjustment:value is a double */
static gboolean
glass_blur_get_mapping (GValue *value, GVariant *variant, gpointer user_data)
{
  g_value_set_double (value, (double) g_variant_get_uint32 (variant));

  return TRUE;
}

static GVariant *
glass_blur_set_mapping (const GValue *value, const GVariantType *type, gpointer user_data)
{
  double radius = CLAMP (g_value_get_double (value), 0.0, 200.0);

  return g_variant_new_uint32 ((guint32) (radius + 0.5));
}

static void
reload_custom_accent_button (CcBackgroundPanel *self)
{
  g_autofree char *custom = NULL;
  GdkRGBA rgba;

  if (self->furios_shell_settings == NULL)
    return;

  custom = g_settings_get_string (self->furios_shell_settings, FURIOS_ACCENT_COLOR_CUSTOM_KEY);

  /* Empty means "follow the swatches above". There is no such thing as an
   * unset colour on the button, so fall back to showing the accent it is
   * deferring to would need the enum -- just show it transparent instead. */
  if (custom == NULL || *custom == '\0' || !gdk_rgba_parse (&rgba, custom))
    rgba = (GdkRGBA) { 0.0, 0.0, 0.0, 0.0 };

  self->updating_custom_accent = TRUE;
  gtk_color_dialog_button_set_rgba (self->custom_accent_button, &rgba);
  self->updating_custom_accent = FALSE;
}

static void
on_custom_accent_changed_cb (GtkColorDialogButton *button,
                             GParamSpec           *pspec,
                             CcBackgroundPanel    *self)
{
  const GdkRGBA *rgba;
  g_autofree char *hex = NULL;

  if (self->updating_custom_accent || self->furios_shell_settings == NULL)
    return;

  rgba = gtk_color_dialog_button_get_rgba (self->custom_accent_button);
  if (rgba == NULL)
    return;

  /* The shell wants a plain colour, not rgba() with an alpha channel */
  hex = g_strdup_printf ("#%02x%02x%02x",
                         (int) (CLAMP (rgba->red,   0.0, 1.0) * 255.0 + 0.5),
                         (int) (CLAMP (rgba->green, 0.0, 1.0) * 255.0 + 0.5),
                         (int) (CLAMP (rgba->blue,  0.0, 1.0) * 255.0 + 0.5));

  g_settings_set_string (self->furios_shell_settings, FURIOS_ACCENT_COLOR_CUSTOM_KEY, hex);
}

static void
on_custom_accent_clear_cb (GtkButton         *button,
                           CcBackgroundPanel *self)
{
  if (self->furios_shell_settings == NULL)
    return;

  g_settings_reset (self->furios_shell_settings, FURIOS_ACCENT_COLOR_CUSTOM_KEY);
}

static void
reload_text_color_button (CcBackgroundPanel *self)
{
  g_autofree char *custom = NULL;
  GdkRGBA rgba;

  if (self->furios_shell_settings == NULL)
    return;

  custom = g_settings_get_string (self->furios_shell_settings, FURIOS_GLASS_TEXT_COLOR_KEY);

  if (custom == NULL || *custom == '\0' || !gdk_rgba_parse (&rgba, custom))
    rgba = (GdkRGBA) { 0.0, 0.0, 0.0, 0.0 };

  self->updating_text_color = TRUE;
  gtk_color_dialog_button_set_rgba (self->text_color_button, &rgba);
  self->updating_text_color = FALSE;
}

static void
on_text_color_changed_cb (GtkColorDialogButton *button,
                          GParamSpec           *pspec,
                          CcBackgroundPanel    *self)
{
  const GdkRGBA *rgba;
  g_autofree char *hex = NULL;

  if (self->updating_text_color || self->furios_shell_settings == NULL)
    return;

  rgba = gtk_color_dialog_button_get_rgba (self->text_color_button);
  if (rgba == NULL)
    return;

  /* The opacity is a separate setting, so only the colour goes here */
  hex = g_strdup_printf ("#%02x%02x%02x",
                         (int) (CLAMP (rgba->red,   0.0, 1.0) * 255.0 + 0.5),
                         (int) (CLAMP (rgba->green, 0.0, 1.0) * 255.0 + 0.5),
                         (int) (CLAMP (rgba->blue,  0.0, 1.0) * 255.0 + 0.5));

  g_settings_set_string (self->furios_shell_settings, FURIOS_GLASS_TEXT_COLOR_KEY, hex);
}

static void
on_text_color_clear_cb (GtkButton         *button,
                        CcBackgroundPanel *self)
{
  if (self->furios_shell_settings == NULL)
    return;

  g_settings_reset (self->furios_shell_settings, FURIOS_GLASS_TEXT_COLOR_KEY);
}

static void
reload_shadow_color_button (CcBackgroundPanel *self)
{
  g_autofree char *custom = NULL;
  GdkRGBA rgba;

  if (self->furios_shell_settings == NULL)
    return;

  custom = g_settings_get_string (self->furios_shell_settings, FURIOS_GLASS_SHADOW_COLOR_KEY);

  if (custom == NULL || *custom == '\0' || !gdk_rgba_parse (&rgba, custom))
    rgba = (GdkRGBA) { 0.0, 0.0, 0.0, 0.0 };

  self->updating_shadow_color = TRUE;
  gtk_color_dialog_button_set_rgba (self->shadow_color_button, &rgba);
  self->updating_shadow_color = FALSE;
}

static void
on_shadow_color_changed_cb (GtkColorDialogButton *button,
                            GParamSpec           *pspec,
                            CcBackgroundPanel    *self)
{
  const GdkRGBA *rgba;
  g_autofree char *hex = NULL;

  if (self->updating_shadow_color || self->furios_shell_settings == NULL)
    return;

  rgba = gtk_color_dialog_button_get_rgba (self->shadow_color_button);
  if (rgba == NULL)
    return;

  /* The strength is a separate setting, so only the colour goes here */
  hex = g_strdup_printf ("#%02x%02x%02x",
                         (int) (CLAMP (rgba->red,   0.0, 1.0) * 255.0 + 0.5),
                         (int) (CLAMP (rgba->green, 0.0, 1.0) * 255.0 + 0.5),
                         (int) (CLAMP (rgba->blue,  0.0, 1.0) * 255.0 + 0.5));

  g_settings_set_string (self->furios_shell_settings, FURIOS_GLASS_SHADOW_COLOR_KEY, hex);
}

static void
on_shadow_color_clear_cb (GtkButton         *button,
                          CcBackgroundPanel *self)
{
  if (self->furios_shell_settings == NULL)
    return;

  g_settings_reset (self->furios_shell_settings, FURIOS_GLASS_SHADOW_COLOR_KEY);
}

static void
reload_accent_text_button (CcBackgroundPanel *self)
{
  g_autofree char *custom = NULL;
  GdkRGBA rgba;

  if (self->furios_shell_settings == NULL)
    return;

  custom = g_settings_get_string (self->furios_shell_settings, FURIOS_GLASS_ACCENT_TEXT_KEY);

  if (custom == NULL || *custom == '\0' || !gdk_rgba_parse (&rgba, custom))
    rgba = (GdkRGBA) { 0.0, 0.0, 0.0, 0.0 };

  self->updating_accent_text = TRUE;
  gtk_color_dialog_button_set_rgba (self->accent_text_button, &rgba);
  self->updating_accent_text = FALSE;
}

static void
on_accent_text_changed_cb (GtkColorDialogButton *button,
                           GParamSpec           *pspec,
                           CcBackgroundPanel    *self)
{
  const GdkRGBA *rgba;
  g_autofree char *hex = NULL;

  if (self->updating_accent_text || self->furios_shell_settings == NULL)
    return;

  rgba = gtk_color_dialog_button_get_rgba (self->accent_text_button);
  if (rgba == NULL)
    return;

  hex = g_strdup_printf ("#%02x%02x%02x",
                         (int) (CLAMP (rgba->red,   0.0, 1.0) * 255.0 + 0.5),
                         (int) (CLAMP (rgba->green, 0.0, 1.0) * 255.0 + 0.5),
                         (int) (CLAMP (rgba->blue,  0.0, 1.0) * 255.0 + 0.5));

  g_settings_set_string (self->furios_shell_settings, FURIOS_GLASS_ACCENT_TEXT_KEY, hex);
}

static void
on_accent_text_clear_cb (GtkButton         *button,
                         CcBackgroundPanel *self)
{
  if (self->furios_shell_settings == NULL)
    return;

  g_settings_reset (self->furios_shell_settings, FURIOS_GLASS_ACCENT_TEXT_KEY);
}

/* The formats GdkPixbuf can load here, matched to phosh's own is_image_name()
 * so that the count shown and the pictures rotated through are the same set. */
static gboolean
is_picture_name (const char *name)
{
  static const char * const exts[] = {
    ".jpg", ".jpeg", ".png", ".webp", ".jxl", ".bmp", ".svg"
  };
  g_autofree char *lower = g_ascii_strdown (name, -1);

  for (guint i = 0; i < G_N_ELEMENTS (exts); i++) {
    if (g_str_has_suffix (lower, exts[i]))
      return TRUE;
  }

  return FALSE;
}


static void on_wallpaper_folder_clicked_cb (CcBackgroundPanel *self);


/*
 * The glass controls live in three places -- two rows inside the Accent group,
 * and the Text and Glass groups -- so that each row can be named for what it
 * does and let its group supply the context. They all appear and disappear
 * together, since they all read the same schema.
 */
static guint
count_pictures (const char *folder)
{
  g_autoptr (GDir) dir = g_dir_open (folder, 0, NULL);
  const char *name;
  guint n = 0;

  if (dir == NULL)
    return 0;

  while ((name = g_dir_read_name (dir))) {
    if (is_picture_name (name))
      n++;
  }

  return n;
}


/*
 * The picture-options values, in the order the Fit row lists them. "none" is
 * left out deliberately: it means no picture at all, which is not a way of
 * fitting one and would look like a broken choice in a list of them.
 */
static const struct {
  const char             *nick;
  GDesktopBackgroundStyle style;
} fit_options[] = {
  { "zoom",      G_DESKTOP_BACKGROUND_STYLE_ZOOM },      /* Fill: cover, crop the rest */
  { "scaled",    G_DESKTOP_BACKGROUND_STYLE_SCALED },    /* Fit: all of it, bars where short */
  { "stretched", G_DESKTOP_BACKGROUND_STYLE_STRETCHED }, /* Stretch: cover, distort */
  { "centered",  G_DESKTOP_BACKGROUND_STYLE_CENTERED },  /* Centered: original size */
  { "wallpaper", G_DESKTOP_BACKGROUND_STYLE_WALLPAPER }, /* Tiled: repeated */
  { "spanned",   G_DESKTOP_BACKGROUND_STYLE_SPANNED },   /* Spanned: across monitors */
};


static void
reload_fit_row (CcBackgroundPanel *self)
{
  GDesktopBackgroundStyle current = g_settings_get_enum (self->settings, WP_OPTIONS_KEY);
  guint selected = 0;

  for (guint i = 0; i < G_N_ELEMENTS (fit_options); i++) {
    if (fit_options[i].style == current) {
      selected = i;
      break;
    }
  }

  /* Anything unrecognised, "none" included, shows as Fill without writing it
   * back: the setting is only changed when the user picks something. */
  self->updating_fit = TRUE;
  adw_combo_row_set_selected (self->fit_row, selected);
  self->updating_fit = FALSE;
}


static void
on_fit_changed_cb (CcBackgroundPanel *self)
{
  guint selected = adw_combo_row_get_selected (self->fit_row);

  if (self->updating_fit || selected >= G_N_ELEMENTS (fit_options))
    return;

  /* set_enum, not set_string: picture-options is an enum key, and writing a
   * string to one fails rather than converting. Both screens together, since
   * the lock screen takes its picture from the same rotation. */
  g_settings_set_enum (self->settings, WP_OPTIONS_KEY, fit_options[selected].style);
  g_settings_set_enum (self->lock_settings, WP_OPTIONS_KEY, fit_options[selected].style);

  /* BOTH objects are in delayed mode -- see g_settings_delay() in init -- so a
   * set() alone only fills the pending buffer. It reads back as the new value,
   * which makes the write look like it worked while nothing has reached dconf
   * and the shell sees nothing at all. */
  g_settings_apply (self->settings);
  g_settings_apply (self->lock_settings);
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
/*
 * Which of the two sources is in use is not a setting of its own: phosh treats
 * a non-empty wallpaper-folder as "rotate" and an empty one as "leave the
 * wallpaper alone", so that key already *is* the mode. Deriving the toggle
 * from it keeps one source of truth, and means an older phosh needs no change
 * to understand what this panel writes.
 */
static gboolean
wallpaper_folder_mode (CcBackgroundPanel *self)
{
  g_autofree char *folder = NULL;

  if (self->furios_shell_settings == NULL)
    return FALSE;

  folder = g_settings_get_string (self->furios_shell_settings, FURIOS_WALLPAPER_FOLDER_KEY);

  return folder != NULL && *folder != '\0';
}


/*
 * Show exactly the control that belongs to the current source. Leaving the
 * picture grid up in folder mode is what made the two modes fight: a picture
 * chosen there looks like it took, and is then overwritten by the next
 * rotation with nothing to explain why.
 *
 * Fit stays visible either way -- it describes how a picture is sized, which
 * is as true of a rotated one as of a fixed one.
 */
static void
update_wallpaper_mode_rows (CcBackgroundPanel *self)
{
  gboolean folder = wallpaper_folder_mode (self);

  gtk_widget_set_visible (self->chooser_bin, !folder);
  gtk_widget_set_visible (self->add_picture_button, !folder);
  gtk_widget_set_visible (self->wallpaper_folder_row, folder);

  self->updating_mode = TRUE;
  adw_combo_row_set_selected (self->wallpaper_mode_row, folder ? 1 : 0);
  self->updating_mode = FALSE;
}


/*
 * Put the first picture of the folder up straight away.
 *
 * Without this, choosing a folder wrote the setting and changed nothing on
 * screen -- the first picture from it only appeared at the next lock, which
 * reads as the folder having been ignored.
 */
static void
apply_first_picture_from_folder (CcBackgroundPanel *self)
{
  g_autofree char *folder = NULL;
  g_autoptr (GDir) dir = NULL;
  g_autoptr (GPtrArray) names = NULL;
  g_autofree char *path = NULL;
  g_autofree char *uri = NULL;
  const char *name;

  if (self->furios_shell_settings == NULL)
    return;

  folder = g_settings_get_string (self->furios_shell_settings, FURIOS_WALLPAPER_FOLDER_KEY);
  if (folder == NULL || *folder == '\0')
    return;

  dir = g_dir_open (folder, 0, NULL);
  if (dir == NULL)
    return;

  names = g_ptr_array_new_with_free_func (g_free);
  while ((name = g_dir_read_name (dir))) {
    if (is_picture_name (name))
      g_ptr_array_add (names, g_strdup (name));
  }

  if (names->len == 0)
    return;

  /* Filename order, the same order phosh rotates in, so what appears now is
   * the start of the sequence rather than a picture out of nowhere. */
  g_ptr_array_sort_values (names, (GCompareFunc) g_strcmp0);

  path = g_build_filename (folder, g_ptr_array_index (names, 0), NULL);
  uri = g_filename_to_uri (path, NULL, NULL);
  if (uri == NULL)
    return;

  g_settings_set_string (self->settings, WP_URI_KEY, uri);
  g_settings_set_string (self->settings, WP_URI_DARK_KEY, uri);
  g_settings_set_string (self->lock_settings, WP_URI_KEY, uri);

  /* Both objects are in delayed mode -- see g_settings_delay() in init. A
   * set() alone reads back as the new value while nothing reaches dconf. */
  g_settings_apply (self->settings);
  g_settings_apply (self->lock_settings);
}


static void
on_wallpaper_mode_changed_cb (CcBackgroundPanel *self)
{
  gboolean want_folder = adw_combo_row_get_selected (self->wallpaper_mode_row) == 1;

  if (self->updating_mode || self->furios_shell_settings == NULL)
    return;

  if (want_folder == wallpaper_folder_mode (self))
    return;

  if (!want_folder) {
    /* Clearing the folder is what turns rotation off; the picture on screen
     * stays as it is and becomes the single wallpaper. */
    g_settings_set_string (self->furios_shell_settings, FURIOS_WALLPAPER_FOLDER_KEY, "");
    update_wallpaper_mode_rows (self);
    return;
  }

  /* Switching to folder mode with nothing chosen has no folder to rotate
   * through, so ask for one immediately rather than leaving a mode that does
   * nothing. The rows update when the choice comes back. */
  update_wallpaper_mode_rows (self);
  on_wallpaper_folder_clicked_cb (self);
}


static void
reload_wallpaper_folder (CcBackgroundPanel *self)
{
  g_autofree char *folder = NULL;
  g_autofree char *label = NULL;
  g_autoptr (GFile) file = NULL;
  g_autofree char *name = NULL;
  guint n;

  if (self->furios_shell_settings == NULL)
    return;

  folder = g_settings_get_string (self->furios_shell_settings, FURIOS_WALLPAPER_FOLDER_KEY);

  if (folder == NULL || *folder == '\0') {
    adw_button_content_set_label (self->wallpaper_folder_content, _("Choose\u2026"));
    return;
  }

  /* Say how many pictures were found: the folder name alone gives no clue
   * whether the right place was picked, and an empty folder is silently a
   * no-op at the other end. */
  file = g_file_new_for_path (folder);
  name = g_file_get_basename (file);
  n = count_pictures (folder);
  label = g_strdup_printf (n == 1 ? _("%s (%u picture)") : _("%s (%u pictures)"), name, n);
  adw_button_content_set_label (self->wallpaper_folder_content, label);
}


static void
on_wallpaper_folder_selected (GObject *source, GAsyncResult *res, gpointer data)
{
  CcBackgroundPanel *self = data;
  g_autoptr (GFile) picture = NULL;
  g_autoptr (GFile) folder = NULL;
  g_autoptr (GError) err = NULL;
  g_autofree char *path = NULL;

  picture = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (source), res, &err);
  if (picture == NULL) {
    /* Dismissed is not an error worth reporting */
    if (!g_error_matches (err, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
      g_warning ("Could not pick a wallpaper folder: %s", err->message);
    return;
  }

  folder = g_file_get_parent (picture);
  path = folder ? g_file_get_path (folder) : NULL;
  if (path == NULL) {
    g_warning ("Wallpaper rotation needs a picture in a local folder");
    return;
  }

  g_settings_set_string (self->furios_shell_settings, FURIOS_WALLPAPER_FOLDER_KEY, path);
  reload_wallpaper_folder (self);
  update_wallpaper_mode_rows (self);
  apply_first_picture_from_folder (self);
}


static void
on_wallpaper_folder_clicked_cb (CcBackgroundPanel *self)
{
  g_autoptr (GtkFileDialog) dialog = gtk_file_dialog_new ();
  g_autoptr (GtkFileFilter) filter = gtk_file_filter_new ();
  g_autoptr (GListStore) filters = g_list_store_new (GTK_TYPE_FILE_FILTER);

  if (self->furios_shell_settings == NULL)
    return;

  /* Ask for a picture rather than a folder, and use the folder it is in.
   * GTK's folder chooser lists directories only, so a folder full of pictures
   * with nothing but pictures in it looks completely empty, which reads as the
   * chooser being broken. */
  gtk_file_filter_set_name (filter, _("Pictures"));
  gtk_file_filter_add_mime_type (filter, "image/*");
  g_list_store_append (filters, filter);
  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
  gtk_file_dialog_set_title (dialog, _("Pick Any Picture in the Folder"));

  gtk_file_dialog_open (dialog,
                        GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (self))),
                        NULL,
                        on_wallpaper_folder_selected,
                        self);
}


static void
on_wallpaper_folder_clear_cb (CcBackgroundPanel *self)
{
  if (self->furios_shell_settings == NULL)
    return;

  g_settings_reset (self->furios_shell_settings, FURIOS_WALLPAPER_FOLDER_KEY);
  reload_wallpaper_folder (self);
  /* Clearing the folder is the same thing as going back to a single picture */
  update_wallpaper_mode_rows (self);
}


static void
set_glass_rows_visible (CcBackgroundPanel *self, gboolean visible)
{
  gtk_widget_set_visible (self->accent_custom_row, visible);
  gtk_widget_set_visible (self->accent_text_row, visible);
  gtk_widget_set_visible (GTK_WIDGET (self->text_glass_group), visible);
  gtk_widget_set_visible (GTK_WIDGET (self->glass_theme_group), visible);
  gtk_widget_set_visible (GTK_WIDGET (self->glass_group), visible);
  gtk_widget_set_visible (self->wallpaper_folder_row, visible);
  gtk_widget_set_visible (GTK_WIDGET (self->lockscreen_tint_switch), visible);
}


static void
setup_glass_group (CcBackgroundPanel *self)
{
  GSettingsSchemaSource *schema_source;
  g_autoptr(GSettingsSchema) schema = NULL;
  const char * const keys[] = {
    FURIOS_GLASS_THEME_KEY,
    FURIOS_GLASS_BLUR_RADIUS_KEY,
    FURIOS_GLASS_OPACITY_KEY,
    FURIOS_GLASS_LIGHTNESS_KEY,
    FURIOS_GLASS_ACCENT_WASH_KEY,
    FURIOS_ACCENT_COLOR_CUSTOM_KEY,
    FURIOS_GLASS_TEXT_COLOR_KEY,
    FURIOS_GLASS_TEXT_OPACITY_KEY,
    FURIOS_GLASS_ACCENT_TEXT_KEY,
    FURIOS_GLASS_TEXT_SHADOW_KEY,
    FURIOS_GLASS_SHADOW_COLOR_KEY,
    FURIOS_WALLPAPER_FOLDER_KEY,
    FURIOS_LOCKSCREEN_TINT_KEY,
  };

  schema_source = g_settings_schema_source_get_default ();

  if (schema_source != NULL)
    schema = g_settings_schema_source_lookup (schema_source,
                                              FURIOS_SHELL_SCHEMA_ID,
                                              TRUE);

  /* An older phosh has the schema but not these keys, and asking it for one
   * that is missing aborts the process */
  for (guint i = 0; schema != NULL && i < G_N_ELEMENTS (keys); i++) {
    if (!g_settings_schema_has_key (schema, keys[i])) {
      g_clear_pointer (&schema, g_settings_schema_unref);
      break;
    }
  }

  if (schema == NULL || ensure_furios_shell_settings (self) == NULL) {
    set_glass_rows_visible (self, FALSE);
    return;
  }

  set_glass_rows_visible (self, TRUE);
  g_settings_bind (self->furios_shell_settings, FURIOS_GLASS_THEME_KEY,
                   self->glass_theme_switch, "active", G_SETTINGS_BIND_DEFAULT);

  /* The rest of the glass settings only reach the shell while the theme is
   * on, so they follow the switch rather than sitting there doing nothing */
  g_object_bind_property (self->glass_theme_switch, "active",
                          self->glass_group, "sensitive",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (self->glass_theme_switch, "active",
                          self->text_glass_group, "sensitive",
                          G_BINDING_SYNC_CREATE);
  /* The custom accent still applies without the glass theme, the text drawn
   * on it does not: that colour only exists in the generated glass CSS. */
  g_object_bind_property (self->glass_theme_switch, "active",
                          self->accent_text_row, "sensitive",
                          G_BINDING_SYNC_CREATE);

  /* Only now: everything below reads through furios_shell_settings, and
   * reload_wallpaper_folder() returns silently while it is still NULL -- which
   * is why the folder row came up empty on every launch while the key it reads
   * was set the whole time. */
  reload_wallpaper_folder (self);
  update_wallpaper_mode_rows (self);
  g_settings_bind (self->furios_shell_settings, FURIOS_LOCKSCREEN_TINT_KEY,
                   self->lockscreen_tint_switch, "active", G_SETTINGS_BIND_DEFAULT);

  g_settings_bind_with_mapping (self->furios_shell_settings,
                                FURIOS_GLASS_BLUR_RADIUS_KEY,
                                self->glass_blur_adjustment, "value",
                                G_SETTINGS_BIND_DEFAULT,
                                glass_blur_get_mapping,
                                glass_blur_set_mapping,
                                NULL, NULL);
  g_settings_bind (self->furios_shell_settings, FURIOS_GLASS_OPACITY_KEY,
                   self->glass_opacity_adjustment, "value", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->furios_shell_settings, FURIOS_GLASS_LIGHTNESS_KEY,
                   self->glass_lightness_adjustment, "value", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->furios_shell_settings, FURIOS_GLASS_ACCENT_WASH_KEY,
                   self->glass_accent_wash_adjustment, "value", G_SETTINGS_BIND_DEFAULT);

  g_settings_bind (self->furios_shell_settings, FURIOS_GLASS_TEXT_OPACITY_KEY,
                   self->glass_text_opacity_adjustment, "value", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->furios_shell_settings, FURIOS_GLASS_TEXT_SHADOW_KEY,
                   self->glass_text_shadow_adjustment, "value", G_SETTINGS_BIND_DEFAULT);

  reload_custom_accent_button (self);
  reload_text_color_button (self);
  reload_accent_text_button (self);
  reload_shadow_color_button (self);

  g_signal_connect_object (self->furios_shell_settings,
                           "changed::" FURIOS_GLASS_ACCENT_TEXT_KEY,
                           G_CALLBACK (reload_accent_text_button),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->furios_shell_settings,
                           "changed::" FURIOS_GLASS_SHADOW_COLOR_KEY,
                           G_CALLBACK (reload_shadow_color_button),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->furios_shell_settings,
                           "changed::" FURIOS_GLASS_TEXT_COLOR_KEY,
                           G_CALLBACK (reload_text_color_button),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->furios_shell_settings,
                           "changed::" FURIOS_ACCENT_COLOR_CUSTOM_KEY,
                           G_CALLBACK (reload_custom_accent_button),
                           self,
                           G_CONNECT_SWAPPED);
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

  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, glass_group);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, text_glass_group);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, accent_custom_row);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, accent_text_row);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, glass_theme_group);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, glass_theme_switch);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, wallpaper_folder_row);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, wallpaper_folder_content);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, lockscreen_tint_switch);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, fit_row);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, wallpaper_mode_row);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, add_picture_button);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, chooser_bin);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, status_indicator_group);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, weather_status_switch);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, glass_blur_adjustment);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, glass_opacity_adjustment);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, glass_lightness_adjustment);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, glass_accent_wash_adjustment);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, custom_accent_button);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, text_color_button);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, accent_text_button);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, glass_text_opacity_adjustment);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, shadow_color_button);
  gtk_widget_class_bind_template_child (widget_class, CcBackgroundPanel, glass_text_shadow_adjustment);
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
  gtk_widget_class_bind_template_callback (widget_class, on_custom_accent_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_custom_accent_clear_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_text_color_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_text_color_clear_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_accent_text_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_accent_text_clear_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_shadow_color_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_shadow_color_clear_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_wallpaper_mode_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_wallpaper_folder_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_wallpaper_folder_clear_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_fit_changed_cb);
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

  /* Fit follows the home screen's setting; the two are written together */
  reload_fit_row (self);
  g_signal_connect_object (self->settings, "changed::" WP_OPTIONS_KEY,
                           G_CALLBACK (reload_fit_row), self, G_CONNECT_SWAPPED);

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

  setup_glass_group (self);
  setup_weather_status_switch (self);

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
