/* -*- mode: c; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* gcal-year-view.c
 *
 * Copyright (C) 2015 Erick Pérez Castellanos <erick.red@gmail.com>
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "gcal-year-view.h"
#include "gcal-view.h"
#include "gcal-utils.h"

#include <glib/gi18n.h>
#include <math.h>
#include <string.h>

#define NAVIGATOR_CELL_WIDTH (210 + 15)
#define NAVIGATOR_CELL_HEIGHT 210
#define SIDEBAR_PREFERRED_WIDTH 200

typedef struct
{
  /* month span from 0 to 11 */
  gint start_day, start_month;
  gint end_day, end_month;
} ButtonData;

typedef struct
{
  gint     box_side;
  GdkPoint coordinates [12];
} GridData;

struct _GcalYearViewPrivate
{
  /* composite, GtkBuilder's widgets */
  GtkWidget    *navigator;
  GtkWidget    *sidebar;
  GtkWidget    *add_event_button;
  GtkWidget    *events_sidebar;

  /* manager singleton */
  GcalManager  *manager;

  /* range shown on the sidebar */
  icaltimetype *start_selected_date;
  icaltimetype *end_selected_date;

  /* geometry info */
  GridData     *navigator_grid;

  /* state flags */
  gboolean      popover_mode;
  gboolean      button_pressed;
  ButtonData   *selected_data;
  gboolean      update_sidebar_needed;

  /**
   * first day of the week according to user locale, being
   * 0 for Sunday, 1 for Monday and so on */
  gint          first_weekday;

  /**
   * clock format from GNOME desktop settings
   */
  gboolean      use_24h_format;

  /* text direction factors */
  gint          k;

  /* weak ref to current date */
  icaltimetype *current_date;

  /* date property */
  icaltimetype *date;
};

enum {
  PROP_0,
  PROP_DATE,
  LAST_PROP
};

enum
{
  EVENT_ACTIVATED,
  NUM_SIGNALS
};

static guint signals[NUM_SIGNALS] = { 0, };

static void   gcal_view_interface_init (GcalViewIface *iface);
static void   gcal_data_model_subscriber_interface_init (ECalDataModelSubscriberInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GcalYearView, gcal_year_view, GTK_TYPE_BOX,
                         G_ADD_PRIVATE (GcalYearView)
                         G_IMPLEMENT_INTERFACE (GCAL_TYPE_VIEW, gcal_view_interface_init)
                         G_IMPLEMENT_INTERFACE (E_TYPE_CAL_DATA_MODEL_SUBSCRIBER,
                                                gcal_data_model_subscriber_interface_init));

static void
update_date (GcalYearView *year_view,
             icaltimetype *new_date)
{
  GcalYearViewPrivate *priv = year_view->priv;

  if (priv->date == NULL || priv->date->year != new_date->year)
    {
      time_t range_start, range_end;
      icaltimetype date;
      icaltimezone* default_zone = gcal_manager_get_system_timezone (priv->manager);

      date = *new_date;
      date.day = 1;
      date.month = 1;
      date.hour = 0;
      date.minute = 0;
      date.second = 0;
      date.is_date = 0;
      range_start = icaltime_as_timet_with_zone (date, default_zone);

      date.day = 31;
      date.month = 12;
      date.hour = 23;
      date.minute = 59;
      date.second = 0;
      range_end = icaltime_as_timet_with_zone (date, default_zone);

      gcal_manager_set_subscriber (priv->manager, E_CAL_DATA_MODEL_SUBSCRIBER (year_view), range_start, range_end);
      gtk_widget_queue_draw (GTK_WIDGET (year_view));
    }

  if (priv->date != NULL)
    g_free (priv->date);
  priv->date = new_date;
}

static void
event_activated (GcalEventWidget *widget,
                 gpointer         user_data)
{
  /* FIXME: if popover_mode, hide popover */
  g_signal_emit (GCAL_YEAR_VIEW (user_data), signals[EVENT_ACTIVATED], 0, widget);
}

static void
order_selected_data (ButtonData *selected_data)
{
  gint swap;
  if (selected_data->end_month < selected_data->start_month)
    {
      swap = selected_data->start_month;
      selected_data->start_month = selected_data->end_month;
      selected_data->end_month = swap;

      swap = selected_data->start_day;
      selected_data->start_day = selected_data->end_day;
      selected_data->end_day = swap;
    }
  else if (selected_data->start_month == selected_data->end_month && selected_data->end_day < selected_data->start_day)
    {
      swap = selected_data->start_day;
      selected_data->start_day = selected_data->end_day;
      selected_data->end_day = swap;
    }
}

