/*
 * Copyright (C) 2026 Bardia Moshiri <bardia@furilabs.com>
 * Copyright (C) 2026 Jesus Higueras <jesus@furilabs.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _GNU_SOURCE

#include "cc-face-panel.h"
#include "cc-face-resources.h"
#include "cc-util.h"

#include <adwaita.h>
#include <glib/gi18n.h>
#include <gst/gst.h>
#include <sys/syscall.h>
#include <sys/mman.h>

#include <biomd/biomd_enums.h>
#include <fart/fart_enums.h>

#define BIOMD_DBUS_NAME              "io.FuriOS.Biomd"
#define BIOMD_DBUS_PATH              "/io/FuriOS/Biomd"
#define BIOMD_DBUS_INTERFACE         "io.FuriOS.Biomd"
#define BIOMD_DBUS_FACE_PATH         "/io/FuriOS/Biomd/Face"
#define BIOMD_DBUS_FACE_INTERFACE    "io.FuriOS.Biomd.Face"
#define BIOMD_DBUS_AGENT_INTERFACE   "io.FuriOS.Biomd.Face.Agent"

#define CC_FACE_FRAME_FORMAT_BGR     0u
#define CC_FACE_SUBMIT_INTERVAL_MS   200

typedef enum {
  FACE_OP_NONE = 0,
  FACE_OP_ENROLL,
  FACE_OP_RECOGNIZE,
  FACE_OP_REMOVE
} FaceOperation;

typedef struct {
  CcFacePanel  *self;
  FaceOperation op;
} FaceAsyncCtx;

struct _CcFacePanel {
  CcPanel          parent;

  AdwToastOverlay *toast_overlay;
  AdwActionRow    *face_row;

  GtkButton       *enroll_button;
  GtkButton       *identify_button;
  GtkButton       *unenroll_button;

  AdwBottomSheet  *bottom_sheet;
  GtkPicture      *viewfinder_picture;
  GtkProgressBar  *enroll_progress;
  GtkLabel        *sheet_title;
  GtkLabel        *sheet_subtitle;

  GDBusProxy      *face_proxy;
  GDBusProxy      *face_props_proxy;
  GDBusProxy      *agent_proxy;

  gchar           *agent_path;

  GstElement      *camera_pipeline;
  GstElement      *appsink;

  gboolean         available;
  gboolean         enrolled;

  gboolean         agent_has_access;
  gboolean         operation_started;
  gboolean         start_call_in_flight;
  gboolean         submit_frames_enabled;
  gboolean         closing_sheet;

  FaceOperation    active_op;
  FaceOperation    pending_op;

  guint            submit_in_flight;
  gint64           last_submit_us;

  guint32          last_enrollment_state;
  guint32          last_recognition_state;
  gint32           last_enrollment_progress;
};

G_DEFINE_TYPE (CcFacePanel, cc_face_panel, CC_TYPE_PANEL)

static FaceAsyncCtx *
face_async_ctx_new (CcFacePanel  *self,
                    FaceOperation op)
{
  FaceAsyncCtx *ctx = g_new0 (FaceAsyncCtx, 1);

  ctx->self = g_object_ref (self);
  ctx->op = op;

  return ctx;
}

static void
face_async_ctx_free (FaceAsyncCtx *ctx)
{
  if (!ctx)
    return;

  g_clear_object (&ctx->self);
  g_free (ctx);
}

static void
show_toast (CcFacePanel *self,
            const char  *format,
            ...)
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

static gboolean
write_all (int           fd,
           const guint8 *buf,
           gsize         len)
{
  gsize off = 0;

  while (off < len) {
    ssize_t written = write (fd, buf + off, len - off);

    if (written < 0) {
      if (errno == EINTR)
        continue;

      return FALSE;
    }

    if (written == 0)
      return FALSE;

    off += (gsize) written;
  }

  return TRUE;
}

static gboolean
ping_biomd (void)
{
  g_autoptr(GDBusProxy) proxy = NULL;
  g_autoptr(GVariant) result = NULL;
  g_autoptr(GError) error = NULL;
  gboolean ok = FALSE;

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                         G_DBUS_PROXY_FLAGS_NONE,
                                         NULL,
                                         BIOMD_DBUS_NAME,
                                         BIOMD_DBUS_PATH,
                                         BIOMD_DBUS_INTERFACE,
                                         NULL,
                                         &error);

  if (!proxy) {
    g_warning ("Failed to create biomd root proxy: %s", error->message);
    return FALSE;
  }

  result = g_dbus_proxy_call_sync (proxy,
                                   "Ping",
                                   NULL,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   NULL,
                                   &error);

  if (!result) {
    g_warning ("Failed to ping biomd: %s", error->message);
    return FALSE;
  }

  g_variant_get (result, "(b)", &ok);
  return ok;
}

static gboolean
update_face_available (CcFacePanel *self)
{
  g_autoptr(GVariant) result = NULL;
  g_autoptr(GError) error = NULL;
  GVariant *value = NULL;
  gint32 implementation_type = 0;

  result = g_dbus_proxy_call_sync (self->face_props_proxy,
                                   "Get",
                                   g_variant_new ("(ss)", BIOMD_DBUS_FACE_INTERFACE, "ImplementationType"),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   NULL,
                                   &error);

  if (!result) {
    g_debug ("Failed to get face property %s: %s", "ImplementationType", error->message);
    self->available = FALSE;
    return FALSE;
  }

  g_variant_get (result, "(v)", &value);

  if (!g_variant_is_of_type (value, G_VARIANT_TYPE_INT32)) {
    g_variant_unref (value);
    self->available = FALSE;
    return FALSE;
  }

  implementation_type = g_variant_get_int32 (value);
  g_variant_unref (value);

  self->available = implementation_type != 0;
  return self->available;
}

static void
update_face_row (CcFacePanel *self)
{
  g_autoptr(GVariant) result = NULL;
  g_autoptr(GError) error = NULL;
  GVariant *value = NULL;

  if (!self->available) {
    adw_action_row_set_subtitle (self->face_row, _("Face recognition is unavailable"));
    gtk_widget_set_visible (GTK_WIDGET (self->enroll_button), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->identify_button), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->unenroll_button), FALSE);
    return;
  }

  result = g_dbus_proxy_call_sync (self->face_props_proxy,
                                   "Get",
                                   g_variant_new ("(ss)", BIOMD_DBUS_FACE_INTERFACE, "FaceEnrolled"),
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,
                                   NULL,
                                   &error);

  if (!result) {
    g_debug ("Failed to get face property %s: %s", "FaceEnrolled", error->message);
  } else {
    g_variant_get (result, "(v)", &value);

    if (g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN)) {
      self->enrolled = g_variant_get_boolean (value);
    }

    if (value)
      g_variant_unref (value);
  }

  g_debug ("FaceEnrolled property says: %d", self->enrolled ? 1 : 0);

  if (self->enrolled) {
    adw_action_row_set_subtitle (self->face_row, _("A face is enrolled"));
    gtk_widget_set_visible (GTK_WIDGET (self->enroll_button), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->identify_button), TRUE);
    gtk_widget_set_visible (GTK_WIDGET (self->unenroll_button), TRUE);
  } else {
    adw_action_row_set_subtitle (self->face_row, _("No face is enrolled"));
    gtk_widget_set_visible (GTK_WIDGET (self->enroll_button), TRUE);
    gtk_widget_set_visible (GTK_WIDGET (self->identify_button), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->unenroll_button), FALSE);
  }
}

static const char *
enrollment_state_to_string (guint32 state)
{
  switch ((EnrollmentState) state) {
    case ENROLLMENT_IDLE:
      return _("Enrollment idle");
    case ENROLLMENT_FAIL:
      return _("Enrollment failed");
    case ENROLLMENT_IN_PROGRESS:
      return _("Enrollment in progress");
    case ENROLLMENT_COMPLETE:
      return _("Enrollment complete");
    case ENROLLMENT_SAVE_FAILED:
      return _("Enrollment completed, but saving failed");
    case ENROLLMENT_MULTIPLE_FACES:
      return _("Multiple faces detected");
    case ENROLLMENT_NO_FACE:
      return _("No face detected");
    case ENROLLMENT_BAD_LIGHTING:
      return _("Lighting is not good enough");
    default:
      return _("Unknown enrollment state");
  }
}

static const char *
recognition_state_to_string (guint32 state)
{
  switch ((RecognitionState) state) {
    case RECOGNITION_IDLE:
      return _("Recognition idle");
    case RECOGNITION_FAIL:
      return _("Recognition failed");
    case RECOGNITION_NO_FACE:
      return _("No face detected");
    case RECOGNITION_MULTIPLE_FACES:
      return _("Multiple faces detected");
    case RECOGNITION_NOT_ENROLLED:
      return _("No face is enrolled");
    case RECOGNITION_RECOGNIZED:
      return _("Face recognized");
    case RECOGNITION_NOT_RECOGNIZED:
      return _("Face not recognized");
    default:
      return _("Unknown recognition state");
  }
}

static void
submit_frame_cb (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  CcFacePanel *self = CC_FACE_PANEL (user_data);
  g_autoptr(GUnixFDList) out_fd_list = NULL;
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GError) error = NULL;

  reply = g_dbus_proxy_call_with_unix_fd_list_finish (G_DBUS_PROXY (source),
                                                      &out_fd_list,
                                                      result,
                                                      &error);

  if (self->submit_in_flight > 0)
    self->submit_in_flight--;

  if (!reply) {
    g_debug ("SubmitFrame failed: %s", error->message);
    return;
  }
}

static void
submit_frame (CcFacePanel  *self,
              const guint8 *data,
              gsize         size,
              gint          width,
              gint          height,
              gint          channels)
{
  g_autoptr(GUnixFDList) fd_list = NULL;
  g_autoptr(GError) error = NULL;
  gint64 now_us;
  gsize expected;
  int fd;
  int handle;

  if (!self->submit_frames_enabled ||
      !self->operation_started ||
      !self->agent_has_access ||
      !self->agent_proxy ||
      (self->active_op != FACE_OP_ENROLL &&
       self->active_op != FACE_OP_RECOGNIZE))
    return;

  now_us = g_get_monotonic_time ();
  if (self->last_submit_us &&
      now_us - self->last_submit_us < (gint64) CC_FACE_SUBMIT_INTERVAL_MS * 1000)
    return;

  if (self->submit_in_flight > 0)
    return;

  expected = (gsize) width * (gsize) height * (gsize) channels;
  if (size < expected)
    return;

  self->last_submit_us = now_us;

  fd = memfd_create ("cc-face-frame", MFD_CLOEXEC);
  if (fd < 0) {
    g_debug ("memfd_create failed: %s", g_strerror (errno));
    return;
  }

  if (ftruncate (fd, (off_t) expected) != 0) {
    g_debug ("ftruncate failed: %s", g_strerror (errno));
    close (fd);
    return;
  }

  if (!write_all (fd, data, expected)) {
    g_debug ("Failed to write frame memfd: %s", g_strerror (errno));
    close (fd);
    return;
  }

  if (lseek (fd, 0, SEEK_SET) < 0) {
    g_debug ("Failed to rewind frame memfd: %s", g_strerror (errno));
    close (fd);
    return;
  }

  fd_list = g_unix_fd_list_new ();
  handle = g_unix_fd_list_append (fd_list, fd, &error);
  close (fd);

  if (handle < 0) {
    g_debug ("Failed to append fd to fd list: %s", error->message);
    return;
  }

  self->submit_in_flight++;

  g_dbus_proxy_call_with_unix_fd_list (self->agent_proxy,
                                       "SubmitFrame",
                                       g_variant_new ("(hiiiu)",
                                                      handle,
                                                      width,
                                                      height,
                                                      channels,
                                                      CC_FACE_FRAME_FORMAT_BGR),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1,
                                       fd_list,
                                       NULL,
                                       submit_frame_cb,
                                       self);
}

static GstFlowReturn
on_new_sample (GstElement *sink,
               gpointer    user_data)
{
  CcFacePanel *self = CC_FACE_PANEL (user_data);
  GstSample *sample = NULL;
  GstBuffer *buffer;
  GstCaps *caps;
  GstStructure *structure;
  GstMapInfo map;
  gint width = 0;
  gint height = 0;

  g_signal_emit_by_name (sink, "pull-sample", &sample);
  if (!sample)
    return GST_FLOW_OK;

  buffer = gst_sample_get_buffer (sample);
  caps = gst_sample_get_caps (sample);

  if (!buffer || !caps) {
    gst_sample_unref (sample);
    return GST_FLOW_OK;
  }

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height)) {
    gst_sample_unref (sample);
    return GST_FLOW_OK;
  }

  if (gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    submit_frame (self, map.data, map.size, width, height, 3);
    gst_buffer_unmap (buffer, &map);
  }

  gst_sample_unref (sample);
  return GST_FLOW_OK;
}

static gboolean
start_camera (CcFacePanel *self)
{
  GstElement *sink;
  GdkPaintable *paintable = NULL;
  g_autoptr(GError) error = NULL;

  if (self->camera_pipeline) {
    gst_element_set_state (self->camera_pipeline, GST_STATE_PLAYING);
    return TRUE;
  }

  self->camera_pipeline = gst_parse_launch (
    "droidcamsrc camera_device=1 mode=2 ! "
    "queue max-size-buffers=1 leaky=downstream ! "
    "video/x-raw,width=640,height=480 ! "
    "videoconvert ! videoflip video-direction=auto ! tee name=t "
    "t. ! queue ! videoconvert ! gtk4paintablesink name=viewsink sync=false "
    "t. ! queue max-size-buffers=1 leaky=downstream ! "
    "videoconvert ! video/x-raw,format=BGR ! "
    "appsink name=appsink max-buffers=1 drop=true emit-signals=true sync=false",
    &error);

  if (!self->camera_pipeline) {
    show_toast (self,
                _("Unable to start camera: %s"),
                error ? error->message : _("Unknown error"));
    return FALSE;
  }

  sink = gst_bin_get_by_name (GST_BIN (self->camera_pipeline), "viewsink");
  if (!sink) {
    show_toast (self, _("Unable to create camera viewfinder"));
    gst_clear_object (&self->camera_pipeline);
    return FALSE;
  }

  g_object_get (sink, "paintable", &paintable, NULL);
  if (paintable) {
    gtk_picture_set_paintable (self->viewfinder_picture, paintable);
    g_object_unref (paintable);
  }

  gst_object_unref (sink);

  self->appsink = gst_bin_get_by_name (GST_BIN (self->camera_pipeline), "appsink");
  if (!self->appsink) {
    show_toast (self, _("Unable to create camera frame sink"));
    gst_clear_object (&self->camera_pipeline);
    return FALSE;
  }

  g_signal_connect (self->appsink,
                    "new-sample",
                    G_CALLBACK (on_new_sample),
                    self);

  gst_element_set_state (self->camera_pipeline, GST_STATE_PLAYING);
  return TRUE;
}

static void
open_camera_sheet (CcFacePanel *self,
                   const char  *title,
                   const char  *subtitle,
                   gboolean     show_progress)
{
  gtk_label_set_text (self->sheet_title, title);
  gtk_label_set_text (self->sheet_subtitle, subtitle);

  gtk_widget_set_visible (GTK_WIDGET (self->enroll_progress), show_progress);
  gtk_progress_bar_set_fraction (self->enroll_progress, 0.0);

  adw_bottom_sheet_set_open (self->bottom_sheet, TRUE);
  start_camera (self);
}

static void
close_camera_sheet (CcFacePanel *self)
{
  self->closing_sheet = TRUE;
  adw_bottom_sheet_set_open (self->bottom_sheet, FALSE);
  self->closing_sheet = FALSE;

  self->submit_frames_enabled = FALSE;
  self->operation_started = FALSE;
  self->start_call_in_flight = FALSE;
  self->last_submit_us = 0;

  if (self->camera_pipeline)
    gst_element_set_state (self->camera_pipeline, GST_STATE_NULL);
}

static void
destroy_agent (CcFacePanel *self)
{
  g_autofree gchar *path = NULL;

  self->submit_frames_enabled = FALSE;
  self->operation_started = FALSE;
  self->start_call_in_flight = FALSE;
  self->agent_has_access = FALSE;
  self->pending_op = FACE_OP_NONE;

  if (self->agent_proxy)
    g_dbus_proxy_call (self->agent_proxy,
                       "Cancel",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       NULL,
                       NULL);

  if (self->face_proxy) {
    g_dbus_proxy_call (self->face_proxy,
                       "UnregisterEnrollmentAgent",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       NULL,
                       NULL);

    g_dbus_proxy_call (self->face_proxy,
                       "UnregisterRecognitionAgent",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       NULL,
                       NULL);
  }

  if (self->agent_path && self->face_proxy) {
    path = g_strdup (self->agent_path);

    g_dbus_proxy_call (self->face_proxy,
                       "DestroyAgent",
                       g_variant_new ("(o)", path),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       NULL,
                       NULL);
  }

  g_clear_object (&self->agent_proxy);
  g_clear_pointer (&self->agent_path, g_free);
}

static void
finish_operation (CcFacePanel *self)
{
  self->active_op = FACE_OP_NONE;
  self->pending_op = FACE_OP_NONE;
  self->submit_frames_enabled = FALSE;
  self->operation_started = FALSE;
  self->start_call_in_flight = FALSE;
  self->last_submit_us = 0;

  close_camera_sheet (self);
  destroy_agent (self);
}

static void
start_enrollment_cb (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  FaceAsyncCtx *ctx = user_data;
  CcFacePanel *self = ctx->self;
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GError) error = NULL;
  gboolean success = FALSE;

  self->start_call_in_flight = FALSE;

  reply = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);

  if (reply) {
    if (g_variant_is_of_type (reply, G_VARIANT_TYPE ("(b)")))
      g_variant_get (reply, "(b)", &success);
    else
      success = TRUE;
  }

  if (!success) {
    show_toast (self,
                _("Failed to start enrollment: %s"),
                error ? error->message : _("operation returned false"));
    finish_operation (self);
    face_async_ctx_free (ctx);
    return;
  }

  self->operation_started = TRUE;
  self->submit_frames_enabled = TRUE;
  self->last_submit_us = 0;

  show_toast (self, _("Face enrollment started"));
  face_async_ctx_free (ctx);
}

static void
start_recognition_cb (GObject      *source,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  FaceAsyncCtx *ctx = user_data;
  CcFacePanel *self = ctx->self;
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GError) error = NULL;
  gboolean success = FALSE;

  self->start_call_in_flight = FALSE;

  reply = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);

  if (reply) {
    if (g_variant_is_of_type (reply, G_VARIANT_TYPE ("(b)")))
      g_variant_get (reply, "(b)", &success);
    else
      success = TRUE;
  }

  if (!success) {
    show_toast (self,
                _("Failed to start recognition: %s"),
                error ? error->message : _("operation returned false"));
    finish_operation (self);
    face_async_ctx_free (ctx);
    return;
  }

  self->operation_started = TRUE;
  self->submit_frames_enabled = TRUE;
  self->last_submit_us = 0;

  show_toast (self, _("Face recognition started"));
  face_async_ctx_free (ctx);
}

static void
remove_face_cb (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
  FaceAsyncCtx *ctx = user_data;
  CcFacePanel *self = ctx->self;
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GError) error = NULL;
  gboolean success = FALSE;

  self->start_call_in_flight = FALSE;

  reply = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);

  if (reply) {
    if (g_variant_is_of_type (reply, G_VARIANT_TYPE ("(b)")))
      g_variant_get (reply, "(b)", &success);
    else
      success = TRUE;
  }

  if (!success) {
    show_toast (self,
                _("Failed to remove face: %s"),
                error ? error->message : _("operation returned false"));
    destroy_agent (self);
    face_async_ctx_free (ctx);
    return;
  }

  self->enrolled = FALSE;
  update_face_row (self);
  show_toast (self, _("Face removed"));

  destroy_agent (self);
  face_async_ctx_free (ctx);
}

static void
start_pending_operation (CcFacePanel *self)
{
  FaceAsyncCtx *ctx;

  if (!self->agent_proxy ||
      !self->agent_has_access ||
      self->operation_started ||
      self->start_call_in_flight)
    return;

  if (self->pending_op == FACE_OP_NONE)
    return;

  self->start_call_in_flight = TRUE;

  ctx = face_async_ctx_new (self, self->pending_op);

  if (self->pending_op == FACE_OP_ENROLL) {
    g_dbus_proxy_call (self->agent_proxy,
                       "StartEnrollment",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       start_enrollment_cb,
                       ctx);
  } else if (self->pending_op == FACE_OP_RECOGNIZE) {
    g_dbus_proxy_call (self->agent_proxy,
                       "StartRecognition",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       start_recognition_cb,
                       ctx);
  } else if (self->pending_op == FACE_OP_REMOVE) {
    g_dbus_proxy_call (self->agent_proxy,
                       "RemoveFaceData",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       remove_face_cb,
                       ctx);
  } else {
    self->start_call_in_flight = FALSE;
    face_async_ctx_free (ctx);
  }
}

static void
on_agent_signal (GDBusProxy *proxy,
                 gchar      *sender_name,
                 gchar      *signal_name,
                 GVariant   *parameters,
                 gpointer    user_data)
{
  CcFacePanel *self = CC_FACE_PANEL (user_data);

  if (g_strcmp0 (signal_name, "AccessChanged") == 0) {
    gboolean has_access = FALSE;

    g_variant_get (parameters, "(b)", &has_access);
    self->agent_has_access = has_access;

    g_debug ("AccessChanged: %d", has_access ? 1 : 0);

    if (has_access && self->pending_op != FACE_OP_NONE && !self->operation_started)
      start_pending_operation (self);
    else if (!has_access &&
             (self->active_op == FACE_OP_ENROLL ||
              self->active_op == FACE_OP_RECOGNIZE))
      show_toast (self, _("Face agent access was revoked"));
  } else if (g_strcmp0 (signal_name, "EnrollmentProgressChanged") == 0) {
    gint32 progress = 0;

    g_variant_get (parameters, "(i)", &progress);

    if (progress < 0)
      progress = 0;
    if (progress > 100)
      progress = 100;

    gtk_widget_set_visible (GTK_WIDGET (self->enroll_progress), TRUE);
    gtk_progress_bar_set_fraction (self->enroll_progress, progress / 100.0);

    if (progress != self->last_enrollment_progress) {
      self->last_enrollment_progress = progress;

      if (progress > 0)
        show_toast (self, _("Enrollment progress: %d%%"), progress);
    }
  } else if (g_strcmp0 (signal_name, "EnrollmentStateChanged") == 0) {
    guint32 state = 0;

    g_variant_get (parameters, "(u)", &state);
    g_debug ("EnrollmentStateChanged: %u", state);

    if (state != self->last_enrollment_state) {
      self->last_enrollment_state = state;

      if (state != ENROLLMENT_IDLE && state != ENROLLMENT_IN_PROGRESS && state != ENROLLMENT_FAIL)
        show_toast (self, "%s", enrollment_state_to_string (state));
    }
  } else if (g_strcmp0 (signal_name, "RecognitionStateChanged") == 0) {
    guint32 state = 0;

    g_variant_get (parameters, "(u)", &state);
    g_debug ("RecognitionStateChanged: %u", state);

    if (state != self->last_recognition_state) {
      self->last_recognition_state = state;

      if (state != RECOGNITION_IDLE)
        show_toast (self, "%s", recognition_state_to_string (state));
    }

    if (state == RECOGNITION_NOT_ENROLLED)
      finish_operation (self);
  }
}

static void
register_agent_cb (GObject      *source,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  FaceAsyncCtx *ctx = user_data;
  CcFacePanel *self = ctx->self;
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GError) error = NULL;
  gboolean success = FALSE;

  reply = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);

  if (error) {
    if (g_strstr_len (error->message, -1, "already registered")) {
      g_debug ("Face agent already registered, waiting for access handoff");
      face_async_ctx_free (ctx);
      return;
    }

    show_toast (self,
                _("Failed to register face agent: %s"),
                error->message);

    if (ctx->op == FACE_OP_ENROLL || ctx->op == FACE_OP_RECOGNIZE)
      finish_operation (self);
    else
      destroy_agent (self);

    face_async_ctx_free (ctx);
    return;
  }

  if (reply) {
    if (g_variant_is_of_type (reply, G_VARIANT_TYPE ("(b)")))
      g_variant_get (reply, "(b)", &success);
    else
      success = TRUE;
  }

  if (!success) {
    show_toast (self,
                _("Failed to register face agent: %s"),
                _("operation returned false"));

    if (ctx->op == FACE_OP_ENROLL || ctx->op == FACE_OP_RECOGNIZE)
      finish_operation (self);
    else
      destroy_agent (self);

    face_async_ctx_free (ctx);
    return;
  }

  g_debug ("Agent registered");

  if (self->agent_has_access)
    start_pending_operation (self);

  face_async_ctx_free (ctx);
}

static void
continue_agent_operation (CcFacePanel  *self,
                          FaceOperation op)
{
  FaceAsyncCtx *ctx = face_async_ctx_new (self, op);

  self->pending_op = op;
  self->operation_started = FALSE;
  self->start_call_in_flight = FALSE;
  self->submit_frames_enabled = FALSE;

  if (op == FACE_OP_ENROLL || op == FACE_OP_REMOVE)
    g_dbus_proxy_call (self->face_proxy,
                       "RegisterEnrollmentAgent",
                       g_variant_new ("(o)", self->agent_path),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       register_agent_cb,
                       ctx);
  else if (op == FACE_OP_RECOGNIZE)
    g_dbus_proxy_call (self->face_proxy,
                       "RegisterRecognitionAgent",
                       g_variant_new ("(o)", self->agent_path),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       register_agent_cb,
                       ctx);
  else
    face_async_ctx_free (ctx);
}

static void
agent_proxy_created_cb (GObject      *source,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  FaceAsyncCtx *ctx = user_data;
  CcFacePanel *self = ctx->self;
  g_autoptr(GError) error = NULL;

  self->agent_proxy = g_dbus_proxy_new_for_bus_finish (result, &error);

  if (!self->agent_proxy) {
    show_toast (self,
                _("Failed to create face agent proxy: %s"),
                error ? error->message : _("operation returned false"));
    finish_operation (self);
    face_async_ctx_free (ctx);
    return;
  }

  g_signal_connect (self->agent_proxy,
                    "g-signal",
                    G_CALLBACK (on_agent_signal),
                    self);

  continue_agent_operation (self, ctx->op);
  face_async_ctx_free (ctx);
}

static void
create_agent_cb (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  FaceAsyncCtx *ctx = user_data;
  CcFacePanel *self = ctx->self;
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GError) error = NULL;
  const char *path = NULL;

  reply = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);

  if (!reply) {
    show_toast (self,
                _("Failed to create face agent: %s"),
                error ? error->message : _("operation returned false"));
    finish_operation (self);
    face_async_ctx_free (ctx);
    return;
  }

  g_variant_get (reply, "(&o)", &path);

  g_free (self->agent_path);
  self->agent_path = g_strdup (path);
  self->agent_has_access = FALSE;

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            BIOMD_DBUS_NAME,
                            self->agent_path,
                            BIOMD_DBUS_AGENT_INTERFACE,
                            NULL,
                            agent_proxy_created_cb,
                            ctx);
}

static void
ensure_agent (CcFacePanel  *self,
              FaceOperation op)
{
  FaceAsyncCtx *ctx;

  self->pending_op = op;

  if (self->agent_proxy && self->agent_path) {
    continue_agent_operation (self, op);
    return;
  }

  ctx = face_async_ctx_new (self, op);

  g_dbus_proxy_call (self->face_proxy,
                     "CreateAgent",
                     NULL,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     NULL,
                     create_agent_cb,
                     ctx);
}

static void
on_face_signal (GDBusProxy *proxy,
                gchar      *sender_name,
                gchar      *signal_name,
                GVariant   *parameters,
                gpointer    user_data)
{
  CcFacePanel *self = CC_FACE_PANEL (user_data);

  if (g_strcmp0 (signal_name, "FaceEnrolledChanged") == 0) {
    g_variant_get (parameters, "(b)", &self->enrolled);

    update_face_row (self);

    g_debug ("FaceEnrolled property says: %d", self->enrolled ? 1 : 0);

    if (self->enrolled) {
      show_toast (self, _("Face enrolled"));

      if (self->active_op == FACE_OP_ENROLL)
        finish_operation (self);
    } else {
      show_toast (self, _("Face removed"));
    }
  } else if (g_strcmp0 (signal_name, "StateChanged") == 0) {
    gint32 state = 0;

    g_variant_get (parameters, "(i)", &state);

    if (state == STATE_IDLE)
      g_debug ("Face state changed: IDLE");
    else if (state == STATE_ENROLLING)
      g_debug ("Face state changed: ENROLLING");
    else if (state == STATE_IDENTIFYING)
      g_debug ("Face state changed: IDENTIFYING");
    else
      g_debug ("Face state changed: %d", state);
  }
}

static void
on_enroll_clicked (GtkButton   *button,
                   CcFacePanel *self)
{
  self->active_op = FACE_OP_ENROLL;
  self->agent_has_access = FALSE;
  self->operation_started = FALSE;
  self->start_call_in_flight = FALSE;
  self->submit_frames_enabled = FALSE;
  self->pending_op = FACE_OP_ENROLL;

  self->last_enrollment_state = G_MAXUINT32;
  self->last_recognition_state = G_MAXUINT32;
  self->last_enrollment_progress = -1;

  open_camera_sheet (self,
                     _("Enroll Face"),
                     _("Look at the camera and keep your face centered."),
                     TRUE);

  ensure_agent (self, FACE_OP_ENROLL);
}

static void
on_identify_clicked (GtkButton   *button,
                     CcFacePanel *self)
{
  self->active_op = FACE_OP_RECOGNIZE;
  self->agent_has_access = FALSE;
  self->operation_started = FALSE;
  self->start_call_in_flight = FALSE;
  self->submit_frames_enabled = FALSE;
  self->pending_op = FACE_OP_RECOGNIZE;

  self->last_enrollment_state = G_MAXUINT32;
  self->last_recognition_state = G_MAXUINT32;
  self->last_enrollment_progress = -1;

  open_camera_sheet (self,
                     _("Identify Face"),
                     _("Look at the camera to test recognition."),
                     FALSE);

  ensure_agent (self, FACE_OP_RECOGNIZE);
}

static void
on_unenroll_response (AdwAlertDialog *dialog,
                      const char     *response,
                      CcFacePanel    *self)
{
  if (g_strcmp0 (response, "remove") != 0)
    return;

  self->pending_op = FACE_OP_REMOVE;
  self->agent_has_access = FALSE;
  self->operation_started = FALSE;
  self->start_call_in_flight = FALSE;
  self->submit_frames_enabled = FALSE;

  ensure_agent (self, FACE_OP_REMOVE);
}

static void
on_unenroll_clicked (GtkButton   *button,
                     CcFacePanel *self)
{
  AdwAlertDialog *dialog;

  dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (_("Remove Face?"),
                                                   _("This will remove the enrolled face from this device.")));

  adw_alert_dialog_add_responses (dialog,
                                  "cancel", _("_Cancel"),
                                  "remove", _("_Remove"),
                                  NULL);

  adw_alert_dialog_set_response_appearance (dialog,
                                            "remove",
                                            ADW_RESPONSE_DESTRUCTIVE);
  adw_alert_dialog_set_default_response (dialog, "cancel");
  adw_alert_dialog_set_close_response (dialog, "cancel");

  g_signal_connect (dialog,
                    "response",
                    G_CALLBACK (on_unenroll_response),
                    self);

  adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (self));
}

static void
cc_face_on_bottom_sheet_open_changed (AdwBottomSheet *bottom_sheet,
                                      GParamSpec     *pspec,
                                      CcFacePanel    *self)
{
  if (adw_bottom_sheet_get_open (bottom_sheet))
    return;

  self->submit_frames_enabled = FALSE;
  self->operation_started = FALSE;
  self->start_call_in_flight = FALSE;
  self->last_submit_us = 0;

  if (self->camera_pipeline)
    gst_element_set_state (self->camera_pipeline, GST_STATE_NULL);

  if (self->closing_sheet)
    return;

  if (self->agent_proxy &&
      (self->active_op == FACE_OP_ENROLL ||
       self->active_op == FACE_OP_RECOGNIZE)) {
    g_dbus_proxy_call (self->agent_proxy,
                       "Cancel",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       NULL,
                       NULL);

    show_toast (self, _("Face operation canceled"));
  }

  self->active_op = FACE_OP_NONE;
  self->pending_op = FACE_OP_NONE;

  destroy_agent (self);
}

static gboolean
init_dbus_proxies (CcFacePanel *self)
{
  g_autoptr(GError) error = NULL;

  self->face_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                    G_DBUS_PROXY_FLAGS_NONE,
                                                    NULL,
                                                    BIOMD_DBUS_NAME,
                                                    BIOMD_DBUS_FACE_PATH,
                                                    BIOMD_DBUS_FACE_INTERFACE,
                                                    NULL,
                                                    &error);

  if (!self->face_proxy) {
    g_warning ("Failed to create face proxy: %s", error->message);
    return FALSE;
  }

  self->face_props_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                          G_DBUS_PROXY_FLAGS_NONE,
                                                          NULL,
                                                          BIOMD_DBUS_NAME,
                                                          BIOMD_DBUS_FACE_PATH,
                                                          "org.freedesktop.DBus.Properties",
                                                          NULL,
                                                          &error);

  if (!self->face_props_proxy) {
    g_warning ("Failed to create face properties proxy: %s", error->message);
    return FALSE;
  }

  g_signal_connect (self->face_proxy,
                    "g-signal",
                    G_CALLBACK (on_face_signal),
                    self);

  return TRUE;
}

static void
cc_face_panel_finalize (GObject *object)
{
  CcFacePanel *self = CC_FACE_PANEL (object);

  destroy_agent (self);

  if (self->camera_pipeline)
    gst_element_set_state (self->camera_pipeline, GST_STATE_NULL);

  if (self->appsink) {
    gst_object_unref (self->appsink);
    self->appsink = NULL;
  }

  gst_clear_object (&self->camera_pipeline);

  g_clear_object (&self->face_proxy);
  g_clear_object (&self->face_props_proxy);
  g_clear_object (&self->agent_proxy);
  g_clear_pointer (&self->agent_path, g_free);

  G_OBJECT_CLASS (cc_face_panel_parent_class)->finalize (object);
}

static void
cc_face_panel_class_init (CcFacePanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = cc_face_panel_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/face/cc-face-panel.ui");

  gtk_widget_class_bind_template_child (widget_class, CcFacePanel, toast_overlay);
  gtk_widget_class_bind_template_child (widget_class, CcFacePanel, face_row);
  gtk_widget_class_bind_template_child (widget_class, CcFacePanel, enroll_button);
  gtk_widget_class_bind_template_child (widget_class, CcFacePanel, identify_button);
  gtk_widget_class_bind_template_child (widget_class, CcFacePanel, unenroll_button);
  gtk_widget_class_bind_template_child (widget_class, CcFacePanel, bottom_sheet);
  gtk_widget_class_bind_template_child (widget_class, CcFacePanel, viewfinder_picture);
  gtk_widget_class_bind_template_child (widget_class, CcFacePanel, enroll_progress);
  gtk_widget_class_bind_template_child (widget_class, CcFacePanel, sheet_title);
  gtk_widget_class_bind_template_child (widget_class, CcFacePanel, sheet_subtitle);

  gtk_widget_class_bind_template_callback (widget_class, on_enroll_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_identify_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_unenroll_clicked);
  gtk_widget_class_bind_template_callback (widget_class, cc_face_on_bottom_sheet_open_changed);
}

static void
cc_face_panel_init (CcFacePanel *self)
{
  g_autoptr(GtkCssProvider) provider = NULL;

  g_resources_register (cc_face_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  gst_init (NULL, NULL);

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider,
                                       "/org/gnome/control-center/face/face.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  self->available = FALSE;
  self->enrolled = FALSE;
  self->agent_has_access = FALSE;
  self->operation_started = FALSE;
  self->start_call_in_flight = FALSE;
  self->submit_frames_enabled = FALSE;
  self->closing_sheet = FALSE;
  self->active_op = FACE_OP_NONE;
  self->pending_op = FACE_OP_NONE;
  self->submit_in_flight = 0;
  self->last_submit_us = 0;

  self->last_enrollment_state = G_MAXUINT32;
  self->last_recognition_state = G_MAXUINT32;
  self->last_enrollment_progress = -1;

  if (!ping_biomd ()) {
    adw_action_row_set_subtitle (self->face_row, _("biomd is unavailable"));
    gtk_widget_set_visible (GTK_WIDGET (self->enroll_button), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->identify_button), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->unenroll_button), FALSE);
    return;
  }

  if (!init_dbus_proxies (self)) {
    adw_action_row_set_subtitle (self->face_row, _("Face recognition is unavailable"));
    gtk_widget_set_visible (GTK_WIDGET (self->enroll_button), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->identify_button), FALSE);
    gtk_widget_set_visible (GTK_WIDGET (self->unenroll_button), FALSE);
    return;
  }

  update_face_available (self);
  update_face_row (self);

}

CcFacePanel *
cc_face_panel_new (void)
{
  return CC_FACE_PANEL (g_object_new (CC_TYPE_FACE_PANEL, NULL));
}
