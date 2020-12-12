/*
 * Copyright © 2020 Benjamin Otte
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Benjamin Otte <otte@gnome.org>
 */

#include "config.h"

#include "ottiecreationprivate.h"

#include "ottielayerprivate.h"
#include "ottieparserprivate.h"
#include "ottieshapelayerprivate.h"

#include <glib/gi18n-lib.h>
#include <json-glib/json-glib.h>

#define GDK_ARRAY_ELEMENT_TYPE OttieLayer *
#define GDK_ARRAY_FREE_FUNC g_object_unref
#define GDK_ARRAY_TYPE_NAME OttieLayerList
#define GDK_ARRAY_NAME ottie_layer_list
#define GDK_ARRAY_PREALLOC 4
#include "gdk/gdkarrayimpl.c"

struct _OttieCreation
{
  GObject parent;

  char *name;
  double frame_rate;
  double start_frame;
  double end_frame;
  double width;
  double height;

  OttieLayerList layers;

  GCancellable *cancellable;
};

struct _OttieCreationClass
{
  GObjectClass parent_class;
};

enum {
  PROP_0,
  PROP_END_FRAME,
  PROP_FRAME_RATE,
  PROP_HEIGHT,
  PROP_LOADING,
  PROP_NAME,
  PROP_PREPARED,
  PROP_START_FRAME,
  PROP_WIDTH,

  N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

G_DEFINE_TYPE (OttieCreation, ottie_creation, G_TYPE_OBJECT)

static void
ottie_creation_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)