static void
update_sidebar (GcalYearView *year_view)
{
  GcalYearViewPrivate *priv = year_view->priv;

  GtkWidget *child_widget;

  GList *events, *l;
  GList **days_widgets_array;

  gint i, days_span;

  const icaltimetype *dt_start, *dt_end;
  icaltimetype date, second_date;

  if (priv->selected_data->start_day != 0)
    {
      ButtonData selected_data = *(priv->selected_data);
      order_selected_data (&selected_data);

      priv->start_selected_date->day = selected_data.start_day;
      priv->start_selected_date->month = selected_data.start_month + 1;
      priv->start_selected_date->year = priv->date->year;
      priv->start_selected_date->is_date = 1;

      priv->end_selected_date->day = selected_data.end_day + 1;
      priv->end_selected_date->month = selected_data.end_month + 1;
      priv->end_selected_date->year = priv->date->year;
      priv->end_selected_date->is_date = 1;
      *(priv->end_selected_date) = icaltime_normalize (*(priv->end_selected_date));
    }
  else
    {
      *(priv->start_selected_date) = *(priv->current_date);
      priv->start_selected_date->is_date = 1;

      *(priv->end_selected_date) = *(priv->current_date);
      priv->end_selected_date->day += 1;
      priv->end_selected_date->is_date = 1;
      *(priv->end_selected_date) = icaltime_normalize (*(priv->end_selected_date));
    }

  gtk_container_foreach (GTK_CONTAINER (priv->events_sidebar), (GtkCallback) gtk_widget_destroy, NULL);

  days_span = icaltime_day_of_year(*(priv->end_selected_date)) - icaltime_day_of_year(*(priv->start_selected_date));
  days_widgets_array = g_new0 (GList*, days_span);

  events = gcal_manager_get_events (priv->manager, priv->start_selected_date, priv->end_selected_date);
  for (l = events; l != NULL; l = g_list_next (l))
    {
      GcalEventData *data = l->data;
      gboolean child_widget_used = FALSE;

      child_widget = gcal_event_widget_new_from_data (data);
      gcal_event_widget_set_read_only (GCAL_EVENT_WIDGET (child_widget),
                                       gcal_manager_is_client_writable (priv->manager, data->source));

      dt_start = gcal_event_widget_peek_start_date (GCAL_EVENT_WIDGET (child_widget));
      dt_end = gcal_event_widget_peek_end_date (GCAL_EVENT_WIDGET (child_widget));

      /* normalize date on each new event */
      date = *(priv->start_selected_date);
      second_date = *(priv->start_selected_date);
      second_date.day += 1;
      second_date = icaltime_normalize (second_date);

      /* marking and cloning */
      for (i = 0; i < days_span; i++)
        {
          GtkWidget *cloned_child = child_widget;
          gint start_comparison, end_comparison;

          if (i != 0)
            {
              icaltime_adjust (&date, 1, 0, 0, 0);
              icaltime_adjust (&second_date, 1, 0, 0, 0);
            }

          start_comparison = icaltime_compare_date_only (*dt_start, date);
          if (start_comparison == 0 || start_comparison == -1)
            {
              if (child_widget_used)
                cloned_child = gcal_event_widget_clone (GCAL_EVENT_WIDGET (child_widget));
              else
                child_widget_used = TRUE;
            }
          else
            {
              cloned_child = NULL;
            }

          if (cloned_child != NULL)
            {
              days_widgets_array[i] = g_list_insert_sorted (days_widgets_array[i],
                                                            cloned_child,
                                                            (GCompareFunc) gcal_event_widget_compare_for_single_day);

              if (start_comparison == -1)
                gtk_style_context_add_class (gtk_widget_get_style_context (cloned_child), "slanted-start");

              end_comparison = icaltime_compare_date_only (second_date, *dt_end);
              if (end_comparison == -1)
                gtk_style_context_add_class (gtk_widget_get_style_context (cloned_child), "slanted-end");
              else
                break;
            }
        }
    }

  for (i = 0; i < days_span; i++)
    {
      GList *current_day = days_widgets_array[i];
      for (l = current_day; l != NULL; l = g_list_next (l))
        {
          child_widget = l->data;
          gtk_widget_show (child_widget);
          g_signal_connect (child_widget, "activate", G_CALLBACK (event_activated), year_view);
          g_object_set_data (G_OBJECT (child_widget), "shift", GINT_TO_POINTER (i));
          gtk_container_add (GTK_CONTAINER (priv->events_sidebar), child_widget);
        }

      g_list_free (current_day);
    }

  g_list_free_full (events, g_free);
  g_free (days_widgets_array);
}

