/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* cc-wwan-bands-dialog.c
 *
 * Copyright 2019,2022 Purism SPC
 * Copyright 2025 Furi Labs
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *   Jesús Higueras <jesus@furilabs.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "cc-network-bands-dialog"

#include <config.h>
#include <glib/gi18n.h>
#include <libmm-glib.h>

#include "cc-wwan-bands-dialog.h"
#include "cc-wwan-resources.h"

/**
 * @short_description: WWAN network bands selection dialog
 */

#define CC_TYPE_WWAN_BAND_ROW (cc_wwan_band_row_get_type())
G_DECLARE_FINAL_TYPE (CcWwanBandRow, cc_wwan_band_row, CC, WWAN_BAND_ROW, GtkListBoxRow)

struct _CcWwanBandsDialog
{
  GtkDialog      parent_instance;

  CcWwanDevice  *device;
  GtkListBox    *twog_band_list;
  GtkListBox    *threeg_band_list;
  GtkListBox    *fourg_band_list;
  GtkListBox    *fiveg_band_list;

  GList         *band_rows;
};

struct BandDefinition
{
  const gchar *name;
  const guint struct_val;
};

static const struct BandDefinition bands[4][64] =
{
  // 2G
  {
    {"GSM 850", MM_MODEM_BAND_G850},
    {"GSM 900", MM_MODEM_BAND_EGSM},
    {"GSM 1800", MM_MODEM_BAND_DCS},
    {"GSM 1900", MM_MODEM_BAND_PCS},
    {NULL, 0},
  },
  // 3G
  {
    {"PCS-1900", MM_MODEM_BAND_UTRAN_2},
    {"UMTS-900", MM_MODEM_BAND_UTRAN_8},
    {NULL, 0},
  },
  // 4G
  {
    {"Band 1", MM_MODEM_BAND_EUTRAN_1},
    {"Band 2", MM_MODEM_BAND_EUTRAN_2},
    {"Band 3", MM_MODEM_BAND_EUTRAN_3},
    {"Band 4", MM_MODEM_BAND_EUTRAN_4},
    {"Band 5", MM_MODEM_BAND_EUTRAN_5},
    {"Band 6", MM_MODEM_BAND_EUTRAN_6},
    {"Band 7", MM_MODEM_BAND_EUTRAN_7},
    {"Band 8", MM_MODEM_BAND_EUTRAN_8},
    {"Band 9", MM_MODEM_BAND_EUTRAN_9},
    {"Band 10", MM_MODEM_BAND_EUTRAN_10},
    {"Band 11", MM_MODEM_BAND_EUTRAN_11},
    {"Band 12", MM_MODEM_BAND_EUTRAN_12},
    {"Band 13", MM_MODEM_BAND_EUTRAN_13},
    {"Band 14", MM_MODEM_BAND_EUTRAN_14},
    {"Band 17", MM_MODEM_BAND_EUTRAN_17},
    {"Band 18", MM_MODEM_BAND_EUTRAN_18},
    {"Band 19", MM_MODEM_BAND_EUTRAN_19},
    {"Band 20", MM_MODEM_BAND_EUTRAN_20},
    {"Band 21", MM_MODEM_BAND_EUTRAN_21},
    {"Band 22", MM_MODEM_BAND_EUTRAN_22},
    {"Band 23", MM_MODEM_BAND_EUTRAN_23},
    {"Band 24", MM_MODEM_BAND_EUTRAN_24},
    {"Band 25", MM_MODEM_BAND_EUTRAN_25},
    {"Band 26", MM_MODEM_BAND_EUTRAN_26},
    {"Band 27", MM_MODEM_BAND_EUTRAN_27},
    {"Band 28", MM_MODEM_BAND_EUTRAN_28},
    {"Band 29", MM_MODEM_BAND_EUTRAN_29},
    {"Band 30", MM_MODEM_BAND_EUTRAN_30},
    {"Band 31", MM_MODEM_BAND_EUTRAN_31},
    {"Band 32", MM_MODEM_BAND_EUTRAN_32},
    {"Band 33", MM_MODEM_BAND_EUTRAN_33},
    {"Band 34", MM_MODEM_BAND_EUTRAN_34},
    {"Band 35", MM_MODEM_BAND_EUTRAN_35},
    {"Band 36", MM_MODEM_BAND_EUTRAN_36},
    {"Band 37", MM_MODEM_BAND_EUTRAN_37},
    {"Band 38", MM_MODEM_BAND_EUTRAN_38},
    {"Band 39", MM_MODEM_BAND_EUTRAN_39},
    {"Band 40", MM_MODEM_BAND_EUTRAN_40},
    {"Band 41", MM_MODEM_BAND_EUTRAN_41},
    {"Band 42", MM_MODEM_BAND_EUTRAN_42},
    {"Band 43", MM_MODEM_BAND_EUTRAN_43},
    {"Band 44", MM_MODEM_BAND_EUTRAN_44},
    {"Band 45", MM_MODEM_BAND_EUTRAN_45},
    {"Band 46", MM_MODEM_BAND_EUTRAN_46},
    {"Band 47", MM_MODEM_BAND_EUTRAN_47},
    {"Band 48", MM_MODEM_BAND_EUTRAN_48},
    {"Band 49", MM_MODEM_BAND_EUTRAN_49},
    {"Band 50", MM_MODEM_BAND_EUTRAN_50},
    {"Band 51", MM_MODEM_BAND_EUTRAN_51},
    {"Band 52", MM_MODEM_BAND_EUTRAN_52},
    {"Band 53", MM_MODEM_BAND_EUTRAN_53},
    {"Band 54", MM_MODEM_BAND_EUTRAN_54},
    {"Band 55", MM_MODEM_BAND_EUTRAN_55},
    {"Band 56", MM_MODEM_BAND_EUTRAN_56},
    {"Band 57", MM_MODEM_BAND_EUTRAN_57},
    {"Band 58", MM_MODEM_BAND_EUTRAN_58},
    {"Band 59", MM_MODEM_BAND_EUTRAN_59},
    {"Band 60", MM_MODEM_BAND_EUTRAN_60},
    {"Band 61", MM_MODEM_BAND_EUTRAN_61},
    {"Band 62", MM_MODEM_BAND_EUTRAN_62},
    {"Band 63", MM_MODEM_BAND_EUTRAN_63},
    {"Band 64", MM_MODEM_BAND_EUTRAN_64},
    {"Band 65", MM_MODEM_BAND_EUTRAN_65},
    {"Band 66", MM_MODEM_BAND_EUTRAN_66},
  },
  // 5G
  {
    {"Band 1", MM_MODEM_BAND_NGRAN_1},
    {"Band 2", MM_MODEM_BAND_NGRAN_2},
    {"Band 3", MM_MODEM_BAND_NGRAN_3},
    {"Band 5", MM_MODEM_BAND_NGRAN_5},
    {"Band 7", MM_MODEM_BAND_NGRAN_7},
    {"Band 8", MM_MODEM_BAND_NGRAN_8},
    {"Band 12", MM_MODEM_BAND_NGRAN_12},
    {"Band 13", MM_MODEM_BAND_NGRAN_13},
    {"Band 14", MM_MODEM_BAND_NGRAN_14},
    {"Band 18", MM_MODEM_BAND_NGRAN_18},
    {"Band 20", MM_MODEM_BAND_NGRAN_20},
    {"Band 25", MM_MODEM_BAND_NGRAN_25},
    {"Band 26", MM_MODEM_BAND_NGRAN_26},
    {"Band 28", MM_MODEM_BAND_NGRAN_28},
    {"Band 29", MM_MODEM_BAND_NGRAN_29},
    {"Band 30", MM_MODEM_BAND_NGRAN_30},
    {"Band 34", MM_MODEM_BAND_NGRAN_34},
    {"Band 38", MM_MODEM_BAND_NGRAN_38},
    {"Band 39", MM_MODEM_BAND_NGRAN_39},
    {"Band 40", MM_MODEM_BAND_NGRAN_40},
    {"Band 41", MM_MODEM_BAND_NGRAN_41},
    {"Band 48", MM_MODEM_BAND_NGRAN_48},
    {"Band 50", MM_MODEM_BAND_NGRAN_50},
    {"Band 51", MM_MODEM_BAND_NGRAN_51},
    {"Band 53", MM_MODEM_BAND_NGRAN_53},
    {"Band 65", MM_MODEM_BAND_NGRAN_65},
    {"Band 66", MM_MODEM_BAND_NGRAN_66},
    // {"Band 67", MM_MODEM_BAND_NGRAN_67},
    {"Band 70", MM_MODEM_BAND_NGRAN_70},
    {"Band 71", MM_MODEM_BAND_NGRAN_71},
    {"Band 74", MM_MODEM_BAND_NGRAN_74},
    {"Band 75", MM_MODEM_BAND_NGRAN_75},
    {"Band 76", MM_MODEM_BAND_NGRAN_76},
    {"Band 77", MM_MODEM_BAND_NGRAN_77},
    {"Band 78", MM_MODEM_BAND_NGRAN_78},
    {"Band 79", MM_MODEM_BAND_NGRAN_79},
    {"Band 80", MM_MODEM_BAND_NGRAN_80},
    {"Band 81", MM_MODEM_BAND_NGRAN_81},
    {"Band 82", MM_MODEM_BAND_NGRAN_82},
    {"Band 83", MM_MODEM_BAND_NGRAN_83},
    {"Band 84", MM_MODEM_BAND_NGRAN_84},
    {"Band 86", MM_MODEM_BAND_NGRAN_86},
    {"Band 89", MM_MODEM_BAND_NGRAN_89},
    {"Band 90", MM_MODEM_BAND_NGRAN_90},
    {"Band 91", MM_MODEM_BAND_NGRAN_91},
    {"Band 92", MM_MODEM_BAND_NGRAN_92},
    {"Band 93", MM_MODEM_BAND_NGRAN_93},
    {"Band 94", MM_MODEM_BAND_NGRAN_94},
    {"Band 95", MM_MODEM_BAND_NGRAN_95},
    {"Band 257", MM_MODEM_BAND_NGRAN_257},
    {"Band 258", MM_MODEM_BAND_NGRAN_258},
    {"Band 260", MM_MODEM_BAND_NGRAN_260},
    {"Band 261", MM_MODEM_BAND_NGRAN_261},
    {NULL, 0},
  }
};

