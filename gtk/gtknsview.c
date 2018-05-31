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
#include "gtkviewport.h"
#include "gtkwindow.h"
#include "gtkprivate.h"
#include "gtkintl.h"
#include "gtkalias.h"


/* #define DEBUG_FOCUS 1 */


enum
{
  PROP_0,
  PROP_VIEW,
  PROP_ENABLE_SWIZZLE
};


struct _GtkNSViewPrivate
{
  NSView *view;
  guint   map_timeout;
  gboolean enable_swizzle;
};

#define GTK_NS_VIEW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                      GTK_TYPE_NS_VIEW, GtkNSViewPrivate))


static void       gtk_ns_view_constructed   (GObject        *object);
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

static void       gtk_ns_view_native_child_event   (GdkWindow     *window,
                                                    NSView        *view,
                                                    NSEvent       *event,
                                                    GtkNSView     *ns_view);
static void       gtk_ns_view_move_native_children (GdkWindow     *window,
                                                    GtkNSView     *ns_view);
static gboolean   gtk_ns_view_forward_event        (GtkWidget     *widget,
                                                    GdkEventKey   *event);


G_DEFINE_TYPE (GtkNSView, gtk_ns_view, GTK_TYPE_WIDGET)

static void
gtk_ns_view_class_init (GtkNSViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GtkNSViewPrivate));

  object_class->constructed = gtk_ns_view_constructed;
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

  g_object_class_install_property (object_class,
				   PROP_ENABLE_SWIZZLE,
				   g_param_spec_boolean ("enable-swizzle",
							 P_("Enable swizzle"),
							 P_("Enable swizzle"),
							 FALSE,
							 GTK_PARAM_READWRITE));
}

static void
gtk_ns_view_init (GtkNSView *ns_view)
{
  ns_view->priv = GTK_NS_VIEW_GET_PRIVATE (ns_view);

  gtk_widget_set_has_window (GTK_WIDGET (ns_view), FALSE);
}


static void   gtk_ns_view_swizzle_draw_rect_recursive (NSView      *view,
                                                       const gchar *associated_key,
                                                       gpointer     associated_object);

@implementation NSView (myDidAddSubview)
- (void) myDidAddSubview: (NSView *) aView
{
  void *associated_object;
  GtkNSView *gtknsview;

  associated_object = objc_getAssociatedObject (self, "gtknsview");

  if (associated_object)
    {
      gtknsview = (GtkNSView *)associated_object;

      if (gtknsview->priv->enable_swizzle)
	{
	  gtk_ns_view_swizzle_draw_rect_recursive (aView, "gtknsview", associated_object);
	}
    }
  else
    {
      associated_object = objc_getAssociatedObject (self, "gtkwindow");

      if (associated_object)
        gtk_ns_view_swizzle_draw_rect_recursive (aView, "gtkwindow", associated_object);
    }

  [self myDidAddSubview:aView];
}
@end

static GtkNSView *
get_associated_gtknsview (NSView *view)
{
  GtkNSView *ns_view;

  ns_view = (GtkNSView *) objc_getAssociatedObject (view, "gtknsview");

  if (! ns_view && ([view class] == [NSTextView class]))
    {
      /*  if it's not a GtkNSView, check if it's the NSWindow's cell
       *  editor editing an NSTextField managed by a GtkNSView
       */
      GtkWindow *window = (GtkWindow *) objc_getAssociatedObject (view, "gtkwindow");

      if (GTK_IS_WINDOW (window))
        {
          GtkWidget *focus = gtk_window_get_focus (window);

          if (GTK_IS_NS_VIEW (focus))
            ns_view = GTK_NS_VIEW (focus);
        }
    }

  if (ns_view &&
      ns_view->priv->view != [ns_view->priv->view ancestorSharedWithView: view])
    {
      return NULL;
    }

  return ns_view;
}

