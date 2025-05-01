/*
 * Copyright (C) 2023 Eugenio "g7" Paolantonio <me@medesimo.eu>
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-crypted-panel.h"
#include "cc-crypted-resources.h"
#include "cc-util.h"

#include <adwaita.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>
#include <string.h>

struct _CcCryptedPanel {
  CcPanel            parent;

  AdwToastOverlay    *toast_overlay;

  GCancellable       *cancellable;

  GDBusProxy              *encryption_service;
  EncryptionServiceStatus  encryption_service_status;
  guint                    encryption_service_timeout;

  GtkSwitch *enable_switch;
  GtkStack *stack;

  GtkSpinner *encryption_spinner;
  GtkLabel   *encryption_status;

  AdwPasswordEntryRow *passphrase_entry;
  AdwPasswordEntryRow *passphrase_confirm_entry;
  GtkButton           *encryption_start_button;
  gboolean             passphrases_match;

  AdwPasswordEntryRow *current_password_entry;
  AdwPasswordEntryRow *new_password_entry;
  GtkButton           *password_change_button;
  gboolean             password_fields_filled;

  AdwStatusPage      *encryption_unknown_page;
  AdwStatusPage      *encryption_unsupported_page;
  AdwStatusPage      *encryption_unconfigured_page;
  AdwPreferencesPage *encryption_enable_page;
  AdwPreferencesPage *encryption_status_page;
};

G_DEFINE_TYPE (CcCryptedPanel, cc_crypted_panel, CC_TYPE_PANEL)

static const char* status_text[] = {
  "Unknown",
  "Unsupported",
  "Unconfigured",
  "Configuring",
  "Please reboot to start",
  "Encrypting",
  "Encrypted",
  "Failed"
};

static void
show_toast (CcCryptedPanel *self, const char *format, ...)
{
  va_list args;
  char *message;
  AdwToast *toast;

  va_start (args, format);
  message = g_strdup_vprintf (format, args);
  va_end (args);

  toast = adw_toast_new (message);
  adw_toast_set_timeout (toast, 3);

  adw_toast_overlay_add_toast (self->toast_overlay, toast);

  g_free (message);
}

static void
select_page (CcCryptedPanel *self,
             EncryptionServiceStatus status)
{
  g_return_if_fail (CC_IS_CRYPTED_PANEL (self));

  switch (status) {
  case ENCRYPTION_SERVICE_STATUS_UNKNOWN:
    gtk_widget_set_sensitive (GTK_WIDGET (self->enable_switch), FALSE);
    gtk_stack_set_visible_child (self->stack, GTK_WIDGET (self->encryption_unknown_page));
    break;
  case ENCRYPTION_SERVICE_STATUS_UNSUPPORTED:
    gtk_widget_set_sensitive (GTK_WIDGET (self->enable_switch), FALSE);
    gtk_stack_set_visible_child (self->stack, GTK_WIDGET (self->encryption_unsupported_page));
    break;
  case ENCRYPTION_SERVICE_STATUS_UNCONFIGURED:
    gtk_widget_set_sensitive (GTK_WIDGET (self->enable_switch), TRUE);
    gtk_stack_set_visible_child (self->stack, GTK_WIDGET (self->encryption_unconfigured_page));
    break;
  default:
    /* Should the spinner be shown? */
    gtk_widget_set_visible (GTK_WIDGET (self->encryption_spinner),
                            (status == ENCRYPTION_SERVICE_STATUS_CONFIGURING ||
                             status == ENCRYPTION_SERVICE_STATUS_ENCRYPTING));

    /* Should the switch show that the encryption is enabled? */
    gtk_switch_set_active (self->enable_switch, (status == ENCRYPTION_SERVICE_STATUS_ENCRYPTED ||
                                                 status == ENCRYPTION_SERVICE_STATUS_ENCRYPTING ||
                                                 status == ENCRYPTION_SERVICE_STATUS_CONFIGURING ||
                                                 status == ENCRYPTION_SERVICE_STATUS_CONFIGURED));

    gtk_label_set_text (GTK_LABEL (self->encryption_status),
                        (status < G_N_ELEMENTS (status_text)) ? status_text[status] : "Unknown");
    gtk_widget_set_sensitive (GTK_WIDGET (self->enable_switch), FALSE);

    /* Show password change fields when encryption is fully enabled */
    if (status == ENCRYPTION_SERVICE_STATUS_ENCRYPTED) {
      gtk_widget_set_visible (GTK_WIDGET (self->current_password_entry), TRUE);
      gtk_widget_set_visible (GTK_WIDGET (self->new_password_entry), TRUE);
      gtk_widget_set_visible (GTK_WIDGET (self->password_change_button), TRUE);
    } else {
      gtk_widget_set_visible (GTK_WIDGET (self->current_password_entry), FALSE);
      gtk_widget_set_visible (GTK_WIDGET (self->new_password_entry), FALSE);
      gtk_widget_set_visible (GTK_WIDGET (self->password_change_button), FALSE);
    }

    gtk_stack_set_visible_child (self->stack, GTK_WIDGET (self->encryption_status_page));
    break;
  }
}

