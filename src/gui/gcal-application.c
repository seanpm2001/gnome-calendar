/* -*- mode: c; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * gcal-application.c
 * Copyright (C) 2015 Erick Pérez Castellanos <erickpc@gnome.org>
 *
 * gnome-calendar is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gnome-calendar is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define G_LOG_DOMAIN "GcalApplication"

#include "config.h"

#include "gcal-application.h"
#include "gcal-context.h"
#include "gcal-debug.h"
#include "gcal-log.h"
#include "gcal-shell-search-provider.h"
#include "gcal-window.h"

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <handy.h>

struct _GcalApplication
{
  DzlApplication      parent;

  GtkWidget          *window;

  gchar              *uuid;
  GDateTime          *initial_date;

  GcalShellSearchProvider *search_provider;

  GcalContext        *context;
};

G_DEFINE_TYPE (GcalApplication, gcal_application, DZL_TYPE_APPLICATION);

static gboolean show_version = FALSE;

static GOptionEntry gcal_application_goptions[] = {
  { 
    "quit", 'q', 0,
    G_OPTION_ARG_NONE, NULL,
    N_("Quit GNOME Calendar"), NULL
  },
  {
    "version", 'v', 0,
    G_OPTION_ARG_NONE, &show_version,
    N_("Display version number"), NULL
  },
  {
    "debug", 0, 0,
    G_OPTION_ARG_NONE, NULL,
    N_("Enable debug messages"), NULL
  },
  {
    "date", 'd', 0,
    G_OPTION_ARG_STRING, NULL,
    N_("Open calendar on the passed date"), NULL
  },
  {
    "uuid", 'u', 0,
    G_OPTION_ARG_STRING, NULL,
    N_("Open calendar showing the passed event"), NULL
  },
  { NULL }
};

enum
{
  PROP_0,
  PROP_CONTEXT,
  PROP_MANAGER,
  N_PROPS
};

static GParamSpec* properties[N_PROPS] = { NULL, };

/*
 * Callbacks
 */

static void
gcal_application_open_event (GSimpleAction *sync,
                             GVariant      *parameter,
                             gpointer       app)
{
  GcalApplication *self;
  const gchar *event_uuid;

  self = GCAL_APPLICATION (app);
  event_uuid = g_variant_get_string (parameter, NULL);
  g_assert (event_uuid != NULL);

  g_debug ("Opening event %s", event_uuid);

  gcal_window_open_event_by_uuid (GCAL_WINDOW (self->window), event_uuid);
  gtk_window_present (GTK_WINDOW (self->window));
}

static void
gcal_application_sync (GSimpleAction *sync,
                       GVariant      *parameter,
                       gpointer       app)
{
  GcalWeatherService *weather_service;
  GcalApplication *self;

  self = GCAL_APPLICATION (app);
  weather_service = gcal_context_get_weather_service (self->context);

  gcal_manager_refresh (gcal_context_get_manager (self->context));
  gcal_weather_service_update (weather_service);
}

static void
gcal_application_launch_search (GSimpleAction *search,
                                GVariant      *parameter,
                                gpointer       app)
{
}

static void
on_about_response (GtkAboutDialog *about,
                   int             response_id,
                   gpointer        user_data)
{
  g_return_if_fail (GTK_IS_ABOUT_DIALOG (about));
  g_return_if_fail (user_data == NULL);

  if (response_id == GTK_RESPONSE_CANCEL)
    gtk_widget_destroy (GTK_WIDGET (about));
}

static gchar*
build_about_copyright (GcalApplication *self)
{
  g_autoptr (GDateTime) dt = NULL;
  GcalWeatherService *weather_service;
  const gchar *attribution;
  GString *builder;

  builder = g_string_new ("<span size=\"small\">");
  dt = g_date_time_new_now_local ();

  /* Build string: */
  g_string_append_printf (builder,
                         _("Copyright \xC2\xA9 2012\xE2\x80\x93%d " "The Calendar authors"),
                          g_date_time_get_year (dt));

  weather_service = gcal_context_get_weather_service (self->context);
  attribution = gcal_weather_service_get_attribution (weather_service);
  if (attribution)
    {
      g_string_append_c (builder, '\n');
      g_string_append (builder, attribution);
    }
  g_string_append (builder, "</span>");

  return g_string_free (builder, FALSE);
}