static void
update_sidebar_headers (GtkListBoxRow *row,
                        GtkListBoxRow *before,
                        gpointer user_data)
{
  GcalYearViewPrivate *priv;
  GtkWidget *row_child, *before_child = NULL, *row_header = NULL;
  const icaltimetype *row_date, *before_date = NULL;
  icaltimetype date;
  gint row_shift, before_shift =-1;

  priv = GCAL_YEAR_VIEW (user_data)->priv;
  row_child = gtk_bin_get_child (GTK_BIN (row));
  row_date = gcal_event_widget_peek_start_date (GCAL_EVENT_WIDGET (row_child));
  row_shift = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (row_child), "shift"));
  if (before != NULL)
    {
      before_child = gtk_bin_get_child (GTK_BIN (before));
      before_date = gcal_event_widget_peek_start_date (GCAL_EVENT_WIDGET (before_child));
      before_shift = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (before_child), "shift"));
    }

  if (before_shift == -1 || before_shift != row_shift)
    {
      GtkWidget *label;
      gchar *label_str;

      row_header = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
      date = *(priv->start_selected_date);
      icaltime_adjust (&date, row_shift, 0, 0, 0);

      /* XXX: compare_date_only does not work if is_date field is different */
      date.is_date = priv->current_date->is_date;
      if (icaltime_compare_date_only (date, *(priv->current_date)) == 0)
        label_str = g_strdup (_("Today"));
      else
        label_str = g_strdup_printf ("%s %d", gcal_get_month_name (date.month  - 1), date.day);

      label = gtk_label_new (label_str);
      gtk_style_context_add_class (gtk_widget_get_style_context (label), "sidebar-header");
      g_object_set (label, "margin", 6, "halign", GTK_ALIGN_START, NULL);
      g_free (label_str);

      gtk_container_add (GTK_CONTAINER (row_header), label);
    }

  if (!gcal_event_widget_is_multiday (GCAL_EVENT_WIDGET (row_child)) &&
      !gcal_event_widget_get_all_day (GCAL_EVENT_WIDGET (row_child)) &&
      (before_date == NULL || before_date->hour != row_date->hour))
    {
      gchar *time;
      GtkWidget *label;

      if (priv->use_24h_format)
        time = g_strdup_printf ("%.2d:00", row_date->hour);
      else
        time = g_strdup_printf ("%.2d:00 %s", row_date->hour % 12, row_date->hour < 12 ? "AM" : "PM");

      label = gtk_label_new (time);
      gtk_style_context_add_class (gtk_widget_get_style_context (label), GTK_STYLE_CLASS_DIM_LABEL);
      g_object_set (label, "margin", 6, "halign", GTK_ALIGN_START, NULL);

      if (row_header == NULL)
        row_header = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

      gtk_container_add (GTK_CONTAINER (row_header), label);
      gtk_container_add (GTK_CONTAINER (row_header), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));

      g_free (time);
    }

  if (row_header != NULL)
    {
      gtk_widget_show_all (row_header);
      gtk_list_box_row_set_header (row, row_header);
    }
}

static gboolean
calculate_day_month_for_coord (GcalYearView *year_view,
                               gdouble       x,
                               gdouble       y,
                               gint         *out_day,
                               gint         *out_month)
{
  GcalYearViewPrivate *priv = year_view->priv;
  gint row, column, i, sw, box_side, clicked_cell, day, month;

  row = -1;
  column = -1;
  box_side = priv->navigator_grid->box_side;
  sw = 1 - 2 * priv->k;

  /* y selection */
  for (i = 0; i < 4 && ((row == -1) || (column == -1)); i++)
    {
      if (row == -1 &&
          y > priv->navigator_grid->coordinates[i * 4].y + box_side &&
          y < priv->navigator_grid->coordinates[i * 4].y + box_side * 7)
        {
          row = i;
        }
      if (column == -1 &&
          x > priv->navigator_grid->coordinates[i].x + box_side * 0.5 &&
          x < priv->navigator_grid->coordinates[i].x + box_side * 7.5)
        {
          column = i;
        }
    }

  if (row == -1 || column == -1)
    return FALSE;

  month = row * 4 + column;
  row = (y - (priv->navigator_grid->coordinates[month].y + box_side)) / box_side;
  column = (x - (priv->navigator_grid->coordinates[month].x + box_side * 0.5)) / box_side;
  clicked_cell = row * 7 + column;
  day = 7 * ((clicked_cell + 7 * priv->k) / 7) + sw * (clicked_cell % 7) + (1 - priv->k);
  day -= ((time_day_of_week (1, month, priv->date->year) - priv->first_weekday + 7) % 7);

  if (day < 1 || day > time_days_in_month (priv->date->year, month))
    return FALSE;

  *out_day = day;
  *out_month = month;
  return TRUE;
}