static void
service_refresh_status (CcCryptedPanel *self)
{
  g_return_if_fail (CC_IS_CRYPTED_PANEL (self));

  /* We don't care about the callback as we're going to react anyway if
   * the status changes */
  g_dbus_proxy_call (self->encryption_service,
                     "RefreshStatus",
                     NULL,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     self->cancellable,
                     NULL,
                     NULL);
}

/**
 * This method simply fetches the Status property from DBus - it doesn't
 * call RefreshStatus(). This should only be used to determine the status
 * on first start (if the encryption service is already running).
 */
static void
service_refresh_status_property (CcCryptedPanel *self)
{
  g_autoptr (GVariant) variant = NULL;
  guint32 value;

  g_return_if_fail (CC_IS_CRYPTED_PANEL (self));

  variant = g_dbus_proxy_get_cached_property (self->encryption_service, "Status");
  if (variant != NULL) {
    value = g_variant_get_uint32 (variant);

    g_debug ("Got encryption status from DBus: %u (%s)",
             value, (value < G_N_ELEMENTS (status_text)) ? status_text[value] : "Invalid");

    self->encryption_service_status = (EncryptionServiceStatus) value;

    /* Update UI based on new status */
    select_page (self, self->encryption_service_status);
  }
}

static void
select_enable_page (CcCryptedPanel *self)
{
  g_return_if_fail (CC_IS_CRYPTED_PANEL (self));

  /* Reset and show entries */
  gtk_editable_set_text (GTK_EDITABLE (self->passphrase_entry), "");
  gtk_editable_set_text (GTK_EDITABLE (self->passphrase_confirm_entry), "");
  gtk_widget_set_visible (GTK_WIDGET (self->passphrase_entry), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (self->passphrase_confirm_entry), TRUE);

  gtk_widget_grab_focus (GTK_WIDGET (self->passphrase_entry));

  gtk_stack_set_visible_child (self->stack, GTK_WIDGET (self->encryption_enable_page));
}

static void
on_enable_switch_changed (CcCryptedPanel *self,
                          GtkSwitch      *switch_widget)
{
  g_return_if_fail (CC_IS_CRYPTED_PANEL (self));

  if (gtk_switch_get_state (self->enable_switch))
    /* Show the enable page */
    select_enable_page (self);
  else
    /* Only supported variant right now is to go back */
    select_page (self, self->encryption_service_status);
}

static void
on_service_encrypt_call_done (GDBusProxy     *proxy,
                              GAsyncResult   *res,
                              CcCryptedPanel *self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) result = NULL;

  g_return_if_fail (CC_IS_CRYPTED_PANEL (self));

  result = g_dbus_proxy_call_finish (proxy, res, &error);

  if (result == NULL)
      g_warning ("Failure while calling Encrypt(): %s", error->message);
  else
      /* Schedule refresh */
      service_refresh_status (self);
}

static void
on_encryption_start_button_clicked (CcCryptedPanel *self,
                                    GtkButton      *button)
{
  g_return_if_fail (CC_IS_CRYPTED_PANEL (self));

  g_dbus_proxy_call (self->encryption_service,
                     "Encrypt",
                     g_variant_new ("(s)",
                                    gtk_editable_get_text (GTK_EDITABLE (self->passphrase_entry))),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     self->cancellable,
                     (GAsyncReadyCallback) on_service_encrypt_call_done,
                     self);
}

static void
on_passphrase_changed (CcCryptedPanel *self,
                       GtkEditable    *editable)
{
  gboolean match = FALSE;
  const char* passphrase_entry_text;
  const char* passphrase_confirm_entry_text;

  g_return_if_fail (CC_IS_CRYPTED_PANEL (self));

  passphrase_entry_text = gtk_editable_get_text (GTK_EDITABLE (self->passphrase_entry));
  passphrase_confirm_entry_text = gtk_editable_get_text (GTK_EDITABLE (self->passphrase_confirm_entry));

  match = (strlen (passphrase_entry_text) > 0 &&
           (g_strcmp0 (passphrase_entry_text, passphrase_confirm_entry_text) == 0));

  if (match != self->passphrases_match) {
      self->passphrases_match = match;
      gtk_widget_set_sensitive (GTK_WIDGET (self->encryption_start_button), match);
  }
}