G_DEFINE_TYPE (CcWwanBandsDialog, cc_wwan_bands_dialog, GTK_TYPE_DIALOG)


enum {
  PROP_0,
  PROP_DEVICE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

struct _CcWwanBandRow
{
  GtkListBoxRow      parent_instance;
  GtkCheckButton    *checkbox;
  MMModem           *modem;
  guint              band_val;
  CcWwanBandsDialog *parent_dialog;
};

G_DEFINE_TYPE (CcWwanBandRow, cc_wwan_band_row, GTK_TYPE_LIST_BOX_ROW)

static void
cc_wwan_band_row_class_init (CcWwanBandRowClass *klass)
{
}

static void
cc_wwan_band_row_init (CcWwanBandRow *row)
{
}

static void
cc_wwan_bands_dialog_row_toggle_cb (GtkCheckButton *button,
                                    CcWwanBandRow *row)
{
  guint n_bands;
  GList *l;

  for (l = row->parent_dialog->band_rows; l; l = l->next)
    {
      CcWwanBandRow *band_row = l->data;
      if (gtk_check_button_get_active (band_row->checkbox))
        n_bands++;
    }

  MMModemBand *new_bands = g_new0 (MMModemBand, n_bands);

  n_bands = 0;

  for (l = row->parent_dialog->band_rows; l; l = l->next)
    {
      CcWwanBandRow *band_row = l->data;
      if (gtk_check_button_get_active (band_row->checkbox))
        new_bands[n_bands++] = band_row->band_val;
    }

  mm_modem_set_current_bands (row->modem, new_bands, n_bands, NULL, NULL, NULL);

  g_free (new_bands);
}

static void
cc_wwan_bands_dialog_row_activated_cb (CcWwanBandRow *row,
                                       gint           n_press,
                                       gdouble        x,
                                       gdouble        y)
{
  gtk_check_button_set_active (row->checkbox, !gtk_check_button_get_active (row->checkbox));
}

static GtkWidget *
cc_wwan_bands_dialog_row_new (CcWwanBandsDialog       *self,
                              struct BandDefinition    band,
                              gboolean                 enabled,
                              MMModem                 *modem)
{
  CcWwanBandRow *row;
  GtkWidget *box, *label, *checkbox;
  GtkGesture *listener;
  g_autofree gchar *mode = NULL;

  g_assert (CC_WWAN_BANDS_DIALOG (self));

  row = g_object_new (CC_TYPE_WWAN_BAND_ROW, NULL);

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  g_object_set (box,
                "margin-top", 18,
                "margin-bottom", 18,
                "margin-start", 18,
                "margin-end", 18,
                NULL);
  gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);

