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

#include "config.h"

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/quartz/gdkquartz.h>
#include <objc/runtime.h>

#include "gtknsview.h"
#include "gtkprivate.h"
#include "gtkintl.h"
#include "gtkalias.h"


/* #define DEBUG_FOCUS 1 */


enum
{
  PROP_0,
  PROP_VIEW
};


struct _GtkNSViewPrivate
{
  NSView *view;
};

#define GTK_NS_VIEW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                      GTK_TYPE_NS_VIEW, GtkNSViewPrivate))


static void       gtk_ns_view_finalize      (GObject        *object);
static void       gtk_ns_view_set_property  (GObject        *object,
                                             guint           prop_id,
                                             const GValue   *value,
                                             GParamSpec     *pspec);
static void       gtk_ns_view_get_property  (GObject        *object,
                                             guint           prop_id,
                                             GValue         *value,
                                             GParamSpec     *pspec);
static void       gtk_ns_view_notify        (GObject        *object,
                                             GParamSpec     *pspec);

static void       gtk_ns_view_unrealize     (GtkWidget      *widget);
static void       gtk_ns_view_map           (GtkWidget      *widget);
static void       gtk_ns_view_unmap         (GtkWidget      *widget);
static void       gtk_ns_view_size_request  (GtkWidget      *widget,
                                             GtkRequisition *requisition);
static void       gtk_ns_view_size_allocate (GtkWidget      *widget,
                                             GtkAllocation  *allocation);
static void       gtk_ns_view_grab_focus    (GtkWidget      *widget);
static gboolean   gtk_ns_view_key_press     (GtkWidget      *widget,
                                             GdkEventKey    *event);
static gboolean   gtk_ns_view_key_release   (GtkWidget      *widget,
                                             GdkEventKey    *event);

static void       gtk_ns_view_native_child_event (GdkWindow     *window,
                                                  NSView        *view,
                                                  NSEvent       *event,
                                                  GtkNSView     *ns_view);
static gboolean   gtk_ns_view_forward_event      (GtkWidget     *widget,
                                                  GdkEventKey   *event);


G_DEFINE_TYPE (GtkNSView, gtk_ns_view, GTK_TYPE_WIDGET)


static void
gtk_ns_view_class_init (GtkNSViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GtkNSViewPrivate));

  object_class->finalize = gtk_ns_view_finalize;
  object_class->set_property = gtk_ns_view_set_property;
  object_class->get_property = gtk_ns_view_get_property;
  object_class->notify = gtk_ns_view_notify;

  widget_class->unrealize = gtk_ns_view_unrealize;
  widget_class->map = gtk_ns_view_map;
  widget_class->unmap = gtk_ns_view_unmap;
  widget_class->size_request = gtk_ns_view_size_request;
  widget_class->size_allocate = gtk_ns_view_size_allocate;
  widget_class->grab_focus = gtk_ns_view_grab_focus;
  widget_class->key_press_event = gtk_ns_view_key_press;
  widget_class->key_release_event = gtk_ns_view_key_release;

  /**
   * GtkNSView:view:
   *
   * The widget's NSView.
   *
   * Since: 2.24
   */
  g_object_class_install_property (object_class,
				   PROP_VIEW,
				   g_param_spec_pointer ("view",
                                                         P_("View"),
                                                         P_("The NSView"),
                                                         GTK_PARAM_READWRITE |
                                                         G_PARAM_CONSTRUCT_ONLY));
}

static void
gtk_ns_view_init (GtkNSView *ns_view)
{
  ns_view->priv = GTK_NS_VIEW_GET_PRIVATE (ns_view);

  gtk_widget_set_has_window (GTK_WIDGET (ns_view), FALSE);
}

static void
gtk_ns_view_finalize (GObject *object)
{
  GtkNSView *ns_view = GTK_NS_VIEW (object);

  if (ns_view->priv->view)
    {
      [ns_view->priv->view release];
      ns_view->priv->view = NULL;
    }

  G_OBJECT_CLASS (gtk_ns_view_parent_class)->finalize (object);
}

