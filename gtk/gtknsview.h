/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * GtkNSView - Native NSView embedding widget
 * Copyright (C) 2011 Michael Natterer <mitch@lanedo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GTK_NS_VIEW_H__
#define __GTK_NS_VIEW_H__

#if defined(GTK_DISABLE_SINGLE_INCLUDES) && !defined (__GTK_H_INSIDE__) && !defined (GTK_COMPILATION)
#error "Only <gtk/gtk.h> can be included directly."
#endif

#include <gtk/gtkwidget.h>

G_BEGIN_DECLS

#define GTK_TYPE_NS_VIEW            (gtk_ns_view_get_type ())
#define GTK_NS_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_NS_VIEW, GtkNSView))
#define GTK_NS_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_NS_VIEW, GtkNSViewClass))
#define GTK_IS_NS_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_NS_VIEW))
#define GTK_IS_NS_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_NS_VIEW))
#define GTK_NS_VIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_NS_VIEW, GtkNSViewClass))

typedef struct _GtkNSView        GtkNSView;
typedef struct _GtkNSViewClass   GtkNSViewClass;
typedef struct _GtkNSViewPrivate GtkNSViewPrivate;

struct _GtkNSView
{
  GtkWidget parent_instance;

  GtkNSViewPrivate *priv;
};

struct _GtkNSViewClass
{
  GtkWidgetClass parent_class;
};

GType       gtk_ns_view_get_type (void) G_GNUC_CONST;
GtkWidget * gtk_ns_view_new      (gpointer  nsview);
gpointer    gtk_ns_view_get_nsview (GtkNSView *gtknsview);

G_END_DECLS

#endif /* __GTK_NS_VIEW_H__ */
