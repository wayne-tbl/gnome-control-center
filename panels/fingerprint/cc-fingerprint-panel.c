/*
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 * Copyright (C) 2025 Jesus Higueras <jesus@furilabs.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-fingerprint-panel.h"
#include "cc-fingerprint-resources.h"
#include "cc-util.h"

#include <glib/gi18n.h>
#include <adwaita.h>
#include <biomd/biomd_enums.h>

#define BIOMD_DBUS_NAME          "io.FuriOS.Biomd"
#define BIOMD_DBUS_FINGERPRINT_PATH   "/io/FuriOS/Biomd/Fingerprint"
#define BIOMD_DBUS_FINGERPRINT_INTERFACE  "io.FuriOS.Biomd.Fingerprint"

struct _CcFingerprintPanel {
  CcPanel            parent;

  AdwToastOverlay   *toast_overlay;
  GtkPicture        *fingerprint_image;
  GtkProgressBar    *enroll_progress;
  GtkListBox        *finger_list;
  GtkListBox        *unenrolled_finger_list;
  AdwBottomSheet    *bottom_sheet;
  GtkStack          *enroll_stack;
  GtkScrolledWindow *select_finger_step;
  GtkScrolledWindow *enroll_step;
  AdwAlertDialog    *delete_dialog;

  gboolean           enrolling;
  gboolean           enrollment_done;
  gboolean           identification_done;
  gboolean           finger_canceled;
  gboolean           awaiting_cancel;
  gboolean           wants_remove;
  gboolean           wants_death;
  gboolean           sensitive;
  gboolean           initialized;
  GList             *finger_widgets;
  gchar             *selected_finger;

  GDBusProxy        *fingerprint_proxy;
  GDBusProxy        *props_proxy;
};

G_DEFINE_TYPE (CcFingerprintPanel, cc_fingerprint_panel, CC_TYPE_PANEL)

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

  g_debug ("Toast: %s", message);

  g_free (message);
}

static gchar **
get_enrolled_fingers (CcFingerprintPanel *self)
{
  GError *error = NULL;
  GVariant *result;
  gchar **enrolled_fingers = NULL;

  if (!self->props_proxy) {
    g_debug ("Properties proxy not available");
    return NULL;
  }

  result = g_dbus_proxy_call_sync(
    self->props_proxy,
    "Get",
    g_variant_new ("(ss)", BIOMD_DBUS_FINGERPRINT_INTERFACE, "EnrolledFingers"),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling Get for EnrolledFingers: %s", error->message);
    g_clear_error (&error);
    return NULL;
  } else {
    GVariant *enrolled_variant;
    g_variant_get (result, "(v)", &enrolled_variant);
    enrolled_fingers = g_variant_dup_strv (enrolled_variant, NULL);
    g_variant_unref (enrolled_variant);
    g_variant_unref (result);
  }

  return enrolled_fingers;
}

static gchar **
get_valid_finger_names (CcFingerprintPanel *self)
{
  GError *error = NULL;
  GVariant *result;
  gchar **valid_fingers = NULL;

  if (!self->props_proxy) {
    g_debug ("Properties proxy not available");
    return NULL;
  }

  result = g_dbus_proxy_call_sync(
    self->props_proxy,
    "Get",
    g_variant_new ("(ss)", BIOMD_DBUS_FINGERPRINT_INTERFACE, "ValidFingerNames"),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    &error
  );

  if (error) {
    g_debug ("Error calling Get for ValidFingerNames: %s", error->message);
    g_clear_error (&error);
    return NULL;
  } else {
    GVariant *valid_variant;
    g_variant_get (result, "(v)", &valid_variant);
    valid_fingers = g_variant_dup_strv (valid_variant, NULL);
    g_variant_unref (valid_variant);
    g_variant_unref (result);
  }

  return valid_fingers;
}

static gint32
get_fingerprint_state (CcFingerprintPanel *self, GError **error)
{
  GVariant *result;
  gint32 state = STATE_IDLE;

  if (!self->props_proxy) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                 "Properties proxy not available");
    return state;
  }

  result = g_dbus_proxy_call_sync(
    self->props_proxy,
    "Get",
    g_variant_new("(ss)", BIOMD_DBUS_FINGERPRINT_INTERFACE, "State"),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    error
  );

  if (result) {
    GVariant *state_variant;
    g_variant_get (result, "(v)", &state_variant);
    state = g_variant_get_int32 (state_variant);
    g_variant_unref (state_variant);
    g_variant_unref (result);
  }

  return state;
}

static gboolean
fingerprint_enroll (CcFingerprintPanel *self, const gchar *finger_name, GError **error)
{
  GVariant *result;
  gboolean success = FALSE;

  if (!self->fingerprint_proxy) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                 "Fingerprint proxy not available");
    return FALSE;
  }

  g_debug ("Enrolling %s", finger_name);

  result = g_dbus_proxy_call_sync(
    self->fingerprint_proxy,
    "Enroll",
    g_variant_new ("(s)", finger_name),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    error
  );

  if (result) {
    g_variant_get (result, "(b)", &success);
    g_variant_unref (result);
  }

  return success;
}

static gboolean
fingerprint_identify (CcFingerprintPanel *self, GError **error)
{
  GVariant *result;
  gboolean success = FALSE;

  if (!self->fingerprint_proxy) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                 "Fingerprint proxy not available");
    return FALSE;
  }

  result = g_dbus_proxy_call_sync(
    self->fingerprint_proxy,
    "Identify",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    error
  );

  if (result) {
    g_variant_get (result, "(b)", &success);
    g_variant_unref (result);
  }

  return success;
}

static gboolean
fingerprint_remove_finger (CcFingerprintPanel *self, const gchar *finger_name, GError **error)
{
  GVariant *result;
  gboolean success = FALSE;

  if (!self->fingerprint_proxy) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                 "Fingerprint proxy not available");
    return FALSE;
  }

  result = g_dbus_proxy_call_sync(
    self->fingerprint_proxy,
    "RemoveFinger",
    g_variant_new ("(s)", finger_name),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    error
  );

  if (result) {
    g_variant_get (result, "(b)", &success);
    g_variant_unref (result);
  }

  return success;
}

static gboolean
fingerprint_stop_enroll (CcFingerprintPanel *self, GError **error)
{
  GVariant *result;

  if (!self->fingerprint_proxy) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                 "Fingerprint proxy not available");
    return FALSE;
  }

  result = g_dbus_proxy_call_sync(
    self->fingerprint_proxy,
    "StopEnroll",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    error
  );

  if (result) {
    g_variant_unref (result);
    return TRUE;
  }

  return FALSE;
}

static gboolean
fingerprint_stop_identify (CcFingerprintPanel *self, GError **error)
{
  GVariant *result;

  if (!self->fingerprint_proxy) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                 "Fingerprint proxy not available");
    return FALSE;
  }

  result = g_dbus_proxy_call_sync(
    self->fingerprint_proxy,
    "StopIdentify",
    NULL,
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL,
    error
  );

  if (result) {
    g_variant_unref (result);
    return TRUE;
  }

  return FALSE;
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
  gchar **valid_finger_names = get_valid_finger_names (self);
  gchar **enrolled_fingers = get_enrolled_fingers (self);

  if (!valid_finger_names) {
    g_debug ("Failed to get valid finger names");
    if (enrolled_fingers)
      g_strfreev (enrolled_fingers);
    return;
  }

  gtk_list_box_remove_all (target_list);

  if (show_enrolled) {
    g_list_free_full (self->finger_widgets, g_object_unref);
    self->finger_widgets = NULL;
  }

  for (int i = 0; valid_finger_names[i] != NULL; i++) {
    gboolean is_enrolled = enrolled_fingers && g_strv_contains ((const gchar *const *) enrolled_fingers, valid_finger_names[i]);
    if ((show_enrolled && is_enrolled) || (!show_enrolled && !is_enrolled)) {
      GtkWidget *row = create_finger_row (valid_finger_names[i], is_enrolled);
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

  g_strfreev (valid_finger_names);
  if (enrolled_fingers)
    g_strfreev (enrolled_fingers);
}

static void
cc_fingerprint_panel_remove_finger (AdwDialog *dialog, gchar *response, CcFingerprintPanel *self)
{
  if (g_strcmp0 (response, "remove"))
    return;

  if (self->selected_finger != NULL)
    self->wants_remove = TRUE;
  else
    show_toast (self, "Please select a finger to remove");
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
  gboolean operation_success;
  gint32 current_state = STATE_IDLE;

  while (1) {
    if (self->wants_death)
      break;

    current_state = get_fingerprint_state (self, &error);

    if (error) {
      g_warning ("Error getting State property: %s\n", error->message);
      g_clear_error (&error);
    }

    if (self->wants_remove) {
      if (current_state == STATE_IDENTIFYING) {
        g_debug ("Stopping identification before finger removal");
        self->awaiting_cancel = TRUE;
        operation_success = fingerprint_stop_identify (self, &error);
        if (error) {
          g_warning ("Error stopping identification: %s\n", error->message);
          g_clear_error (&error);
        }

        g_usleep (300 * 1000);

        current_state = get_fingerprint_state (self, &error);
        if (error) {
          g_warning ("Error getting state: %s\n", error->message);
          g_clear_error (&error);
        }
      }

      if (current_state == STATE_IDLE && self->selected_finger != NULL) {
        g_debug ("Removing finger: %s", self->selected_finger);
        operation_success = fingerprint_remove_finger (self, self->selected_finger, &error);

        if (error) {
          g_debug ("Error calling RemoveFinger: %s", error->message);
          g_clear_error (&error);
        }

        self->enrollment_done = TRUE;
      } else if (current_state != STATE_IDLE) {
        g_debug ("Cannot remove finger: device not in idle state (current state: %d)", current_state);
      }

      self->wants_remove = FALSE;
    } else if (self->enrolling && current_state != STATE_ENROLLING) {
      if (current_state == STATE_IDENTIFYING) {
        g_debug ("Stopping identification before enrollment");
        self->awaiting_cancel = TRUE;
        operation_success = fingerprint_stop_identify (self, &error);
        if (error) {
          g_warning ("Error stopping identification: %s\n", error->message);
          g_clear_error (&error);
        }

        g_usleep (300 * 1000);

        current_state = get_fingerprint_state (self, &error);
        if (error) {
          g_warning ("Error getting state: %s\n", error->message);
          g_clear_error (&error);
        }
      }

      if (current_state == STATE_IDLE && self->selected_finger != NULL) {
        self->enrollment_done = FALSE;
        self->finger_canceled = FALSE;
        g_debug ("Enrolling %s", self->selected_finger);

        operation_success = fingerprint_enroll (self, self->selected_finger, &error);

        if (error) {
          g_warning ("Error calling Enroll: %s\n", error->message);
          g_clear_error (&error);
          self->enrolling = FALSE;
        } else if (!operation_success) {
          self->enrolling = FALSE;
          g_debug ("Enrollment failed to start");
        }
      } else if (current_state != STATE_IDLE) {
        g_debug ("Cannot start enrollment: device not in idle state (current state: %d)", current_state);
        self->enrolling = FALSE;
      }
    } else if (self->finger_canceled && current_state != STATE_IDLE) {
      if (current_state == STATE_ENROLLING) {
        g_debug ("Stopping enrollment due to cancellation");
        operation_success = fingerprint_stop_enroll (self, &error);
      } else if (current_state == STATE_IDENTIFYING) {
        g_debug ("Stopping identification due to cancellation");
        operation_success = fingerprint_stop_identify (self, &error);
      }

      if (error) {
        g_warning ("Error canceling operation: %s\n", error->message);
        g_clear_error (&error);
      }

      self->finger_canceled = FALSE;
    } else if (!self->enrolling && current_state == STATE_IDLE && !self->finger_canceled && !self->enrollment_done) {
      self->identification_done = FALSE;
      g_debug ("Starting identification");
      operation_success = fingerprint_identify (self, &error);

      if (error) {
        g_debug ("Identification error: %s", error->message);
        g_clear_error (&error);
      } else if (operation_success) {
        g_debug ("Identification started successfully");
      } else {
        g_debug ("Identification failed to start");
      }
    }

    if (self->enrolling) {
      while (!self->enrollment_done && !self->finger_canceled && !self->wants_death) {
        g_usleep (100 * 1000);
      }

      if (!self->finger_canceled && self->enrollment_done) {
        self->enrolling = FALSE;
        adw_bottom_sheet_set_open (self->bottom_sheet, FALSE);
      }
    } else {
      g_usleep (500 * 1000);
    }
  }

  self->wants_death = FALSE;
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

    if (!GTK_IS_LABEL (label))
      continue;

    if (!g_strcmp0 (gtk_label_get_text (GTK_LABEL (label)), finger)) {
      GtkWidget *parent_row = gtk_widget_get_parent (row);
      gtk_widget_add_css_class (parent_row, "identified");
      g_timeout_add (150, cc_fingerprint_panel_delayed_decay_highlight, self);
      return;
    }
  }
}

static void
handle_signal (GDBusProxy *proxy, gchar *sender_name, gchar *signal_name, GVariant *parameters, gpointer user_data)
{
  CcFingerprintPanel *self = (CcFingerprintPanel *) user_data;
  gint progress;
  gchar *finger_name;
  gint32 state_code, error_code, acquisition_code;
  GError *error = NULL;
  gboolean operation_success;

  if (g_strcmp0 (signal_name, "EnrollmentProgressChanged") == 0) {
    g_variant_get (parameters, "(i)", &progress);

    gtk_widget_set_visible (GTK_WIDGET (self->enroll_progress), TRUE);
    gtk_progress_bar_set_fraction (self->enroll_progress, progress / 100.0);

    g_debug ("Enrollment percentage: %d", progress);
    if (progress == 100) {
      self->enrollment_done = TRUE;
      adw_bottom_sheet_set_open (self->bottom_sheet, FALSE);
    }
  } else if (g_strcmp0 (signal_name, "EnrolledFingersChanged") == 0) {
    g_debug ("EnrolledFingersChanged signal received");
    g_timeout_add (100, cc_fingerprint_panel_delayed_refresh, self);

    if (self->enrolling) {
      self->enrolling = FALSE;
      self->enrollment_done = TRUE;
    }

    self->identification_done = FALSE;

    gint32 current_state = get_fingerprint_state (self, &error);

    if (error) {
      g_warning ("Error getting State property: %s\n", error->message);
      g_clear_error (&error);
    } else if (current_state == STATE_IDLE) {
      g_debug ("Starting identification after EnrolledFingersChanged");
      operation_success = fingerprint_identify (self, &error);

      if (error) {
        g_debug ("Identification error after EnrolledFingersChanged: %s", error->message);
        g_clear_error (&error);
      } else if (operation_success) {
        g_debug ("Identification started successfully after EnrolledFingersChanged");
      } else {
        g_debug ("Identification failed to start after EnrolledFingersChanged");
      }
    } else {
      g_debug ("Not starting identification: device not in idle state (current state: %d)", current_state);
    }
  } else if (g_strcmp0 (signal_name, "Identified") == 0) {
    g_variant_get (parameters, "(s)", &finger_name);
    g_debug ("%s received: %s", signal_name, finger_name);
    self->identification_done = TRUE;
    cc_fingerprint_panel_highlight_finger (self, g_strdup (finger_name));
    g_free (finger_name);
  } else if (g_strcmp0 (signal_name, "StateChanged") == 0) {
    g_variant_get (parameters, "(i)", &state_code);

    const gchar *state_str;
    switch ((BiometricState)state_code) {
      case STATE_IDLE:
        state_str = "IDLE";
        self->enrollment_done = FALSE;
        self->identification_done = FALSE;
        break;
      case STATE_ENROLLING:
        state_str = "ENROLLING";
        break;
      case STATE_IDENTIFYING:
        state_str = "IDENTIFYING";
        break;
      default:
        state_str = "UNKNOWN";
        break;
    }

    g_debug ("%s received: %s", signal_name, state_str);
  } else if (g_strcmp0 (signal_name, "ErrorInfoChanged") == 0) {
    g_variant_get (parameters, "(i)", &error_code);

    switch ((BiometricError)error_code) {
      case ERROR_NO_SPACE:
        show_toast (self, "No space available for new fingerprints");
        break;
      case ERROR_HW_UNAVAILABLE:
        show_toast (self, "Fingerprint hardware is unavailable");
        break;
      case ERROR_UNABLE_TO_PROCESS:
        show_toast (self, "Unable to process fingerprint");
        break;
      case ERROR_TIMEOUT:
        show_toast (self, "Fingerprint operation timed out");
        break;
      case ERROR_CANCELED:
        if (self->awaiting_cancel) {
          self->awaiting_cancel = FALSE;
          return;
        }

        if ((self->enrolling || self->wants_remove) && !self->identification_done) {
          g_debug ("Ignoring cancel event during state transition");
          self->identification_done = TRUE;
          return;
        }

        if (!self->enrolling && !self->identification_done) {
          self->identification_done = TRUE;
          return;
        }

        show_toast (self, "Fingerprint operation was canceled");
        break;
      case ERROR_REMOVE:
        show_toast (self, "Unable to remove the fingerprint");
        break;
      case ERROR_LOCKOUT:
        show_toast (self, "Too many attempts, fingerprint sensor locked");
        break;
      case ERROR_GENERAL:
        show_toast (self, "An error occurred with the fingerprint reader");
        break;
      case ERROR_FINGER_NOT_RECOGNIZED:
        g_debug ("Finger is not recognized");
        break;
      case ERROR_NONE:
      default:
        break;
    }

    self->enrolling = FALSE;
    if ((BiometricError)error_code != ERROR_FINGER_NOT_RECOGNIZED)
        self->finger_canceled = TRUE;
  } else if (g_strcmp0 (signal_name, "AcquisitionInfoChanged") == 0) {
    g_variant_get (parameters, "(i)", &acquisition_code);

    switch ((BiometricAcquisition)acquisition_code) {
      case ACQUISITION_PARTIAL:
        show_toast (self, "Partial fingerprint detected");
        break;
      case ACQUISITION_IMAGER_DIRTY:
        show_toast (self, "The sensor is dirty");
        break;
      case ACQUISITION_TOO_FAST:
        show_toast (self, "Finger moved too fast");
        break;
      case ACQUISITION_TOO_SLOW:
        show_toast (self, "Finger moved too slow");
        break;
      case ACQUISITION_INSUFFICIENT:
        show_toast (self, "Couldn't process fingerprint");
        break;
      case ACQUISITION_NONE:
      case ACQUISITION_GOOD:
      default:
        break;
    }
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

  if (!GTK_IS_LABEL (label))
    return;

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
init_dbus_proxies (CcFingerprintPanel *self)
{
  GError *error = NULL;

  self->fingerprint_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    BIOMD_DBUS_NAME,
    BIOMD_DBUS_FINGERPRINT_PATH,
    BIOMD_DBUS_FINGERPRINT_INTERFACE,
    NULL,
    &error
  );

  if (error) {
    g_warning ("Error creating fingerprint proxy: %s\n", error->message);
    g_clear_error (&error);
    return FALSE;
  }

  self->props_proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    BIOMD_DBUS_NAME,
    BIOMD_DBUS_FINGERPRINT_PATH,
    "org.freedesktop.DBus.Properties",
    NULL,
    &error
  );

  if (error) {
    g_warning ("Error creating properties proxy: %s\n", error->message);
    g_clear_error (&error);
    g_clear_object (&self->fingerprint_proxy);
    return FALSE;
  }

  g_signal_connect (self->fingerprint_proxy, "g-signal", G_CALLBACK (handle_signal), self);

  return TRUE;
}

static gboolean
ping_biomd (void)
{
  GDBusProxy *proxy;
  GError *error = NULL;
  GVariant *result;
  gboolean ping_result = FALSE;

  proxy = g_dbus_proxy_new_for_bus_sync(
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    NULL,
    BIOMD_DBUS_NAME,
    "/io/FuriOS/Biomd",
    "io.FuriOS.Biomd",
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

  if (error) {
    g_warning ("Error calling Ping: %s\n", error->message);
    g_clear_error (&error);
  } else {
    g_variant_get (result, "(b)", &ping_result);
    g_variant_unref (result);
  }

  g_object_unref (proxy);

  return ping_result;
}

static void
cc_fingerprint_panel_finalize (GObject *object)
{
  CcFingerprintPanel *self = CC_FINGERPRINT_PANEL (object);

  if (self->initialized) {
    self->wants_death = TRUE;
    while (self->wants_death) {
      g_usleep (500 * 100);
    }

    fingerprint_stop_identify (self, NULL);

    g_list_free_full (self->finger_widgets, g_object_unref);
    g_free (self->selected_finger);

    g_clear_object (&self->fingerprint_proxy);
    g_clear_object (&self->props_proxy);
  }

  G_OBJECT_CLASS (cc_fingerprint_panel_parent_class)->finalize (object);
}

static void
cc_fingerprint_on_bottom_sheet_open_changed (GtkWidget *container, GParamSpec *pspec, CcFingerprintPanel *self)
{
  if (!adw_bottom_sheet_get_open (self->bottom_sheet)) {
    if (self->enrolling) {
      if (!self->enrollment_done)
        self->finger_canceled = TRUE;

      self->enrolling = FALSE;
    }
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
  self->enrollment_done = FALSE;
  self->awaiting_cancel = FALSE;
  self->wants_death = FALSE;
  self->initialized = FALSE;

  if (ping_biomd ()) {
    if (init_dbus_proxies (self)) {
      g_signal_connect (self->finger_list, "row-activated", G_CALLBACK (on_finger_activated), self);
      g_signal_connect (self->unenrolled_finger_list, "row-activated", G_CALLBACK (on_unenrolled_finger_activated), self);
      g_signal_connect (self->bottom_sheet, "notify::open", G_CALLBACK (cc_fingerprint_on_bottom_sheet_open_changed), self);

      gtk_list_box_set_selection_mode (self->finger_list, GTK_SELECTION_NONE);
      gtk_list_box_set_selection_mode (self->unenrolled_finger_list, GTK_SELECTION_NONE);

      refresh_fingerprint_list (self, TRUE, self->finger_list);
      refresh_fingerprint_list (self, FALSE, self->unenrolled_finger_list);
      cc_fingerprint_panel_start_worker (self);
      self->initialized = TRUE;
    }
  }
}

CcFingerprintPanel *
cc_fingerprint_panel_new (void)
{
  return CC_FINGERPRINT_PANEL (g_object_new (CC_TYPE_FINGERPRINT_PANEL, NULL));
}