{
  //OttieCreation *self = OTTIE_CREATION (object);

  switch (prop_id)
    {

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
ottie_creation_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  OttieCreation *self = OTTIE_CREATION (object);

  switch (prop_id)
    {
    case PROP_END_FRAME:
      g_value_set_double (value, self->end_frame);
      break;

    case PROP_FRAME_RATE:
      g_value_set_double (value, self->frame_rate);
      break;

    case PROP_HEIGHT:
      g_value_set_double (value, self->height);
      break;

    case PROP_PREPARED:
      g_value_set_boolean (value, self->cancellable != NULL);
      break;

    case PROP_NAME:
      g_value_set_string (value, self->name);
      break;

    case PROP_START_FRAME:
      g_value_set_double (value, self->start_frame);
      break;

    case PROP_WIDTH:
      g_value_set_double (value, self->width);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
ottie_creation_stop_loading (OttieCreation *self,
                             gboolean       emit)
{
  if (self->cancellable == NULL)
    return;

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  if (emit)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOADING]);
}

static void
ottie_creation_reset (OttieCreation *self)
{
  ottie_layer_list_clear (&self->layers);

  g_clear_pointer (&self->name, g_free);
  self->frame_rate = 0;
  self->start_frame = 0;
  self->end_frame = 0;
  self->width = 0;
  self->height = 0;
}

static void
ottie_creation_dispose (GObject *object)
{
  OttieCreation *self = OTTIE_CREATION (object);

  ottie_creation_stop_loading (self, FALSE);
  ottie_creation_reset (self);

  G_OBJECT_CLASS (ottie_creation_parent_class)->dispose (object);
}

static void
ottie_creation_finalize (GObject *object)
{
#if 0
  OttieCreation *self = OTTIE_CREATION (object);
#endif

  G_OBJECT_CLASS (ottie_creation_parent_class)->finalize (object);
}

static void
ottie_creation_class_init (OttieCreationClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);

  gobject_class->set_property = ottie_creation_set_property;
  gobject_class->get_property = ottie_creation_get_property;
  gobject_class->finalize = ottie_creation_finalize;
  gobject_class->dispose = ottie_creation_dispose;

  /**
   * OttieCreation:end-frame:
   *
   * End frame of the creation
   */
  properties[PROP_END_FRAME] =
    g_param_spec_double ("end-frame",
                         "End frame",
                         "End frame of the creation",
                         0.0, G_MAXDOUBLE, 0.0,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * OttieCreation:loading:
   *
   * Whether the creation is currently loading.
   */
  properties[PROP_LOADING] =
    g_param_spec_boolean ("loading",
                          "Loading",
                          "Whether the creation is currently loading",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * OttieCreation:frame-rate:
   *
   * Frame rate of this creation
   */
  properties[PROP_FRAME_RATE] =
    g_param_spec_double ("frame-rate",
                         "Frame rate",
                         "Frame rate of this creation",
                         0.0, G_MAXDOUBLE, 0.0,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * OttieCreation:height:
   *
   * Height of this creation
   */
  properties[PROP_HEIGHT] =
    g_param_spec_double ("height",
                         "Height",
                         "Height of this creation",
                         0.0, G_MAXDOUBLE, 0.0,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * OttieCreation:name:
   *
   * The name of the creation.
   */
  properties[PROP_NAME] =
    g_param_spec_string ("name",
                         "Name",
                         "The name of the creation",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * OttieCreation:prepared:
   *
   * Whether the creation is prepared to render
   */
  properties[PROP_PREPARED] =
    g_param_spec_boolean ("prepared",
                          "Prepared",
                          "Whether the creation is prepared to render",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * OttieCreation:start-frame:
   *
   * Start frame of the creation
   */
  properties[PROP_START_FRAME] =
    g_param_spec_double ("start-frame",
                         "Start frame",
                         "Start frame of the creation",
                         0.0, G_MAXDOUBLE, 0.0,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * OttieCreation:width:
   *
   * Width of this creation
   */
  properties[PROP_WIDTH] =
    g_param_spec_double ("width",
                         "Width",
                         "Width of this creation",
                         0.0, G_MAXDOUBLE, 0.0,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, N_PROPS, properties);
}

static void
ottie_creation_init (OttieCreation *self)
{
}

/**
 * ottie_creation_is_loading:
 * @self: a #OttieCreation
 *
 * Returns whether @self is still in the process of loading. This may not just involve
 * the creation itself, but also any assets that are a part of the creation.
 *
 * Returns: %TRUE if the creation is loading
 */
gboolean
ottie_creation_is_loading (OttieCreation *self)
{
  g_return_val_if_fail (OTTIE_IS_CREATION (self), FALSE);

  return self->cancellable != NULL;
}

/**
 * ottie_creation_is_prepared:
 * @self: a #OttieCreation
 *
 * Returns whether @self has successfully loaded a document that it can display.
 *
 * Returns: %TRUE if the creation can be used
 */
gboolean
ottie_creation_is_prepared (OttieCreation *self)
{
  g_return_val_if_fail (OTTIE_IS_CREATION (self), FALSE);

  return self->frame_rate > 0;
}

/**
 * ottie_creation_get_name:
 * @self: a #OttieCreation
 *
 * Returns the name of the current creation or %NULL if the creation is unnamed.
 *
 * Returns: (allow-none): The name of the creation
 */
const char *
ottie_creation_get_name (OttieCreation *self)
{
  g_return_val_if_fail (OTTIE_IS_CREATION (self), FALSE);

  return self->name;
}

static void
ottie_creation_emit_error (OttieCreation *self,
                           const GError  *error)
{
  g_print ("Ottie is sad: %s\n", error->message);
}


static gboolean
ottie_creation_parse_layers (JsonReader *reader,
                             gsize       offset,
                             gpointer    data)
{
  OttieCreation *self = data;

  if (!json_reader_is_array (reader))
    {
      ottie_parser_error_syntax (reader, "Layers are not an array.");
      return FALSE;
    }

  for (int i = 0; ; i++)
    {
      OttieLayer *layer;
      int type;

      if (!json_reader_read_element (reader, i))
        break;

      if (!json_reader_is_object (reader))
        {
          ottie_parser_error_syntax (reader, "Layer %d is not an object", i);
          continue;
        }

      if (!json_reader_read_member (reader, "ty"))
        {
          ottie_parser_error_syntax (reader, "Layer %d has no type", i);
          json_reader_end_member (reader);
          json_reader_end_element (reader);
          continue;
        }

      type = json_reader_get_int_value (reader);
      json_reader_end_member (reader);

      switch (type)
      {
        case 4:
          layer = ottie_shape_layer_parse (reader);
          break;

        default:
          ottie_parser_error_value (reader, "Layer %d has unknown type %d", i, type);
          layer = NULL;
          break;
      }

      if (layer)
        ottie_layer_list_append (&self->layers, layer);
      json_reader_end_element (reader);
    }

  json_reader_end_element (reader);

  return TRUE;
}

static gboolean
ottie_creation_load_from_reader (OttieCreation *self,
                                 JsonReader    *reader)
{
  OttieParserOption options[] = {
    { "fr", ottie_parser_option_double, G_STRUCT_OFFSET (OttieCreation, frame_rate) },
    { "w", ottie_parser_option_double, G_STRUCT_OFFSET (OttieCreation, width) },
    { "h", ottie_parser_option_double, G_STRUCT_OFFSET (OttieCreation, height) },
    { "nm", ottie_parser_option_string, G_STRUCT_OFFSET (OttieCreation, name) },
    { "ip", ottie_parser_option_double, G_STRUCT_OFFSET (OttieCreation, start_frame) },
    { "op", ottie_parser_option_double, G_STRUCT_OFFSET (OttieCreation, end_frame) },
    { "ddd", ottie_parser_option_3d, 0 },
    { "v", ottie_parser_option_skip, 0 },
    { "layers", ottie_creation_parse_layers, 0 },
  };

  return ottie_parser_parse_object (reader, "toplevel", options, G_N_ELEMENTS (options), self);
}

static void
ottie_creation_notify_prepared (OttieCreation *self)
{
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PREPARED]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRAME_RATE]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIDTH]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HEIGHT]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_START_FRAME]);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_END_FRAME]);
}

