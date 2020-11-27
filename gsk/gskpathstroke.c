/*
 * Copyright © 2020 Red Hat, Inc.
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
 * Authors: Matthias Clasen <mclasen@redhat.com>
 */

#include "gskpathprivate.h"

#include "gskpathbuilder.h"

#include "gskstrokeprivate.h"
#include "gskcurveprivate.h"
#include "gskpathdashprivate.h"
#include "gskpathopprivate.h"

#define RAD_TO_DEG(r) ((r)*180.0/M_PI)
#define DEG_TO_RAD(d) ((d)*M_PI/180.0)

/* {{{ graphene utilities */

static void
get_tangent (const graphene_point_t *p0,
             const graphene_point_t *p1,
             graphene_vec2_t        *t)
{
  graphene_vec2_init (t, p1->x - p0->x, p1->y - p0->y);
  graphene_vec2_normalize (t, t);
}

static void
get_normal (const graphene_point_t *p0,
            const graphene_point_t *p1,
            graphene_vec2_t        *n)
{
  graphene_vec2_init (n, p0->y - p1->y, p1->x - p0->x);
  graphene_vec2_normalize (n, n);
}

/* Return the angle between t1 and t2 in radians, such that
 * 0 means straight continuation
 * < 0 means right turn
 * > 0 means left turn
 */
static float
angle_between (const graphene_vec2_t *t1,
               const graphene_vec2_t *t2)
{
  float angle = atan2 (graphene_vec2_get_y (t2), graphene_vec2_get_x (t2))
                - atan2 (graphene_vec2_get_y (t1), graphene_vec2_get_x (t1));

  if (angle > M_PI)
    angle -= 2 * M_PI;
  if (angle < - M_PI)
    angle += 2 * M_PI;

  return angle;
}

/* Set p to the intersection of the lines a + t * ab * and
 * c + s * cd. Return 1 if the lines intersect, 0 otherwise.
 */
static int
line_intersect (const graphene_point_t *a,
                const graphene_vec2_t  *ab,
                const graphene_point_t *c,
                const graphene_vec2_t  *cd,
                graphene_point_t       *p)
{
  float a1 = graphene_vec2_get_y (ab);
  float b1 = - graphene_vec2_get_x (ab);
  float c1 = a1 * a->x + b1 * a->y;

  float a2 = graphene_vec2_get_y (cd);
  float b2 = - graphene_vec2_get_x (cd);
  float c2 = a2 * c->x + b2 * c->y;

  float det = a1 * b2 - a2 * b1;

  if (fabs (det) <= 0.001)
    return 0;

  p->x = (b2 * c1 - b1 * c2) / det;
  p->y = (a1 * c2 - a2 * c1) / det;

  return 1;
}

/* }}} */
 /* {{{ GskPathBuilder utilities */

static void
path_builder_move_to_point (GskPathBuilder         *builder,
                            const graphene_point_t *point)
{
  gsk_path_builder_move_to (builder, point->x, point->y);
}

static void
path_builder_line_to_point (GskPathBuilder         *builder,
                            const graphene_point_t *point)
{
  gsk_path_builder_line_to (builder, point->x, point->y);
}

/* Assumes that the current point of the builder is
 * the start point of the curve
 */
static void
path_builder_add_curve (GskPathBuilder *builder,
                        const GskCurve *curve)
{
  const graphene_point_t *p;

  switch (curve->op)
    {
    case GSK_PATH_LINE:
      p = curve->line.points;
      gsk_path_builder_line_to (builder, p[1].x, p[1].y);
      break;

    case GSK_PATH_CURVE:
      p = curve->curve.points;
      gsk_path_builder_curve_to (builder, p[1].x, p[1].y,
                                          p[2].x, p[2].y,
                                          p[3].x, p[3].y);
      break;

    case GSK_PATH_CONIC:
      p = curve->conic.points;
      gsk_path_builder_conic_to (builder, p[1].x, p[1].y,
                                          p[3].x, p[3].y,
                                          p[2].x);
      break;

    case GSK_PATH_MOVE:
    case GSK_PATH_CLOSE:
    default:
      g_assert_not_reached ();
    }
}