static void
draw_month_grid (GcalYearView *year_view,
                 GtkWidget    *widget,
                 cairo_t      *cr,
                 gint          month_nr,
                 gint         *weeks_counter)
{
  GcalYearViewPrivate *priv = year_view->priv;

  GtkStyleContext *context;
  GtkStateFlags state_flags;

  PangoLayout *layout, *slayout;
  PangoFontDescription *font_desc, *sfont_desc;

  GdkRGBA color;
  gint layout_width, layout_height, i, j, sw;
  gint x, y, column, row, box_side, box_padding_top, box_padding_start;
  gint days_delay, days, shown_rows, sunday_idx;
  gchar *str, *nr_day, *nr_week;
  gboolean selected_day;

  cairo_save (cr);
  context = gtk_widget_get_style_context (widget);
  state_flags = gtk_widget_get_state_flags (widget);
  sw = 1 - 2 * priv->k;
  box_side = priv->navigator_grid->box_side;
  x = priv->navigator_grid->coordinates[month_nr].x;
  y = priv->navigator_grid->coordinates[month_nr].y;

  gtk_style_context_get (context, state_flags | GTK_STATE_FLAG_SELECTED, "font", &sfont_desc, NULL);
  slayout = gtk_widget_create_pango_layout (widget, NULL);
  pango_layout_set_font_description (slayout, sfont_desc);

  /* header */
  gtk_style_context_save (context);
  gtk_style_context_add_class (context, "header");

  str = g_strdup (gcal_get_month_name (month_nr));
  gtk_style_context_get (context, state_flags, "font", &font_desc, NULL);

  layout = gtk_widget_create_pango_layout (widget, str);
  pango_layout_set_font_description (layout, font_desc);
  pango_layout_get_pixel_size (layout, &layout_width, &layout_height);
  gtk_render_layout (context, cr, x + (box_side * 8 - layout_width) / 2, y + (box_side - layout_height) / 2,
                     layout);

  gtk_render_background (context, cr,
                         x + (box_side * 8 - layout_width) / 2, y + (box_side - layout_height) / 2,
                         layout_width, layout_width);

  pango_font_description_free (font_desc);
  g_free (str);
  gtk_style_context_restore (context);

  /* separator line */
  gtk_style_context_save (context);
  gtk_style_context_add_class (context, "lines");

  gtk_style_context_get_color (context, state_flags, &color);
  cairo_set_line_width (cr, 0.2);
  gdk_cairo_set_source_rgba (cr, &color);
  cairo_move_to (cr, x + box_side / 2, y + box_side + 0.4);
  cairo_rel_line_to (cr, 7 * box_side, 0);
  cairo_stroke (cr);

  gtk_style_context_restore (context);

  /* days */
  gtk_style_context_save (context);
  gtk_style_context_add_class (context, "days");

  gtk_style_context_get (context, state_flags, "font", &font_desc, NULL);
  pango_layout_set_font_description (layout, font_desc);

  days_delay = (time_day_of_week (1, month_nr, priv->date->year) - priv->first_weekday + 7) % 7;
  days = days_delay + icaltime_days_in_month (month_nr + 1, priv->date->year);
  shown_rows = ceil (days / 7.0);
  sunday_idx = priv->k * 6 + sw * ((7 - priv->first_weekday) % 7);

  for (i = 0; i < 7 * shown_rows; i++)
    {
      column = i % 7;
      row = i / 7;

      j = 7 * ((i + 7 * priv->k) / 7) + sw * (i % 7) + (1 - priv->k);
      if (j <= days_delay)
        continue;
      else if (j > days)
        continue;
      j -= days_delay;

      nr_day = g_strdup_printf ("%d", j);
      pango_layout_set_text (layout, nr_day, -1);
      pango_layout_get_pixel_size (layout, &layout_width, &layout_height);
      box_padding_top = (box_side - layout_height) / 2 > 0 ? (box_side - layout_height) / 2 : 0;
      box_padding_start = (box_side - layout_width) / 2 > 0 ? (box_side - layout_width) / 2 : 0;

      selected_day = FALSE;
      if (priv->selected_data->start_day != 0)
        {
          ButtonData selected_data = *(priv->selected_data);
          order_selected_data (&selected_data);
          if (month_nr > selected_data.start_month && month_nr < selected_data.end_month)
            {
              selected_day = TRUE;
            }
          else if (month_nr == selected_data.start_month && month_nr == selected_data.end_month)
            {
              selected_day = j >= selected_data.start_day && j <= selected_data.end_day;
            }
          else if (month_nr == selected_data.start_month && j >= selected_data.start_day)
            {
              selected_day = TRUE;
            }
          else if (month_nr == selected_data.end_month && j <= selected_data.end_day)
            {
              selected_day = TRUE;
            }
        }

      if (priv->date->year == priv->current_date->year && month_nr + 1 == priv->current_date->month &&
          j == priv->current_date->day)
        {
          PangoLayout *clayout;
          PangoFontDescription *cfont_desc;

          gtk_style_context_save (context);
          gtk_style_context_add_class (context, "current");

          clayout = gtk_widget_create_pango_layout (widget, nr_day);
          gtk_style_context_get (context, state_flags, "font", &cfont_desc, NULL);
          pango_layout_set_font_description (clayout, cfont_desc);
          pango_layout_get_pixel_size (clayout, &layout_width, &layout_height);
          box_padding_top = (box_side - layout_height) / 2 > 0 ? (box_side - layout_height) / 2 : 0;
          box_padding_start = (box_side - layout_width) / 2 > 0 ? (box_side - layout_width) / 2 : 0;

          /* FIXME: hardcoded padding of the number background */
          gtk_render_background (context, cr,
                                 box_side * (column + 0.5 + priv->k) + x + sw * box_padding_start - priv->k * layout_width - 2.0,
                                 box_side * (row + 1) + y + box_padding_top - 1.0,
                                 layout_width + 4.0, layout_height + 2.0);
          gtk_render_layout (context, cr,
                             box_side * (column + 0.5 + priv->k) + x + sw * box_padding_start - priv->k * layout_width,
                             box_side * (row + 1) + y + box_padding_top,
                             clayout);

          gtk_style_context_restore (context);
          pango_font_description_free (cfont_desc);
          g_object_unref (clayout);
        }
      else if (selected_day)
        {
          gtk_style_context_set_state (context, state_flags | GTK_STATE_FLAG_SELECTED);

          pango_layout_set_text (slayout, nr_day, -1);
          pango_layout_get_pixel_size (slayout, &layout_width, &layout_height);
          box_padding_top = (box_side - layout_height) / 2 > 0 ? (box_side - layout_height) / 2 : 0;
          box_padding_start = (box_side - layout_width) / 2 > 0 ? (box_side - layout_width) / 2 : 0;

          gtk_render_layout (context, cr,
                             box_side * (column + 0.5 + priv->k) + x + sw * box_padding_start - priv->k * layout_width,
                             box_side * (row + 1) + y + box_padding_top,
                             slayout);

          gtk_style_context_set_state (context, state_flags);
        }
      else if (column == sunday_idx)
        {
          gtk_style_context_add_class (context, "sunday");
          gtk_render_layout (context, cr,
                             box_side * (column + 0.5 + priv->k) + x + sw * box_padding_start - priv->k * layout_width,
                             box_side * (row + 1) + y + box_padding_top,
                             layout);
          gtk_style_context_remove_class (context, "sunday");
        }
      else
        {
          gtk_render_layout (context, cr,
                             box_side * (column + 0.5 + priv->k) + x + sw * box_padding_start - priv->k * layout_width,
                             box_side * (row + 1) + y + box_padding_top,
                             layout);
        }

      g_free (nr_day);
    }
  pango_font_description_free (font_desc);
  gtk_style_context_restore (context);

  /* week numbers */
  gtk_style_context_save (context);
  gtk_style_context_add_class (context, "week-numbers");

  gtk_style_context_get (context, state_flags, "font", &font_desc, NULL);
  pango_layout_set_font_description (layout, font_desc);

  for (i = 0; i < shown_rows; i++)
    {
      if (i == 0)
        {
          if (days_delay == 0)
            *weeks_counter = *weeks_counter + 1;
        }
      else
        *weeks_counter = *weeks_counter + 1;

      nr_week = g_strdup_printf ("%d", *weeks_counter);

      pango_layout_set_text (layout, nr_week, -1);
      pango_layout_get_pixel_size (layout, &layout_width, &layout_height);
      box_padding_top = (box_side - layout_height) / 2 > 0 ? (box_side - layout_height) / 2 : 0;
      box_padding_start = ((box_side / 2) - layout_width) / 2 > 0 ? ((box_side / 2) - layout_width) / 2 : 0;

      gtk_render_layout (context, cr,
                         x + sw * box_padding_start + priv->k * (8 * box_side - layout_width),
                         box_side * (i + 1) + y + box_padding_top,
                         layout);

      g_free (nr_week);
    }
  gtk_style_context_restore (context);

  pango_font_description_free (sfont_desc);
  g_object_unref (slayout);

  pango_font_description_free (font_desc);
  g_object_unref (layout);
  cairo_restore (cr);
}

