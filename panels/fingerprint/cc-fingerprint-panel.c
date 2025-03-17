/*
 * Copyright (C) 2024 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-fingerprint-panel.h"
#include "cc-fingerprint-resources.h"
#include "cc-util.h"

#include <glib/gi18n.h>
#include <adwaita.h>

#define FPD_DBUS_NAME         "org.droidian.fingerprint"
#define FPD_DBUS_PATH         "/org/droidian/fingerprint"
#define FPD_DBUS_INTERFACE    "org.droidian.fingerprint"

struct _CcFingerprintPanel {
  CcPanel            parent;
  AdwToastOverlay   *toast_overlay;
  GtkPicture        *fingerprint_image;
  GtkProgressBar    *enroll_progress;
  GtkListBox        *finger_list;
  GtkListBox        *unenrolled_finger_list;
  gboolean           enrolling;
  gboolean           enrollment_done;
  gboolean           identification_done;
  gboolean           finger_canceled;
  gboolean           awaiting_cancel;
  gboolean           wants_remove;
  gboolean           sensitive;
  GList             *finger_widgets;
  gchar             *selected_finger;
  AdwBottomSheet    *bottom_sheet;
  GtkStack          *enroll_stack;
  GtkScrolledWindow *select_finger_step;
  GtkScrolledWindow *enroll_step;
  AdwAlertDialog    *delete_dialog;
};

G_DEFINE_TYPE (CcFingerprintPanel, cc_fingerprint_panel, CC_TYPE_PANEL)

static void
cc_fingerprint_panel_finalize (GObject *object)
{
  CcFingerprintPanel *self = CC_FINGERPRINT_PANEL (object);

  g_list_free_full (self->finger_widgets, g_object_unref);
  g_free (self->selected_finger);

  G_OBJECT_CLASS (cc_fingerprint_panel_parent_class)->finalize (object);
}

static void
show_toast (CcFingerprintPanel *self, const char *format, ...)
{
  va_list args;
  char *message;
  AdwToast *toast;

  va_start (args, format);
  message = g_strdup_vprintf (format, args);
  va_end (args);

  toast = adw_toast_new (message);
  adw_toast_set_timeout (toast, 3);

  adw_toast_overlay_dismiss_all (self->toast_overlay);
  adw_toast_overlay_add_toast (self->toast_overlay, toast);

  g_free (message);
}

static gchar **
get_enrolled_fingers (void)
{
  GDBusProxy *fpd_proxy;
  GError *error = NULL;
  GVariant *result;
  gchar **fpd_fingers = NULL;

  fpd_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    FPD_DBUS_NAME,
    FPD_DBUS_PATH,
    FPD_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return NULL;
  }

  result = g_dbus_proxy_call_sync(
    fpd_proxy,
    "GetAll",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling GetAll: %s\n", error->message);
    g_clear_error (&error);
    g_object_unref (fpd_proxy);
    return NULL;
  } else {
    GVariant *fpd_list;
    fpd_list = g_variant_get_child_value (result, 0);
    fpd_fingers = g_variant_dup_strv (fpd_list, NULL);
    g_variant_unref (fpd_list);
    g_variant_unref (result);
  }

  g_object_unref (fpd_proxy);
  return fpd_fingers;
}

static GtkWidget *
create_finger_row (const gchar *finger_name, gboolean add_delete)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_top (box, 12);
  gtk_widget_set_margin_bottom (box, 8);
  gtk_widget_set_margin_start (box, 16);
  gtk_widget_set_margin_end (box, 16);

  GtkWidget *icon = gtk_image_new_from_icon_name ("auth-fingerprint-symbolic");
  gtk_image_set_icon_size (GTK_IMAGE (icon), GTK_ICON_SIZE_LARGE);
  gtk_box_append (GTK_BOX (box), icon);

  GtkWidget *label = gtk_label_new (finger_name);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_box_append (GTK_BOX (box), label);

  if (add_delete) {
    GtkWidget *delete_icon = gtk_image_new_from_icon_name ("user-trash-symbolic");
    gtk_image_set_icon_size (GTK_IMAGE (delete_icon), GTK_ICON_SIZE_NORMAL);
    gtk_box_append (GTK_BOX (box), delete_icon);
  }

  return box;
}

static GtkWidget *
create_register_finger_row ()
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_top (box, 8);
  gtk_widget_set_margin_bottom (box, 8);
  gtk_widget_set_margin_start (box, 16);
  gtk_widget_set_margin_end (box, 16);

  GtkWidget *icon = gtk_image_new_from_icon_name ("list-add");
  gtk_image_set_pixel_size (GTK_IMAGE (icon), 20);
  gtk_widget_set_margin_top (icon, 6);
  gtk_widget_set_margin_bottom (icon, 6);
  gtk_widget_set_margin_start (icon, 6);
  gtk_widget_set_margin_end (icon, 6);
  gtk_box_append (GTK_BOX (box), icon);

  GtkWidget *label = gtk_label_new (_("Register New Finger"));
  gtk_widget_set_hexpand (label, TRUE);
  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_box_append (GTK_BOX (box), label);

  return box;
}

static void
refresh_fingerprint_list (CcFingerprintPanel *self, gboolean show_enrolled, GtkListBox *target_list)
{
  gchar *all_fingers[] = {
    "right-index-finger",
    "left-index-finger",
    "right-thumb",
    "right-middle-finger",
    "right-ring-finger",
    "right-little-finger",
    "left-thumb",
    "left-middle-finger",
    "left-ring-finger",
    "left-little-finger",
    NULL
  };

  gchar **enrolled_fingers = get_enrolled_fingers ();

  gtk_list_box_remove_all (target_list);

  if (show_enrolled) {
    g_list_free_full (self->finger_widgets, g_object_unref);
    self->finger_widgets = NULL;
  }

  for (int i = 0; all_fingers[i] != NULL; i++) {
    gboolean is_enrolled = g_strv_contains ((const gchar *const *) enrolled_fingers, all_fingers[i]);
    if ((show_enrolled && is_enrolled) || (!show_enrolled && !is_enrolled)) {
      GtkWidget *row = create_finger_row (all_fingers[i], is_enrolled);
      gtk_list_box_append (target_list, row);
      if (show_enrolled) {
        self->finger_widgets = g_list_append (self->finger_widgets, g_object_ref (row));
      }
    }
  }

  if (show_enrolled) {
    GtkWidget *row = create_register_finger_row ();
    gtk_widget_set_name (row, "register-finger");
    gtk_list_box_append (target_list, row);
  }

  g_strfreev (enrolled_fingers);
}

static void
cc_fingerprint_panel_remove_finger (AdwDialog *dialog, gchar *response, CcFingerprintPanel *self)
{
  if (g_strcmp0 (response, "remove")) return;

  if (self->selected_finger != NULL) {
    self->wants_remove = TRUE;
  } else {
    show_toast (self, "Please select a finger to remove");
  }
}

static void
handle_signal (GDBusProxy *proxy, gchar *sender_name, gchar *signal_name, GVariant *parameters, gpointer user_data);

static gboolean
cc_fingerprint_panel_delayed_refresh (gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;

  refresh_fingerprint_list (self, TRUE, self->finger_list);
  refresh_fingerprint_list (self, FALSE, self->unenrolled_finger_list);

  return FALSE;
}

static gpointer
fingerprint_worker_thread (gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;
  GError *error = NULL;
  GVariant *result;
  GDBusProxy *proxy;

  proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    FPD_DBUS_NAME,
    FPD_DBUS_PATH,
    FPD_DBUS_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_warning ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return NULL;
  }

  g_signal_connect (proxy, "g-signal", G_CALLBACK (handle_signal), self);

  while (1) {
    while (self->awaiting_cancel) {
      g_usleep (500 * 100);
    }

    // Let things settle for a moment...
    g_usleep (1000 * 100);

    if (self->wants_remove) {
      result = g_dbus_proxy_call_sync(
        proxy,
        "Remove",
        g_variant_new ("(s)", self->selected_finger),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
      );

      if (error) {
        g_debug ("Error calling Remove: %s\n", error->message);
        g_clear_error (&error);
      }

      g_variant_unref (result);
      self->wants_remove = FALSE;
      g_timeout_add (200, cc_fingerprint_panel_delayed_refresh, self);
    } else if (!self->enrolling) {
      self->identification_done = FALSE;

      result = g_dbus_proxy_call_sync(
        proxy,
        "Identify",
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
      );

      if (error) {
        g_warning ("Error calling Identify: %s\n", error->message);
        g_clear_error (&error);
      }

      g_variant_unref(result);

      while (!self->identification_done && !self->enrolling && !self->wants_remove)
        g_usleep (500 * 100);

      if (self->enrolling || self->wants_remove) {
        self->awaiting_cancel = TRUE;

        result = g_dbus_proxy_call_sync(
          proxy,
          "Abort",
          NULL,
          G_DBUS_CALL_FLAGS_NONE,
          -1,
          NULL,
          &error
        );

        g_variant_unref (result);

        if (error) {
          g_warning ("Error calling Abort: %s\n", error->message);
          g_clear_error (&error);
        }
      }
    } else {
      self->enrollment_done = FALSE;
      self->finger_canceled = FALSE;
      g_debug ("Enrolling %s", self->selected_finger);

      if (self->selected_finger != NULL) {
        result = g_dbus_proxy_call_sync(
          proxy,
          "Enroll",
          g_variant_new ("(s)", self->selected_finger),
          G_DBUS_CALL_FLAGS_NONE,
          -1,
          NULL,
          &error
        );

        if (error) {
          g_warning ("Error calling Enroll: %s\n", error->message);
          g_clear_error (&error);
        }

        g_variant_unref (result);

        while (!self->enrollment_done && !self->finger_canceled)
          g_usleep (500 * 100);

        if (self->finger_canceled && !self->enrollment_done) {
          if (self->enrolling && !self->enrollment_done) {
            // This means we got canceled by the user, not an error.
            // Need to abort the operation.
            self->awaiting_cancel = TRUE;

            result = g_dbus_proxy_call_sync(
              proxy,
              "Abort",
              NULL,
              G_DBUS_CALL_FLAGS_NONE,
              -1,
              NULL,
              &error
            );

            g_variant_unref (result);

            if (error) {
              g_warning ("Error calling Abort: %s\n", error->message);
              g_clear_error (&error);
            }
          }
        } else {
          g_timeout_add (200, cc_fingerprint_panel_delayed_refresh, self);
        }

        self->enrolling = FALSE;
        self->finger_canceled = FALSE;
        adw_bottom_sheet_set_open (self->bottom_sheet, FALSE);
      }
    }

    g_usleep (500 * 100);
  }

  g_object_unref (proxy);

  return NULL;
}

static void
cc_fingerprint_panel_start_worker (CcFingerprintPanel *self)
{
  g_thread_new (NULL, fingerprint_worker_thread, self);
}

static gboolean
cc_fingerprint_panel_delayed_decay_highlight (gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;

  GList *entry;
  GtkWidget *row;

  for (entry = self->finger_widgets; entry; entry = entry->next) {
    row = entry->data;

    GtkWidget *parent_row = gtk_widget_get_parent (row);
    gtk_widget_remove_css_class (parent_row, "identified");
  }

  return FALSE;
}

static void
cc_fingerprint_panel_highlight_finger (CcFingerprintPanel *self, gchar *finger)
{
  GList *entry;
  GtkWidget *row;

  for (entry = self->finger_widgets; entry; entry = entry->next) {
    row = entry->data;
    GtkWidget *label = gtk_widget_get_first_child (row);

    while (label && !GTK_IS_LABEL (label)) {
      label = gtk_widget_get_next_sibling (label);
    }

    if (!GTK_IS_LABEL (label)) continue;

    if (!g_strcmp0 (gtk_label_get_text (GTK_LABEL (label)), finger)) {
      GtkWidget *parent_row = gtk_widget_get_parent (row);
      gtk_widget_add_css_class (parent_row, "identified");
      g_timeout_add (50, cc_fingerprint_panel_delayed_decay_highlight, self);
      return;
    }
  }
}

static void
handle_signal (GDBusProxy *proxy, gchar *sender_name, gchar *signal_name, GVariant *parameters, gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;
  gint progress;
  gchar *info;

  if (g_strcmp0 (signal_name, "EnrollProgressChanged") == 0) {
    g_variant_get (parameters, "(i)", &progress);

    gtk_widget_set_visible (GTK_WIDGET (self->enroll_progress), TRUE);
    gtk_progress_bar_set_fraction (self->enroll_progress, progress / 100.0);

    g_debug ("Enrollment percentage: %d", progress);
    if (progress == 100) {
      self->enrollment_done = TRUE;
      adw_bottom_sheet_set_open (self->bottom_sheet, FALSE);
    }
  } else if (g_strcmp0 (signal_name, "Identified") == 0) {
    g_variant_get (parameters, "(s)", &info);
    g_debug ("%s received: %s", signal_name, info);
    self->identification_done = TRUE;
    cc_fingerprint_panel_highlight_finger (self, g_strdup (info));
    g_free (info);
  } else if (g_strcmp0 (signal_name, "StateChanged") == 0) {
    g_variant_get (parameters, "(s)", &info);
    g_debug ("%s received: %s", signal_name, info);
    g_free (info);
  } else if (g_strcmp0 (signal_name, "ErrorInfo") == 0) {
    g_variant_get (parameters, "(s)", &info);
    g_debug ("%s received: %s", signal_name, info);

    if (g_strcmp0 (info, "ERROR_NO_SPACE") == 0)
      show_toast (self, "No space available for new fingerprints");
    else if (g_strcmp0 (info, "ERROR_HW_UNAVAILABLE") == 0)
      show_toast (self, "Fingerprint hardware is unavailable");
    else if (g_strcmp0 (info, "ERROR_UNABLE_TO_PROCESS") == 0)
      show_toast (self, "Unable to process fingerprint");
    else if (g_strcmp0 (info, "ERROR_TIMEOUT") == 0)
      show_toast (self, "Fingerprint operation timed out");
    else if (g_strcmp0 (info, "ERROR_CANCELED") == 0) {
      if (self->awaiting_cancel) {
        self->awaiting_cancel = FALSE;
        g_free (info);
        return;
      }

      // If we're trying to identify we might have just timed out. Restart the identification loop.
      if (!self->enrolling && !self->identification_done) {
        self->identification_done = TRUE;
        g_free (info);
        return;
      }
      show_toast (self, "Fingerprint operation was canceled");
    } else if (g_strcmp0 (info, "ERROR_UNABLE_TO_REMOVE") == 0)
      show_toast (self, "Unable to remove the fingerprint");
    else if (g_strcmp0 (info, "FINGER_NOT_RECOGNIZED") == 0)
      show_toast (self, "Fingerprint is not recognized");
    else
      show_toast (self, "An error occurred with the fingerprint reader");

    self->enrolling = FALSE;
    self->finger_canceled = TRUE;
    g_free (info);
  } else if (g_strcmp0 (signal_name, "AcquisitionInfo") == 0) {
    g_variant_get (parameters, "(s)", &info);
    g_debug ("%s received: %s", signal_name, info);
    if (g_strcmp0 (info, "FPACQUIRED_PARTIAL") == 0)
      show_toast (self, "Partial fingerprint detected. Please try again");
    else if (g_strcmp0 (info, "FPACQUIRED_IMAGER_DIRTY") == 0)
      show_toast (self, "The sensor is dirty. Please clean and try again");
    else if (g_strcmp0 (info, "FPACQUIRED_TOO_FAST") == 0)
      show_toast (self, "Finger moved too fast. Please try again");
    else if (g_strcmp0 (info, "FPACQUIRED_TOO_SLOW") == 0)
      show_toast (self, "Finger moved too slow. Please try again");
    else if (g_strcmp0 (info, "FPACQUIRED_INSUFFICIENT") == 0)
      show_toast (self, "Couldn't process fingerprint. Please try again");

    g_free (info);
  }
}

static void
on_finger_activated (GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;

  GtkWidget *child = gtk_list_box_row_get_child (row);
  GtkWidget *label = gtk_widget_get_first_child (child);

  while (label && !GTK_IS_LABEL (label)) {
    label = gtk_widget_get_next_sibling (label);
  }

  if (!GTK_IS_LABEL (label)) return;

  if (g_strcmp0 (gtk_widget_get_name (child), "register-finger")) {
    const gchar *finger_name = gtk_label_get_text (GTK_LABEL (label));
    g_free (self->selected_finger);
    self->selected_finger = g_strdup (finger_name);

    self->delete_dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (_("Remove Fingerprint?"), NULL));
    adw_alert_dialog_format_body (self->delete_dialog, _("Are you sure you want to remove “%s”?"), self->selected_finger);

    adw_alert_dialog_add_responses (self->delete_dialog,
                                    "cancel",  _("_Cancel"),
                                    "remove", _("_Remove"),
                                    NULL);

    adw_alert_dialog_set_response_appearance (self->delete_dialog,
                                              "remove",
                                              ADW_RESPONSE_DESTRUCTIVE);

    adw_alert_dialog_set_default_response (self->delete_dialog, "cancel");
    adw_alert_dialog_set_close_response (self->delete_dialog, "cancel");

    g_signal_connect (self->delete_dialog, "response", G_CALLBACK (cc_fingerprint_panel_remove_finger), self);

    adw_dialog_present (ADW_DIALOG (self->delete_dialog), GTK_WIDGET (self));
  } else {
    gtk_stack_set_visible_child (self->enroll_stack, GTK_WIDGET (self->select_finger_step));
    adw_bottom_sheet_set_open (self->bottom_sheet, TRUE);
  }
}

static void
on_unenrolled_finger_activated (GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;

  GtkWidget *child = gtk_list_box_row_get_child (row);
  GtkWidget *label = gtk_widget_get_first_child (child);

  while (label && !GTK_IS_LABEL (label)) {
    label = gtk_widget_get_next_sibling (label);
  }

  if (GTK_IS_LABEL (label)) {
    const gchar *finger_name = gtk_label_get_text (GTK_LABEL (label));

    g_free (self->selected_finger);
    self->selected_finger = g_strdup (finger_name);

    gtk_stack_set_visible_child (self->enroll_stack, GTK_WIDGET (self->enroll_step));

    self->enrollment_done = FALSE;
    self->enrolling = TRUE;
  }
}

static gboolean
ping_fpd (void)
{
  GDBusProxy *proxy;
  GError *error = NULL;
  GVariant *result;

  proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    FPD_DBUS_NAME,
    FPD_DBUS_PATH,
    "org.freedesktop.DBus.Peer",
    NULL,
    &error
  );

  if (error) {
    g_warning ("Error creating proxy: %s\n", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  result = g_dbus_proxy_call_sync(
    proxy,
    "Ping",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  g_object_unref (proxy);

  if (error) {
    g_warning ("Error calling Ping: %s\n", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  g_variant_unref (result);
  return TRUE;
}

static void
cc_fingerprint_on_bottom_sheet_open_changed (GtkWidget *container, GParamSpec *pspec, CcFingerprintPanel *self)
{
  if (!adw_bottom_sheet_get_open (self->bottom_sheet)) {
    self->finger_canceled = TRUE;
  }
}

static void
cc_fingerprint_panel_class_init (CcFingerprintPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_fingerprint_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/fingerprint/cc-fingerprint-panel.ui");

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        toast_overlay);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        enroll_progress);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        finger_list);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        unenrolled_finger_list);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        bottom_sheet);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        enroll_stack);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        select_finger_step);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcFingerprintPanel,
                                        enroll_step);

  gtk_widget_class_bind_template_callback (widget_class,
                                           cc_fingerprint_on_bottom_sheet_open_changed);
}

static void
cc_fingerprint_panel_init (CcFingerprintPanel *self)
{
  g_autoptr(GtkCssProvider) provider = NULL;

  g_resources_register (cc_fingerprint_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider, "/org/gnome/control-center/fingerprint/fingerprint.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  self->finger_widgets = NULL;
  self->selected_finger = NULL;
  self->enrolling = FALSE;

  if (ping_fpd ()) {
    g_signal_connect (self->finger_list, "row-activated", G_CALLBACK (on_finger_activated), self);
    g_signal_connect (self->unenrolled_finger_list, "row-activated", G_CALLBACK (on_unenrolled_finger_activated), self);
    g_signal_connect (self->bottom_sheet, "notify::open", G_CALLBACK (cc_fingerprint_on_bottom_sheet_open_changed), self);

    gtk_list_box_set_selection_mode (self->finger_list, GTK_SELECTION_NONE);
    gtk_list_box_set_selection_mode (self->unenrolled_finger_list, GTK_SELECTION_NONE);

    refresh_fingerprint_list (self, TRUE, self->finger_list);
    refresh_fingerprint_list (self, FALSE, self->unenrolled_finger_list);
    cc_fingerprint_panel_start_worker (self);
  }
}

CcFingerprintPanel *
cc_fingerprint_panel_new (void)
{
  return CC_FINGERPRINT_PANEL (g_object_new (CC_TYPE_FINGERPRINT_PANEL, NULL));
}
