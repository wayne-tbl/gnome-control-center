/*
 * Copyright (C) 2024 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-display-customization.h"
#include "cc-display-resources.h"
#include "cc-util.h"

#include <adwaita.h>
#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>

#define PQ_DBUS_NAME          "io.FuriOS.PQ"
#define PQ_DBUS_PATH          "/io/FuriOS/PQ"
#define PQ_DBUS_INTERFACE     "io.FuriOS.PQ"

struct _CcDisplayCustomization {
  CcPanel            parent;
  GtkWidget          *pq_mode_switch;
  GtkWidget          *display_color_switch;
  GtkWidget          *content_color_switch;
  GtkWidget          *content_color_video_switch;
  GtkWidget          *sharpness_switch;
  GtkWidget          *dynamic_contrast_switch;
  GtkWidget          *dynamic_sharpness_switch;
  GtkWidget          *display_ccorr_switch;
  GtkWidget          *display_gamma_switch;
  GtkWidget          *display_over_drive_switch;
  GtkWidget          *iso_adaptive_sharpness_switch;
  GtkWidget          *ultra_resolution_switch;
  GtkWidget          *video_hdr_switch;

  GSettings          *settings;
};

G_DEFINE_TYPE (CcDisplayCustomization, cc_display_customization, CC_TYPE_PANEL)

static void
pq_set_feature (const char *method, int mode)
{
  GDBusProxy *pq_proxy;
  GError *error = NULL;

  pq_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    PQ_DBUS_NAME,
    PQ_DBUS_PATH,
    PQ_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return;
  }

  g_dbus_proxy_call(
    pq_proxy,
    method,
    g_variant_new("(i)", mode),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    NULL,
    NULL
  );

  g_object_unref (pq_proxy);
}

static void
pq_mode_switch_toggled (GtkSwitch *widget, GParamSpec *pspec, gpointer user_data)
{
  gboolean active = gtk_switch_get_active (widget);
  pq_set_feature ("SetPQMode", active ? 1 : 0);
}

static void
display_color_switch_toggled (GtkSwitch *widget, GParamSpec *pspec, gpointer user_data)
{
  gboolean active = gtk_switch_get_active (widget);
  pq_set_feature ("SetFeatureDisplayColor", active ? 1 : 0);
}

static void
content_color_switch_toggled (GtkSwitch *widget, GParamSpec *pspec, gpointer user_data)
{
  gboolean active = gtk_switch_get_active (widget);
  pq_set_feature ("SetFeatureContentColor", active ? 1 : 0);
}

static void
content_color_video_switch_toggled (GtkSwitch *widget, GParamSpec *pspec, gpointer user_data)
{
  gboolean active = gtk_switch_get_active (widget);
  pq_set_feature ("SetFeatureContentColorVideo", active ? 1 : 0);
}

static void
sharpness_switch_toggled (GtkSwitch *widget, GParamSpec *pspec, gpointer user_data)
{
  gboolean active = gtk_switch_get_active (widget);
  pq_set_feature ("SetFeatureSharpness", active ? 1 : 0);
}

static void
dynamic_contrast_switch_toggled (GtkSwitch *widget, GParamSpec *pspec, gpointer user_data)
{
  gboolean active = gtk_switch_get_active (widget);
  pq_set_feature ("SetFeatureDynamicContrast", active ? 1 : 0);
}

static void
dynamic_sharpness_switch_toggled (GtkSwitch *widget, GParamSpec *pspec, gpointer user_data)
{
  gboolean active = gtk_switch_get_active (widget);
  pq_set_feature ("SetFeatureDynamicSharpness", active ? 1 : 0);
}

static void
display_ccorr_switch_toggled (GtkSwitch *widget, GParamSpec *pspec, gpointer user_data)
{
  gboolean active = gtk_switch_get_active (widget);
  pq_set_feature ("SetFeatureDisplayCCorr", active ? 1 : 0);
}

static void
display_gamma_switch_toggled (GtkSwitch *widget, GParamSpec *pspec, gpointer user_data)
{
  gboolean active = gtk_switch_get_active (widget);
  pq_set_feature ("SetFeatureDisplayGamma", active ? 1 : 0);
}

static void
display_over_drive_switch_toggled (GtkSwitch *widget, GParamSpec *pspec, gpointer user_data)
{
  gboolean active = gtk_switch_get_active (widget);
  pq_set_feature ("SetFeatureDisplayOverDrive", active ? 1 : 0);
}

static void
iso_adaptive_sharpness_switch_toggled (GtkSwitch *widget, GParamSpec *pspec, gpointer user_data)
{
  gboolean active = gtk_switch_get_active (widget);
  pq_set_feature ("SetFeatureISOAdaptiveSharpness", active ? 1 : 0);
}

static void
ultra_resolution_switch_toggled (GtkSwitch *widget, GParamSpec *pspec, gpointer user_data)
{
  gboolean active = gtk_switch_get_active (widget);
  pq_set_feature ("SetFeatureUltraResolution", active ? 1 : 0);
}

static void
video_hdr_switch_toggled (GtkSwitch *widget, GParamSpec *pspec, gpointer user_data)
{
  gboolean active = gtk_switch_get_active (widget);
  pq_set_feature ("SetFeatureVideoHDR", active ? 1 : 0);
}

static void
cc_display_customization_finalize (GObject *object)
{
  CcDisplayCustomization *self = CC_DISPLAY_CUSTOMIZATION (object);

  g_clear_object (&self->settings);

  G_OBJECT_CLASS (cc_display_customization_parent_class)->finalize (object);
}

static void
cc_display_customization_class_init (CcDisplayCustomizationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_display_customization_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/display/cc-display-customization.ui");

  gtk_widget_class_bind_template_child (widget_class,
                                        CcDisplayCustomization,
                                        pq_mode_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcDisplayCustomization,
                                        display_color_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcDisplayCustomization,
                                        content_color_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcDisplayCustomization,
                                        content_color_video_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcDisplayCustomization,
                                        sharpness_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcDisplayCustomization,
                                        dynamic_contrast_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcDisplayCustomization,
                                        dynamic_sharpness_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcDisplayCustomization,
                                        display_ccorr_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcDisplayCustomization,
                                        display_gamma_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcDisplayCustomization,
                                        display_over_drive_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcDisplayCustomization,
                                        iso_adaptive_sharpness_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcDisplayCustomization,
                                        ultra_resolution_switch);
  gtk_widget_class_bind_template_child (widget_class,
                                        CcDisplayCustomization,
                                        video_hdr_switch);
}

static void
cc_display_customization_init (CcDisplayCustomization *self)
{
  g_resources_register (cc_display_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));
  g_autoptr(GtkCssProvider) provider = NULL;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider, "/org/gnome/control-center/display/preview.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  GSettingsSchemaSource *schema_source = g_settings_schema_source_get_default ();
  if (schema_source) {
    GSettingsSchema *schema = g_settings_schema_source_lookup (schema_source, "io.furios.pq", TRUE);
    if (schema) {
      self->settings = g_settings_new ("io.furios.pq");

      gtk_switch_set_active (GTK_SWITCH (self->pq_mode_switch),
                             g_settings_get_int (self->settings, "pq-mode") == 1);
      gtk_switch_set_active (GTK_SWITCH (self->display_color_switch),
                             g_settings_get_int (self->settings, "display-color") == 1);
      gtk_switch_set_active (GTK_SWITCH (self->content_color_switch),
                             g_settings_get_int (self->settings, "content-color") == 1);
      gtk_switch_set_active (GTK_SWITCH (self->content_color_video_switch),
                             g_settings_get_int (self->settings, "content-color-video") == 1);
      gtk_switch_set_active (GTK_SWITCH (self->sharpness_switch),
                             g_settings_get_int (self->settings, "sharpness") == 1);
      gtk_switch_set_active (GTK_SWITCH (self->dynamic_contrast_switch),
                             g_settings_get_int (self->settings, "dynamic-contrast") == 1);
      gtk_switch_set_active (GTK_SWITCH (self->dynamic_sharpness_switch),
                             g_settings_get_int (self->settings, "dynamic-sharpness") == 1);
      gtk_switch_set_active (GTK_SWITCH (self->display_ccorr_switch),
                             g_settings_get_int (self->settings, "display-ccorr") == 1);
      gtk_switch_set_active (GTK_SWITCH (self->display_gamma_switch),
                             g_settings_get_int (self->settings, "display-gamma") == 1);
      gtk_switch_set_active (GTK_SWITCH (self->display_over_drive_switch),
                             g_settings_get_int (self->settings, "display-over-drive") == 1);
      gtk_switch_set_active (GTK_SWITCH (self->iso_adaptive_sharpness_switch),
                             g_settings_get_int (self->settings, "iso-adaptive-sharpness") == 1);
      gtk_switch_set_active (GTK_SWITCH (self->ultra_resolution_switch),
                             g_settings_get_int (self->settings, "ultra-resolution") == 1);
      gtk_switch_set_active (GTK_SWITCH (self->video_hdr_switch),
                             g_settings_get_int (self->settings, "video-hdr") == 1);
    }
  }

  g_signal_connect (self->pq_mode_switch, "notify::active", G_CALLBACK (pq_mode_switch_toggled), NULL);
  g_signal_connect (self->display_color_switch, "notify::active", G_CALLBACK (display_color_switch_toggled), NULL);
  g_signal_connect (self->content_color_switch, "notify::active", G_CALLBACK (content_color_switch_toggled), NULL);
  g_signal_connect (self->content_color_video_switch, "notify::active", G_CALLBACK (content_color_video_switch_toggled), NULL);
  g_signal_connect (self->sharpness_switch, "notify::active", G_CALLBACK (sharpness_switch_toggled), NULL);
  g_signal_connect (self->dynamic_contrast_switch, "notify::active", G_CALLBACK (dynamic_contrast_switch_toggled), NULL);
  g_signal_connect (self->dynamic_sharpness_switch, "notify::active", G_CALLBACK (dynamic_sharpness_switch_toggled), NULL);
  g_signal_connect (self->display_ccorr_switch, "notify::active", G_CALLBACK (display_ccorr_switch_toggled), NULL);
  g_signal_connect (self->display_gamma_switch, "notify::active", G_CALLBACK (display_gamma_switch_toggled), NULL);
  g_signal_connect (self->display_over_drive_switch, "notify::active", G_CALLBACK (display_over_drive_switch_toggled), NULL);
  g_signal_connect (self->iso_adaptive_sharpness_switch, "notify::active", G_CALLBACK (iso_adaptive_sharpness_switch_toggled), NULL);
  g_signal_connect (self->ultra_resolution_switch, "notify::active", G_CALLBACK (ultra_resolution_switch_toggled), NULL);
  g_signal_connect (self->video_hdr_switch, "notify::active", G_CALLBACK (video_hdr_switch_toggled), NULL);
}

CcDisplayCustomization *
cc_display_customization_new (void)
{
  return CC_DISPLAY_CUSTOMIZATION (g_object_new (CC_TYPE_DISPLAY_CUSTOMIZATION, NULL));
}