static gboolean
draw_navigator (GcalYearView *year_view,
                cairo_t      *cr,
                GtkWidget    *widget)
{
  GcalYearViewPrivate *priv;

  GtkStyleContext *context;
  GtkStateFlags state_flags;

  gint header_padding_left, header_padding_top, header_height, layout_width, layout_height;
  gint width, height, box_side, real_padding_left, real_padding_top, i, sw, weeks_counter;

  gchar *header_str;

  PangoLayout *header_layout;
  PangoFontDescription *font_desc;

  priv = year_view->priv;
  context = gtk_widget_get_style_context (GTK_WIDGET (year_view));
  state_flags = gtk_widget_get_state_flags (GTK_WIDGET (year_view));
  sw = 1 - 2 * priv->k;
  width = gtk_widget_get_allocated_width (widget);

  /* read header from CSS code related to the view */
  gtk_style_context_save (context);
  gtk_style_context_add_class (context, "first-view-header");

  header_str = g_strdup_printf ("%d", priv->date->year);
  gtk_style_context_get (context, state_flags,
                         "padding-left", &header_padding_left, "padding-top", &header_padding_top,
                         "font", &font_desc, NULL);

  gtk_style_context_restore (context);

  /* draw header on navigator */
  context = gtk_widget_get_style_context (widget);
  state_flags = gtk_widget_get_state_flags (GTK_WIDGET (year_view));

  header_layout = gtk_widget_create_pango_layout (widget, header_str);
  pango_layout_set_font_description (header_layout, font_desc);
  pango_layout_get_pixel_size (header_layout, &layout_width, &layout_height);
  /* XXX: here the color of the text isn't read from year-view but from navigator widget,
   * which has the same color on the CSS file */
  gtk_render_layout (context, cr,
                     priv->k * (width - layout_width) + sw * header_padding_left, header_padding_top, header_layout);

  pango_font_description_free (font_desc);
  g_object_unref (header_layout);
  g_free (header_str);

  header_height = header_padding_top * 2 + layout_height;
  height = gtk_widget_get_allocated_height (widget) - header_height;

  if (((width / 4) / 8) < ((height / 3) / 7))
    box_side = (width / 4) / 8;
  else
    box_side = (height / 3) / 7;

  real_padding_left = (width - (8 * 4 * box_side)) / 5;
  real_padding_top = (height - (7 * 3 * box_side)) / 4;

  priv->navigator_grid->box_side = box_side;
  weeks_counter = 1;
  for (i = 0; i < 12; i++)
    {
      gint row = i / 4;
      gint column = priv->k * 3 + sw * (i % 4);

      priv->navigator_grid->coordinates[i].x = (column + 1) * real_padding_left + column * box_side * 8;
      priv->navigator_grid->coordinates[i].y = (row + 1) * real_padding_top + row * box_side * 7 + header_height;
      draw_month_grid (year_view, widget, cr, i, &weeks_counter);
    }

  return FALSE;
}

