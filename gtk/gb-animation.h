/* gb-animation.h
 *
 * Copyright (C) 2010 Christian Hergert <chris@dronelabs.com>
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
 */

#ifndef GB_ANIMATION_H
#define GB_ANIMATION_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GB_TYPE_ANIMATION            (_gb_animation_get_type())
#define GB_TYPE_ANIMATION_MODE       (_gb_animation_mode_get_type())
#define GB_ANIMATION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GB_TYPE_ANIMATION, GbAnimation))
#define GB_ANIMATION_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GB_TYPE_ANIMATION, GbAnimation const))
#define GB_ANIMATION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  GB_TYPE_ANIMATION, GbAnimationClass))
#define GB_IS_ANIMATION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GB_TYPE_ANIMATION))
#define GB_IS_ANIMATION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  GB_TYPE_ANIMATION))
#define GB_ANIMATION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  GB_TYPE_ANIMATION, GbAnimationClass))

typedef struct _GbAnimation        GbAnimation;
typedef struct _GbAnimationClass   GbAnimationClass;
typedef struct _GbAnimationPrivate GbAnimationPrivate;
typedef enum   _GbAnimationMode    GbAnimationMode;

enum _GbAnimationMode
{
	GB_ANIMATION_LINEAR,
	GB_ANIMATION_EASE_IN_QUAD,
	GB_ANIMATION_EASE_OUT_QUAD,
	GB_ANIMATION_EASE_IN_OUT_QUAD,
	GB_ANIMATION_EASE_IN_CUBIC,

	GB_ANIMATION_LAST
};

struct _GbAnimation
{
	GInitiallyUnowned parent;

	/*< private >*/
	GbAnimationPrivate *priv;
};

struct _GbAnimationClass
{
	GInitiallyUnownedClass parent_class;
};

GType _gb_animation_get_type         (void) G_GNUC_CONST;
GType _gb_animation_mode_get_type    (void) G_GNUC_CONST;
void  _gb_animation_start            (GbAnimation      *animation);
void  _gb_animation_stop             (GbAnimation      *animation);
void  _gb_animation_add_property     (GbAnimation      *animation,
                                      GParamSpec       *pspec,
                                      const GValue     *value);
GbAnimation* _gb_object_animate      (gpointer          object,
                                      GbAnimationMode   mode,
                                      guint             duration_msec,
                                      const gchar      *first_property,
                                      ...) G_GNUC_NULL_TERMINATED;
GbAnimation* _gb_object_animate_full (gpointer          object,
                                      GbAnimationMode   mode,
                                      guint             duration_msec,
                                      guint             frame_rate,
                                      GDestroyNotify    notify,
                                      gpointer          notify_data,
                                      const gchar      *first_property,
                                      ...) G_GNUC_NULL_TERMINATED;

G_END_DECLS

#endif /* GB_ANIMATION_H */