  label = gtk_label_new (band.name);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_box_append (GTK_BOX (box), label);

  checkbox = gtk_check_button_new ();
  gtk_check_button_set_active (GTK_CHECK_BUTTON (checkbox), enabled);
  g_signal_connect (checkbox, "toggled",
                    G_CALLBACK (cc_wwan_bands_dialog_row_toggle_cb), row);

  listener = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (listener), GDK_BUTTON_PRIMARY);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (listener), GTK_PHASE_TARGET);
  g_signal_connect_swapped (listener, "released",
                            G_CALLBACK (cc_wwan_bands_dialog_row_activated_cb), row);

  gtk_widget_add_controller (box, GTK_EVENT_CONTROLLER (listener));

  row->checkbox = GTK_CHECK_BUTTON (checkbox);
  row->modem = modem;
  row->band_val = band.struct_val;
  row->parent_dialog = self;
  gtk_box_append (GTK_BOX (box), checkbox);

  self->band_rows = g_list_prepend (self->band_rows, row);

  return GTK_WIDGET (row);
}

static void
cc_wwan_bands_dialog_update (CcWwanBandsDialog *self)
{
  size_t i, list_idx;
  const MMModemBand *supported_bands, *enabled_bands;
  guint n_supported_bands, n_enabled_bands;
  MMModem *modem = cc_wwan_device_get_mm_modem (self->device);

  g_assert (CC_IS_WWAN_BANDS_DIALOG (self));

  if (!mm_modem_peek_supported_bands (modem, &supported_bands, &n_supported_bands))
    {
      g_warning ("Failed to get supported bands!");
      return;
    }

  if (!mm_modem_peek_current_bands (modem, &enabled_bands, &n_enabled_bands))
    {
      g_warning ("Failed to get enabled bands!");
      return;
    }

  GtkListBox *lists[4] = {self->twog_band_list, self->threeg_band_list, self->fourg_band_list, self->fiveg_band_list};

  for (list_idx = 0; list_idx < G_N_ELEMENTS (lists); list_idx++)
    {
      for (i = 0; i < G_N_ELEMENTS (bands[list_idx]); i++)
        {
          GtkWidget *row;
          gboolean is_supported = false;
          gboolean is_enabled = false;

          if (bands[list_idx][i].name == NULL)
            break;

          // If we don't support this band, skip it
          for (size_t j = 0; j < n_supported_bands; j++)
            {
              if (supported_bands[j] == bands[list_idx][i].struct_val)
                {
                  is_supported = true;
                  break;
                }
            }

          if (!is_supported)
            continue;

          // Now check if this band is enabled
          for (size_t j = 0; j < n_enabled_bands; j++)
            {
              if (enabled_bands[j] == bands[list_idx][i].struct_val)
                {
                  is_enabled = true;
                  break;
                }
            }

          row = cc_wwan_bands_dialog_row_new (self, bands[list_idx][i], is_enabled, modem);
          gtk_list_box_append (GTK_LIST_BOX (lists[list_idx]), row);
        }
    }
}