static gboolean
navigator_button_press_cb (GcalYearView   *year_view,
                           GdkEventButton *event,
                           GtkWidget      *widget)
{
  gint day, month;
  if (!calculate_day_month_for_coord (year_view, event->x, event->y, &day, &month))
    return FALSE;

  year_view->priv->button_pressed = TRUE;
  year_view->priv->selected_data->start_day = day;
  year_view->priv->selected_data->start_month = month;

  return TRUE;
}

static gboolean
navigator_button_release_cb (GcalYearView   *year_view,
                             GdkEventButton *event,
                             GtkWidget      *widget)
{
  GcalYearViewPrivate *priv = year_view->priv;
  gint day, month;

  if (!priv->button_pressed)
    return FALSE;

  if (!calculate_day_month_for_coord (year_view, event->x, event->y, &day, &month))
    goto fail;

  priv->button_pressed = FALSE;
  priv->selected_data->end_day = day;
  priv->selected_data->end_month = month;
  gtk_widget_queue_draw (widget);

  update_sidebar (year_view);
  return TRUE;

fail:
  priv->button_pressed = FALSE;
  memset (priv->selected_data, 0, sizeof (ButtonData));
  gtk_widget_queue_draw (widget);

  update_sidebar (year_view);
  return TRUE;
}

static gboolean
navigator_motion_notify_cb (GcalYearView   *year_view,
                            GdkEventMotion *event,
                            GtkWidget      *widget)
{
  GcalYearViewPrivate *priv = year_view->priv;
  gint day, month;

  if (!priv->button_pressed)
    return FALSE;

  if (!calculate_day_month_for_coord (year_view, event->x, event->y, &day, &month))
    goto fail;

  priv->selected_data->end_day = day;
  priv->selected_data->end_month = month;
  gtk_widget_queue_draw (widget);
  return TRUE;

fail:
  priv->selected_data->end_day = priv->selected_data->start_day;
  priv->selected_data->end_month = priv->selected_data->start_month;
  gtk_widget_queue_draw (widget);
  return TRUE;
}

static void
add_event_clicked_cb (GcalYearView *year_view,
                      GtkButton    *button)
{
  GcalYearViewPrivate *priv = year_view->priv;
  icaltimetype *start_date, *end_date = NULL;

  if (priv->start_selected_date->day == 0)
    {
      start_date = gcal_dup_icaltime (priv->current_date);
    }
  else
    {
      start_date = gcal_dup_icaltime (priv->start_selected_date);
      end_date = gcal_dup_icaltime (priv->end_selected_date);
      end_date->is_date = 1;
    }

  start_date->is_date = 1;
  g_signal_emit_by_name (GCAL_VIEW (year_view), "create-event-detailed", start_date, end_date);
  g_free (start_date);
  if (end_date != NULL)
    g_free (end_date);
}

static void
gcal_year_view_finalize (GObject *object)
{
  GcalYearViewPrivate *priv = GCAL_YEAR_VIEW (object)->priv;

  g_free (priv->navigator_grid);
  g_free (priv->selected_data);

  if (priv->date != NULL)
    g_free (priv->date);

  /* FIXME: move into window */
  if (priv->current_date != NULL)
    g_free (priv->current_date);

  G_OBJECT_CLASS (gcal_year_view_parent_class)->finalize (object);
}