static void
gcal_application_show_about (GSimpleAction *simple,
                             GVariant      *parameter,
                             gpointer       user_data)
{
  GcalApplication *self;
  GtkWidget *dialog;
  GtkLabel *copyright_label;
  g_autofree gchar *program_name = NULL;
  g_autofree gchar *copyright = NULL;

  const gchar *authors[] = {
    "Erick Pérez Castellanos <erickpc@gnome.org>",
    "Georges Basile Stavracas Neto <georges.stavracas@gmail.com>",
    "Isaque Galdino <igaldino@gmail.com>",
    NULL
  };
  const gchar *artists[] = {
    "Jakub Steiner <jimmac@gmail.com>",
    "Lapo Calamandrei <calamandrei@gmail.com>",
    "Reda Lazri <the.red.shortcut@gmail.com>",
    "William Jon McCann <jmccann@redhat.com>",
    NULL
  };


  self = GCAL_APPLICATION (user_data);

  program_name = g_strconcat (_("Calendar"), NAME_SUFFIX, NULL);
  dialog = g_object_new (GTK_TYPE_ABOUT_DIALOG,
                         "transient-for", GTK_WINDOW (self->window),
                         "modal", TRUE,
                         "destroy-with-parent", TRUE,
                         "program-name", program_name,
                         "version", VERSION,
                         "license-type", GTK_LICENSE_GPL_3_0,
                         "authors", authors,
                         "artists", artists,
                         "logo-icon-name", APPLICATION_ID,
                         "translator-credits", _("translator-credits"),
                         NULL);

  copyright = build_about_copyright (self);
  copyright_label = GTK_LABEL (gtk_widget_get_template_child (GTK_WIDGET (dialog),
                                                              GTK_TYPE_ABOUT_DIALOG,
                                                              "copyright_label"));
  gtk_label_set_markup (copyright_label, copyright);
  gtk_widget_show (GTK_WIDGET (copyright_label));

  g_signal_connect (dialog, "response", G_CALLBACK (on_about_response), NULL);
  gtk_widget_show (dialog);
}

static void
gcal_application_quit (GSimpleAction *simple,
                       GVariant      *parameter,
                       gpointer       user_data)
{
  GcalApplication *self = GCAL_APPLICATION (user_data);

  gtk_widget_destroy (self->window);
}


/*
 * GObject overrides
 */

static void
gcal_application_finalize (GObject *object)
{
 GcalApplication *self = GCAL_APPLICATION (object);

  GCAL_ENTRY;

  gcal_clear_date_time (&self->initial_date);
  g_clear_pointer (&self->uuid, g_free);
  g_clear_object (&self->context);
  g_clear_object (&self->search_provider);

  G_OBJECT_CLASS (gcal_application_parent_class)->finalize (object);

  GCAL_EXIT;
}

