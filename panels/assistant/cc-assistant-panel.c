/*
 * Copyright (C) 2024 Bardia Moshiri <fakeshell@bardia.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cc-assistant-panel.h"
#include "cc-assistant-resources.h"
#include "cc-util.h"

typedef enum {
  GESTURE_SHORT_PRESS,
  GESTURE_LONG_PRESS,
  GESTURE_DOUBLE_PRESS
} AssistantGesture;

enum PredefinedAction {
    NO_ACTION = 0,
    FLASHLIGHT = 1,
    OPEN_CAMERA = 2,
    TAKE_PICTURE = 3,
    TAKE_SCREENSHOT = 4,
    SEND_TAB = 5,
    MANUAL_AUTOROTATE = 6,
    SEND_XF86BACK = 7,
    SEND_ESCAPE = 8
};

struct _CcAssistantPanel {
  CcPanel            parent;
  GtkWidget         *main_page;
  GtkCheckButton    *no_action;
  GtkCheckButton    *toggle_flashlight;
  GtkCheckButton    *open_camera;
  GtkCheckButton    *take_picture;
  GtkCheckButton    *take_screenshot;
  GtkCheckButton    *send_tab;
  GtkCheckButton    *manual_autorotate;
  GtkCheckButton    *send_xf86back;
  GtkCheckButton    *send_escape;
  GtkToggleButton   *short_press;
  GtkToggleButton   *long_press;
  GtkToggleButton   *double_press;
  AssistantGesture   current_gesture;
};

G_DEFINE_TYPE (CcAssistantPanel, cc_assistant_panel, CC_TYPE_PANEL)

static void
update_action_sensitivity (CcAssistantPanel *self, gboolean sensitive)
{
  gtk_widget_set_sensitive (GTK_WIDGET (self->no_action), sensitive);
  gtk_widget_set_sensitive (GTK_WIDGET (self->toggle_flashlight), sensitive);
  gtk_widget_set_sensitive (GTK_WIDGET (self->open_camera), sensitive);
  gtk_widget_set_sensitive (GTK_WIDGET (self->take_picture), sensitive);
  gtk_widget_set_sensitive (GTK_WIDGET (self->take_screenshot), sensitive);
  gtk_widget_set_sensitive (GTK_WIDGET (self->send_tab), sensitive);
  gtk_widget_set_sensitive (GTK_WIDGET (self->manual_autorotate), sensitive);
  gtk_widget_set_sensitive (GTK_WIDGET (self->send_xf86back), sensitive);
  gtk_widget_set_sensitive (GTK_WIDGET (self->send_escape), sensitive);
}

static void
ensure_config_directory (void)
{
  gchar *config_dir = g_build_filename (g_get_home_dir (), ".config", "assistant-button", NULL);

  if (g_mkdir_with_parents (config_dir, 0755) == -1)
    g_warning ("Failed to create config directory: %s", g_strerror (errno));

  g_free (config_dir);
}

static void
save_predefined_action (AssistantGesture gesture, enum PredefinedAction action)
{
  ensure_config_directory ();

  const gchar *filename;
  switch (gesture) {
    case GESTURE_SHORT_PRESS:
      filename = "short_press_predefined";
      break;
    case GESTURE_LONG_PRESS:
      filename = "long_press_predefined";
      break;
    case GESTURE_DOUBLE_PRESS:
      filename = "double_press_predefined";
      break;
    default:
      g_warning ("Unknown gesture type");
      return;
  }

  gchar *file_path = g_build_filename (g_get_home_dir (), ".config", "assistant-button", filename, NULL);
  gchar *content = g_strdup_printf ("%d\n", action);

  GError *error = NULL;
  if (!g_file_set_contents (file_path, content, -1, &error)) {
    g_warning ("Failed to save predefined action: %s", error->message);
    g_error_free (error);
  }

  g_free (file_path);
  g_free (content);
}

static enum PredefinedAction
load_predefined_action (AssistantGesture gesture)
{
  const gchar *filename;
  switch (gesture) {
    case GESTURE_SHORT_PRESS:
      filename = "short_press_predefined";
      break;
    case GESTURE_LONG_PRESS:
      filename = "long_press_predefined";
      break;
    case GESTURE_DOUBLE_PRESS:
      filename = "double_press_predefined";
      break;
    default:
      g_warning ("Unknown gesture type");
      return NO_ACTION;
  }

  gchar *file_path = g_build_filename (g_get_home_dir (), ".config", "assistant-button", filename, NULL);
  gchar *content = NULL;
  gsize length;
  GError *error = NULL;

  if (g_file_get_contents (file_path, &content, &length, &error)) {
    enum PredefinedAction action = (enum PredefinedAction) g_ascii_strtoll (content, NULL, 10);
    g_free (content);
    g_free (file_path);
    return action;
  } else {
    if (error->code != G_FILE_ERROR_NOENT)
      g_warning ("Failed to load predefined action: %s", error->message);

    g_error_free (error);
    g_free (file_path);
    return NO_ACTION;
  }
}

static void
update_action_buttons (CcAssistantPanel *self, enum PredefinedAction action)
{
  gtk_check_button_set_active (self->no_action, action == NO_ACTION);
  gtk_check_button_set_active (self->toggle_flashlight, action == FLASHLIGHT);
  gtk_check_button_set_active (self->open_camera, action == OPEN_CAMERA);
  gtk_check_button_set_active (self->take_picture, action == TAKE_PICTURE);
  gtk_check_button_set_active (self->take_screenshot, action == TAKE_SCREENSHOT);
  gtk_check_button_set_active (self->send_tab, action == SEND_TAB);
  gtk_check_button_set_active (self->manual_autorotate, action == MANUAL_AUTOROTATE);
  gtk_check_button_set_active (self->send_xf86back, action == SEND_XF86BACK);
  gtk_check_button_set_active (self->send_escape, action == SEND_ESCAPE);
}

static void
on_action_toggled (GtkCheckButton *button, gpointer user_data)
{
  CcAssistantPanel *self = CC_ASSISTANT_PANEL (user_data);
  const gchar *action_id = gtk_buildable_get_buildable_id (GTK_BUILDABLE (button));

  if (gtk_check_button_get_active (button)) {
    enum PredefinedAction action = NO_ACTION;

    if (g_strcmp0 (action_id, "no_action") == 0)
      action = NO_ACTION;
    else if (g_strcmp0 (action_id, "toggle_flashlight") == 0)
      action = FLASHLIGHT;
    else if (g_strcmp0 (action_id, "open_camera") == 0)
      action = OPEN_CAMERA;
    else if (g_strcmp0 (action_id, "take_picture") == 0)
      action = TAKE_PICTURE;
    else if (g_strcmp0 (action_id, "take_screenshot") == 0)
      action = TAKE_SCREENSHOT;
    else if (g_strcmp0 (action_id, "send_tab") == 0)
      action = SEND_TAB;
    else if (g_strcmp0 (action_id, "manual_autorotate") == 0)
      action = MANUAL_AUTOROTATE;
    else if (g_strcmp0 (action_id, "send_xf86back") == 0)
      action = SEND_XF86BACK;
    else if (g_strcmp0 (action_id, "send_escape") == 0)
      action = SEND_ESCAPE;

    save_predefined_action (self->current_gesture, action);
    g_debug ("Action selected: %s (enum value: %d) for gesture: %d", action_id, action, self->current_gesture);
  }
}

static void
on_gesture_toggled (GtkToggleButton *button, gpointer user_data)
{
  CcAssistantPanel *self = CC_ASSISTANT_PANEL (user_data);
  const gchar *button_id = gtk_buildable_get_buildable_id (GTK_BUILDABLE (button));

  if (gtk_toggle_button_get_active (button)) {
    update_action_sensitivity (self, TRUE);
    AssistantGesture new_gesture;
    if (g_strcmp0 (button_id, "short_press") == 0)
      new_gesture = GESTURE_SHORT_PRESS;
    else if (g_strcmp0 (button_id, "long_press") == 0)
      new_gesture = GESTURE_LONG_PRESS;
    else if (g_strcmp0 (button_id, "double_press") == 0)
      new_gesture = GESTURE_DOUBLE_PRESS;
    else
      return;

    self->current_gesture = new_gesture;
    enum PredefinedAction action = load_predefined_action (new_gesture);
    update_action_buttons (self, action);

    g_debug ("Gesture selected: %s (enum value: %d), Loaded action: %d", button_id ? button_id : "unknown", self->current_gesture, action);
  }
}

static void
cc_assistant_panel_finalize (GObject *object)
{
  G_OBJECT_CLASS (cc_assistant_panel_parent_class)->finalize (object);
}

static void
cc_assistant_panel_dispose (GObject *object)
{
  CcAssistantPanel *self = CC_ASSISTANT_PANEL (object);

  if (self->main_page) {
    gtk_widget_unparent (self->main_page);
    self->main_page = NULL;
  }

  G_OBJECT_CLASS (cc_assistant_panel_parent_class)->dispose (object);
}

static void
cc_assistant_panel_class_init (CcAssistantPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_assistant_panel_finalize;
  object_class->dispose = cc_assistant_panel_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/assistant/cc-assistant-panel.ui");

  gtk_widget_class_bind_template_child (widget_class, CcAssistantPanel, main_page);
  gtk_widget_class_bind_template_child (widget_class, CcAssistantPanel, no_action);
  gtk_widget_class_bind_template_child (widget_class, CcAssistantPanel, toggle_flashlight);
  gtk_widget_class_bind_template_child (widget_class, CcAssistantPanel, open_camera);
  gtk_widget_class_bind_template_child (widget_class, CcAssistantPanel, take_picture);
  gtk_widget_class_bind_template_child (widget_class, CcAssistantPanel, take_screenshot);
  gtk_widget_class_bind_template_child (widget_class, CcAssistantPanel, send_tab);
  gtk_widget_class_bind_template_child (widget_class, CcAssistantPanel, manual_autorotate);
  gtk_widget_class_bind_template_child (widget_class, CcAssistantPanel, send_xf86back);
  gtk_widget_class_bind_template_child (widget_class, CcAssistantPanel, send_escape);
  gtk_widget_class_bind_template_child (widget_class, CcAssistantPanel, short_press);
  gtk_widget_class_bind_template_child (widget_class, CcAssistantPanel, long_press);
  gtk_widget_class_bind_template_child (widget_class, CcAssistantPanel, double_press);
}

static void
cc_assistant_panel_init (CcAssistantPanel *self)
{
  g_resources_register (cc_assistant_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect (self->no_action, "toggled", G_CALLBACK (on_action_toggled), self);
  g_signal_connect (self->toggle_flashlight, "toggled", G_CALLBACK (on_action_toggled), self);
  g_signal_connect (self->open_camera, "toggled", G_CALLBACK (on_action_toggled), self);
  g_signal_connect (self->take_picture, "toggled", G_CALLBACK (on_action_toggled), self);
  g_signal_connect (self->take_screenshot, "toggled", G_CALLBACK (on_action_toggled), self);
  g_signal_connect (self->send_tab, "toggled", G_CALLBACK (on_action_toggled), self);
  g_signal_connect (self->manual_autorotate, "toggled", G_CALLBACK (on_action_toggled), self);
  g_signal_connect (self->send_xf86back, "toggled", G_CALLBACK (on_action_toggled), self);
  g_signal_connect (self->send_escape, "toggled", G_CALLBACK (on_action_toggled), self);

  g_signal_connect (self->short_press, "toggled", G_CALLBACK (on_gesture_toggled), self);
  g_signal_connect (self->long_press, "toggled", G_CALLBACK (on_gesture_toggled), self);
  g_signal_connect (self->double_press, "toggled", G_CALLBACK (on_gesture_toggled), self);

  ensure_config_directory ();

  // on startup nothing is selected, set to false
  update_action_sensitivity (self, FALSE);
}

CcAssistantPanel *
cc_assistant_panel_new (void)
{
  return CC_ASSISTANT_PANEL (g_object_new (CC_TYPE_ASSISTANT_PANEL, NULL));
}