static CGContextRef
clip_to_parent_viewports (GtkNSView *ns_view,
                          NSView    *view)
{
  CGContextRef cg_context = nil;
  GtkWidget *viewport;

  for (viewport = gtk_widget_get_ancestor (GTK_WIDGET (ns_view), GTK_TYPE_VIEWPORT);
       viewport;
       viewport = gtk_widget_get_ancestor (gtk_widget_get_parent (viewport),
                                           GTK_TYPE_VIEWPORT))
    {
      GdkWindow *window;
      GtkAllocation viewport_allocation;
      CGRect rect;

      gtk_widget_get_allocation (viewport, &viewport_allocation);

      /* evil: don't clip to the viewport's width/height but to that
       * of its parent window, because we know we hacked an
       * overshoot_window into GtkScrolledWindow and need to restrict
       * rendering to its area
       */
      window = gtk_widget_get_parent_window (viewport);

      viewport_allocation.width = gdk_window_get_width (window);
      viewport_allocation.height = gdk_window_get_height (window);

      if (gtk_viewport_get_shadow_type (GTK_VIEWPORT (viewport)) != GTK_SHADOW_NONE)
        {
          GtkStyle *style = gtk_widget_get_style (viewport);

          viewport_allocation.x += style->xthickness;
          viewport_allocation.y += style->ythickness;
          viewport_allocation.width -= 2 * style->xthickness;
          viewport_allocation.height -= 2 * style->ythickness;
        }

      gtk_widget_translate_coordinates (viewport, GTK_WIDGET (ns_view),
                                        viewport_allocation.x,
                                        viewport_allocation.y,
                                        &viewport_allocation.x,
                                        &viewport_allocation.y);

      rect.origin.x = viewport_allocation.x;
      rect.origin.y = viewport_allocation.y;
      rect.size.width = viewport_allocation.width;
      rect.size.height = viewport_allocation.height;

      /*  need to translate rect if this is not the view itself but a subview  */
      if (ns_view->priv->view != view)
        {
          NSRect offset = NSMakeRect (0, 0, 0, 0);

          offset = [ns_view->priv->view convertRect: offset
                                           fromView: view];

          rect.origin.x -= offset.origin.x;
          rect.origin.y -= offset.origin.y;
        }

      if (! cg_context)
        {
          cg_context = [[NSGraphicsContext currentContext] graphicsPort];
          CGContextSaveGState (cg_context);
        }

      CGContextClipToRect (cg_context, rect);
    }

  return cg_context;
}

@implementation NSView (myDrawRect)
- (void) myDrawRect: (NSRect) dirtyRect
{
  GtkNSView *ns_view;
  CGContextRef cg_context = nil;

  ns_view = get_associated_gtknsview (self);

  [self lockFocusIfCanDraw];
  if (ns_view)
    cg_context = clip_to_parent_viewports (ns_view, self);

  [self myDrawRect: dirtyRect];

  if (cg_context)
    CGContextRestoreGState (cg_context);

  [self unlockFocus];
}
@end

@implementation NSTextView (myDrawInsertionPointInRect)
- (void) myDrawInsertionPointInRect: (NSRect) aRect
                              color: (NSColor *) aColor
                           turnedOn: (BOOL) flag
{
  GtkNSView *ns_view;
  CGContextRef cg_context = nil;;

  ns_view = get_associated_gtknsview (self);

  if (ns_view)
    cg_context = clip_to_parent_viewports (ns_view, self);

  [self myDrawInsertionPointInRect: aRect
                             color: aColor
                          turnedOn: flag];

  if (cg_context)
    CGContextRestoreGState (cg_context);
}
@end

static void
gtk_ns_view_swizzle_draw_rect_recursive (NSView      *view,
                                         const gchar *associated_key,
                                         gpointer     associated_object)
{
  Method original_drawRect;
  Method my_drawRect;
  Method original_didAddSubview;
  Method my_didAddSubview;
  NSArray *subviews;
  gint i;

  /* This is a private method that disable automatic focus ring handling.
   * It looks like automatic focus ring drawing is handled by a toplevel
   * NSView, so it cannot be caught by our clipping. If we disable this
   * automatic handling, the focus ring appears to be drawn locally and
   * is affected by our clipping.
   */
  [view _setAutomaticFocusRingDisabled:YES];

  /* Swizzle drawRect */
  original_drawRect = class_getInstanceMethod ([view class],
                                               @selector (drawRect:));
  my_drawRect = class_getInstanceMethod ([view class],
                                         @selector (myDrawRect:));

  if (class_addMethod ([view class],
                       @selector (myDrawRect:),
                       method_getImplementation (original_drawRect),
                       method_getTypeEncoding (original_drawRect)))
    {
      class_replaceMethod ([view class],
                           @selector (drawRect:),
                           method_getImplementation (my_drawRect),
                           method_getTypeEncoding (my_drawRect));
    }

  /* Swizzle didAddSubView */
  original_didAddSubview = class_getInstanceMethod ([view class],
                                                    @selector (didAddSubview:));
  my_didAddSubview = class_getInstanceMethod ([view class],
                                              @selector (myDidAddSubview:));

  if (class_addMethod ([view class],
                       @selector (myDidAddSubview:),
                       method_getImplementation (original_didAddSubview),
                       method_getTypeEncoding (original_didAddSubview)))
    {
      class_replaceMethod ([view class],
                           @selector (didAddSubview:),
                           method_getImplementation (my_didAddSubview),
                           method_getTypeEncoding (my_didAddSubview));
    }

  objc_setAssociatedObject (view, associated_key, (id) associated_object,
                            OBJC_ASSOCIATION_ASSIGN);

  subviews = [view subviews];

  for (i = 0; i < [subviews count]; i++)
    {
      gtk_ns_view_swizzle_draw_rect_recursive ([subviews objectAtIndex: i],
                                               associated_key,
                                               associated_object);
    }
}