static void
ottie_creation_load_from_node (OttieCreation *self, 
                               JsonNode      *root)
{
  JsonReader *reader = json_reader_new (root);

  ottie_creation_load_from_reader (self, reader);

  g_object_unref (reader);
}

static void
ottie_creation_load_file_parsed (GObject      *parser,
                                 GAsyncResult *res,
                                 gpointer      data)
{
  OttieCreation *self = data;
  GError *error = NULL;

  if (!json_parser_load_from_stream_finish (JSON_PARSER (parser), res, &error))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      ottie_creation_emit_error (self, error);
      g_error_free (error);
      ottie_creation_stop_loading (self, TRUE);
      return;
    }

  g_object_freeze_notify (G_OBJECT (self));

  ottie_creation_load_from_node (self, json_parser_get_root (JSON_PARSER (parser)));
  ottie_creation_stop_loading (self, TRUE);
  ottie_creation_notify_prepared (self);

  g_object_thaw_notify (G_OBJECT (self));
}

static void
ottie_creation_load_file_open (GObject      *file,
                               GAsyncResult *res,
                               gpointer      data)
{
  OttieCreation *self = data;
  GFileInputStream *stream;
  GError *error = NULL;
  JsonParser *parser;

  stream = g_file_read_finish (G_FILE (file), res, &error);
  if (stream == NULL)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      ottie_creation_emit_error (self, error);
      g_error_free (error);
      ottie_creation_stop_loading (self, TRUE);
      return;
    }

  parser = json_parser_new ();
  json_parser_load_from_stream_async (parser,
                                      G_INPUT_STREAM (stream), 
                                      self->cancellable,
                                      ottie_creation_load_file_parsed,
                                      self);
  g_object_unref (parser);
}

void
ottie_creation_load_file (OttieCreation *self,
                          GFile         *file)
{
  g_return_if_fail (OTTIE_IS_CREATION (self));
  g_return_if_fail (G_IS_FILE (file));

  g_object_freeze_notify (G_OBJECT (self));

  ottie_creation_stop_loading (self, FALSE);
  if (self->frame_rate)
    {
      ottie_creation_reset (self);
      ottie_creation_notify_prepared (self);
    }

  self->cancellable = g_cancellable_new ();

  g_file_read_async (file,
                     G_PRIORITY_DEFAULT,
                     self->cancellable,
                     ottie_creation_load_file_open,
                     self);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LOADING]);

  g_object_thaw_notify (G_OBJECT (self));
}

void
ottie_creation_load_filename (OttieCreation *self,
                              const char    *filename)
{
  GFile *file;

  g_return_if_fail (OTTIE_IS_CREATION (self));
  g_return_if_fail (filename != NULL);

  file = g_file_new_for_path (filename);

  ottie_creation_load_file (self, file);

  g_clear_object (&file);
}

OttieCreation *
ottie_creation_new (void)
{
  return g_object_new (OTTIE_TYPE_CREATION, NULL);
}

OttieCreation *
ottie_creation_new_for_file (GFile *file)
{
  OttieCreation *self;

  g_return_val_if_fail (G_IS_FILE (file), NULL);

  self = g_object_new (OTTIE_TYPE_CREATION, NULL);

  ottie_creation_load_file (self, file);

  return self;
}

OttieCreation *
ottie_creation_new_for_filename (const char *filename)
{
  OttieCreation *self;
  GFile *file;

  g_return_val_if_fail (filename != NULL, NULL);

  file = g_file_new_for_path (filename);

  self = ottie_creation_new_for_file (file);

  g_clear_object (&file);

  return self;
}

double
ottie_creation_get_frame_rate (OttieCreation *self)
{
  g_return_val_if_fail (OTTIE_IS_CREATION (self), 0);

  return self->frame_rate;
}

double
ottie_creation_get_start_frame (OttieCreation *self)
{
  g_return_val_if_fail (OTTIE_IS_CREATION (self), 0);

  return self->start_frame;
}

double
ottie_creation_get_end_frame (OttieCreation *self)
{
  g_return_val_if_fail (OTTIE_IS_CREATION (self), 0);

  return self->end_frame;
}

double
ottie_creation_get_width (OttieCreation *self)
{
  g_return_val_if_fail (OTTIE_IS_CREATION (self), 0);

  return self->width;
}

double
ottie_creation_get_height (OttieCreation *self)
{
  g_return_val_if_fail (OTTIE_IS_CREATION (self), 0);

  return self->height;
}

void
ottie_creation_snapshot (OttieCreation *self,
                         GtkSnapshot   *snapshot,
                         double         timestamp)
{
  for (gsize i = 0; i < ottie_layer_list_get_size (&self->layers); i++)
    {
      ottie_layer_snapshot (ottie_layer_list_get (&self->layers, i),
                            snapshot,
                            timestamp * self->frame_rate);
    }
}


