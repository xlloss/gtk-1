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

#include "ottietrimshapeprivate.h"

#include "ottiedoublevalueprivate.h"
#include "ottieparserprivate.h"
#include "ottieshapeprivate.h"

#include <glib/gi18n-lib.h>
#include <gsk/gsk.h>

struct _OttieTrimShape
{
  OttieShape parent;

  OttieDoubleValue start;
  OttieDoubleValue end;
  OttieDoubleValue offset;
};

struct _OttieTrimShapeClass
{
  OttieShapeClass parent_class;
};

G_DEFINE_TYPE (OttieTrimShape, ottie_trim_shape, OTTIE_TYPE_SHAPE)

static void
ottie_trim_shape_snapshot (OttieShape         *shape,
                           GtkSnapshot        *snapshot,
                           OttieShapeSnapshot *snapshot_data,
                           double              timestamp)
{
  OttieTrimShape *self = OTTIE_TRIM_SHAPE (shape);
  GskPathMeasure *measure;
  GskPath *path;
  GskPathBuilder *builder;
  double start, end, offset;

  path = ottie_shape_snapshot_get_path (snapshot_data);
  measure = gsk_path_measure_new (path);
  offset = ottie_double_value_get (&self->offset, timestamp) / 360.f;
  start = ottie_double_value_get (&self->start, timestamp) / 100.f + offset;
  start -= floor (start);
  start *= gsk_path_measure_get_length (measure);
  end = ottie_double_value_get (&self->end, timestamp) / 100.f + offset;
  end -= floor (end);
  end *= gsk_path_measure_get_length (measure);

  builder = gsk_path_builder_new ();
  gsk_path_builder_add_segment (builder,
                                measure,
                                MIN (start, end),
                                MAX (start, end));
  path = gsk_path_builder_free_to_path (builder);

  ottie_shape_snapshot_clear (snapshot_data);
  ottie_shape_snapshot_add_path (snapshot_data, path);

  gsk_path_measure_unref (measure);
}

static void
ottie_trim_shape_dispose (GObject *object)
{
  OttieTrimShape *self = OTTIE_TRIM_SHAPE (object);

  ottie_double_value_clear (&self->start);
  ottie_double_value_clear (&self->end);
  ottie_double_value_clear (&self->offset);

  G_OBJECT_CLASS (ottie_trim_shape_parent_class)->dispose (object);
}

static void
ottie_trim_shape_finalize (GObject *object)
{
  //OttieTrimShape *self = OTTIE_TRIM_SHAPE (object);

  G_OBJECT_CLASS (ottie_trim_shape_parent_class)->finalize (object);
}

static void
ottie_trim_shape_class_init (OttieTrimShapeClass *klass)
{
  OttieShapeClass *shape_class = OTTIE_SHAPE_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  shape_class->snapshot = ottie_trim_shape_snapshot;

  gobject_class->finalize = ottie_trim_shape_finalize;
  gobject_class->dispose = ottie_trim_shape_dispose;
}

static void
ottie_trim_shape_init (OttieTrimShape *self)
{
  ottie_double_value_init (&self->start, 0);
  ottie_double_value_init (&self->end, 100);
  ottie_double_value_init (&self->offset, 0);
}

OttieShape *
ottie_trim_shape_parse (JsonReader *reader)
{
  OttieParserOption options[] = {
    { "nm", ottie_parser_option_string, G_STRUCT_OFFSET (OttieShape, name) },
    { "mn", ottie_parser_option_string, G_STRUCT_OFFSET (OttieShape, match_name) },
    { "hd", ottie_parser_option_boolean, G_STRUCT_OFFSET (OttieShape, hidden) },
    { "s", ottie_double_value_parse, G_STRUCT_OFFSET (OttieTrimShape, start) },
    { "e", ottie_double_value_parse, G_STRUCT_OFFSET (OttieTrimShape, end) },
    { "o", ottie_double_value_parse, G_STRUCT_OFFSET (OttieTrimShape, offset) },
    { "ty", ottie_parser_option_skip, 0 },
  };
  OttieTrimShape *self;

  self = g_object_new (OTTIE_TYPE_TRIM_SHAPE, NULL);

  if (!ottie_parser_parse_object (reader, "trim shape", options, G_N_ELEMENTS (options), self))
    {
      g_object_unref (self);
      return NULL;
    }

  return OTTIE_SHAPE (self);
}