static void
gtk_ns_view_replace_draw_insertion_point (void)
{
  static volatile gsize draw_insertion_point_replaced = 0;

  /* We replace the drawInsertPointInRect:color:turnedOn: method of the
   * NSTextView class. This should only be done once per application
   * instance.
   */

  if (g_once_init_enter (&draw_insertion_point_replaced))
    {
      Method original_drawInsertionPointInRect;
      Method my_drawInsertionPointInRect;
      IMP original_implementation;
      Class text_view_class = [NSTextView class];

      /* Get the original method and an explicit reference to the
       * implementation behind this method. This because after the
       * first replace method call, the method will point at a different
       * implementation.
       */
      original_drawInsertionPointInRect = class_getInstanceMethod (text_view_class,
                                                                   @selector (drawInsertionPointInRect:color:turnedOn:));
      original_implementation = method_getImplementation (original_drawInsertionPointInRect);

      my_drawInsertionPointInRect = class_getInstanceMethod (text_view_class,
                                                             @selector (myDrawInsertionPointInRect:color:turnedOn:));

      class_replaceMethod (text_view_class,
                           @selector (drawInsertionPointInRect:color:turnedOn:),
                           method_getImplementation (my_drawInsertionPointInRect),
                           method_getTypeEncoding (my_drawInsertionPointInRect));

      class_replaceMethod (text_view_class,
                           @selector (myDrawInsertionPointInRect:color:turnedOn:),
                           original_implementation,
                           method_getTypeEncoding (original_drawInsertionPointInRect));

      g_once_init_leave (&draw_insertion_point_replaced, TRUE);
    }
}

static void
gtk_ns_view_constructed (GObject *object)
{
  GtkNSView *ns_view = GTK_NS_VIEW (object);

  G_OBJECT_CLASS (gtk_ns_view_parent_class)->constructed (object);

  gtk_widget_set_can_focus (GTK_WIDGET (ns_view),
                            [ns_view->priv->view acceptsFirstResponder]);

#if DEBUG_FOCUS
  g_printerr ("%s can focus: %d\n",
              class_getName ([ns_view->priv->view class]),
              gtk_widget_get_can_focus (GTK_WIDGET (ns_view)));
#endif

  gtk_ns_view_replace_draw_insertion_point ();
  if (ns_view->priv->enable_swizzle)
    {
      gtk_ns_view_swizzle_draw_rect_recursive (ns_view->priv->view,
					       "gtknsview", ns_view);
    }
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
        [ns_view->priv->view retain];
      break;

    case PROP_ENABLE_SWIZZLE:
      ns_view->priv->enable_swizzle = g_value_get_boolean (value);
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

    case PROP_ENABLE_SWIZZLE:
      g_value_set_boolean (value, ns_view->priv->enable_swizzle);

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
  GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (object));

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
      else if ([ns_window firstResponder] == ns_view->priv->view || (GTK_IS_WINDOW (toplevel) && !gtk_window_is_active (GTK_WINDOW (toplevel))))
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
  GtkNSView *ns_view = GTK_NS_VIEW (widget);

  if (ns_view->priv->map_timeout != 0)
    {
      g_source_remove (ns_view->priv->map_timeout);
      ns_view->priv->map_timeout = 0;
      g_object_unref (widget);
    }

  if (gtk_widget_get_mapped (widget))
    gtk_widget_unmap (widget);

  GTK_WIDGET_CLASS (gtk_ns_view_parent_class)->unrealize (widget);
}

static gboolean
really_map (GtkWidget *widget) {
  GtkNSView *ns_view = GTK_NS_VIEW (widget);
  GtkWidget *toplevel = gtk_widget_get_toplevel (widget);
  GtkAllocation allocation;
  NSView *parent_view;
  NSWindow *window;

  g_object_unref (widget);

  if (gtk_widget_get_mapped (widget))
    return FALSE;

  gtk_widget_get_allocation (widget, &allocation);
  gtk_ns_view_position_view (ns_view, &allocation);

  parent_view = gdk_quartz_window_get_nsview (gtk_widget_get_window (widget));
  [parent_view addSubview:ns_view->priv->view];

  [ns_view->priv->view setNextKeyView:nil];

  g_signal_connect_object (gtk_widget_get_window (toplevel), "native-child-event",
                           G_CALLBACK (gtk_ns_view_native_child_event),
                           G_OBJECT (widget), 0);

  g_signal_connect_object (gtk_widget_get_window (widget), "move-native-children",
                           G_CALLBACK (gtk_ns_view_move_native_children),
                           G_OBJECT (widget), 0);

  GTK_WIDGET_CLASS (gtk_ns_view_parent_class)->map (widget);

  window = [ns_view->priv->view window];

  if (window)
    {
      NSText *text = [window fieldEditor: YES
                               forObject: nil];

      if (text && ns_view->priv->enable_swizzle)
        {
          gtk_ns_view_swizzle_draw_rect_recursive (text, "gtkwindow", toplevel);
        }
    }

  ns_view->priv->map_timeout = 0;

  return FALSE;
}