static void
gcal_year_view_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  GcalYearViewPrivate *priv = GCAL_YEAR_VIEW (object)->priv;

  switch (prop_id)
    {
    case PROP_DATE:
      g_value_set_boxed (value, priv->date);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gcal_year_view_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  switch (prop_id)
    {
    case PROP_DATE:
      update_date (GCAL_YEAR_VIEW (object), g_value_dup_boxed (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gcal_year_view_get_preferred_width (GtkWidget *widget,
                                    gint      *minimum,
                                    gint      *natural)
{
  GcalYearViewPrivate *priv = GCAL_YEAR_VIEW (widget)->priv;
  gint padding_left;

  gtk_style_context_get (gtk_widget_get_style_context (priv->navigator),
                         gtk_widget_get_state_flags (priv->navigator),
                         "padding-left", &padding_left, NULL);

  if (minimum != NULL)
    *minimum = NAVIGATOR_CELL_WIDTH * 4 + padding_left * 8;
  if (natural != NULL)
    *natural = NAVIGATOR_CELL_WIDTH * 4 + padding_left * 8 + SIDEBAR_PREFERRED_WIDTH;
}

static void
gcal_year_view_get_preferred_height_for_width (GtkWidget *widget,
                                               gint       width,
                                               gint      *minimum,
                                               gint      *natural)
{
  GcalYearViewPrivate *priv = GCAL_YEAR_VIEW (widget)->priv;
  gint padding_top;

  gtk_style_context_get (gtk_widget_get_style_context (priv->navigator),
                         gtk_widget_get_state_flags (priv->navigator),
                         "padding-top", &padding_top, NULL);

  if (minimum != NULL)
    *minimum = NAVIGATOR_CELL_HEIGHT * 3 + padding_top * 6;
  if (natural != NULL)
    *natural = NAVIGATOR_CELL_HEIGHT * 3 + padding_top * 6;
}

static void
gcal_year_view_size_allocate (GtkWidget     *widget,
                              GtkAllocation *alloc)
{
  GcalYearViewPrivate *priv = GCAL_YEAR_VIEW (widget)->priv;
  GtkStyleContext *context;
  gint padding_left;

  context = gtk_widget_get_style_context (widget);
  gtk_style_context_save (context);
  gtk_style_context_add_class (context, "year-navigator");
  gtk_style_context_get (context, gtk_widget_get_state_flags (widget), "padding-left", &padding_left, NULL);
  gtk_style_context_restore (context);

  priv->popover_mode = (alloc->width < NAVIGATOR_CELL_WIDTH * 4 + padding_left * 8 + SIDEBAR_PREFERRED_WIDTH);
  if (gtk_widget_get_visible (priv->sidebar) == priv->popover_mode)
    gtk_widget_set_visible (priv->sidebar, !priv->popover_mode);

  GTK_WIDGET_CLASS (gcal_year_view_parent_class)->size_allocate (widget, alloc);
}

static void
gcal_year_view_direction_changed (GtkWidget        *widget,
                                  GtkTextDirection  previous_direction)
{
  GcalYearViewPrivate *priv = GCAL_YEAR_VIEW (widget)->priv;

  if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_LTR)
    priv->k = 0;
  else if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL)
    priv->k = 1;
}

static gchar*
gcal_year_view_get_left_header (GcalView *view)
{
  GcalYearViewPrivate *priv = GCAL_YEAR_VIEW (view)->priv;

  return g_strdup_printf ("%d", priv->date->year);
}

static gchar*
gcal_year_view_get_right_header (GcalView *view)
{
  return g_strdup ("");
}

static void
gcal_year_view_component_changed (ECalDataModelSubscriber *subscriber,
                                  ECalClient              *client,
                                  ECalComponent           *comp)
{
  GcalYearViewPrivate *priv = GCAL_YEAR_VIEW (subscriber)->priv;
  ECalComponentDateTime dtstart, dtend;

  if (priv->update_sidebar_needed || priv->start_selected_date->day == 0)
    return;

  e_cal_component_get_dtstart (comp, &dtstart);
  e_cal_component_get_dtend (comp, &dtend);

  /* FIXME: this implementation clear the sidebar are rebuild it
   * it should only add, what's new, and add it sorted in its position */
  /* XXX: implement using shift as data and GtkListBox sort_func */
  if (icaltime_compare_date_only (*(dtstart.value), *(priv->start_selected_date)) >= 0)
    priv->update_sidebar_needed = TRUE;
  else if (icaltime_compare_date_only (*(dtend.value), *(priv->end_selected_date)) <= 0)
    priv->update_sidebar_needed = TRUE;

  e_cal_component_free_datetime (&dtstart);
  e_cal_component_free_datetime (&dtend);
}

static void
gcal_year_view_component_removed (ECalDataModelSubscriber *subscriber,
                                  ECalClient              *client,
                                  const gchar             *uid,
                                  const gchar             *rid)
{
  GcalYearViewPrivate *priv = GCAL_YEAR_VIEW (subscriber)->priv;
  GList *children, *l;
  ESource *source;
  gchar *uuid;

  source = e_client_get_source (E_CLIENT (client));
  if (rid != NULL)
    uuid = g_strdup_printf ("%s:%s:%s", e_source_get_uid (source), uid, rid);
  else
    uuid = g_strdup_printf ("%s:%s", e_source_get_uid (source), uid);

  children = gtk_container_get_children (GTK_CONTAINER (priv->events_sidebar));
  for (l = children; l != NULL; l = g_list_next (l))
    {
      GcalEventWidget *child_widget = GCAL_EVENT_WIDGET (gtk_bin_get_child (GTK_BIN (l->data)));
      if (g_strcmp0 (uuid, gcal_event_widget_peek_uuid (child_widget)) == 0)
        gtk_widget_destroy (GTK_WIDGET (child_widget));
    }
  g_list_free (children);
  g_free (uuid);
}

static void
gcal_year_view_freeze (ECalDataModelSubscriber *subscriber)
{
  ;
}

static void
gcal_year_view_thaw (ECalDataModelSubscriber *subscriber)
{
  GcalYearViewPrivate *priv = GCAL_YEAR_VIEW (subscriber)->priv;
  if (priv->update_sidebar_needed)
    {
      priv->update_sidebar_needed = FALSE;
      update_sidebar (GCAL_YEAR_VIEW (subscriber));
    }
}

static void
gcal_year_view_class_init (GcalYearViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gcal_year_view_finalize;
  object_class->get_property = gcal_year_view_get_property;
  object_class->set_property = gcal_year_view_set_property;

  widget_class->get_preferred_width = gcal_year_view_get_preferred_width;
  widget_class->get_preferred_height_for_width = gcal_year_view_get_preferred_height_for_width;
  widget_class->size_allocate = gcal_year_view_size_allocate;
  widget_class->direction_changed = gcal_year_view_direction_changed;

  g_object_class_override_property (object_class, PROP_DATE, "active-date");

  /* FIXME: it will problably go back to GcalView */
  signals[EVENT_ACTIVATED] = g_signal_new ("event-activated", GCAL_TYPE_YEAR_VIEW, G_SIGNAL_RUN_LAST,
                                           G_STRUCT_OFFSET (GcalYearViewClass, event_activated),
                                           NULL, NULL, NULL,
                                           G_TYPE_NONE, 1, GCAL_TYPE_EVENT_WIDGET);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/calendar/year-view.ui");

  gtk_widget_class_bind_template_child_private (widget_class, GcalYearView, navigator);
  gtk_widget_class_bind_template_child_private (widget_class, GcalYearView, sidebar);
  gtk_widget_class_bind_template_child_private (widget_class, GcalYearView, add_event_button);
  gtk_widget_class_bind_template_child_private (widget_class, GcalYearView, events_sidebar);

  gtk_widget_class_bind_template_callback (widget_class, draw_navigator);
  gtk_widget_class_bind_template_callback (widget_class, navigator_button_press_cb);
  gtk_widget_class_bind_template_callback (widget_class, navigator_button_release_cb);
  gtk_widget_class_bind_template_callback (widget_class, navigator_motion_notify_cb);
  gtk_widget_class_bind_template_callback (widget_class, add_event_clicked_cb);
}

static void
gcal_year_view_init (GcalYearView *self)
{
  self->priv = gcal_year_view_get_instance_private (self);

  gtk_widget_init_template (GTK_WIDGET (self));
  gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)), "calendar-view");

  if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_LTR)
    self->priv->k = 0;
  else if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
    self->priv->k = 1;

  self->priv->navigator_grid = g_new0 (GridData, 1);
  self->priv->selected_data = g_new0 (ButtonData, 1);

  self->priv->start_selected_date = g_new0 (icaltimetype, 1);
  self->priv->end_selected_date = g_new0 (icaltimetype, 1);

  gtk_list_box_set_header_func (GTK_LIST_BOX (self->priv->events_sidebar), update_sidebar_headers, self, NULL);
}