static void
on_change_passphrase_changed (CcCryptedPanel *self,
                              GtkEditable    *editable)
{
  gboolean fields_filled = FALSE;
  const char* current_password_text;
  const char* new_password_text;

  g_return_if_fail (CC_IS_CRYPTED_PANEL (self));

  current_password_text = gtk_editable_get_text (GTK_EDITABLE (self->current_password_entry));
  new_password_text = gtk_editable_get_text (GTK_EDITABLE (self->new_password_entry));

  fields_filled = (strlen (current_password_text) > 0 && strlen (new_password_text) > 0);

  if (fields_filled != self->password_fields_filled) {
      self->password_fields_filled = fields_filled;
      gtk_widget_set_sensitive (GTK_WIDGET (self->password_change_button), fields_filled);
  }
}

static void
on_service_change_password_call_done (GDBusProxy     *proxy,
                                      GAsyncResult   *res,
                                      CcCryptedPanel *self)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) result = NULL;
  gboolean success = FALSE;

  g_return_if_fail (CC_IS_CRYPTED_PANEL (self));

  result = g_dbus_proxy_call_finish (proxy, res, &error);

  if (result == NULL) {
      g_warning ("Failure while calling ChangePassword(): %s", error->message);
      show_toast (self, "Failed to change the password");
  } else {
      g_variant_get (result, "(b)", &success);
      if (success) {
          /* Clear the password fields */
          gtk_editable_set_text (GTK_EDITABLE (self->current_password_entry), "");
          gtk_editable_set_text (GTK_EDITABLE (self->new_password_entry), "");
          /* Schedule refresh */
          service_refresh_status (self);
          show_toast (self, "Password changed successfully");
      } else {
          g_warning ("Password change was unsuccessful");
          show_toast (self, "Failed to change the password");
      }
  }
}

static void
on_password_change_button_clicked (CcCryptedPanel *self,
                                   GtkButton      *button)
{
  g_return_if_fail (CC_IS_CRYPTED_PANEL (self));

  g_dbus_proxy_call (self->encryption_service,
                     "ChangePassword",
                     g_variant_new ("(ss)",
                                    gtk_editable_get_text (GTK_EDITABLE (self->current_password_entry)),
                                    gtk_editable_get_text (GTK_EDITABLE (self->new_password_entry))),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     self->cancellable,
                     (GAsyncReadyCallback) on_service_change_password_call_done,
                     self);
}

static void
on_service_properties_changed (GDBusProxy         *proxy,
                               GVariant           *changed_properties,
                               const gchar* const *invalidated_properties,
                               CcCryptedPanel     *self)
{
  guint32 value;

  if (g_variant_lookup (changed_properties, "Status", "u", &value)) {
      g_debug ("Encryption status changed to: %u", value);
      self->encryption_service_status = (EncryptionServiceStatus) value;
      select_page (self, self->encryption_service_status);
  }
}

static gboolean
on_refresh_status_timeout_elapsed (CcCryptedPanel *self)
{
  gboolean result = G_SOURCE_REMOVE;

  g_return_val_if_fail (CC_IS_CRYPTED_PANEL (self), result);

  switch (self->encryption_service_status) {
  case ENCRYPTION_SERVICE_STATUS_UNKNOWN:
  case ENCRYPTION_SERVICE_STATUS_UNCONFIGURED:
  case ENCRYPTION_SERVICE_STATUS_CONFIGURING:
  case ENCRYPTION_SERVICE_STATUS_ENCRYPTING:
    /* Non-permanent status, ensure we refresh again and keep the timeout */
    g_debug ("Scheduling new refresh status");

    result = G_SOURCE_CONTINUE;
    service_refresh_status (self);
    break;
  default:
    /* Permanent status, we can't do anything, drop */
    g_debug ("Status is permanent, removing timeout");

    self->encryption_service_timeout = 0;
    break;
  }

  return result;
}