static void
gcal_application_get_property (GObject    *object,
                               guint       property_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  GcalApplication *self = GCAL_APPLICATION (object);

  switch (property_id)
    {
    case PROP_CONTEXT:
      g_value_set_object (value, self->context);
      break;

    case PROP_MANAGER:
      g_value_set_object (value, gcal_context_get_manager (self->context));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

/*
 * GApplication overrides
 */

static void
gcal_application_activate (GApplication *application)
{
  GcalApplication *self;

  GCAL_ENTRY;

  self = GCAL_APPLICATION (application);

  if (!self->window)
    {
      if (!self->initial_date)
        self->initial_date = g_date_time_new_now (gcal_context_get_timezone (self->context));

      self->window =  g_object_new (GCAL_TYPE_WINDOW,
                                    "application", self,
                                    "context", self->context,
                                    "active-date", self->initial_date,
                                    NULL);

      g_signal_connect (self->window, "destroy", G_CALLBACK (gtk_widget_destroyed), &self->window);
      g_object_add_weak_pointer (G_OBJECT (self->window), (gpointer*) &self->window);
      gtk_widget_show (self->window);
    }

  gtk_window_present (GTK_WINDOW (self->window));

  if (self->initial_date)
    g_object_set (self->window, "active-date", self->initial_date, NULL);

  gcal_clear_date_time (&self->initial_date);

  if (self->uuid != NULL)
    {
      gcal_window_open_event_by_uuid (GCAL_WINDOW (self->window), self->uuid);
      g_clear_pointer (&self->uuid, g_free);
    }

  GCAL_EXIT;
}

static void
gcal_application_startup (GApplication *app)
{
  GcalApplication *self;

  static const GActionEntry gcal_app_entries[] = {
    { "open-event", gcal_application_open_event, "s" },
    { "sync",   gcal_application_sync },
    { "search", gcal_application_launch_search },
    { "about",  gcal_application_show_about },
    { "quit",   gcal_application_quit },
  };

  GCAL_ENTRY;

  self = GCAL_APPLICATION (app);

  gtk_window_set_default_icon_name (APPLICATION_ID);

  /* add actions */
  g_action_map_add_action_entries (G_ACTION_MAP (app),
                                   gcal_app_entries,
                                   G_N_ELEMENTS (gcal_app_entries),
                                   app);

  G_APPLICATION_CLASS (gcal_application_parent_class)->startup (app);

  /* We're assuming the application is called as a service only by the shell search system */
  if ((g_application_get_flags (app) & G_APPLICATION_IS_SERVICE) != 0)
    g_application_set_inactivity_timeout (app, 3 * 60 * 1000);

  /*  initialize libhandy */
  hdy_init();

  /* Startup the manager */
  gcal_context_startup (self->context);

  GCAL_EXIT;
}

static gint
gcal_application_command_line (GApplication            *app,
                               GApplicationCommandLine *command_line)
{
  g_autoptr (GVariant) option = NULL;
  g_auto (GStrv) arguments = NULL;
  GcalApplication *self;
  GVariantDict *options;
  const gchar* date = NULL;
  const gchar* uuid = NULL;
  gsize length;
  gint n_arguments;
  gint i;

  GCAL_ENTRY;

  self = GCAL_APPLICATION (app);
  options = g_application_command_line_get_options_dict (command_line);

  if (g_variant_dict_contains (options, "quit"))
    {
      g_application_quit (app);
      GCAL_RETURN (0);
    }

  if (g_variant_dict_contains (options, "uuid"))
    {
      option = g_variant_dict_lookup_value (options, "uuid", G_VARIANT_TYPE_STRING);
      uuid = g_variant_get_string (option, &length);

      gcal_application_set_uuid (GCAL_APPLICATION (app), uuid);
    }
  else if (g_variant_dict_contains (options, "date"))
    {
      struct tm result;

      option = g_variant_dict_lookup_value (options, "date", G_VARIANT_TYPE_STRING);
      date = g_variant_get_string (option, &length);

      if (e_time_parse_date_and_time (date, &result) == E_TIME_PARSE_OK)
        {
          g_autoptr (GDateTime) initial_date = NULL;

          initial_date = g_date_time_new (gcal_context_get_timezone (self->context),
                                          result.tm_year,
                                          result.tm_mon,
                                          result.tm_mday,
                                          result.tm_hour,
                                          result.tm_min,
                                          0);

          gcal_set_date_time (&self->initial_date, initial_date);
        }
      else
        {
          g_warning ("Date %s is invalid", date);
        }
    }

  g_application_activate (app);

  arguments = g_application_command_line_get_arguments (command_line, &n_arguments);
  if (n_arguments > 1)
    {
      g_autoptr (GHashTable) unique_files = NULL;
      g_autoptr (GPtrArray) files = NULL;
      gint n_files = 0;

      files = g_ptr_array_new_full (n_arguments - 1, g_object_unref);
      unique_files = g_hash_table_new (g_file_hash, (GEqualFunc) g_file_equal);

      for (i = 1; i < n_arguments; i++)
        {
          g_autoptr (GFile) file = NULL;
          const gchar *arg;

          arg = arguments[i];
          file = g_application_command_line_create_file_for_arg (command_line, arg);

          if (g_str_has_prefix (arg, "-") || g_hash_table_contains (unique_files, file))
            continue;

          g_hash_table_add (unique_files, file);
          g_ptr_array_add (files, g_steal_pointer (&file));

          n_files++;
        }

      if (n_files > 0)
        g_application_open (app, (GFile **) files->pdata, n_files, "");
    }

  GCAL_RETURN (0);
}

static gint
gcal_application_handle_local_options (GApplication *app,
                                       GVariantDict *options)
{
  /* Initialize logging before anything else */
  if (g_variant_dict_contains (options, "debug"))
    gcal_log_init ();

  if (show_version)
    {
      g_print ("gnome-calendar: Version %s\n", PACKAGE_VERSION);
      return 0;
    }

  return -1;
}

static gboolean
gcal_application_dbus_register (GApplication     *application,
                                GDBusConnection  *connection,
                                const gchar      *object_path,
                                GError          **error)
{
  GcalApplication *self;
  g_autofree gchar *search_provider_path = NULL;

  GCAL_ENTRY;

  self = GCAL_APPLICATION (application);

  if (!G_APPLICATION_CLASS (gcal_application_parent_class)->dbus_register (application, connection, object_path, error))
    GCAL_RETURN (FALSE);

  search_provider_path = g_strconcat (object_path, PROFILE, "/SearchProvider", NULL);

  if (!gcal_shell_search_provider_dbus_export (self->search_provider, connection, search_provider_path, error))
    GCAL_RETURN (FALSE);

  GCAL_RETURN (TRUE);
}

static void
gcal_application_dbus_unregister (GApplication    *application,
                                  GDBusConnection *connection,
                                  const gchar     *object_path)
{
  GcalApplication *self;
  g_autofree gchar *search_provider_path = NULL;

  GCAL_ENTRY;

  self = GCAL_APPLICATION (application);

  search_provider_path = g_strconcat (object_path, "/SearchProvider", NULL);
  gcal_shell_search_provider_dbus_unexport (self->search_provider, connection, search_provider_path);

  G_APPLICATION_CLASS (gcal_application_parent_class)->dbus_unregister (application, connection, object_path);

  GCAL_EXIT;
}

static void
gcal_application_open (GApplication  *application,
                       GFile        **files,
                       gint           n_files,
                       const gchar   *hint)
{
  GcalApplication *self;

  GCAL_ENTRY;

  self = GCAL_APPLICATION (application);

  g_application_activate (G_APPLICATION (application));
  gcal_window_import_files (GCAL_WINDOW (self->window), files, n_files);

  GCAL_EXIT;
}

static void
gcal_application_class_init (GcalApplicationClass *klass)
{
  GObjectClass *object_class;
  GApplicationClass *application_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = gcal_application_finalize;
  object_class->get_property = gcal_application_get_property;

  application_class = G_APPLICATION_CLASS (klass);
  application_class->activate = gcal_application_activate;
  application_class->startup = gcal_application_startup;
  application_class->command_line = gcal_application_command_line;
  application_class->open = gcal_application_open;
  application_class->handle_local_options = gcal_application_handle_local_options;
  application_class->dbus_register = gcal_application_dbus_register;
  application_class->dbus_unregister = gcal_application_dbus_unregister;

  properties[PROP_CONTEXT] = g_param_spec_object ("context",
                                                  "Context",
                                                  "Context",
                                                  GCAL_TYPE_CONTEXT,
                                                  G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  properties[PROP_MANAGER] = g_param_spec_object ("manager",
                                                  "The manager object",
                                                  "The manager object",
                                                  GCAL_TYPE_MANAGER,
                                                  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
gcal_application_init (GcalApplication *self)
{
  g_application_add_main_option_entries (G_APPLICATION (self), gcal_application_goptions);

  self->context = gcal_context_new ();
  self->search_provider = gcal_shell_search_provider_new (self->context);
}

/* Public API */
GcalApplication*
gcal_application_new (void)
{
  return g_object_new (gcal_application_get_type (),
                       "resource-base-path", "/org/gnome/calendar",
                       "application-id", APPLICATION_ID,
                       "flags", G_APPLICATION_HANDLES_COMMAND_LINE | G_APPLICATION_HANDLES_OPEN,
                       NULL);
}

/**
 * gcal_application_get_context:
 * @self: a #GcalApplication
 *
 * Retrieves the #GcalContext of the application.
 *
 * Returns: (transfer none): a #GcalContext
 */
GcalContext*
gcal_application_get_context (GcalApplication *self)
{
  g_return_val_if_fail (GCAL_IS_APPLICATION (self), NULL);

  return self->context;
}

void
gcal_application_set_uuid (GcalApplication *self,
                           const gchar     *app_uuid)
{
  g_return_if_fail (GCAL_IS_APPLICATION (self));

  g_free (self->uuid);
  self->uuid = g_strdup (app_uuid);
}

void
gcal_application_set_initial_date (GcalApplication *self,
                                   GDateTime       *initial_date)
{
  g_return_if_fail (GCAL_IS_APPLICATION (self));

  gcal_set_date_time (&self->initial_date, initial_date);
}