static void
cc_wwan_bands_dialog_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  CcWwanBandsDialog *self = CC_WWAN_BANDS_DIALOG (object);

  switch (prop_id)
    {
    case PROP_DEVICE:
      self->device = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
cc_wwan_bands_dialog_constructed (GObject *object)
{
  CcWwanBandsDialog *self = CC_WWAN_BANDS_DIALOG (object);

  G_OBJECT_CLASS (cc_wwan_bands_dialog_parent_class)->constructed (object);

  cc_wwan_bands_dialog_update (self);
}

static void
cc_wwan_bands_dialog_dispose (GObject *object)
{
  CcWwanBandsDialog *self = CC_WWAN_BANDS_DIALOG (object);

  g_clear_object (&self->device);

  G_OBJECT_CLASS (cc_wwan_bands_dialog_parent_class)->dispose (object);
}

static void
cc_wwan_bands_dialog_class_init (CcWwanBandsDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = cc_wwan_bands_dialog_set_property;
  object_class->constructed  = cc_wwan_bands_dialog_constructed;
  object_class->dispose = cc_wwan_bands_dialog_dispose;


  properties[PROP_DEVICE] =
    g_param_spec_object ("device",
                         "Device",
                         "The WWAN Device",
                         CC_TYPE_WWAN_DEVICE,
                         G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/control-center/wwan/cc-wwan-bands-dialog.ui");

  gtk_widget_class_bind_template_child (widget_class, CcWwanBandsDialog, twog_band_list);
  gtk_widget_class_bind_template_child (widget_class, CcWwanBandsDialog, threeg_band_list);
  gtk_widget_class_bind_template_child (widget_class, CcWwanBandsDialog, fourg_band_list);
  gtk_widget_class_bind_template_child (widget_class, CcWwanBandsDialog, fiveg_band_list);
}

static void
cc_wwan_bands_dialog_init (CcWwanBandsDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWindow *
cc_wwan_bands_dialog_new (GtkWindow    *parent_window,
                          CcWwanDevice *device)
{
  g_return_val_if_fail (GTK_IS_WINDOW (parent_window), NULL);
  g_return_val_if_fail (CC_IS_WWAN_DEVICE (device), NULL);

  return GTK_WINDOW (g_object_new (CC_TYPE_WWAN_BANDS_DIALOG,
                                   "transient-for", parent_window,
                                   "use-header-bar", 1,
                                   "device", device,
                                   NULL));
}