static void
on_encryption_service_ready (GObject        *source_object,
                             GAsyncResult   *res,
                             CcCryptedPanel *self)
{
  g_autoptr (GError) error = NULL;

  g_return_if_fail (CC_IS_CRYPTED_PANEL (self));

  self->encryption_service = g_dbus_proxy_new_for_bus_finish (res, &error);
  if (!self->encryption_service) {
      g_warning ("Unable to connect to encryption service: %s", error->message);
      return;
  }

  g_signal_connect (self->encryption_service,
                    "g-properties-changed",
                    G_CALLBACK (on_service_properties_changed),
                    self);

  /* Refresh status property and try to obtain a fresher state */
  service_refresh_status_property (self);
  service_refresh_status (self);

  /* Schedule status refresh */
  self->encryption_service_timeout = g_timeout_add_seconds (30, G_SOURCE_FUNC (on_refresh_status_timeout_elapsed), self);
}

static void
cc_crypted_panel_constructed (GObject *obj)
{
  CcCryptedPanel *self = CC_CRYPTED_PANEL (obj);

  G_OBJECT_CLASS (cc_crypted_panel_parent_class)->constructed (obj);

  g_signal_connect_object (self->enable_switch, "notify::state", G_CALLBACK (on_enable_switch_changed),
                           self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->passphrase_entry, "changed", G_CALLBACK (on_passphrase_changed),
                           self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->passphrase_confirm_entry, "changed", G_CALLBACK (on_passphrase_changed),
                           self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->encryption_start_button, "clicked", G_CALLBACK (on_encryption_start_button_clicked),
                           self, G_CONNECT_SWAPPED);

  /* Connect password change signals */
  g_signal_connect_object (self->current_password_entry, "changed", G_CALLBACK (on_change_passphrase_changed),
                           self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->new_password_entry, "changed", G_CALLBACK (on_change_passphrase_changed),
                           self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->password_change_button, "clicked", G_CALLBACK (on_password_change_button_clicked),
                           self, G_CONNECT_SWAPPED);

  gtk_widget_set_sensitive (GTK_WIDGET (self->encryption_start_button), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->password_change_button), FALSE);
}

static void
cc_crypted_panel_dispose (GObject *object)
{
  CcCryptedPanel *self = CC_CRYPTED_PANEL (object);

  if (self->encryption_service_timeout > 0)
    g_source_remove (self->encryption_service_timeout);

  if (self->encryption_service)
    g_clear_object (&self->encryption_service);

  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (cc_crypted_panel_parent_class)->dispose (object);
}

static void
cc_crypted_panel_class_init (CcCryptedPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = cc_crypted_panel_dispose;
  object_class->constructed = cc_crypted_panel_constructed;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/crypted/cc-crypted-panel.ui");

  /* Generic stuff */
  gtk_widget_class_bind_template_child (widget_class,
                                        CcCryptedPanel,
                                        enable_switch);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcCryptedPanel,
                                        stack);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcCryptedPanel,
                                        toast_overlay);

  /* Status pages */
  gtk_widget_class_bind_template_child (widget_class,
                                        CcCryptedPanel,
                                        encryption_unknown_page);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcCryptedPanel,
                                        encryption_unsupported_page);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcCryptedPanel,
                                        encryption_unconfigured_page);

  /* Encryption status page */
  gtk_widget_class_bind_template_child (widget_class,
                                        CcCryptedPanel,
                                        encryption_status_page);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcCryptedPanel,
                                        encryption_status);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcCryptedPanel,
                                        encryption_spinner);

  /* Encryption enable page */
  gtk_widget_class_bind_template_child (widget_class,
                                        CcCryptedPanel,
                                        encryption_enable_page);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcCryptedPanel,
                                        passphrase_entry);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcCryptedPanel,
                                        passphrase_confirm_entry);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcCryptedPanel,
                                        encryption_start_button);

  /* Password change fields */
  gtk_widget_class_bind_template_child (widget_class,
                                        CcCryptedPanel,
                                        current_password_entry);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcCryptedPanel,
                                        new_password_entry);

  gtk_widget_class_bind_template_child (widget_class,
                                        CcCryptedPanel,
                                        password_change_button);
}

static void
cc_crypted_panel_init (CcCryptedPanel *self)
{
  g_resources_register (cc_crypted_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  self->cancellable = g_cancellable_new ();

  self->encryption_service = NULL;
  self->encryption_service_status = ENCRYPTION_SERVICE_STATUS_UNKNOWN;
  self->encryption_service_timeout = 0;
  self->passphrases_match = FALSE;
  self->password_fields_filled = FALSE;

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            "io.furios.Crypted",
                            "/io/furios/Crypted",
                            "io.furios.Crypted",
                            self->cancellable,
                            (GAsyncReadyCallback) on_encryption_service_ready,
                            self);
}

CcCryptedPanel *
cc_crypted_panel_new (void)
{
  return CC_CRYPTED_PANEL (g_object_new (CC_TYPE_CRYPTED_PANEL, NULL));
}