static void
gtk_ns_view_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  GtkNSView *ns_view = GTK_NS_VIEW (object);

  switch (prop_id)
    {
    case PROP_VIEW:
      ns_view->priv->view = g_value_get_pointer (value);
      if (ns_view->priv->view)
        {
          [ns_view->priv->view retain];
          gtk_widget_set_can_focus (GTK_WIDGET (ns_view),
                                    [ns_view->priv->view acceptsFirstResponder]);

#if DEBUG_FOCUS
          g_printerr ("%s can focus: %d\n",
                      class_getName ([ns_view->priv->view class]),
                      gtk_widget_get_can_focus (GTK_WIDGET (ns_view)));
#endif
        }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk_ns_view_get_property (GObject      *object,
                          guint         prop_id,
                          GValue       *value,
                          GParamSpec   *pspec)
{
  GtkNSView *ns_view = GTK_NS_VIEW (object);

  switch (prop_id)
    {
    case PROP_VIEW:
      g_value_set_pointer (value, ns_view->priv->view);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk_ns_view_notify (GObject    *object,
                    GParamSpec *pspec)
{
  GtkNSView *ns_view = GTK_NS_VIEW (object);

  if (G_OBJECT_CLASS (gtk_ns_view_parent_class)->notify)
    G_OBJECT_CLASS (gtk_ns_view_parent_class)->notify (object, pspec);

  if (!strcmp (pspec->name, "has-focus"))
    {
      NSWindow *ns_window = [ns_view->priv->view window];

#if DEBUG_FOCUS
      g_printerr ("%s has-focus: %d\n",
                  class_getName ([ns_view->priv->view class]),
                  gtk_widget_has_focus (GTK_WIDGET (object)));
#endif

      if (gtk_widget_has_focus (GTK_WIDGET (object)))
        [ns_window makeFirstResponder:ns_view->priv->view];
      else
        [ns_window makeFirstResponder:nil];
    }
}

static void
gtk_ns_view_position_view (GtkNSView     *ns_view,
                           GtkAllocation *allocation)
{
  GdkWindow *window = gtk_widget_get_window (GTK_WIDGET (ns_view));
  GdkWindow *native;
  gdouble x, y;
  NSSize size;
  NSPoint origin;

  x = allocation->x;
  y = allocation->y;

  /* convert to the coordinate system of the innermost parent window
   * that has an NSView
   */
  native = window;
  while (! gdk_window_has_native (native))
    {
      gdk_window_coords_to_parent (native, x, y, &x, &y);
      native = gdk_window_get_parent (native);
    }

  size.width = allocation->width;
  size.height = allocation->height;
  [ns_view->priv->view setFrameSize:size];

  origin.x = x;
  origin.y = y;
  [ns_view->priv->view setFrameOrigin:origin];
}

static void
gtk_ns_view_unrealize (GtkWidget *widget)
{
  if (gtk_widget_get_mapped (widget))
    gtk_widget_unmap (widget);

  GTK_WIDGET_CLASS (gtk_ns_view_parent_class)->unrealize (widget);
}

static void
gtk_ns_view_map (GtkWidget *widget)
{
  GtkNSView *ns_view = GTK_NS_VIEW (widget);
  GtkWidget *toplevel = gtk_widget_get_toplevel (widget);
  GtkAllocation allocation;
  NSView *parent_view;

  gtk_widget_get_allocation (widget, &allocation);
  gtk_ns_view_position_view (ns_view, &allocation);

  parent_view = gdk_quartz_window_get_nsview (gtk_widget_get_window (widget));
  [parent_view addSubview:ns_view->priv->view];

  [ns_view->priv->view setNextKeyView:nil];

  g_signal_connect_object (gtk_widget_get_window (toplevel), "native-child-event",
                           G_CALLBACK (gtk_ns_view_native_child_event),
                           G_OBJECT (widget), 0);

  GTK_WIDGET_CLASS (gtk_ns_view_parent_class)->map (widget);
}

static void
gtk_ns_view_unmap (GtkWidget *widget)
{
  GtkNSView *ns_view = GTK_NS_VIEW (widget);
  GtkWidget *toplevel = gtk_widget_get_toplevel (widget);

  g_signal_handlers_disconnect_by_func (gtk_widget_get_window (toplevel),
                                        gtk_ns_view_native_child_event,
                                        widget);

  [ns_view->priv->view removeFromSuperview];

  GTK_WIDGET_CLASS (gtk_ns_view_parent_class)->unmap (widget);
}

static void
gtk_ns_view_size_request (GtkWidget      *widget,
                          GtkRequisition *requisition)
{
  requisition->width = 1;
  requisition->height = 1;
}

static void
gtk_ns_view_size_allocate (GtkWidget     *widget,
                           GtkAllocation *allocation)
{
  GtkNSView *ns_view = GTK_NS_VIEW (widget);

  widget->allocation = *allocation;

  if (gtk_widget_get_mapped (widget))
    gtk_ns_view_position_view (ns_view, allocation);
}

static void
gtk_ns_view_grab_focus (GtkWidget *widget)
{
  GtkNSView *ns_view = GTK_NS_VIEW (widget);
  NSWindow *ns_window;

  GTK_WIDGET_CLASS (gtk_ns_view_parent_class)->grab_focus (widget);

  ns_window = [ns_view->priv->view window];
  [ns_window makeFirstResponder:ns_view->priv->view];
}

static gboolean
gtk_ns_view_key_press (GtkWidget   *widget,
                       GdkEventKey *event)
{
  GtkNSView *ns_view = GTK_NS_VIEW (widget);
  NSEvent *nsevent = gdk_quartz_event_get_nsevent ((GdkEvent *) event);
  NSWindow *ns_window;

  if (gtk_ns_view_forward_event (widget, event))
    {
      ns_window = [ns_view->priv->view window];
      [ns_window sendEvent:nsevent];

      return TRUE;
    }

  return GTK_WIDGET_CLASS (gtk_ns_view_parent_class)->key_press_event (widget, event);
}

static gboolean
gtk_ns_view_key_release (GtkWidget   *widget,
                         GdkEventKey *event)
{
  GtkNSView *ns_view = GTK_NS_VIEW (widget);
  NSEvent *nsevent = gdk_quartz_event_get_nsevent ((GdkEvent *) event);
  NSWindow *ns_window;

  if (gtk_ns_view_forward_event (widget, event))
    {
      ns_window = [ns_view->priv->view window];
      [ns_window sendEvent:nsevent];

      return TRUE;
    }

  return GTK_WIDGET_CLASS (gtk_ns_view_parent_class)->key_release_event (widget, event);
}

static void
gtk_ns_view_native_child_event (GdkWindow *window,
                                NSView    *view,
                                NSEvent   *event,
                                GtkNSView *ns_view)
{
  if (view == ns_view->priv->view)
    {
#if 0
      g_printerr ("native child event on %s\n",
                  class_getName ([ns_view->priv->view class]));
#endif

      switch ([event type])
        {
        case NSLeftMouseDown:
          if (! gtk_widget_has_focus (GTK_WIDGET (ns_view)) &&

              /*  other code can set can-focus, so check for both  */
              gtk_widget_get_can_focus (GTK_WIDGET (ns_view)) &&
              [ns_view->priv->view acceptsFirstResponder])
            {
#if DEBUG_FOCUS
              g_printerr ("grabbing focus on %s\n",
                          class_getName ([ns_view->priv->view class]));
#endif

              gtk_widget_grab_focus (GTK_WIDGET (ns_view));
            }
          break;

        default:
          break;
        }
    }
}

static gboolean
gtk_ns_view_forward_event (GtkWidget   *widget,
                           GdkEventKey *event)
{
  GtkNSView *ns_view = GTK_NS_VIEW (widget);
  NSWindow *ns_window;
  NSResponder *first_responder;
  NSView *next_key_view;

  if (event->type != GDK_KEY_PRESS ||
      (event->keyval != GDK_KEY_Tab &&
       event->keyval != GDK_KEY_ISO_Left_Tab))
    {
      return TRUE;
    }

  ns_window = [ns_view->priv->view window];
  first_responder = [ns_window firstResponder];

#if DEBUG_FOCUS
  g_printerr ("first reponder: %p  %s\n", first_responder,
              class_getName ([first_responder class]));
#endif

  if (event->keyval == GDK_KEY_Tab)
    next_key_view = [first_responder nextValidKeyView];
  else
    next_key_view = [first_responder previousValidKeyView];

#if DEBUG_FOCUS
  g_printerr ("next key view: %p  %s\n", next_key_view,
              class_getName ([next_key_view class]));
#endif

  if (next_key_view &&
      next_key_view != ns_view->priv->view &&
      [next_key_view isDescendantOf:ns_view->priv->view])
    {
      return TRUE;
    }

  return FALSE;
}

GtkWidget *
gtk_ns_view_new (gpointer nsview)
{
  g_return_val_if_fail (nsview != NULL, NULL);

  return g_object_new (GTK_TYPE_NS_VIEW,
                       "view", nsview,
                       NULL);
}

#define __GTK_NS_VIEW_C__
#include "gtkaliasdef.c"
