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

#include "ottieshapelayerprivate.h"

#include "ottiefillshapeprivate.h"
#include "ottiegroupshapeprivate.h"
#include "ottieparserprivate.h"
#include "ottiepathshapeprivate.h"
#include "ottieshapeprivate.h"
#include "ottiestrokeshapeprivate.h"
#include "ottietransformprivate.h"

#include <glib/gi18n-lib.h>
#include <gsk/gsk.h>

struct _OttieShapeLayer
{
  OttieLayer parent;

  OttieTransform *transform;
  gboolean auto_orient;
  GskBlendMode blend_mode;
  double index;
  char *layer_name;
  char *name;
  double start_frame;
  double end_frame;
  double start_time;
  double stretch;

  OttieShape *shapes;
};

struct _OttieShapeLayerClass
{
  OttieLayerClass parent_class;
};

G_DEFINE_TYPE (OttieShapeLayer, ottie_shape_layer, OTTIE_TYPE_LAYER)

static void
ottie_shape_layer_snapshot (OttieLayer  *layer,
                            GtkSnapshot *snapshot,
                            double       timestamp)
{
  OttieShapeLayer *self = OTTIE_SHAPE_LAYER (layer);
  OttieShapeSnapshot snapshot_data;
  GskTransform *transform;

  ottie_shape_snapshot_init (&snapshot_data, NULL);

  if (self->transform)
    {
      transform = ottie_transform_get_transform (self->transform, timestamp);
      gtk_snapshot_transform (snapshot, transform);
      gsk_transform_unref (transform);
    }

  ottie_shape_snapshot (self->shapes,
                        snapshot,
                        &snapshot_data,
                        timestamp);

  ottie_shape_snapshot_clear (&snapshot_data);
}

static void
ottie_shape_layer_dispose (GObject *object)
{
  OttieShapeLayer *self = OTTIE_SHAPE_LAYER (object);

  g_clear_object (&self->shapes);
  g_clear_object (&self->transform);

  G_OBJECT_CLASS (ottie_shape_layer_parent_class)->dispose (object);
}

static void
ottie_shape_layer_finalize (GObject *object)
{
  //OttieShapeLayer *self = OTTIE_SHAPE_LAYER (object);

  G_OBJECT_CLASS (ottie_shape_layer_parent_class)->finalize (object);
}

static void
ottie_shape_layer_class_init (OttieShapeLayerClass *klass)
{
  OttieLayerClass *layer_class = OTTIE_LAYER_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  layer_class->snapshot = ottie_shape_layer_snapshot;

  gobject_class->finalize = ottie_shape_layer_finalize;
  gobject_class->dispose = ottie_shape_layer_dispose;
}

static void
ottie_shape_layer_init (OttieShapeLayer *self)
{
  self->stretch = 1;
  self->blend_mode = GSK_BLEND_MODE_DEFAULT;
  self->shapes = ottie_group_shape_new ();
}

static gboolean
ottie_shape_layer_parse_shapes (JsonReader *reader,
                                gsize       offset,
                                gpointer    data)
{
  OttieShapeLayer *self = data;

  return ottie_group_shape_parse_shapes (reader, 0, self->shapes);
}

OttieLayer *
ottie_shape_layer_parse (JsonReader *reader)
{
  OttieParserOption options[] = {
    { "ks", ottie_parser_option_transform, G_STRUCT_OFFSET (OttieShapeLayer, transform) },
    { "ao", ottie_parser_option_boolean, G_STRUCT_OFFSET (OttieShapeLayer, auto_orient) },
    { "bm", ottie_parser_option_blend_mode, G_STRUCT_OFFSET (OttieShapeLayer, blend_mode) },
    { "ind", ottie_parser_option_double, G_STRUCT_OFFSET (OttieShapeLayer, index) },
    { "ln", ottie_parser_option_string, G_STRUCT_OFFSET (OttieShapeLayer, layer_name) },
    { "nm", ottie_parser_option_string, G_STRUCT_OFFSET (OttieShapeLayer, name) },
    { "ip", ottie_parser_option_double, G_STRUCT_OFFSET (OttieShapeLayer, start_frame) },
    { "op", ottie_parser_option_double, G_STRUCT_OFFSET (OttieShapeLayer, end_frame) },
    { "st", ottie_parser_option_double, G_STRUCT_OFFSET (OttieShapeLayer, start_time) },
    { "sr", ottie_parser_option_double, G_STRUCT_OFFSET (OttieShapeLayer, stretch) },
    { "ddd", ottie_parser_option_3d, 0 },
    { "ty", ottie_parser_option_skip, 0 },
    { "shapes", ottie_shape_layer_parse_shapes, 0 },
  };
  OttieShapeLayer *self;

  self = g_object_new (OTTIE_TYPE_SHAPE_LAYER, NULL);

  if (!ottie_parser_parse_object (reader, "shape layer", options, G_N_ELEMENTS (options), self))
    {
      g_object_unref (self);
      return NULL;
    }

  return OTTIE_LAYER (self);
}