static gboolean
add_op (GskPathOperation        op,
        const graphene_point_t *pts,
        gsize                   n_pts,
        float                   weight,
        gpointer                user_data)
{
  GskCurve c;
  GskCurve *curve;
  GList **ops = user_data;

  if (op == GSK_PATH_MOVE)
    return TRUE;

  gsk_curve_init_foreach (&c, op, pts, n_pts, weight);
  curve = g_new0 (GskCurve, 1);
  gsk_curve_reverse (&c, curve);

  *ops = g_list_prepend (*ops, curve);

  return TRUE;
}

static void
path_builder_add_reverse_path (GskPathBuilder *builder,
                               GskPath        *path)
{
  GList *l, *ops;

  ops = NULL;
  gsk_path_foreach (path,
                    GSK_PATH_FOREACH_ALLOW_CURVE | GSK_PATH_FOREACH_ALLOW_CONIC,
                    add_op,
                    &ops);
  for (l = ops; l; l = l->next)
    {
      GskCurve *curve = l->data;
      path_builder_add_curve (builder, curve);
    }
  g_list_free_full (ops, g_free);
}

/* }}} */
/* {{{ Stroke helpers */

static void
add_line_join (GskPathBuilder         *builder,
               GskStroke              *stroke,
               const graphene_point_t *c,
               const graphene_point_t *a,
               const graphene_vec2_t  *ta,
               const graphene_point_t *b,
               const graphene_vec2_t  *tb,
               float                   angle)
{
  switch (stroke->line_join)
    {
    case GSK_LINE_JOIN_MITER:
    case GSK_LINE_JOIN_MITER_CLIP:
      {
        graphene_point_t p;

        if (line_intersect (a, ta, b, tb, &p))
          {
            float s = fabs (sin ((M_PI - angle) / 2));

            if (1.0 / s <= stroke->miter_limit)
              {
                path_builder_line_to_point (builder, &p);
                path_builder_line_to_point (builder, b);
              }
            else if (stroke->line_join == GSK_LINE_JOIN_MITER_CLIP)
              {
                graphene_point_t q, a1, b1;
                graphene_vec2_t n;

                q.x = (c->x + p.x) / 2;
                q.y = (c->y + p.y) / 2;
                get_normal (c, &p, &n);

                line_intersect (a, ta, &q, &n, &a1);
                line_intersect (b, tb, &q, &n, &b1);

                path_builder_line_to_point (builder, &a1);
                path_builder_line_to_point (builder, &b1);
                path_builder_line_to_point (builder, b);
              }
            else
              {
                path_builder_line_to_point (builder, b);
              }
          }
      }
      break;

    case GSK_LINE_JOIN_ROUND:
      gsk_path_builder_svg_arc_to (builder,
                                   stroke->line_width / 2, stroke->line_width / 2,
                                   0, 0, angle > 0 ? 1 : 0,
                                   b->x, b->y);
      break;

    case GSK_LINE_JOIN_BEVEL:
      path_builder_line_to_point (builder, b);
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
add_line_cap (GskPathBuilder         *builder,
              GskStroke              *stroke,
              const graphene_point_t *s,
              const graphene_point_t *e)
{
    switch (stroke->line_cap)
    {
    case GSK_LINE_CAP_BUTT:
      path_builder_line_to_point (builder, e);
      break;

    case GSK_LINE_CAP_ROUND:
      gsk_path_builder_svg_arc_to (builder,
                                   stroke->line_width / 2, stroke->line_width / 2,
                                   0, 1, 0,
                                   e->x, e->y);
      break;

    case GSK_LINE_CAP_SQUARE:
      {
        float cx = (s->x + e->x) / 2;
        float cy = (s->y + e->y) / 2;
        float dx = s->y - cy;
        float dy = - s->x + cx;

        gsk_path_builder_line_to (builder, s->x + dx, s->y + dy);
        gsk_path_builder_line_to (builder, e->x + dx, e->y + dy);
        path_builder_line_to_point (builder, e);
      }
      break;

    default:
      g_assert_not_reached ();
      break;
    }
}

/* }}} */

/* The general theory of operation for the stroker:
 *
 * We walk the segments of the path, offsetting each segment
 * to the left and right, and collect the offset segments in
 * a left and a right contour.
 *
 * When the segment is too curvy, we subdivide it before we
 * add the pieces.
 *
 * Whenever we add a segment, we need to decide if the join
 * is a smooth connection, a right turn, or a left turn. For
 * the smooth connections, we just connect the end points of
 * the offset curves with line segments. For sharp turns, we
 * add a line join on the one side, and intersect the offset
 * curves on the other.
 *
 * Since the intersection shortens both segments, we have to
 * delay adding the previous segments to the outlines until
 * we've handled the join at their end. We also need to hold
 * off on adding the initial segment to the outlines until
 * we've seen the end of the current contour of the path, to
 * handle the join at before the initial segment for closed
 * contours.
 *
 * If the contour turns out to not be closed when we reach
 * the end, we collect the pending segments, reverse the
 * left contour, and connect the right and left contour
 * with end caps, closing the resulting outline.
 *
 * If the path isn't done after we've finished handling the
 * outlines of the current contour, we start over with
 * collecting offset segments of the next contour.
 *
 * We rely on the ability to offset, subdivide, intersect
 * and reverse curves.
 */
typedef struct
{
  GskPathBuilder *builder;  // builder that collects the stroke
  GskStroke *stroke;  // stroke parameters

  GskPathBuilder *left;  // accumulates the left contour
  GskPathBuilder *right; // accumulates the right contour

  gboolean has_current_point;  // r0, l0 have been set from a move
  gboolean has_current_curve;  // c, l, r are set from a curve
  gboolean is_first_curve; // if c, l, r are the first segments we've seen

  GskCurve c;  // previous segment of the path
  GskCurve l;  // candidate for left contour of c
  GskCurve r;  // candidate for right contour of c

  GskCurve c0; // first segment of the path
  GskCurve l0; // first segment of left contour
  GskCurve r0; // first segment of right contour
} StrokeData;

static void
append_right (StrokeData     *stroke_data,
              const GskCurve *curve)
{
  if (stroke_data->is_first_curve)
    {
      stroke_data->r0 = *curve;
      path_builder_move_to_point (stroke_data->right, gsk_curve_get_end_point (curve));
    }
  else
    path_builder_add_curve (stroke_data->right, curve);
}

static void
append_left (StrokeData     *stroke_data,
             const GskCurve *curve)
{
  if (stroke_data->is_first_curve)
    {
      stroke_data->l0 = *curve;
      path_builder_move_to_point (stroke_data->left, gsk_curve_get_end_point (curve));
    }
  else
    path_builder_add_curve (stroke_data->left, curve);
}

/* Add the previous segments, stroke_data->l and ->r, and the join between
 * stroke_data->c and curve and update stroke_data->l, ->r and ->c to point
 * to the given curves.
 *
 * If stroke_data->c is the first segment of the contour, we don't add it
 * yet, but save it in stroke_data->c0, ->r0 and ->l0 for later when we
 * know if the contour is closed or not.
 */
static void
add_segments (StrokeData     *stroke_data,
              const GskCurve *curve,
              GskCurve       *r,
              GskCurve       *l)
{
  float angle;
  float t1, t2;
  graphene_vec2_t tangent1, tangent2;
  graphene_point_t p;

  gsk_curve_get_end_tangent (&stroke_data->c, &tangent1);
  gsk_curve_get_start_tangent (curve, &tangent2);
  angle = angle_between (&tangent1, &tangent2);

  if (fabs (angle) < DEG_TO_RAD (5))
    {
      /* Close enough to a straight line */
      append_right (stroke_data, &stroke_data->r);
      path_builder_line_to_point (stroke_data->right, gsk_curve_get_start_point (r));

      append_left (stroke_data, &stroke_data->l);
      path_builder_line_to_point (stroke_data->left, gsk_curve_get_start_point (l));
    }
  else if (angle > 0)
    {
      /* Right turn */
      if (gsk_curve_intersect (&stroke_data->r, r, &t1, &t2, &p, 1) > 0)
        {
          GskCurve c1, c2;

          gsk_curve_split (&stroke_data->r, t1, &c1, &c2);
          stroke_data->r = c1;
          gsk_curve_split (r, t2, &c1, &c2);
          *r = c2;

          append_right (stroke_data, &stroke_data->r);
        }
      else
        {
          append_right (stroke_data, &stroke_data->r);
          path_builder_line_to_point (stroke_data->right, gsk_curve_get_start_point (r));
        }

      append_left (stroke_data, &stroke_data->l);

      add_line_join (stroke_data->left,
                     stroke_data->stroke,
                     gsk_curve_get_start_point (curve),
                     gsk_curve_get_end_point (&stroke_data->l),
                     &tangent1,
                     gsk_curve_get_start_point (l),
                     &tangent2,
                     angle);
    }
  else
    {
      /* Left turn */
      append_right (stroke_data, &stroke_data->r);

      add_line_join (stroke_data->right,
                     stroke_data->stroke,
                     gsk_curve_get_start_point (curve),
                     gsk_curve_get_end_point (&stroke_data->r),
                     &tangent1,
                     gsk_curve_get_start_point (r),
                     &tangent2,
                     angle);

      if (gsk_curve_intersect (&stroke_data->l, l, &t1, &t2, &p, 1) > 0)
        {
          GskCurve c1, c2;

          gsk_curve_split (&stroke_data->l, t1, &c1, &c2);
          stroke_data->l = c1;
          gsk_curve_split (l, t2, &c1, &c2);
          *l = c2;

          append_left (stroke_data, &stroke_data->l);
        }
      else
        {
          append_left (stroke_data, &stroke_data->l);
          path_builder_line_to_point (stroke_data->left, gsk_curve_get_start_point (l));
        }
    }

  stroke_data->c = *curve;
  stroke_data->r = *r;
  stroke_data->l = *l;
}

/* Add a curve to the in-progress stroke. We look at the angle between
 * the previous curve and this one to determine on which side we need
 * to intersect the curves, and on which to add a join.
 */
static void
add_curve (StrokeData     *stroke_data,
           const GskCurve *curve)
{
  GskCurve l, r;

  gsk_curve_offset (curve, - stroke_data->stroke->line_width / 2, &l);
  gsk_curve_offset (curve, stroke_data->stroke->line_width / 2, &r);

  if (!stroke_data->has_current_curve)
    {
      stroke_data->c0 = *curve;
      stroke_data->r0 = r;
      stroke_data->l0 = l;
      path_builder_move_to_point (stroke_data->right, gsk_curve_get_start_point (&r));
      path_builder_move_to_point (stroke_data->left, gsk_curve_get_start_point (&l));

      stroke_data->c = *curve;
      stroke_data->r = r;
      stroke_data->l = l;

      stroke_data->has_current_curve = TRUE;
      stroke_data->is_first_curve = TRUE;
    }
  else
    {
      add_segments (stroke_data, curve, &r, &l);

      stroke_data->is_first_curve = FALSE;
    }
}

static gboolean
cubic_is_simple (const GskCurve *curve)
{
  const graphene_point_t *pts = curve->curve.points;
  float a1, a2, s;
  graphene_vec2_t t1, t2, t3;
  graphene_vec2_t n1, n2;

  get_tangent (&pts[0], &pts[1], &t1);
  get_tangent (&pts[1], &pts[2], &t2);
  get_tangent (&pts[2], &pts[3], &t3);
  a1 = angle_between (&t1, &t2);
  a2 = angle_between (&t2, &t3);

  if ((a1 < 0 && a2 > 0) || (a1 > 0 && a2 < 0))
    return FALSE;

  get_normal (&pts[0], &pts[1], &n1);
  get_normal (&pts[2], &pts[3], &n2);

  s = graphene_vec2_dot (&n1, &n2);

  if (fabs (acos (s)) >= M_PI / 3.f)
    return FALSE;

  return TRUE;
}

static void
align_points (const graphene_point_t *p,
              const graphene_point_t *a,
              const graphene_point_t *b,
              graphene_point_t       *q,
              int                     n)
{
  graphene_vec2_t n1;
  float angle;
  float s, c;

  get_tangent (a, b, &n1);
  angle = - atan2 (graphene_vec2_get_y (&n1), graphene_vec2_get_x (&n1));
  sincosf (angle, &s, &c);

  for (int i = 0; i < n; i++)
    {
      q[i].x = (p[i].x - a->x) * c - (p[i].y - a->y) * s;
      q[i].y = (p[i].x - a->x) * s + (p[i].y - a->y) * c;
    }
}

/* Get the points where the curvature of curve is
 * zero, or a maximum or minimum, inside the open
 * interval from 0 to 1.
 */
static int
cubic_curvature_points (const GskCurve *curve,
                        float           t[3])
{
  const graphene_point_t *pts = curve->curve.points;
  graphene_point_t p[4];
  float a, b, c, d;
  float x, y, z;
  float u2, u, tt;
  int n_roots = 0;

  align_points (pts, &pts[0], &pts[3], p, 4);

  a = p[2].x * p[1].y;
  b = p[3].x * p[1].y;
  c = p[1].x * p[2].y;
  d = p[3].x * p[2].y;

  x = - 3*a + 2*b + 3*c - d;
  y = 3*a - b - 3*c;
  z = c - a;

  if (fabs (x) >= 0.001)
    {
      tt = -y / (2*x);
      if (0 < tt && tt < 1)
        t[n_roots++] = tt;

      u2 = y*y - 4*x*z;
      if (u2 > 0.001)
        {
          u = sqrt (u2);

          tt = (-y + u) / (2*x);
          if (0 < tt && tt < 1)
            t[n_roots++] = tt;

          tt = (-y - u) / (2*x);
          if (0 < tt && tt < 1)
            t[n_roots++] = tt;
        }
    }

  return n_roots;
}

static int
cmpfloat (const void *p1, const void *p2)
{
  const float *f1 = p1;
  const float *f2 = p2;
  return *f1 < *f2 ? -1 : (*f1 > *f2 ? 1 : 0);
}

#define MAX_SUBDIVISION 8

static void
subdivide_and_add_curve (StrokeData     *stroke_data,
                         const GskCurve *curve,
                         int             level)
{
  if (level == 0 || (level < MAX_SUBDIVISION && cubic_is_simple (curve)))
    add_curve (stroke_data, curve);
  else
    {
      float t[5];
      int n = 0;

      t[n++] = 0;
      t[n++] = 1;

      if (level == MAX_SUBDIVISION)
        {
          n += cubic_curvature_points (curve, &t[n]);
          qsort (t, n, sizeof (float), cmpfloat);
        }

      if (n == 2)
        {
          GskCurve c1, c2;

          gsk_curve_split (curve, 0.5, &c1, &c2);
          subdivide_and_add_curve (stroke_data, &c1, level - 1);
          subdivide_and_add_curve (stroke_data, &c2, level - 1);
        }
      else
        {
          GskCurve c;
          for (int i = 0; i + 1 < n; i++)
            {
              gsk_curve_segment (curve, t[i], t[i+1], &c);
              subdivide_and_add_curve (stroke_data, &c, level - 1);
            }
        }
    }
}

static gboolean
conic_is_simple (const GskCurve *curve)
{
  const graphene_point_t *pts = curve->conic.points;
  graphene_vec2_t n1, n2;
  float s;

  get_normal (&pts[0], &pts[1], &n1);
  get_normal (&pts[1], &pts[3], &n2);

  s = graphene_vec2_dot (&n1, &n2);

  if (fabs (acos (s)) >= M_PI / 3.f)
    return FALSE;

  return TRUE;
}

static void
subdivide_and_add_conic (StrokeData     *stroke_data,
                         const GskCurve *curve,
                         int             level)
{
  if (level == 0 || (level < MAX_SUBDIVISION && conic_is_simple (curve)))
    add_curve (stroke_data, curve);
  else
    {
      GskCurve c1, c2;

      gsk_curve_split (curve, 0.5, &c1, &c2);
      subdivide_and_add_conic (stroke_data, &c1, level - 1);
      subdivide_and_add_conic (stroke_data, &c2, level - 1);
    }
}

/* Create a single closed contour and add it to
 * stroke_data->builder, by connecting the right and the
 * reversed left contour with caps.
 *
 * After this call, stroke_data->left and ->right are NULL.
 */
static void
cap_and_connect_contours (StrokeData *stroke_data)
{
  GskPath *path;
  const graphene_point_t *r0, *l0, *r1, *l1;

  r1 = r0 = gsk_curve_get_start_point (&stroke_data->r0);
  l1 = l0 = gsk_curve_get_start_point (&stroke_data->l0);

  if (stroke_data->has_current_curve)
    {
      path_builder_add_curve (stroke_data->right, &stroke_data->r);
      path_builder_add_curve (stroke_data->left, &stroke_data->l);

      r1 = gsk_curve_get_end_point (&stroke_data->r);
      l1 = gsk_curve_get_end_point (&stroke_data->l);
    }
  else
    path_builder_move_to_point (stroke_data->right, r1);

  add_line_cap (stroke_data->right, stroke_data->stroke, r1, l1);

  if (stroke_data->has_current_curve)
    {
      GskCurve c;

      path = gsk_path_builder_free_to_path (stroke_data->left);
      path_builder_add_reverse_path (stroke_data->right, path);
      gsk_path_unref (path);

      if (!stroke_data->is_first_curve)
        {
          /* Add the first segment that wasn't added initially */
          gsk_curve_reverse (&stroke_data->l0, &c);
          path_builder_add_curve (stroke_data->right, &c);
        }
    }

  add_line_cap (stroke_data->right, stroke_data->stroke, l0, r0);

  if (stroke_data->has_current_curve)
    {
      if (!stroke_data->is_first_curve)
        {
          /* Add the first segment that wasn't added initially */
          path_builder_add_curve (stroke_data->right, &stroke_data->r0);
        }
    }

  gsk_path_builder_close (stroke_data->right);

  path = gsk_path_builder_free_to_path (stroke_data->right);
  gsk_path_builder_add_path (stroke_data->builder, path);
  gsk_path_unref (path);

  stroke_data->left = NULL;
  stroke_data->right = NULL;
}

/* Close the left and the right contours and add them to
 * stroke_data->builder.
 *
 * After this call, stroke_data->left and ->right are NULL.
 */
static void
close_contours (StrokeData *stroke_data)
{
  GskPath *path;

  if (stroke_data->has_current_curve)
    {
      /* add final join and first segment */
      add_segments (stroke_data, &stroke_data->c0, &stroke_data->r0, &stroke_data->l0);
      path_builder_add_curve (stroke_data->right, &stroke_data->r);
      path_builder_add_curve (stroke_data->left, &stroke_data->l);
    }

  gsk_path_builder_close (stroke_data->right);
  gsk_path_builder_close (stroke_data->left);

  path = gsk_path_builder_free_to_path (stroke_data->right);
  gsk_path_builder_add_path (stroke_data->builder, path);
  gsk_path_unref (path);

  path = gsk_path_builder_free_to_path (stroke_data->left);
  gsk_path_builder_add_path (stroke_data->builder, path);
  gsk_path_unref (path);

  stroke_data->left = NULL;
  stroke_data->right = NULL;
}

static gboolean
stroke_op (GskPathOperation        op,
           const graphene_point_t *pts,
           gsize                   n_pts,
           float                   weight,
           gpointer                user_data)
{
  StrokeData *stroke_data = user_data;
  GskCurve curve;

  switch (op)
    {
    case GSK_PATH_MOVE:
      if (stroke_data->has_current_point)
        cap_and_connect_contours (stroke_data);

      gsk_curve_init_foreach (&curve,
                              GSK_PATH_LINE,
                              (const graphene_point_t[]) { pts[0], GRAPHENE_POINT_INIT (pts[0].x + 1, pts[0].y) },
                              2, 0.f);
      gsk_curve_offset (&curve, stroke_data->stroke->line_width / 2, &stroke_data->r0);
      gsk_curve_offset (&curve, - stroke_data->stroke->line_width / 2, &stroke_data->l0);

      stroke_data->right = gsk_path_builder_new ();
      stroke_data->left = gsk_path_builder_new ();

      stroke_data->has_current_point = TRUE;
      stroke_data->has_current_curve = FALSE;
      break;

    case GSK_PATH_CLOSE:
      if (stroke_data->has_current_point)
        {
          if (!graphene_point_near (&pts[0], &pts[1], 0.001))
            {
              gsk_curve_init_foreach (&curve, GSK_PATH_LINE, pts, n_pts, weight);
              add_curve (stroke_data, &curve);
            }
          close_contours (stroke_data);
        }

      stroke_data->has_current_point = FALSE;
      stroke_data->has_current_curve = FALSE;
      break;

    case GSK_PATH_LINE:
      gsk_curve_init_foreach (&curve, op, pts, n_pts, weight);
      add_curve (stroke_data, &curve);
      break;

    case GSK_PATH_CURVE:
      gsk_curve_init_foreach (&curve, op, pts, n_pts, weight);
      subdivide_and_add_curve (stroke_data, &curve, MAX_SUBDIVISION);
      break;

    case GSK_PATH_CONIC:
      gsk_curve_init_foreach (&curve, op, pts, n_pts, weight);
      subdivide_and_add_conic (stroke_data, &curve, MAX_SUBDIVISION);
      break;

    default:
      g_assert_not_reached ();
    }

  return TRUE;
}

/*
 * gsk_contour_default_add_stroke:
 * @contour: the GskContour to stroke
 * @builder: a GskPathBuilder to add the results to
 * @stroke: stroke parameters
 *
 * Strokes @contour according to the parameters given in @stroke,
 * and adds the resulting curves to @builder. Note that stroking
 * a contour will in general produce multiple contours - either
 * because @contour is closed and has a left and right outline,
 * or because @stroke requires dashes.
 */
void
gsk_contour_default_add_stroke (const GskContour *contour,
                                GskPathBuilder   *builder,
                                GskStroke        *stroke)
{
  StrokeData stroke_data;

  memset (&stroke_data, 0, sizeof (StrokeData));
  stroke_data.builder = builder;
  stroke_data.stroke = stroke;

  if (stroke->dash_length <= 0)
    gsk_contour_foreach (contour, GSK_PATH_TOLERANCE_DEFAULT, stroke_op, &stroke_data);
  else
    gsk_contour_dash (contour, stroke, GSK_PATH_TOLERANCE_DEFAULT, stroke_op, &stroke_data);

  if (stroke_data.has_current_point)
    cap_and_connect_contours (&stroke_data);
}

/* vim:set foldmethod=marker expandtab: */