static void
gcal_view_interface_init (GcalViewIface *iface)
{
  /* FIXME: implement what's needed */
  iface->get_left_header = gcal_year_view_get_left_header;
  iface->get_right_header = gcal_year_view_get_right_header;
}

static void
gcal_data_model_subscriber_interface_init (ECalDataModelSubscriberInterface *iface)
{
  iface->component_added = gcal_year_view_component_changed;
  iface->component_modified = gcal_year_view_component_changed;
  iface->component_removed = gcal_year_view_component_removed;
  iface->freeze = gcal_year_view_freeze;
  iface->thaw = gcal_year_view_thaw;
}

/* Public API */
GcalYearView *
gcal_year_view_new (void)
{
  return g_object_new (GCAL_TYPE_YEAR_VIEW, NULL);
}

void
gcal_year_view_set_manager (GcalYearView *year_view,
                            GcalManager  *manager)
{
  year_view->priv->manager = manager;
}

void
gcal_year_view_set_first_weekday (GcalYearView *year_view,
                                  gint          nr_day)
{
  year_view->priv->first_weekday = nr_day;
}

void
gcal_year_view_set_use_24h_format (GcalYearView *year_view,
                                   gboolean      use_24h_format)
{
  year_view->priv->use_24h_format = use_24h_format;
}

void
gcal_year_view_set_current_date (GcalYearView *year_view,
                                 icaltimetype *current_date)
{
  /* FIXME: move into window */
  year_view->priv->current_date = g_new0 (icaltimetype, 1);
  *(year_view->priv->current_date) =
  icaltime_current_time_with_zone (gcal_manager_get_system_timezone (year_view->priv->manager));
  *(year_view->priv->current_date) = icaltime_set_timezone (year_view->priv->current_date,
                                                            gcal_manager_get_system_timezone (year_view->priv->manager));

  /* Launches update */
  update_sidebar (year_view);
}