static void
gtk_ns_view_map (GtkWidget *widget)
{
  GtkNSView *ns_view = GTK_NS_VIEW (widget);

  g_object_ref (widget);
  ns_view->priv->map_timeout = g_timeout_add (50, (GSourceFunc)really_map, widget);
}

static void
gtk_ns_view_unmap (GtkWidget *widget)
{
  GtkNSView *ns_view = GTK_NS_VIEW (widget);
  GtkWidget *toplevel = gtk_widget_get_toplevel (widget);

  g_signal_handlers_disconnect_by_func (gtk_widget_get_window (widget),
                                        gtk_ns_view_move_native_children,
                                        widget);

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

  if (gtk_ns_view_forward_event (widget, event))
    {
      NSWindow *ns_window = [ns_view->priv->view window];
      NSResponder *responder = [ns_window firstResponder];

      gint command_mask = gdk_quartz_get_fix_modifiers () ? GDK_MOD2_MASK : GDK_MOD1_MASK;

      if ([responder isKindOfClass: [NSTextView class]] &&
          (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK |
                           GDK_MOD1_MASK | GDK_MOD2_MASK)) == command_mask)
        {
          NSTextView *text_view = (NSTextView *) responder;
          NSRange range = [text_view selectedRange];
          gboolean has_selection = range.length > 0;

          switch (event->keyval)
            {
            case GDK_KEY_c: /* copy */
              if (has_selection)
                [text_view copy: text_view];
              return TRUE;

            case GDK_KEY_x: /* cut */
              if (has_selection)
                [text_view cut: text_view];
              return TRUE;

            case GDK_KEY_v: /* paste */
              [text_view paste: text_view];
              return TRUE;

            case GDK_KEY_a: /* all */
              range.location = 0;
              range.length = [[text_view string] length];
              [text_view setSelectedRange: range];
              return TRUE;

            default:
              break;
            }
        }
      else
        {
          [ns_window sendEvent:nsevent];

          return TRUE;
        }
    }

  return GTK_WIDGET_CLASS (gtk_ns_view_parent_class)->key_press_event (widget, event);
}

static gboolean
gtk_ns_view_key_release (GtkWidget   *widget,
                         GdkEventKey *event)
{
  GtkNSView *ns_view = GTK_NS_VIEW (widget);
  NSEvent *nsevent = gdk_quartz_event_get_nsevent ((GdkEvent *) event);

  if (gtk_ns_view_forward_event (widget, event))
    {
      NSWindow *ns_window = [ns_view->priv->view window];

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
              gtk_widget_get_can_focus (GTK_WIDGET (ns_view)))
            {
              NSPoint point = [[view superview ] convertPoint: [event locationInWindow]
                                                     fromView: nil];
              NSView *hit   = [view hitTest: point];

              if (hit &&
                  (hit == view ||
                   [hit ancestorSharedWithView: view] == view) &&
                  ([hit acceptsFirstResponder] ||
                   [view acceptsFirstResponder]))
                {
#if DEBUG_FOCUS
                  g_printerr ("grabbing focus on %s\n",
                              class_getName ([ns_view->priv->view class]));
#endif

                  gtk_widget_grab_focus (GTK_WIDGET (ns_view));
                }
            }
          break;

        default:
          break;
        }
    }
}

static void
gtk_ns_view_move_native_children (GdkWindow *window,
                                  GtkNSView *ns_view)
{
  GtkAllocation allocation;

  gtk_widget_get_allocation (GTK_WIDGET (ns_view), &allocation);
  gtk_ns_view_position_view (ns_view, &allocation);
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

gpointer
gtk_ns_view_get_nsview (GtkNSView *gtknsview)
{
  g_return_val_if_fail (GTK_IS_NS_VIEW (gtknsview), NULL);

  return gtknsview->priv->view;
}

#define __GTK_NS_VIEW_C__
#include "gtkaliasdef.c"
