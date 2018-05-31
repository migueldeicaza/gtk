/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Modified by the GTK+ Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */

#include "config.h"
#include <math.h>
#include <gdk/gdkkeysyms.h>
#include "gtkbindings.h"
#include "gtkmarshalers.h"
#include "gtkscrolledwindow.h"
#include "gtkwindow.h"
#include "gtkprivate.h"
#include "gb-animation.h"
#include "gtkintl.h"
#include "gtkmain.h"
#include "gtkdnd.h"
#include "gtkalias.h"


/* scrolled window policy and size requisition handling:
 *
 * gtk size requisition works as follows:
 *   a widget upon size-request reports the width and height that it finds
 *   to be best suited to display its contents, including children.
 *   the width and/or height reported from a widget upon size requisition
 *   may be overidden by the user by specifying a width and/or height
 *   other than 0 through gtk_widget_set_size_request().
 *
 * a scrolled window needs (for implementing all three policy types) to
 * request its width and height based on two different rationales.
 * 1)   the user wants the scrolled window to just fit into the space
 *      that it gets allocated for a specifc dimension.
 * 1.1) this does not apply if the user specified a concrete value
 *      value for that specific dimension by either specifying usize for the
 *      scrolled window or for its child.
 * 2)   the user wants the scrolled window to take as much space up as
 *      is desired by the child for a specifc dimension (i.e. POLICY_NEVER).
 *
 * also, kinda obvious:
 * 3)   a user would certainly not have choosen a scrolled window as a container
 *      for the child, if the resulting allocation takes up more space than the
 *      child would have allocated without the scrolled window.
 *
 * conclusions:
 * A) from 1) follows: the scrolled window shouldn't request more space for a
 *    specifc dimension than is required at minimum.
 * B) from 1.1) follows: the requisition may be overidden by usize of the scrolled
 *    window (done automatically) or by usize of the child (needs to be checked).
 * C) from 2) follows: for POLICY_NEVER, the scrolled window simply reports the
 *    child's dimension.
 * D) from 3) follows: the scrolled window child's minimum width and minimum height
 *    under A) at least correspond to the space taken up by its scrollbars.
 */

#define DEFAULT_SCROLLBAR_SPACING  3
#define TOUCH_BYPASS_CAPTURED_THRESHOLD 30

/* Kinetic scrolling */
#define FRAME_INTERVAL (1000 / 60)
#define MAX_OVERSHOOT_DISTANCE 50
#define FRICTION_DECELERATION 0.003
#define OVERSHOOT_INVERSE_ACCELERATION 0.003
#define RELEASE_EVENT_TIMEOUT 1000

/* Overlay scrollbars */
#define SCROLL_INTERVAL_INITIAL 300
#define SCROLL_INTERVAL_REPEAT 100

typedef struct {
  gboolean window_placement_set;
  GtkCornerType real_window_placement;

  /* Kinetic scrolling */
  GdkEvent              *button_press_event;
  GdkWindow             *overshoot_window;
  GdkWindow             *vbackground_window;
  GdkWindow             *hbackground_window;
  guint                  pointer_grabbed           : 1;
  guint                  kinetic_scrolling         : 1;
  guint                  capture_button_press      : 1;
  guint                  in_drag                   : 1;
  guint                  last_button_event_valid   : 1;

  guint                  release_timeout_id;
  guint                  deceleration_id;

  gdouble                last_button_event_x_root;
  gdouble                last_button_event_y_root;

  gdouble                last_motion_event_x_root;
  gdouble                last_motion_event_y_root;
  guint32                last_motion_event_time;

  gdouble                x_velocity;
  gdouble                y_velocity;

  gdouble                unclamped_hadj_value;
  gdouble                unclamped_vadj_value;

  GtkAdjustment *opacity;
  GbAnimation   *opacity_anim;

  gint           sb_min_height;
  gint           sb_padding;
  gint           sb_radius;
  gint           sb_width;
  gboolean       sb_fading_in;
  gint           sb_fade_out_delay;
  guint          sb_fade_out_id;

  gboolean       sb_hovering;
  gboolean       sb_pointer_grabbed;
  gboolean       sb_grab_vscroll;
  gboolean       sb_grab_hscroll;
  gboolean       sb_drag_slider;
  gboolean       sb_visible;

  gint           sb_grab_offset_x;
  gint           sb_grab_offset_y;

  gint           sb_scroll_direction;
  guint          sb_scroll_timeout_id;

  gboolean       overlay_scrollbars;
} GtkScrolledWindowPrivate;

#define GTK_SCROLLED_WINDOW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GTK_TYPE_SCROLLED_WINDOW, GtkScrolledWindowPrivate))

typedef struct
{
  GtkScrolledWindow     *scrolled_window;
  gint64                 last_deceleration_time;

  gdouble                x_velocity;
  gdouble                y_velocity;
  gdouble                vel_cosine;
  gdouble                vel_sine;
} KineticScrollData;

enum {
  PROP_0,
  PROP_HADJUSTMENT,
  PROP_VADJUSTMENT,
  PROP_HSCROLLBAR_POLICY,
  PROP_VSCROLLBAR_POLICY,
  PROP_WINDOW_PLACEMENT,
  PROP_WINDOW_PLACEMENT_SET,
  PROP_SHADOW_TYPE,
  PROP_KINETIC_SCROLLING
};

/* Signals */
enum
{
  SCROLL_CHILD,
  MOVE_FOCUS_OUT,
  LAST_SIGNAL
};

static void     gtk_scrolled_window_destroy            (GtkObject         *object);
static void     gtk_scrolled_window_set_property       (GObject           *object,
                                                        guint              prop_id,
                                                        const GValue      *value,
                                                        GParamSpec        *pspec);
static void     gtk_scrolled_window_get_property       (GObject           *object,
                                                        guint              prop_id,
                                                        GValue            *value,
                                                        GParamSpec        *pspec);

static void     gtk_scrolled_window_screen_changed     (GtkWidget         *widget,
                                                        GdkScreen         *previous_screen);
static gboolean gtk_scrolled_window_expose             (GtkWidget         *widget,
                                                        GdkEventExpose    *event);
static void     gtk_scrolled_window_size_request       (GtkWidget         *widget,
                                                        GtkRequisition    *requisition);
static void     gtk_scrolled_window_size_allocate      (GtkWidget         *widget,
                                                        GtkAllocation     *allocation);
static gboolean gtk_scrolled_window_scroll_event       (GtkWidget         *widget,
                                                        GdkEventScroll    *event);
static gboolean gtk_scrolled_window_captured_event     (GtkWidget         *widget,
                                                        GdkEvent          *event);
static gboolean gtk_scrolled_window_focus              (GtkWidget         *widget,
                                                        GtkDirectionType   direction);
static void     gtk_scrolled_window_add                (GtkContainer      *container,
                                                        GtkWidget         *widget);
static void     gtk_scrolled_window_remove             (GtkContainer      *container,
                                                        GtkWidget         *widget);
static void     gtk_scrolled_window_forall             (GtkContainer      *container,
                                                        gboolean           include_internals,
                                                        GtkCallback        callback,
                                                        gpointer           callback_data);
static gboolean gtk_scrolled_window_scroll_child       (GtkScrolledWindow *scrolled_window,
                                                        GtkScrollType      scroll,
                                                        gboolean           horizontal);
static void     gtk_scrolled_window_move_focus_out     (GtkScrolledWindow *scrolled_window,
                                                        GtkDirectionType   direction_type);

static void     gtk_scrolled_window_relative_allocation(GtkWidget         *widget,
                                                        GtkAllocation     *allocation);
static void     gtk_scrolled_window_adjustment_changed (GtkAdjustment     *adjustment,
                                                        gpointer           data);
static void     gtk_scrolled_window_adjustment_value_changed (GtkAdjustment     *adjustment,
                                                              gpointer           data);

static void  gtk_scrolled_window_update_real_placement (GtkScrolledWindow *scrolled_window);

static void  gtk_scrolled_window_realize               (GtkWidget           *widget);
static void  gtk_scrolled_window_unrealize             (GtkWidget           *widget);
static void  gtk_scrolled_window_map                   (GtkWidget           *widget);
static void  gtk_scrolled_window_unmap                 (GtkWidget           *widget);
static void  gtk_scrolled_window_grab_notify           (GtkWidget           *widget,
                                                        gboolean             was_grabbed);

static gboolean _gtk_scrolled_window_set_adjustment_value      (GtkScrolledWindow *scrolled_window,
                                                                GtkAdjustment     *adjustment,
                                                                gdouble            value,
                                                                gboolean           allow_overshooting,
                                                                gboolean           snap_to_border);

static void gtk_scrolled_window_cancel_animation         (GtkScrolledWindow *scrolled_window);
static void gtk_scrolled_window_start_fade_out_timeout (GtkScrolledWindow *scrolled_window);
static void gtk_scrolled_window_stop_fade_out_timeout (GtkScrolledWindow *scrolled_window);
static void gtk_scrolled_window_start_fade_in_animation  (GtkScrolledWindow *scrolled_window);
static void gtk_scrolled_window_start_fade_out_animation (GtkScrolledWindow *scrolled_window);
static gboolean
           gtk_scrolled_window_over_child_scroll_areas (GtkScrolledWindow *scrolled_window,
                                                        GdkEvent          *event,
                                                        gint               x,
                                                        gint               y,
                                                        gboolean          *over_vscroll,
                                                        gboolean          *over_hscroll);
static void gtk_scrolled_window_get_child_scroll_areas (GtkScrolledWindow *scrolled_window,
                                                        GtkWidget         *child,
                                                        GdkWindow         *child_window,
                                                        GdkRectangle      *vbar_rect,
                                                        GdkRectangle      *vslider_rect,
                                                        GdkRectangle      *hbar_rect,
                                                        GdkRectangle      *hslider_rect);
static gboolean gtk_scrolled_window_child_expose (GtkWidget         *widget,
                                                  GdkEventExpose    *eevent,
                                                  GtkScrolledWindow *scrolled_window);
static void  gtk_scrolled_window_expose_scrollbars (GtkAdjustment     *adj,
                                                    GtkScrolledWindow *scrolled_window);

static void gtk_scrolled_window_overlay_scrollbars_changed (GtkSettings *settings,
                                                            GParamSpec  *arg,
                                                            gpointer     user_data);

static guint signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE (GtkScrolledWindow, gtk_scrolled_window, GTK_TYPE_BIN)

static void
add_scroll_binding (GtkBindingSet  *binding_set,
		    guint           keyval,
		    GdkModifierType mask,
		    GtkScrollType   scroll,
		    gboolean        horizontal)
{
  guint keypad_keyval = keyval - GDK_Left + GDK_KP_Left;
  
  gtk_binding_entry_add_signal (binding_set, keyval, mask,
                                "scroll-child", 2,
                                GTK_TYPE_SCROLL_TYPE, scroll,
				G_TYPE_BOOLEAN, horizontal);
  gtk_binding_entry_add_signal (binding_set, keypad_keyval, mask,
                                "scroll-child", 2,
                                GTK_TYPE_SCROLL_TYPE, scroll,
				G_TYPE_BOOLEAN, horizontal);
}

static void
add_tab_bindings (GtkBindingSet    *binding_set,
		  GdkModifierType   modifiers,
		  GtkDirectionType  direction)
{
  gtk_binding_entry_add_signal (binding_set, GDK_Tab, modifiers,
                                "move-focus-out", 1,
                                GTK_TYPE_DIRECTION_TYPE, direction);
  gtk_binding_entry_add_signal (binding_set, GDK_KP_Tab, modifiers,
                                "move-focus-out", 1,
                                GTK_TYPE_DIRECTION_TYPE, direction);
}

static void
gtk_scrolled_window_class_init (GtkScrolledWindowClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;
  GtkContainerClass *container_class;
  GtkBindingSet *binding_set;

  object_class = (GtkObjectClass*) class;
  widget_class = (GtkWidgetClass*) class;
  container_class = (GtkContainerClass*) class;

  gobject_class->set_property = gtk_scrolled_window_set_property;
  gobject_class->get_property = gtk_scrolled_window_get_property;

  object_class->destroy = gtk_scrolled_window_destroy;

  widget_class->screen_changed = gtk_scrolled_window_screen_changed;
  widget_class->expose_event = gtk_scrolled_window_expose;
  widget_class->size_request = gtk_scrolled_window_size_request;
  widget_class->size_allocate = gtk_scrolled_window_size_allocate;
  widget_class->scroll_event = gtk_scrolled_window_scroll_event;
  widget_class->focus = gtk_scrolled_window_focus;
  widget_class->realize = gtk_scrolled_window_realize;
  widget_class->unrealize = gtk_scrolled_window_unrealize;
  widget_class->map = gtk_scrolled_window_map;
  widget_class->unmap = gtk_scrolled_window_unmap;
  widget_class->grab_notify = gtk_scrolled_window_grab_notify;

  container_class->add = gtk_scrolled_window_add;
  container_class->remove = gtk_scrolled_window_remove;
  container_class->forall = gtk_scrolled_window_forall;

  class->scrollbar_spacing = -1;

  class->scroll_child = gtk_scrolled_window_scroll_child;
  class->move_focus_out = gtk_scrolled_window_move_focus_out;
  
  g_object_class_install_property (gobject_class,
				   PROP_HADJUSTMENT,
				   g_param_spec_object ("hadjustment",
							P_("Horizontal Adjustment"),
							P_("The GtkAdjustment for the horizontal position"),
							GTK_TYPE_ADJUSTMENT,
							GTK_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class,
				   PROP_VADJUSTMENT,
				   g_param_spec_object ("vadjustment",
							P_("Vertical Adjustment"),
							P_("The GtkAdjustment for the vertical position"),
							GTK_TYPE_ADJUSTMENT,
							GTK_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class,
                                   PROP_HSCROLLBAR_POLICY,
                                   g_param_spec_enum ("hscrollbar-policy",
                                                      P_("Horizontal Scrollbar Policy"),
                                                      P_("When the horizontal scrollbar is displayed"),
						      GTK_TYPE_POLICY_TYPE,
						      GTK_POLICY_ALWAYS,
                                                      GTK_PARAM_READABLE | GTK_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class,
                                   PROP_VSCROLLBAR_POLICY,
                                   g_param_spec_enum ("vscrollbar-policy",
                                                      P_("Vertical Scrollbar Policy"),
                                                      P_("When the vertical scrollbar is displayed"),
						      GTK_TYPE_POLICY_TYPE,
						      GTK_POLICY_ALWAYS,
                                                      GTK_PARAM_READABLE | GTK_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class,
                                   PROP_WINDOW_PLACEMENT,
                                   g_param_spec_enum ("window-placement",
                                                      P_("Window Placement"),
                                                      P_("Where the contents are located with respect to the scrollbars. This property only takes effect if \"window-placement-set\" is TRUE."),
						      GTK_TYPE_CORNER_TYPE,
						      GTK_CORNER_TOP_LEFT,
                                                      GTK_PARAM_READABLE | GTK_PARAM_WRITABLE));
  
  /**
   * GtkScrolledWindow:window-placement-set:
   *
   * Whether "window-placement" should be used to determine the location 
   * of the contents with respect to the scrollbars. Otherwise, the 
   * "gtk-scrolled-window-placement" setting is used.
   *
   * Since: 2.10
   */
  g_object_class_install_property (gobject_class,
                                   PROP_WINDOW_PLACEMENT_SET,
                                   g_param_spec_boolean ("window-placement-set",
					   		 P_("Window Placement Set"),
							 P_("Whether \"window-placement\" should be used to determine the location of the contents with respect to the scrollbars."),
							 FALSE,
							 GTK_PARAM_READABLE | GTK_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class,
                                   PROP_SHADOW_TYPE,
                                   g_param_spec_enum ("shadow-type",
                                                      P_("Shadow Type"),
                                                      P_("Style of bevel around the contents"),
						      GTK_TYPE_SHADOW_TYPE,
						      GTK_SHADOW_NONE,
                                                      GTK_PARAM_READABLE | GTK_PARAM_WRITABLE));

  /**
   * GtkScrolledWindow:scrollbars-within-bevel:
   *
   * Whether to place scrollbars within the scrolled window's bevel.
   *
   * Since: 2.12
   */
  gtk_widget_class_install_style_property (widget_class,
					   g_param_spec_boolean ("scrollbars-within-bevel",
							         P_("Scrollbars within bevel"),
							         P_("Place scrollbars within the scrolled window's bevel"),
							         FALSE,
							         GTK_PARAM_READABLE));

  gtk_widget_class_install_style_property (widget_class,
					   g_param_spec_int ("scrollbar-spacing",
							     P_("Scrollbar spacing"),
							     P_("Number of pixels between the scrollbars and the scrolled window"),
							     0,
							     G_MAXINT,
							     DEFAULT_SCROLLBAR_SPACING,
							     GTK_PARAM_READABLE));

  /**
   * GtkScrolledWindow:kinetic-scrolling:
   *
   * The kinetic scrolling behavior flags.
   *
   * Since: X.XX
   */
  g_object_class_install_property (gobject_class,
                                   PROP_KINETIC_SCROLLING,
                                   g_param_spec_boolean ("kinetic-scrolling",
                                                         P_("Kinetic Scrolling"),
                                                         P_("Kinetic scrolling mode."),
                                                         TRUE,
                                                         GTK_PARAM_READABLE |
                                                         GTK_PARAM_WRITABLE));

  /**
   * GtkScrolledWindow::scroll-child:
   * @scrolled_window: a #GtkScrolledWindow
   * @scroll: a #GtkScrollType describing how much to scroll
   * @horizontal: whether the keybinding scrolls the child
   *   horizontally or not
   *
   * The ::scroll-child signal is a
   * <link linkend="keybinding-signals">keybinding signal</link>
   * which gets emitted when a keybinding that scrolls is pressed.
   * The horizontal or vertical adjustment is updated which triggers a
   * signal that the scrolled windows child may listen to and scroll itself.
   */
  signals[SCROLL_CHILD] =
    g_signal_new (I_("scroll-child"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (GtkScrolledWindowClass, scroll_child),
                  NULL, NULL,
                  _gtk_marshal_BOOLEAN__ENUM_BOOLEAN,
                  G_TYPE_BOOLEAN, 2,
                  GTK_TYPE_SCROLL_TYPE,
		  G_TYPE_BOOLEAN);
  signals[MOVE_FOCUS_OUT] =
    g_signal_new (I_("move-focus-out"),
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (GtkScrolledWindowClass, move_focus_out),
                  NULL, NULL,
                  _gtk_marshal_VOID__ENUM,
                  G_TYPE_NONE, 1,
                  GTK_TYPE_DIRECTION_TYPE);
  
  binding_set = gtk_binding_set_by_class (class);

  add_scroll_binding (binding_set, GDK_Left,  GDK_CONTROL_MASK, GTK_SCROLL_STEP_BACKWARD, TRUE);
  add_scroll_binding (binding_set, GDK_Right, GDK_CONTROL_MASK, GTK_SCROLL_STEP_FORWARD,  TRUE);
  add_scroll_binding (binding_set, GDK_Up,    GDK_CONTROL_MASK, GTK_SCROLL_STEP_BACKWARD, FALSE);
  add_scroll_binding (binding_set, GDK_Down,  GDK_CONTROL_MASK, GTK_SCROLL_STEP_FORWARD,  FALSE);

  add_scroll_binding (binding_set, GDK_Page_Up,   GDK_CONTROL_MASK, GTK_SCROLL_PAGE_BACKWARD, TRUE);
  add_scroll_binding (binding_set, GDK_Page_Down, GDK_CONTROL_MASK, GTK_SCROLL_PAGE_FORWARD,  TRUE);
  add_scroll_binding (binding_set, GDK_Page_Up,   0,                GTK_SCROLL_PAGE_BACKWARD, FALSE);
  add_scroll_binding (binding_set, GDK_Page_Down, 0,                GTK_SCROLL_PAGE_FORWARD,  FALSE);

  add_scroll_binding (binding_set, GDK_Home, GDK_CONTROL_MASK, GTK_SCROLL_START, TRUE);
  add_scroll_binding (binding_set, GDK_End,  GDK_CONTROL_MASK, GTK_SCROLL_END,   TRUE);
  add_scroll_binding (binding_set, GDK_Home, 0,                GTK_SCROLL_START, FALSE);
  add_scroll_binding (binding_set, GDK_End,  0,                GTK_SCROLL_END,   FALSE);

  add_tab_bindings (binding_set, GDK_CONTROL_MASK, GTK_DIR_TAB_FORWARD);
  add_tab_bindings (binding_set, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_DIR_TAB_BACKWARD);

  g_type_class_add_private (class, sizeof (GtkScrolledWindowPrivate));
}

static void
gtk_scrolled_window_init (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  GtkSettings *settings;

  gtk_widget_set_has_window (GTK_WIDGET (scrolled_window), FALSE);
  gtk_widget_set_can_focus (GTK_WIDGET (scrolled_window), TRUE);

  scrolled_window->hscrollbar = NULL;
  scrolled_window->vscrollbar = NULL;
  scrolled_window->hscrollbar_policy = GTK_POLICY_ALWAYS;
  scrolled_window->vscrollbar_policy = GTK_POLICY_ALWAYS;
  scrolled_window->hscrollbar_visible = FALSE;
  scrolled_window->vscrollbar_visible = FALSE;
  scrolled_window->focus_out = FALSE;
  scrolled_window->window_placement = GTK_CORNER_TOP_LEFT;
  gtk_scrolled_window_update_real_placement (scrolled_window);

  settings = gtk_widget_get_settings (GTK_WIDGET (scrolled_window));
  g_object_get (settings,
                "gtk-enable-overlay-scrollbars",
                &priv->overlay_scrollbars,
                NULL);
  g_signal_connect (settings, "notify::gtk-enable-overlay-scrollbars",
                    G_CALLBACK (gtk_scrolled_window_overlay_scrollbars_changed),
                    scrolled_window);

  gtk_scrolled_window_set_kinetic_scrolling (scrolled_window, TRUE);
  gtk_scrolled_window_set_capture_button_press (scrolled_window, TRUE);

  priv->opacity = g_object_new (GTK_TYPE_ADJUSTMENT,
                                "lower", 0.0,
                                "upper", 0.5,
                                "value", 0.0,
                                NULL);
  g_object_ref_sink (priv->opacity);

  priv->sb_min_height = 20;
  priv->sb_padding = 2;
  priv->sb_radius = 3;
  priv->sb_width = 6;
  priv->sb_fade_out_delay = 1000;

  g_signal_connect (priv->opacity, "value-changed",
                    G_CALLBACK (gtk_scrolled_window_expose_scrollbars),
                    scrolled_window);
}

/**
 * gtk_scrolled_window_new:
 * @hadjustment: (allow-none): horizontal adjustment
 * @vadjustment: (allow-none): vertical adjustment
 *
 * Creates a new scrolled window.
 *
 * The two arguments are the scrolled window's adjustments; these will be
 * shared with the scrollbars and the child widget to keep the bars in sync 
 * with the child. Usually you want to pass %NULL for the adjustments, which 
 * will cause the scrolled window to create them for you.
 *
 * Returns: a new scrolled window
 */
GtkWidget*
gtk_scrolled_window_new (GtkAdjustment *hadjustment,
			 GtkAdjustment *vadjustment)
{
  GtkWidget *scrolled_window;

  if (hadjustment)
    g_return_val_if_fail (GTK_IS_ADJUSTMENT (hadjustment), NULL);

  if (vadjustment)
    g_return_val_if_fail (GTK_IS_ADJUSTMENT (vadjustment), NULL);

  scrolled_window = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
				    "hadjustment", hadjustment,
				    "vadjustment", vadjustment,
				    NULL);

  return scrolled_window;
}

/**
 * gtk_scrolled_window_set_hadjustment:
 * @scrolled_window: a #GtkScrolledWindow
 * @hadjustment: horizontal scroll adjustment
 *
 * Sets the #GtkAdjustment for the horizontal scrollbar.
 */
void
gtk_scrolled_window_set_hadjustment (GtkScrolledWindow *scrolled_window,
				     GtkAdjustment     *hadjustment)
{
  GtkBin *bin;
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));
  if (hadjustment)
    g_return_if_fail (GTK_IS_ADJUSTMENT (hadjustment));
  else
    hadjustment = (GtkAdjustment*) g_object_new (GTK_TYPE_ADJUSTMENT, NULL);

  bin = GTK_BIN (scrolled_window);

  if (!scrolled_window->hscrollbar)
    {
      gtk_widget_push_composite_child ();
      scrolled_window->hscrollbar = gtk_hscrollbar_new (hadjustment);
      gtk_widget_set_composite_name (scrolled_window->hscrollbar, "hscrollbar");
      gtk_widget_pop_composite_child ();

      gtk_widget_set_parent (scrolled_window->hscrollbar, GTK_WIDGET (scrolled_window));
      g_object_ref (scrolled_window->hscrollbar);
      gtk_widget_show (scrolled_window->hscrollbar);
    }
  else
    {
      GtkAdjustment *old_adjustment;
      
      old_adjustment = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar));
      if (old_adjustment == hadjustment)
	return;

      g_signal_handlers_disconnect_by_func (old_adjustment,
					    gtk_scrolled_window_adjustment_changed,
					    scrolled_window);
      g_signal_handlers_disconnect_by_func (old_adjustment,
                                            gtk_scrolled_window_adjustment_value_changed,
                                            scrolled_window);
      g_signal_handlers_disconnect_by_func (old_adjustment,
                                            gtk_scrolled_window_expose_scrollbars,
                                            scrolled_window);

      gtk_range_set_adjustment (GTK_RANGE (scrolled_window->hscrollbar),
				hadjustment);
    }
  hadjustment = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar));
  g_signal_connect (hadjustment,
		    "changed",
		    G_CALLBACK (gtk_scrolled_window_adjustment_changed),
		    scrolled_window);
  g_signal_connect (hadjustment,
		    "value-changed",
		    G_CALLBACK (gtk_scrolled_window_adjustment_value_changed),
		    scrolled_window);
  gtk_scrolled_window_adjustment_changed (hadjustment, scrolled_window);
  gtk_scrolled_window_adjustment_value_changed (hadjustment, scrolled_window);

#if 0
  g_signal_connect (hadjustment, "value-changed",
                    G_CALLBACK (gtk_scrolled_window_adjustment_value_changed),
                    scrolled_window);
#endif

  g_signal_connect (hadjustment, "changed",
                    G_CALLBACK (gtk_scrolled_window_expose_scrollbars),
                    scrolled_window);
  g_signal_connect (hadjustment, "value-changed",
                    G_CALLBACK (gtk_scrolled_window_expose_scrollbars),
                    scrolled_window);

  if (bin->child)
    gtk_widget_set_scroll_adjustments (bin->child,
                                       gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar)),
                                       gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar)));

  g_object_notify (G_OBJECT (scrolled_window), "hadjustment");
}

/**
 * gtk_scrolled_window_set_vadjustment:
 * @scrolled_window: a #GtkScrolledWindow
 * @vadjustment: vertical scroll adjustment
 *
 * Sets the #GtkAdjustment for the vertical scrollbar.
 */
void
gtk_scrolled_window_set_vadjustment (GtkScrolledWindow *scrolled_window,
				     GtkAdjustment     *vadjustment)
{
  GtkBin *bin;
  GtkScrolledWindowPrivate *priv;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));
  if (vadjustment)
    g_return_if_fail (GTK_IS_ADJUSTMENT (vadjustment));
  else
    vadjustment = (GtkAdjustment*) g_object_new (GTK_TYPE_ADJUSTMENT, NULL);

  bin = GTK_BIN (scrolled_window);
  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  if (!scrolled_window->vscrollbar)
    {
      gtk_widget_push_composite_child ();
      scrolled_window->vscrollbar = gtk_vscrollbar_new (vadjustment);
      gtk_widget_set_composite_name (scrolled_window->vscrollbar, "vscrollbar");
      gtk_widget_pop_composite_child ();

      gtk_widget_set_parent (scrolled_window->vscrollbar, GTK_WIDGET (scrolled_window));
      g_object_ref (scrolled_window->vscrollbar);
      gtk_widget_show (scrolled_window->vscrollbar);
    }
  else
    {
      GtkAdjustment *old_adjustment;
      
      old_adjustment = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar));
      if (old_adjustment == vadjustment)
	return;

      g_signal_handlers_disconnect_by_func (old_adjustment,
					    gtk_scrolled_window_adjustment_changed,
					    scrolled_window);
      g_signal_handlers_disconnect_by_func (old_adjustment,
                                            gtk_scrolled_window_adjustment_value_changed,
                                            scrolled_window);
      g_signal_handlers_disconnect_by_func (old_adjustment,
                                            gtk_scrolled_window_expose_scrollbars,
                                            scrolled_window);

      gtk_range_set_adjustment (GTK_RANGE (scrolled_window->vscrollbar),
				vadjustment);
    }
  vadjustment = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar));
  g_signal_connect (vadjustment,
		    "changed",
		    G_CALLBACK (gtk_scrolled_window_adjustment_changed),
		    scrolled_window);
  g_signal_connect (vadjustment,
		    "value-changed",
		    G_CALLBACK (gtk_scrolled_window_adjustment_value_changed),
		    scrolled_window);
  gtk_scrolled_window_adjustment_changed (vadjustment, scrolled_window);
  gtk_scrolled_window_adjustment_value_changed (vadjustment, scrolled_window);

#if 0
  g_signal_connect (vadjustment,
                    "value-changed",
                    G_CALLBACK (gtk_scrolled_window_adjustment_value_changed),
                    scrolled_window);
#endif

  g_signal_connect (vadjustment, "changed",
                    G_CALLBACK (gtk_scrolled_window_expose_scrollbars),
                    scrolled_window);
  g_signal_connect (vadjustment, "value-changed",
                    G_CALLBACK (gtk_scrolled_window_expose_scrollbars),
                    scrolled_window);

  if (bin->child)
    gtk_widget_set_scroll_adjustments (bin->child,
                                       gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar)),
                                       gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar)));

  g_object_notify (G_OBJECT (scrolled_window), "vadjustment");
}

/**
 * gtk_scrolled_window_get_hadjustment:
 * @scrolled_window: a #GtkScrolledWindow
 *
 * Returns the horizontal scrollbar's adjustment, used to connect the
 * horizontal scrollbar to the child widget's horizontal scroll
 * functionality.
 *
 * Returns: (transfer none): the horizontal #GtkAdjustment
 */
GtkAdjustment*
gtk_scrolled_window_get_hadjustment (GtkScrolledWindow *scrolled_window)
{
  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), NULL);

  return (scrolled_window->hscrollbar ?
	  gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar)) :
	  NULL);
}

/**
 * gtk_scrolled_window_get_vadjustment:
 * @scrolled_window: a #GtkScrolledWindow
 * 
 * Returns the vertical scrollbar's adjustment, used to connect the
 * vertical scrollbar to the child widget's vertical scroll functionality.
 * 
 * Returns: (transfer none): the vertical #GtkAdjustment
 */
GtkAdjustment*
gtk_scrolled_window_get_vadjustment (GtkScrolledWindow *scrolled_window)
{
  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), NULL);

  return (scrolled_window->vscrollbar ?
	  gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar)) :
	  NULL);
}

/**
 * gtk_scrolled_window_get_hscrollbar:
 * @scrolled_window: a #GtkScrolledWindow
 *
 * Returns the horizontal scrollbar of @scrolled_window.
 *
 * Returns: (transfer none): the horizontal scrollbar of the scrolled window,
 *     or %NULL if it does not have one.
 *
 * Since: 2.8
 */
GtkWidget*
gtk_scrolled_window_get_hscrollbar (GtkScrolledWindow *scrolled_window)
{
  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), NULL);
  
  return scrolled_window->hscrollbar;
}

/**
 * gtk_scrolled_window_get_vscrollbar:
 * @scrolled_window: a #GtkScrolledWindow
 * 
 * Returns the vertical scrollbar of @scrolled_window.
 *
 * Returns: (transfer none): the vertical scrollbar of the scrolled window,
 *     or %NULL if it does not have one.
 *
 * Since: 2.8
 */
GtkWidget*
gtk_scrolled_window_get_vscrollbar (GtkScrolledWindow *scrolled_window)
{
  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), NULL);

  return scrolled_window->vscrollbar;
}

/**
 * gtk_scrolled_window_set_policy:
 * @scrolled_window: a #GtkScrolledWindow
 * @hscrollbar_policy: policy for horizontal bar
 * @vscrollbar_policy: policy for vertical bar
 * 
 * Sets the scrollbar policy for the horizontal and vertical scrollbars.
 *
 * The policy determines when the scrollbar should appear; it is a value
 * from the #GtkPolicyType enumeration. If %GTK_POLICY_ALWAYS, the
 * scrollbar is always present; if %GTK_POLICY_NEVER, the scrollbar is
 * never present; if %GTK_POLICY_AUTOMATIC, the scrollbar is present only
 * if needed (that is, if the slider part of the bar would be smaller
 * than the trough - the display is larger than the page size).
 */
void
gtk_scrolled_window_set_policy (GtkScrolledWindow *scrolled_window,
				GtkPolicyType      hscrollbar_policy,
				GtkPolicyType      vscrollbar_policy)
{
  GObject *object = G_OBJECT (scrolled_window);
  
  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));

  if ((scrolled_window->hscrollbar_policy != hscrollbar_policy) ||
      (scrolled_window->vscrollbar_policy != vscrollbar_policy))
    {
      scrolled_window->hscrollbar_policy = hscrollbar_policy;
      scrolled_window->vscrollbar_policy = vscrollbar_policy;

      gtk_widget_queue_resize (GTK_WIDGET (scrolled_window));

      g_object_freeze_notify (object);
      g_object_notify (object, "hscrollbar-policy");
      g_object_notify (object, "vscrollbar-policy");
      g_object_thaw_notify (object);
    }
}

/**
 * gtk_scrolled_window_get_policy:
 * @scrolled_window: a #GtkScrolledWindow
 * @hscrollbar_policy: (out) (allow-none): location to store the policy 
 *     for the horizontal scrollbar, or %NULL.
 * @vscrollbar_policy: (out) (allow-none): location to store the policy
 *     for the vertical scrollbar, or %NULL.
 * 
 * Retrieves the current policy values for the horizontal and vertical
 * scrollbars. See gtk_scrolled_window_set_policy().
 */
void
gtk_scrolled_window_get_policy (GtkScrolledWindow *scrolled_window,
				GtkPolicyType     *hscrollbar_policy,
				GtkPolicyType     *vscrollbar_policy)
{
  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));

  if (hscrollbar_policy)
    *hscrollbar_policy = scrolled_window->hscrollbar_policy;
  if (vscrollbar_policy)
    *vscrollbar_policy = scrolled_window->vscrollbar_policy;
}

static void
gtk_scrolled_window_update_real_placement (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  GtkSettings *settings;

  settings = gtk_widget_get_settings (GTK_WIDGET (scrolled_window));

  if (priv->window_placement_set || settings == NULL)
    priv->real_window_placement = scrolled_window->window_placement;
  else
    g_object_get (settings,
		  "gtk-scrolled-window-placement",
		  &priv->real_window_placement,
		  NULL);
}

static void
gtk_scrolled_window_set_placement_internal (GtkScrolledWindow *scrolled_window,
					    GtkCornerType      window_placement)
{
  if (scrolled_window->window_placement != window_placement)
    {
      scrolled_window->window_placement = window_placement;

      gtk_scrolled_window_update_real_placement (scrolled_window);
      gtk_widget_queue_resize (GTK_WIDGET (scrolled_window));
      
      g_object_notify (G_OBJECT (scrolled_window), "window-placement");
    }
}

static void
gtk_scrolled_window_set_placement_set (GtkScrolledWindow *scrolled_window,
				       gboolean           placement_set,
				       gboolean           emit_resize)
{
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  if (priv->window_placement_set != placement_set)
    {
      priv->window_placement_set = placement_set;

      gtk_scrolled_window_update_real_placement (scrolled_window);
      if (emit_resize)
        gtk_widget_queue_resize (GTK_WIDGET (scrolled_window));

      g_object_notify (G_OBJECT (scrolled_window), "window-placement-set");
    }
}

/**
 * gtk_scrolled_window_set_placement:
 * @scrolled_window: a #GtkScrolledWindow
 * @window_placement: position of the child window
 *
 * Sets the placement of the contents with respect to the scrollbars
 * for the scrolled window.
 * 
 * The default is %GTK_CORNER_TOP_LEFT, meaning the child is
 * in the top left, with the scrollbars underneath and to the right.
 * Other values in #GtkCornerType are %GTK_CORNER_TOP_RIGHT,
 * %GTK_CORNER_BOTTOM_LEFT, and %GTK_CORNER_BOTTOM_RIGHT.
 *
 * See also gtk_scrolled_window_get_placement() and
 * gtk_scrolled_window_unset_placement().
 */
void
gtk_scrolled_window_set_placement (GtkScrolledWindow *scrolled_window,
				   GtkCornerType      window_placement)
{
  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));

  gtk_scrolled_window_set_placement_set (scrolled_window, TRUE, FALSE);
  gtk_scrolled_window_set_placement_internal (scrolled_window, window_placement);
}

/**
 * gtk_scrolled_window_get_placement:
 * @scrolled_window: a #GtkScrolledWindow
 *
 * Gets the placement of the contents with respect to the scrollbars
 * for the scrolled window. See gtk_scrolled_window_set_placement().
 *
 * Return value: the current placement value.
 *
 * See also gtk_scrolled_window_set_placement() and
 * gtk_scrolled_window_unset_placement().
 **/
GtkCornerType
gtk_scrolled_window_get_placement (GtkScrolledWindow *scrolled_window)
{
  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), GTK_CORNER_TOP_LEFT);

  return scrolled_window->window_placement;
}

/**
 * gtk_scrolled_window_unset_placement:
 * @scrolled_window: a #GtkScrolledWindow
 *
 * Unsets the placement of the contents with respect to the scrollbars
 * for the scrolled window. If no window placement is set for a scrolled
 * window, it obeys the "gtk-scrolled-window-placement" XSETTING.
 *
 * See also gtk_scrolled_window_set_placement() and
 * gtk_scrolled_window_get_placement().
 *
 * Since: 2.10
 **/
void
gtk_scrolled_window_unset_placement (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));

  if (priv->window_placement_set)
    {
      priv->window_placement_set = FALSE;

      gtk_widget_queue_resize (GTK_WIDGET (scrolled_window));

      g_object_notify (G_OBJECT (scrolled_window), "window-placement-set");
    }
}

/**
 * gtk_scrolled_window_set_shadow_type:
 * @scrolled_window: a #GtkScrolledWindow
 * @type: kind of shadow to draw around scrolled window contents
 *
 * Changes the type of shadow drawn around the contents of
 * @scrolled_window.
 * 
 **/
void
gtk_scrolled_window_set_shadow_type (GtkScrolledWindow *scrolled_window,
				     GtkShadowType      type)
{
  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));
  g_return_if_fail (type >= GTK_SHADOW_NONE && type <= GTK_SHADOW_ETCHED_OUT);
  
  if (scrolled_window->shadow_type != type)
    {
      scrolled_window->shadow_type = type;

      if (gtk_widget_is_drawable (GTK_WIDGET (scrolled_window)))
	gtk_widget_queue_draw (GTK_WIDGET (scrolled_window));

      gtk_widget_queue_resize (GTK_WIDGET (scrolled_window));

      g_object_notify (G_OBJECT (scrolled_window), "shadow-type");
    }
}

/**
 * gtk_scrolled_window_get_shadow_type:
 * @scrolled_window: a #GtkScrolledWindow
 *
 * Gets the shadow type of the scrolled window. See 
 * gtk_scrolled_window_set_shadow_type().
 *
 * Return value: the current shadow type
 **/
GtkShadowType
gtk_scrolled_window_get_shadow_type (GtkScrolledWindow *scrolled_window)
{
  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_NONE);

  return scrolled_window->shadow_type;
}

/**
 * gtk_scrolled_window_set_kinetic_scrolling:
 * @scrolled_window: a #GtkScrolledWindow
 * @kinetic_scrolling: %TRUE to enable kinetic scrolling
 *
 * Turns kinetic scrolling on or off.
 * Kinetic scrolling only applies to devices with source
 * %GDK_SOURCE_TOUCHSCREEN.
 *
 * Since: X.XX
 **/
void
gtk_scrolled_window_set_kinetic_scrolling (GtkScrolledWindow *scrolled_window,
                                           gboolean           kinetic_scrolling)
{
  GtkScrolledWindowPrivate *priv;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));

  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  if (priv->kinetic_scrolling == kinetic_scrolling)
    return;

  priv->kinetic_scrolling = kinetic_scrolling;
  if (priv->kinetic_scrolling)
    {
      _gtk_widget_set_captured_event_handler (GTK_WIDGET (scrolled_window),
                                              gtk_scrolled_window_captured_event);
    }
  else
    {
      _gtk_widget_set_captured_event_handler (GTK_WIDGET (scrolled_window), NULL);
      if (priv->release_timeout_id)
        {
          g_source_remove (priv->release_timeout_id);
          priv->release_timeout_id = 0;
        }
      if (priv->deceleration_id)
        {
          g_source_remove (priv->deceleration_id);
          priv->deceleration_id = 0;
        }
    }
  g_object_notify (G_OBJECT (scrolled_window), "kinetic-scrolling");
}

/**
 * gtk_scrolled_window_get_kinetic_scrolling:
 * @scrolled_window: a #GtkScrolledWindow
 *
 * Returns the specified kinetic scrolling behavior.
 *
 * Return value: the scrolling behavior flags.
 *
 * Since: X.XX
 */
gboolean
gtk_scrolled_window_get_kinetic_scrolling (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv;

  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), FALSE);

  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  return priv->kinetic_scrolling;
}

/**
 * gtk_scrolled_window_set_capture_button_press:
 * @scrolled_window: a #GtkScrolledWindow
 * @capture_button_press: %TRUE to capture button presses
 *
 * Changes the behaviour of @scrolled_window wrt. to the initial
 * event that possibly starts kinetic scrolling. When @capture_button_press
 * is set to %TRUE, the event is captured by the scrolled window, and
 * then later replayed if it is meant to go to the child widget.
 *
 * This should be enabled if any child widgets perform non-reversible
 * actions on #GtkWidget::button-press-event. If they don't, and handle
 * additionally handle #GtkWidget::grab-broken-event, it might be better
 * to set @capture_button_press to %FALSE.
 *
 * This setting only has an effect if kinetic scrolling is enabled.
 *
 * Since: X.XX
 */
void
gtk_scrolled_window_set_capture_button_press (GtkScrolledWindow *scrolled_window,
                                              gboolean           capture_button_press)
{
  GtkScrolledWindowPrivate *priv;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));

  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  priv->capture_button_press = capture_button_press;
}

/**
 * gtk_scrolled_window_get_capture_button_press:
 * @scrolled_window: a #GtkScrolledWindow
 *
 * Return whether button presses are captured during kinetic
 * scrolling. See gtk_scrolled_window_set_capture_button_press().
 *
 * Returns: %TRUE if button presses are captured during kinetic scrolling
 *
 * Since: X.XX
 */
gboolean
gtk_scrolled_window_get_capture_button_press (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv;

  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), FALSE);

  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  return priv->capture_button_press;
}


static void
gtk_scrolled_window_destroy (GtkObject *object)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (object);
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  gtk_scrolled_window_cancel_animation (scrolled_window);

  if (scrolled_window->hscrollbar)
    {
      g_signal_handlers_disconnect_by_func (gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar)),
					    gtk_scrolled_window_adjustment_changed,
					    scrolled_window);
      g_signal_handlers_disconnect_by_func (gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar)),
                                            gtk_scrolled_window_adjustment_value_changed,
                                            scrolled_window);
      g_signal_handlers_disconnect_by_func (gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar)),
                                            gtk_scrolled_window_expose_scrollbars,
                                            scrolled_window);

      gtk_widget_unparent (scrolled_window->hscrollbar);
      gtk_widget_destroy (scrolled_window->hscrollbar);
      g_object_unref (scrolled_window->hscrollbar);
      scrolled_window->hscrollbar = NULL;
    }
  if (scrolled_window->vscrollbar)
    {
      g_signal_handlers_disconnect_by_func (gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar)),
					    gtk_scrolled_window_adjustment_changed,
					    scrolled_window);
      g_signal_handlers_disconnect_by_func (gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar)),
                                            gtk_scrolled_window_adjustment_value_changed,
                                            scrolled_window);
      g_signal_handlers_disconnect_by_func (gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar)),
                                            gtk_scrolled_window_expose_scrollbars,
                                            scrolled_window);

      gtk_widget_unparent (scrolled_window->vscrollbar);
      gtk_widget_destroy (scrolled_window->vscrollbar);
      g_object_unref (scrolled_window->vscrollbar);
      scrolled_window->vscrollbar = NULL;
    }

  g_signal_handlers_disconnect_by_func (gtk_widget_get_settings (GTK_WIDGET (scrolled_window)),
                                        G_CALLBACK (gtk_scrolled_window_overlay_scrollbars_changed),
                                        scrolled_window);

  if (priv->release_timeout_id)
    {
      g_source_remove (priv->release_timeout_id);
      priv->release_timeout_id = 0;
    }
  if (priv->deceleration_id)
    {
      g_source_remove (priv->deceleration_id);
      priv->deceleration_id = 0;
    }

  if (priv->button_press_event)
    {
      gdk_event_free (priv->button_press_event);
      priv->button_press_event = NULL;
    }

  if (priv->opacity)
    {
      g_object_unref (priv->opacity);
      priv->opacity = NULL;
    }

  GTK_OBJECT_CLASS (gtk_scrolled_window_parent_class)->destroy (object);
}

static void
gtk_scrolled_window_set_property (GObject      *object,
				  guint         prop_id,
				  const GValue *value,
				  GParamSpec   *pspec)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (object);
  
  switch (prop_id)
    {
    case PROP_HADJUSTMENT:
      gtk_scrolled_window_set_hadjustment (scrolled_window,
					   g_value_get_object (value));
      break;
    case PROP_VADJUSTMENT:
      gtk_scrolled_window_set_vadjustment (scrolled_window,
					   g_value_get_object (value));
      break;
    case PROP_HSCROLLBAR_POLICY:
      gtk_scrolled_window_set_policy (scrolled_window,
				      g_value_get_enum (value),
				      scrolled_window->vscrollbar_policy);
      break;
    case PROP_VSCROLLBAR_POLICY:
      gtk_scrolled_window_set_policy (scrolled_window,
				      scrolled_window->hscrollbar_policy,
				      g_value_get_enum (value));
      break;
    case PROP_WINDOW_PLACEMENT:
      gtk_scrolled_window_set_placement_internal (scrolled_window,
		      				  g_value_get_enum (value));
      break;
    case PROP_WINDOW_PLACEMENT_SET:
      gtk_scrolled_window_set_placement_set (scrolled_window,
		      			     g_value_get_boolean (value),
					     TRUE);
      break;
    case PROP_SHADOW_TYPE:
      gtk_scrolled_window_set_shadow_type (scrolled_window,
					   g_value_get_enum (value));
      break;
    case PROP_KINETIC_SCROLLING:
      gtk_scrolled_window_set_kinetic_scrolling (scrolled_window,
                                                 g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk_scrolled_window_get_property (GObject    *object,
				  guint       prop_id,
				  GValue     *value,
				  GParamSpec *pspec)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (object);
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  
  switch (prop_id)
    {
    case PROP_HADJUSTMENT:
      g_value_set_object (value,
			  G_OBJECT (gtk_scrolled_window_get_hadjustment (scrolled_window)));
      break;
    case PROP_VADJUSTMENT:
      g_value_set_object (value,
			  G_OBJECT (gtk_scrolled_window_get_vadjustment (scrolled_window)));
      break;
    case PROP_HSCROLLBAR_POLICY:
      g_value_set_enum (value, scrolled_window->hscrollbar_policy);
      break;
    case PROP_VSCROLLBAR_POLICY:
      g_value_set_enum (value, scrolled_window->vscrollbar_policy);
      break;
    case PROP_WINDOW_PLACEMENT:
      g_value_set_enum (value, scrolled_window->window_placement);
      break;
    case PROP_WINDOW_PLACEMENT_SET:
      g_value_set_boolean (value, priv->window_placement_set);
      break;
    case PROP_SHADOW_TYPE:
      g_value_set_enum (value, scrolled_window->shadow_type);
      break;
    case PROP_KINETIC_SCROLLING:
      g_value_set_boolean (value, priv->kinetic_scrolling);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
traverse_container (GtkWidget *widget,
		    gpointer   data)
{
  if (GTK_IS_SCROLLED_WINDOW (widget))
    {
      gtk_scrolled_window_update_real_placement (GTK_SCROLLED_WINDOW (widget));
      gtk_widget_queue_resize (widget);
    }
  else if (GTK_IS_CONTAINER (widget))
    gtk_container_forall (GTK_CONTAINER (widget), traverse_container, NULL);
}

static void
gtk_scrolled_window_settings_changed (GtkSettings *settings)
{
  GList *list, *l;

  list = gtk_window_list_toplevels ();

  for (l = list; l; l = l->next)
    gtk_container_forall (GTK_CONTAINER (l->data), 
			  traverse_container, NULL);

  g_list_free (list);
}

static void
gtk_scrolled_window_screen_changed (GtkWidget *widget,
				    GdkScreen *previous_screen)
{
  GtkSettings *settings;
  guint window_placement_connection;

  gtk_scrolled_window_update_real_placement (GTK_SCROLLED_WINDOW (widget));

  if (!gtk_widget_has_screen (widget))
    return;

  settings = gtk_widget_get_settings (widget);

  window_placement_connection = 
    GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (settings), 
					 "gtk-scrolled-window-connection"));
  
  if (window_placement_connection)
    return;

  window_placement_connection =
    g_signal_connect (settings, "notify::gtk-scrolled-window-placement",
		      G_CALLBACK (gtk_scrolled_window_settings_changed), NULL);
  g_object_set_data (G_OBJECT (settings), 
		     I_("gtk-scrolled-window-connection"),
		     GUINT_TO_POINTER (window_placement_connection));
}

static void
gtk_scrolled_window_paint (GtkWidget    *widget,
			   GdkRectangle *area)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkAllocation relative_allocation;

  if (scrolled_window->shadow_type != GTK_SHADOW_NONE)
    {
      gboolean scrollbars_within_bevel;

      gtk_widget_style_get (widget, "scrollbars-within-bevel", &scrollbars_within_bevel, NULL);
      
      if (!scrollbars_within_bevel)
        {
          gtk_scrolled_window_relative_allocation (widget, &relative_allocation);

          relative_allocation.x -= widget->style->xthickness;
          relative_allocation.y -= widget->style->ythickness;
          relative_allocation.width += 2 * widget->style->xthickness;
          relative_allocation.height += 2 * widget->style->ythickness;
        }
      else
        {
          GtkContainer *container = GTK_CONTAINER (widget);

          relative_allocation.x = container->border_width;
          relative_allocation.y = container->border_width;
          relative_allocation.width = widget->allocation.width - 2 * container->border_width;
          relative_allocation.height = widget->allocation.height - 2 * container->border_width;
        }

      gtk_paint_shadow (widget->style, widget->window,
			GTK_STATE_NORMAL, scrolled_window->shadow_type,
			area, widget, "scrolled_window",
			widget->allocation.x + relative_allocation.x,
			widget->allocation.y + relative_allocation.y,
			relative_allocation.width,
			relative_allocation.height);
    }
}

static gboolean
gtk_scrolled_window_expose (GtkWidget      *widget,
			    GdkEventExpose *event)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (widget);

  if (gtk_widget_is_drawable (widget))
    {
      GdkWindow *hscrollbar_window = NULL;
      GdkWindow *vscrollbar_window = NULL;

      if (scrolled_window->hscrollbar)
        hscrollbar_window = gtk_widget_get_window (scrolled_window->hscrollbar);

      if (scrolled_window->vscrollbar)
        vscrollbar_window = gtk_widget_get_window (scrolled_window->vscrollbar);

      if (event->window == priv->overshoot_window ||
          event->window == priv->vbackground_window ||
          event->window == priv->hbackground_window ||
          event->window == hscrollbar_window ||
          event->window == vscrollbar_window)
        GTK_WIDGET_CLASS (gtk_scrolled_window_parent_class)->expose_event (widget, event);
      else
        gtk_scrolled_window_paint (widget, &event->area);
    }

  return FALSE;
}

static void
gtk_scrolled_window_forall (GtkContainer *container,
			    gboolean	  include_internals,
			    GtkCallback   callback,
			    gpointer      callback_data)
{
  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (container));
  g_return_if_fail (callback != NULL);

  GTK_CONTAINER_CLASS (gtk_scrolled_window_parent_class)->forall (container,
					      include_internals,
					      callback,
					      callback_data);
  if (include_internals)
    {
      GtkScrolledWindow *scrolled_window;

      scrolled_window = GTK_SCROLLED_WINDOW (container);
      
      if (scrolled_window->vscrollbar)
	callback (scrolled_window->vscrollbar, callback_data);
      if (scrolled_window->hscrollbar)
	callback (scrolled_window->hscrollbar, callback_data);
    }
}

static gboolean
gtk_scrolled_window_scroll_child (GtkScrolledWindow *scrolled_window,
				  GtkScrollType      scroll,
				  gboolean           horizontal)
{
  GtkAdjustment *adjustment = NULL;
  
  switch (scroll)
    {
    case GTK_SCROLL_STEP_UP:
      scroll = GTK_SCROLL_STEP_BACKWARD;
      horizontal = FALSE;
      break;
    case GTK_SCROLL_STEP_DOWN:
      scroll = GTK_SCROLL_STEP_FORWARD;
      horizontal = FALSE;
      break;
    case GTK_SCROLL_STEP_LEFT:
      scroll = GTK_SCROLL_STEP_BACKWARD;
      horizontal = TRUE;
      break;
    case GTK_SCROLL_STEP_RIGHT:
      scroll = GTK_SCROLL_STEP_FORWARD;
      horizontal = TRUE;
      break;
    case GTK_SCROLL_PAGE_UP:
      scroll = GTK_SCROLL_PAGE_BACKWARD;
      horizontal = FALSE;
      break;
    case GTK_SCROLL_PAGE_DOWN:
      scroll = GTK_SCROLL_PAGE_FORWARD;
      horizontal = FALSE;
      break;
    case GTK_SCROLL_PAGE_LEFT:
      scroll = GTK_SCROLL_STEP_BACKWARD;
      horizontal = TRUE;
      break;
    case GTK_SCROLL_PAGE_RIGHT:
      scroll = GTK_SCROLL_STEP_FORWARD;
      horizontal = TRUE;
      break;
    case GTK_SCROLL_STEP_BACKWARD:
    case GTK_SCROLL_STEP_FORWARD:
    case GTK_SCROLL_PAGE_BACKWARD:
    case GTK_SCROLL_PAGE_FORWARD:
    case GTK_SCROLL_START:
    case GTK_SCROLL_END:
      break;
    default:
      g_warning ("Invalid scroll type %u for GtkScrolledWindow::scroll-child", scroll);
      return FALSE;
    }

  if ((horizontal && (!scrolled_window->hscrollbar || !scrolled_window->hscrollbar_visible)) ||
      (!horizontal && (!scrolled_window->vscrollbar || !scrolled_window->vscrollbar_visible)))
    return FALSE;

  if (horizontal)
    {
      if (scrolled_window->hscrollbar)
	adjustment = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar));
    }
  else
    {
      if (scrolled_window->vscrollbar)
	adjustment = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar));
    }

  if (adjustment)
    {
      gdouble value = adjustment->value;
      
      switch (scroll)
	{
	case GTK_SCROLL_STEP_FORWARD:
	  value += adjustment->step_increment;
	  break;
	case GTK_SCROLL_STEP_BACKWARD:
	  value -= adjustment->step_increment;
	  break;
	case GTK_SCROLL_PAGE_FORWARD:
	  value += adjustment->page_increment;
	  break;
	case GTK_SCROLL_PAGE_BACKWARD:
	  value -= adjustment->page_increment;
	  break;
	case GTK_SCROLL_START:
	  value = adjustment->lower;
	  break;
	case GTK_SCROLL_END:
	  value = adjustment->upper;
	  break;
	default:
	  g_assert_not_reached ();
	  break;
	}

      value = CLAMP (value, adjustment->lower, adjustment->upper - adjustment->page_size);
      
      gtk_adjustment_set_value (adjustment, value);

      return TRUE;
    }

  return FALSE;
}

static void
gtk_scrolled_window_move_focus_out (GtkScrolledWindow *scrolled_window,
				    GtkDirectionType   direction_type)
{
  GtkWidget *toplevel;
  
  /* Focus out of the scrolled window entirely. We do this by setting
   * a flag, then propagating the focus motion to the notebook.
   */
  toplevel = gtk_widget_get_toplevel (GTK_WIDGET (scrolled_window));
  if (!gtk_widget_is_toplevel (toplevel))
    return;

  g_object_ref (scrolled_window);
  
  scrolled_window->focus_out = TRUE;
  g_signal_emit_by_name (toplevel, "move-focus", direction_type);
  scrolled_window->focus_out = FALSE;
  
  g_object_unref (scrolled_window);
}

static void
gtk_scrolled_window_size_request (GtkWidget      *widget,
				  GtkRequisition *requisition)
{
  GtkScrolledWindow *scrolled_window;
  GtkBin *bin;
  gint extra_width;
  gint extra_height;
  gint scrollbar_spacing;
  GtkRequisition hscrollbar_requisition;
  GtkRequisition vscrollbar_requisition;
  GtkRequisition child_requisition;
  GtkScrolledWindowPrivate *priv;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (widget));
  g_return_if_fail (requisition != NULL);

  scrolled_window = GTK_SCROLLED_WINDOW (widget);
  bin = GTK_BIN (scrolled_window);
  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  scrollbar_spacing = _gtk_scrolled_window_get_scrollbar_spacing (scrolled_window);

  extra_width = 0;
  extra_height = 0;
  requisition->width = 0;
  requisition->height = 0;
  
  gtk_widget_size_request (scrolled_window->hscrollbar,
			   &hscrollbar_requisition);
  gtk_widget_size_request (scrolled_window->vscrollbar,
			   &vscrollbar_requisition);
  
  if (bin->child && gtk_widget_get_visible (bin->child))
    {
      gtk_widget_size_request (bin->child, &child_requisition);

      if (scrolled_window->hscrollbar_policy == GTK_POLICY_NEVER)
	requisition->width += child_requisition.width;
      else if (! priv->overlay_scrollbars)
	{
	  GtkWidgetAuxInfo *aux_info = _gtk_widget_get_aux_info (bin->child, FALSE);

	  if (aux_info && aux_info->width > 0)
	    {
	      requisition->width += aux_info->width;
	      extra_width = -1;
	    }
	  else
	    requisition->width += vscrollbar_requisition.width;
	}

      if (scrolled_window->vscrollbar_policy == GTK_POLICY_NEVER)
	requisition->height += child_requisition.height;
      else if (! priv->overlay_scrollbars)
	{
	  GtkWidgetAuxInfo *aux_info = _gtk_widget_get_aux_info (bin->child, FALSE);

	  if (aux_info && aux_info->height > 0)
	    {
	      requisition->height += aux_info->height;
	      extra_height = -1;
	    }
	  else
	    requisition->height += hscrollbar_requisition.height;
	}
    }

  if (! priv->overlay_scrollbars)
    {
      if (scrolled_window->hscrollbar_policy == GTK_POLICY_AUTOMATIC ||
          scrolled_window->hscrollbar_policy == GTK_POLICY_ALWAYS)
        {
          requisition->width = MAX (requisition->width, hscrollbar_requisition.width);
          if (!extra_height || scrolled_window->hscrollbar_policy == GTK_POLICY_ALWAYS)
            extra_height = scrollbar_spacing + hscrollbar_requisition.height;
        }

      if (scrolled_window->vscrollbar_policy == GTK_POLICY_AUTOMATIC ||
          scrolled_window->vscrollbar_policy == GTK_POLICY_ALWAYS)
        {
          requisition->height = MAX (requisition->height, vscrollbar_requisition.height);
          if (!extra_height || scrolled_window->vscrollbar_policy == GTK_POLICY_ALWAYS)
            extra_width = scrollbar_spacing + vscrollbar_requisition.width;
        }
    }

  requisition->width += GTK_CONTAINER (widget)->border_width * 2 + MAX (0, extra_width);
  requisition->height += GTK_CONTAINER (widget)->border_width * 2 + MAX (0, extra_height);

  if (scrolled_window->shadow_type != GTK_SHADOW_NONE)
    {
      requisition->width += 2 * widget->style->xthickness;
      requisition->height += 2 * widget->style->ythickness;
    }
}

static void
gtk_scrolled_window_relative_allocation (GtkWidget     *widget,
					 GtkAllocation *allocation)
{
  GtkScrolledWindow *scrolled_window;
  GtkScrolledWindowPrivate *priv;
  gint scrollbar_spacing;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (allocation != NULL);

  scrolled_window = GTK_SCROLLED_WINDOW (widget);
  scrollbar_spacing = _gtk_scrolled_window_get_scrollbar_spacing (scrolled_window);

  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  allocation->x = GTK_CONTAINER (widget)->border_width;
  allocation->y = GTK_CONTAINER (widget)->border_width;

  if (scrolled_window->shadow_type != GTK_SHADOW_NONE)
    {
      allocation->x += widget->style->xthickness;
      allocation->y += widget->style->ythickness;
    }
  
  allocation->width = MAX (1, (gint)widget->allocation.width - allocation->x * 2);
  allocation->height = MAX (1, (gint)widget->allocation.height - allocation->y * 2);

  if (priv->overlay_scrollbars)
    return;

  if (scrolled_window->vscrollbar_visible)
    {
      GtkRequisition vscrollbar_requisition;
      gboolean is_rtl;

      gtk_widget_get_child_requisition (scrolled_window->vscrollbar,
					&vscrollbar_requisition);
      is_rtl = gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL;
  
      if ((!is_rtl && 
	   (priv->real_window_placement == GTK_CORNER_TOP_RIGHT ||
	    priv->real_window_placement == GTK_CORNER_BOTTOM_RIGHT)) ||
	  (is_rtl && 
	   (priv->real_window_placement == GTK_CORNER_TOP_LEFT ||
	    priv->real_window_placement == GTK_CORNER_BOTTOM_LEFT)))
	allocation->x += (vscrollbar_requisition.width +  scrollbar_spacing);

      allocation->width = MAX (1, allocation->width - (vscrollbar_requisition.width + scrollbar_spacing));
    }
  if (scrolled_window->hscrollbar_visible)
    {
      GtkRequisition hscrollbar_requisition;
      gtk_widget_get_child_requisition (scrolled_window->hscrollbar,
					&hscrollbar_requisition);
  
      if (priv->real_window_placement == GTK_CORNER_BOTTOM_LEFT ||
	  priv->real_window_placement == GTK_CORNER_BOTTOM_RIGHT)
	allocation->y += (hscrollbar_requisition.height + scrollbar_spacing);

      allocation->height = MAX (1, allocation->height - (hscrollbar_requisition.height + scrollbar_spacing));
    }
}

static gboolean
_gtk_scrolled_window_get_overshoot (GtkScrolledWindow *scrolled_window,
                                    gint              *overshoot_x,
                                    gint              *overshoot_y)
{
  GtkScrolledWindowPrivate *priv;
  GtkAdjustment *vadjustment, *hadjustment;
  gdouble lower, upper, x, y;

  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  /* Vertical overshoot */
  vadjustment = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar));
  lower = gtk_adjustment_get_lower (vadjustment);
  upper = gtk_adjustment_get_upper (vadjustment) -
    gtk_adjustment_get_page_size (vadjustment);

  if (priv->unclamped_vadj_value < lower)
    y = priv->unclamped_vadj_value - lower;
  else if (priv->unclamped_vadj_value > upper)
    y = priv->unclamped_vadj_value - upper;
  else
    y = 0;

  /* Horizontal overshoot */
  hadjustment = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar));
  lower = gtk_adjustment_get_lower (hadjustment);
  upper = gtk_adjustment_get_upper (hadjustment) -
    gtk_adjustment_get_page_size (hadjustment);

  if (priv->unclamped_hadj_value < lower)
    x = priv->unclamped_hadj_value - lower;
  else if (priv->unclamped_hadj_value > upper)
    x = priv->unclamped_hadj_value - upper;
  else
    x = 0;

  if (overshoot_x)
    *overshoot_x = x;

  if (overshoot_y)
    *overshoot_y = y;

  return (x != 0 || y != 0);
}

static void
_gtk_scrolled_window_allocate_overshoot_window (GtkScrolledWindow *scrolled_window)
{
  GtkAllocation window_allocation, relative_allocation, allocation;
  GtkScrolledWindowPrivate *priv;
  GtkWidget *widget = GTK_WIDGET (scrolled_window);
  gint overshoot_x, overshoot_y;

  if (!gtk_widget_get_realized (widget))
    return;

  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  gtk_widget_get_allocation (widget, &allocation);
  gtk_scrolled_window_relative_allocation (widget, &relative_allocation);
  _gtk_scrolled_window_get_overshoot (scrolled_window,
                                      &overshoot_x, &overshoot_y);

  /* Overshoot window */
  window_allocation = relative_allocation;
  window_allocation.x += allocation.x;
  window_allocation.y += allocation.y;

  if (overshoot_x < 0)
    window_allocation.x += -overshoot_x;

  if (overshoot_y < 0)
    window_allocation.y += -overshoot_y;

  window_allocation.width -= ABS (overshoot_x);
  window_allocation.height -= ABS (overshoot_y);

  gdk_window_move_resize (priv->overshoot_window,
                          window_allocation.x, window_allocation.y,
                          window_allocation.width, window_allocation.height);

  /* Vertical background window */
  window_allocation = relative_allocation;
  window_allocation.x += allocation.x;
  window_allocation.y += allocation.y;

  if (ABS (overshoot_x) > 0)
    {
      window_allocation.width = ABS (overshoot_x);
      if (overshoot_x > 0)
        window_allocation.x += relative_allocation.width - overshoot_x;

      gdk_window_move_resize (priv->vbackground_window,
                              window_allocation.x, window_allocation.y,
                              window_allocation.width,
                              window_allocation.height);
      gdk_window_show (priv->vbackground_window);
    }
  else
    gdk_window_hide (priv->vbackground_window);

  /* Horizontal background window */
  window_allocation = relative_allocation;
  window_allocation.x += allocation.x;
  window_allocation.y += allocation.y;

  if (ABS (overshoot_y) > 0)
    {
      window_allocation.height = ABS (overshoot_y);
      if (overshoot_y > 0)
        window_allocation.y += relative_allocation.height - overshoot_y;

      gdk_window_move_resize (priv->hbackground_window,
                              window_allocation.x, window_allocation.y,
                              window_allocation.width,
                              window_allocation.height);
      gdk_window_show (priv->hbackground_window);
    }
  else
    gdk_window_hide (priv->hbackground_window);
}

static void
gtk_scrolled_window_allocate_child (GtkScrolledWindow *swindow,
				    GtkAllocation     *relative_allocation)
{
  GtkWidget     *widget = GTK_WIDGET (swindow), *child;
  GtkAllocation  allocation;
  GtkAllocation  child_allocation;
  gint           overshoot_x, overshoot_y;

  child = gtk_bin_get_child (GTK_BIN (widget));

  gtk_widget_get_allocation (widget, &allocation);

  gtk_scrolled_window_relative_allocation (widget, relative_allocation);
  _gtk_scrolled_window_get_overshoot (swindow, &overshoot_x, &overshoot_y);

  child_allocation.x = child_allocation.y = 0;
  child_allocation.width = relative_allocation->width;
  child_allocation.height = relative_allocation->height;

  gtk_widget_size_allocate (child, &child_allocation);
}

static void
gtk_scrolled_window_size_allocate (GtkWidget     *widget,
				   GtkAllocation *allocation)
{
  GtkScrolledWindow *scrolled_window;
  GtkScrolledWindowPrivate *priv;
  GtkBin *bin;
  GtkAllocation relative_allocation;
  GtkAllocation child_allocation;
  gboolean scrollbars_within_bevel;
  gint scrollbar_spacing;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (widget));
  g_return_if_fail (allocation != NULL);

  scrolled_window = GTK_SCROLLED_WINDOW (widget);
  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  bin = GTK_BIN (scrolled_window);

  gtk_scrolled_window_expose_scrollbars (NULL, scrolled_window);

  scrollbar_spacing = _gtk_scrolled_window_get_scrollbar_spacing (scrolled_window);
  gtk_widget_style_get (widget, "scrollbars-within-bevel", &scrollbars_within_bevel, NULL);

  widget->allocation = *allocation;

  if (scrolled_window->hscrollbar_policy == GTK_POLICY_ALWAYS)
    scrolled_window->hscrollbar_visible = TRUE;
  else if (scrolled_window->hscrollbar_policy == GTK_POLICY_NEVER)
    scrolled_window->hscrollbar_visible = FALSE;
  if (scrolled_window->vscrollbar_policy == GTK_POLICY_ALWAYS)
    scrolled_window->vscrollbar_visible = TRUE;
  else if (scrolled_window->vscrollbar_policy == GTK_POLICY_NEVER)
    scrolled_window->vscrollbar_visible = FALSE;

  if (bin->child && gtk_widget_get_visible (bin->child))
    {
      gboolean previous_hvis;
      gboolean previous_vvis;
      guint count = 0;
      
      do
	{
	  previous_hvis = scrolled_window->hscrollbar_visible;
	  previous_vvis = scrolled_window->vscrollbar_visible;
          gtk_scrolled_window_allocate_child (scrolled_window, &relative_allocation);

	  /* If, after the first iteration, the hscrollbar and the
	   * vscrollbar flip visiblity, then we need both.
	   */
	  if (count &&
	      previous_hvis != scrolled_window->hscrollbar_visible &&
	      previous_vvis != scrolled_window->vscrollbar_visible)
	    {
	      scrolled_window->hscrollbar_visible = TRUE;
	      scrolled_window->vscrollbar_visible = TRUE;

              gtk_scrolled_window_allocate_child (scrolled_window, &relative_allocation);

	      /* a new resize is already queued at this point,
	       * so we will immediatedly get reinvoked
	       */
	      return;
	    }
	  
	  count++;
	}
      while (previous_hvis != scrolled_window->hscrollbar_visible ||
	     previous_vvis != scrolled_window->vscrollbar_visible);
    }
  else
    {
      scrolled_window->hscrollbar_visible = scrolled_window->hscrollbar_policy == GTK_POLICY_ALWAYS;
      scrolled_window->vscrollbar_visible = scrolled_window->vscrollbar_policy == GTK_POLICY_ALWAYS;
      gtk_scrolled_window_relative_allocation (widget, &relative_allocation);
    }
  
  if (!priv->overlay_scrollbars && scrolled_window->hscrollbar_visible)
    {
      GtkRequisition hscrollbar_requisition;
      gtk_widget_get_child_requisition (scrolled_window->hscrollbar,
					&hscrollbar_requisition);
  
      if (!gtk_widget_get_visible (scrolled_window->hscrollbar))
	gtk_widget_show (scrolled_window->hscrollbar);

      child_allocation.x = relative_allocation.x;
      if (priv->real_window_placement == GTK_CORNER_TOP_LEFT ||
	  priv->real_window_placement == GTK_CORNER_TOP_RIGHT)
	child_allocation.y = (relative_allocation.y +
			      relative_allocation.height +
			      scrollbar_spacing +
			      (scrolled_window->shadow_type == GTK_SHADOW_NONE ?
			       0 : widget->style->ythickness));
      else
	child_allocation.y = GTK_CONTAINER (scrolled_window)->border_width;

      child_allocation.width = relative_allocation.width;
      child_allocation.height = hscrollbar_requisition.height;
      child_allocation.x += allocation->x;
      child_allocation.y += allocation->y;

      if (scrolled_window->shadow_type != GTK_SHADOW_NONE)
	{
          if (!scrollbars_within_bevel)
            {
              child_allocation.x -= widget->style->xthickness;
              child_allocation.width += 2 * widget->style->xthickness;
            }
          else if (GTK_CORNER_TOP_RIGHT == priv->real_window_placement ||
                   GTK_CORNER_TOP_LEFT == priv->real_window_placement)
            {
              child_allocation.y -= widget->style->ythickness;
            }
          else
            {
              child_allocation.y += widget->style->ythickness;
            }
	}

      gtk_widget_size_allocate (scrolled_window->hscrollbar, &child_allocation);
    }
  else if (gtk_widget_get_visible (scrolled_window->hscrollbar))
    gtk_widget_hide (scrolled_window->hscrollbar);

  if (!priv->overlay_scrollbars && scrolled_window->vscrollbar_visible)
    {
      GtkRequisition vscrollbar_requisition;
      if (!gtk_widget_get_visible (scrolled_window->vscrollbar))
	gtk_widget_show (scrolled_window->vscrollbar);

      gtk_widget_get_child_requisition (scrolled_window->vscrollbar,
					&vscrollbar_requisition);

      if ((gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL && 
	   (priv->real_window_placement == GTK_CORNER_TOP_RIGHT ||
	    priv->real_window_placement == GTK_CORNER_BOTTOM_RIGHT)) ||
	  (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_LTR && 
	   (priv->real_window_placement == GTK_CORNER_TOP_LEFT ||
	    priv->real_window_placement == GTK_CORNER_BOTTOM_LEFT)))
	child_allocation.x = (relative_allocation.x +
			      relative_allocation.width +
			      scrollbar_spacing +
			      (scrolled_window->shadow_type == GTK_SHADOW_NONE ?
			       0 : widget->style->xthickness));
      else
	child_allocation.x = GTK_CONTAINER (scrolled_window)->border_width;

      child_allocation.y = relative_allocation.y;
      child_allocation.width = vscrollbar_requisition.width;
      child_allocation.height = relative_allocation.height;
      child_allocation.x += allocation->x;
      child_allocation.y += allocation->y;

      if (scrolled_window->shadow_type != GTK_SHADOW_NONE)
	{
          if (!scrollbars_within_bevel)
            {
              child_allocation.y -= widget->style->ythickness;
	      child_allocation.height += 2 * widget->style->ythickness;
            }
          else if (GTK_CORNER_BOTTOM_LEFT == priv->real_window_placement ||
                   GTK_CORNER_TOP_LEFT == priv->real_window_placement)
            {
              child_allocation.x -= widget->style->xthickness;
            }
          else
            {
              child_allocation.x += widget->style->xthickness;
            }
	}

      gtk_widget_size_allocate (scrolled_window->vscrollbar, &child_allocation);
    }
  else if (gtk_widget_get_visible (scrolled_window->vscrollbar))
    gtk_widget_hide (scrolled_window->vscrollbar);

  _gtk_scrolled_window_allocate_overshoot_window (scrolled_window);
}

static gboolean
gtk_scrolled_window_scroll_event (GtkWidget      *widget,
				  GdkEventScroll *event)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  gboolean handled = FALSE;
  gdouble delta_x;
  gdouble delta_y;

  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);  

  if (gdk_event_get_scroll_deltas ((GdkEvent *) event, &delta_x, &delta_y))
    {
      if (delta_x != 0.0 && scrolled_window->hscrollbar &&
          (priv->overlay_scrollbars || gtk_widget_get_visible (scrolled_window->hscrollbar)))
        {
          GtkAdjustment *adj;
          gdouble new_value;

          adj = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar));

          new_value = CLAMP (gtk_adjustment_get_value (adj) + delta_x,
                             gtk_adjustment_get_lower (adj),
                             gtk_adjustment_get_upper (adj) -
                             gtk_adjustment_get_page_size (adj));

          gtk_adjustment_set_value (adj, new_value);

          handled = TRUE;
        }

      if (delta_y != 0.0 && scrolled_window->vscrollbar &&
          (priv->overlay_scrollbars || gtk_widget_get_visible (scrolled_window->vscrollbar)))
        {
          GtkAdjustment *adj;
          gdouble new_value;

          adj = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar));

          new_value = CLAMP (gtk_adjustment_get_value (adj) + delta_y,
                             gtk_adjustment_get_lower (adj),
                             gtk_adjustment_get_upper (adj) -
                             gtk_adjustment_get_page_size (adj));

          gtk_adjustment_set_value (adj, new_value);

          handled = TRUE;
        }
    }
  else
    {
      GtkWidget *range;

      if (event->direction == GDK_SCROLL_UP || event->direction == GDK_SCROLL_DOWN)
        range = scrolled_window->vscrollbar;
      else
        range = scrolled_window->hscrollbar;

      if (range && (priv->overlay_scrollbars || gtk_widget_get_visible (range)))
        {
          GtkAdjustment *adj = GTK_RANGE (range)->adjustment;
          gdouble delta, new_value;

          delta = _gtk_range_get_wheel_delta (GTK_RANGE (range), event);

          new_value = CLAMP (adj->value + delta, adj->lower, adj->upper - adj->page_size);

          gtk_adjustment_set_value (adj, new_value);

          handled = TRUE;
        }
    }

  return handled;
}

static gboolean
_gtk_scrolled_window_set_adjustment_value (GtkScrolledWindow *scrolled_window,
                                           GtkAdjustment     *adjustment,
                                           gdouble            value,
                                           gboolean           allow_overshooting,
                                           gboolean           snap_to_border)
{
  GtkScrolledWindowPrivate *priv;
  gdouble lower, upper, *prev_value;

  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  lower = gtk_adjustment_get_lower (adjustment);
  upper = gtk_adjustment_get_upper (adjustment) -
    gtk_adjustment_get_page_size (adjustment);

  if (adjustment == gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar)))
    prev_value = &priv->unclamped_hadj_value;
  else if (adjustment == gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar)))
    prev_value = &priv->unclamped_vadj_value;
  else
    return FALSE;

  if (snap_to_border)
    {
      if (*prev_value < 0 && value > 0)
        value = 0;
      else if (*prev_value > upper && value < upper)
        value = upper;
    }

  if (allow_overshooting)
    {
      lower -= MAX_OVERSHOOT_DISTANCE;
      upper += MAX_OVERSHOOT_DISTANCE;
    }

  *prev_value = CLAMP (value, lower, upper);
  gtk_adjustment_set_value (adjustment, *prev_value);

  return (*prev_value != value);
}

static gboolean
scrolled_window_deceleration_cb (gpointer user_data)
{
  KineticScrollData *data = user_data;
  GtkScrolledWindow *scrolled_window = data->scrolled_window;
  GtkScrolledWindowPrivate *priv;
  GtkAdjustment *hadjustment, *vadjustment;
  gint old_overshoot_x, old_overshoot_y, overshoot_x, overshoot_y;
  gdouble value;
  gint64 current_time;
  guint elapsed;

  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  hadjustment = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar));
  vadjustment = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar));

  _gtk_scrolled_window_get_overshoot (scrolled_window,
                                      &old_overshoot_x, &old_overshoot_y);

  current_time = g_get_monotonic_time ();
  elapsed = (current_time - data->last_deceleration_time) / 1000;
  data->last_deceleration_time = current_time;

  if (hadjustment && scrolled_window->hscrollbar_visible)
    {
      value = priv->unclamped_hadj_value + (data->x_velocity * elapsed);

      if (_gtk_scrolled_window_set_adjustment_value (scrolled_window,
                                                     hadjustment,
                                                     value, TRUE, TRUE))
        data->x_velocity = 0;
    }
  else
    data->x_velocity = 0;

  if (vadjustment && scrolled_window->vscrollbar_visible)
    {
      value = priv->unclamped_vadj_value + (data->y_velocity * elapsed);

      if (_gtk_scrolled_window_set_adjustment_value (scrolled_window,
                                                     vadjustment,
                                                     value, TRUE, TRUE))
        data->y_velocity = 0;
    }
  else
    data->y_velocity = 0;

  _gtk_scrolled_window_get_overshoot (scrolled_window,
                                      &overshoot_x, &overshoot_y);

  if (overshoot_x == 0)
    {
      if (old_overshoot_x != 0)
        {
          /* Overshooting finished snapping back */
          data->x_velocity = 0;
        }
      else if (data->x_velocity > 0)
        {
          data->x_velocity -= FRICTION_DECELERATION * elapsed * data->vel_sine;
          data->x_velocity = MAX (0, data->x_velocity);
        }
      else if (data->x_velocity < 0)
        {
          data->x_velocity += FRICTION_DECELERATION * elapsed * data->vel_sine;
          data->x_velocity = MIN (0, data->x_velocity);
        }
    }
  else if (overshoot_x < 0)
    data->x_velocity += OVERSHOOT_INVERSE_ACCELERATION * elapsed;
  else if (overshoot_x > 0)
    data->x_velocity -= OVERSHOOT_INVERSE_ACCELERATION * elapsed;

  if (overshoot_y == 0)
    {
      if (old_overshoot_y != 0)
        {
          /* Overshooting finished snapping back */
          data->y_velocity = 0;
        }
      else if (data->y_velocity > 0)
        {
          data->y_velocity -= FRICTION_DECELERATION * elapsed * data->vel_cosine;
          data->y_velocity = MAX (0, data->y_velocity);
        }
      else if (data->y_velocity < 0)
        {
          data->y_velocity += FRICTION_DECELERATION * elapsed * data->vel_cosine;
          data->y_velocity = MIN (0, data->y_velocity);
        }
    }
  else if (overshoot_y < 0)
    data->y_velocity += OVERSHOOT_INVERSE_ACCELERATION * elapsed;
  else if (overshoot_y > 0)
    data->y_velocity -= OVERSHOOT_INVERSE_ACCELERATION * elapsed;

  if (old_overshoot_x != overshoot_x ||
      old_overshoot_y != overshoot_y)
    _gtk_scrolled_window_allocate_overshoot_window (scrolled_window);

  if (overshoot_x != 0 || overshoot_y != 0 ||
      data->x_velocity != 0 || data->y_velocity != 0)
    return TRUE;
  else
    {
      priv->deceleration_id = 0;
      return FALSE;
    }
}

static void
gtk_scrolled_window_cancel_deceleration (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  if (priv->deceleration_id)
    {
      g_source_remove (priv->deceleration_id);
      priv->deceleration_id = 0;
    }
}

static void
gtk_scrolled_window_start_deceleration (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  KineticScrollData *data;
  gdouble angle;

  data = g_new0 (KineticScrollData, 1);
  data->scrolled_window = scrolled_window;
  data->last_deceleration_time = g_get_monotonic_time ();
  data->x_velocity = priv->x_velocity;
  data->y_velocity = priv->y_velocity;

  /* We use sine/cosine as a factor to deceleration x/y components
   * of the vector, so we care about the sign later.
   */
  angle = atan2 (ABS (data->x_velocity), ABS (data->y_velocity));
  data->vel_cosine = cos (angle);
  data->vel_sine = sin (angle);

  priv->deceleration_id =
    gdk_threads_add_timeout_full (G_PRIORITY_DEFAULT,
                                  FRAME_INTERVAL,
                                  scrolled_window_deceleration_cb,
                                  data, (GDestroyNotify) g_free);
}

static gboolean
gtk_scrolled_window_release_captured_event (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  /* Cancel the scrolling and send the button press
   * event to the child widget
   */
  if (!priv->button_press_event)
    return FALSE;

  if (priv->pointer_grabbed)
    {
      gtk_grab_remove (GTK_WIDGET (scrolled_window));
      priv->pointer_grabbed = FALSE;
    }

  if (priv->capture_button_press)
    {
      GtkWidget *event_widget;

      event_widget = gtk_get_event_widget (priv->button_press_event);

      if (!_gtk_propagate_captured_event (event_widget,
                                          priv->button_press_event,
                                          gtk_bin_get_child (GTK_BIN (scrolled_window))))
        gtk_propagate_event (event_widget, priv->button_press_event);

      gdk_event_free (priv->button_press_event);
      priv->button_press_event = NULL;
    }

  if (_gtk_scrolled_window_get_overshoot (scrolled_window, NULL, NULL))
    gtk_scrolled_window_start_deceleration (scrolled_window);

  return FALSE;
}

static gboolean
gtk_scrolled_window_calculate_velocity (GtkScrolledWindow *scrolled_window,
					GdkEvent          *event)
{
  GtkScrolledWindowPrivate *priv;
  gdouble x_root, y_root;
  guint32 _time;

#define STILL_THRESHOLD 40

  if (!gdk_event_get_root_coords (event, &x_root, &y_root))
    return FALSE;

  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  _time = gdk_event_get_time (event);

  if (priv->last_motion_event_x_root != x_root ||
      priv->last_motion_event_y_root != y_root ||
      ABS (_time - priv->last_motion_event_time) > STILL_THRESHOLD)
    {
      priv->x_velocity = (priv->last_motion_event_x_root - x_root) /
        (gdouble) (_time - priv->last_motion_event_time);
      priv->y_velocity = (priv->last_motion_event_y_root - y_root) /
        (gdouble) (_time - priv->last_motion_event_time);
    }

  priv->last_motion_event_x_root = x_root;
  priv->last_motion_event_y_root = y_root;
  priv->last_motion_event_time = _time;

#undef STILL_THRESHOLD

  return TRUE;
}

static gboolean
gtk_scrolled_window_captured_button_release_kinetic (GtkWidget *widget,
                                                     GdkEvent  *event)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv;
  GtkWidget *child;
  gboolean overshoot;
  gdouble x_root, y_root;

  if (event->button.button != 1)
    return FALSE;

  child = gtk_bin_get_child (GTK_BIN (widget));
  if (!child)
    return FALSE;

  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  gtk_grab_remove (widget);
  priv->pointer_grabbed = FALSE;

  if (priv->release_timeout_id)
    {
      g_source_remove (priv->release_timeout_id);
      priv->release_timeout_id = 0;
    }

  overshoot = _gtk_scrolled_window_get_overshoot (scrolled_window, NULL, NULL);

  if (priv->in_drag)
    gdk_pointer_ungrab (gdk_event_get_time (event));
  else
    {
      /* There hasn't been scrolling at all, so just let the
       * child widget handle the button press normally
       */
      gtk_scrolled_window_release_captured_event (scrolled_window);

      if (!overshoot)
        return FALSE;
    }
  priv->in_drag = FALSE;

  if (priv->button_press_event)
    {
      gdk_event_free (priv->button_press_event);
      priv->button_press_event = NULL;
    }

  gtk_scrolled_window_calculate_velocity (scrolled_window, event);

  /* Zero out vector components without a visible scrollbar */
  if (!scrolled_window->hscrollbar_visible)
    priv->x_velocity = 0;
  if (!scrolled_window->vscrollbar_visible)
    priv->y_velocity = 0;

  if (priv->x_velocity != 0 || priv->y_velocity != 0 || overshoot)
    {
      gtk_scrolled_window_start_deceleration (scrolled_window);
      priv->x_velocity = priv->y_velocity = 0;
      priv->last_button_event_valid = FALSE;
    }
  else
    {
      gdk_event_get_root_coords (event, &x_root, &y_root);
      priv->last_button_event_x_root = x_root;
      priv->last_button_event_y_root = y_root;
      priv->last_button_event_valid = TRUE;
    }

  if (priv->capture_button_press)
    return TRUE;
  else
    return FALSE;
}

static gboolean
gtk_scrolled_window_captured_motion_notify_kinetic (GtkWidget *widget,
                                                    GdkEvent  *event)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv;
  gint old_overshoot_x, old_overshoot_y;
  gint new_overshoot_x, new_overshoot_y;
  GtkWidget *child;
  GtkAdjustment *hadjustment;
  GtkAdjustment *vadjustment;
  gdouble dx, dy;
  GdkModifierType state;
  gdouble x_root, y_root;

  gdk_event_get_state (event, &state);
  if (!(state & GDK_BUTTON1_MASK))
    return FALSE;

  child = gtk_bin_get_child (GTK_BIN (widget));
  if (!child)
    return FALSE;

  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  /* Check if we've passed the drag threshold */
  gdk_event_get_root_coords (event, &x_root, &y_root);
  if (!priv->in_drag)
    {
      if (gtk_drag_check_threshold (widget,
                                    priv->last_button_event_x_root,
                                    priv->last_button_event_y_root,
                                    x_root, y_root))
        {
          if (priv->release_timeout_id)
            {
              g_source_remove (priv->release_timeout_id);
              priv->release_timeout_id = 0;
            }

          priv->last_button_event_valid = FALSE;
          priv->in_drag = TRUE;
        }
      else
        return TRUE;
    }

  gdk_pointer_grab (gtk_widget_get_window (widget),
                    TRUE,
                    GDK_BUTTON_RELEASE_MASK | GDK_BUTTON1_MOTION_MASK,
                    NULL, NULL,
                    gdk_event_get_time (event));

  priv->last_button_event_valid = FALSE;

  if (priv->button_press_event)
    {
      gdk_event_free (priv->button_press_event);
      priv->button_press_event = NULL;
    }

  _gtk_scrolled_window_get_overshoot (scrolled_window,
                                      &old_overshoot_x, &old_overshoot_y);

  hadjustment = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar));
  if (hadjustment && scrolled_window->hscrollbar_visible)
    {
      dx = (priv->last_motion_event_x_root - x_root) + priv->unclamped_hadj_value;
      _gtk_scrolled_window_set_adjustment_value (scrolled_window, hadjustment,
                                                 dx, TRUE, FALSE);
    }

  vadjustment = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar));
  if (vadjustment && scrolled_window->vscrollbar_visible)
    {
      dy = (priv->last_motion_event_y_root - y_root) + priv->unclamped_vadj_value;
      _gtk_scrolled_window_set_adjustment_value (scrolled_window, vadjustment,
                                                 dy, TRUE, FALSE);
    }

  _gtk_scrolled_window_get_overshoot (scrolled_window,
                                      &new_overshoot_x, &new_overshoot_y);

  if (old_overshoot_x != new_overshoot_x ||
      old_overshoot_y != new_overshoot_y)
    _gtk_scrolled_window_allocate_overshoot_window (scrolled_window);

  gtk_scrolled_window_calculate_velocity (scrolled_window, event);

  return TRUE;
}

static gboolean
gtk_scrolled_window_captured_button_press_kinetic (GtkWidget *widget,
                                                   GdkEvent  *event)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv;
  GtkWidget *child;
  GtkWidget *event_widget;
  gdouble x_root, y_root;

  /* If scrollbars are not visible, we don't do kinetic scrolling */
  if (!scrolled_window->vscrollbar_visible &&
      !scrolled_window->hscrollbar_visible)
    return FALSE;

  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  event_widget = gtk_get_event_widget (event);

  /* If there's another scrolled window between the widget
   * receiving the event and this capturing scrolled window,
   * let it handle the events.
   */
  if (widget != gtk_widget_get_ancestor (event_widget, GTK_TYPE_SCROLLED_WINDOW))
    return FALSE;

  /* Check whether the button press is close to the previous one,
   * take that as a shortcut to get the child widget handle events
   */
  gdk_event_get_root_coords (event, &x_root, &y_root);
  if (priv->last_button_event_valid &&
      ABS (x_root - priv->last_button_event_x_root) < TOUCH_BYPASS_CAPTURED_THRESHOLD &&
      ABS (y_root - priv->last_button_event_y_root) < TOUCH_BYPASS_CAPTURED_THRESHOLD)
    {
      priv->last_button_event_valid = FALSE;
      return FALSE;
    }

  priv->last_button_event_x_root = priv->last_motion_event_x_root = x_root;
  priv->last_button_event_y_root = priv->last_motion_event_y_root = y_root;
  priv->last_motion_event_time = gdk_event_get_time (event);
  priv->last_button_event_valid = TRUE;

  if (event->button.button != 1)
    return FALSE;

  child = gtk_bin_get_child (GTK_BIN (widget));
  if (!child)
    return FALSE;

  if (scrolled_window->hscrollbar == event_widget ||
      scrolled_window->vscrollbar == event_widget)
    return FALSE;

  priv->pointer_grabbed = TRUE;
  gtk_grab_add (widget);

  gtk_scrolled_window_cancel_deceleration (scrolled_window);

  /* Only set the timeout if we're going to store an event */
  if (priv->capture_button_press)
    priv->release_timeout_id =
      gdk_threads_add_timeout (RELEASE_EVENT_TIMEOUT,
                               (GSourceFunc) gtk_scrolled_window_release_captured_event,
                               scrolled_window);

  priv->in_drag = FALSE;

  if (priv->capture_button_press)
    {
      /* Store the button press event in
       * case we need to propagate it later
       */
      priv->button_press_event = gdk_event_copy (event);
      return TRUE;
    }
  else
    return FALSE;
}

static void
gtk_scrolled_window_scroll_step (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  GtkAdjustment *adj;
  gdouble value;

  if (priv->sb_grab_vscroll)
    {
      adj = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar));
    }
  else if (priv->sb_grab_hscroll)
    {
      adj = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar));
    }

  value = adj->value + (priv->sb_scroll_direction * adj->page_size);
  value = CLAMP (value, adj->lower, adj->upper - adj->page_size);

  gtk_adjustment_set_value (adj, value);
}

static gboolean
gtk_scrolled_window_scroll_step_timeout (gpointer data)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (data);
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  gtk_scrolled_window_scroll_step (scrolled_window);

  g_source_remove (priv->sb_scroll_timeout_id);

  priv->sb_scroll_timeout_id =
    gdk_threads_add_timeout (SCROLL_INTERVAL_REPEAT,
                             gtk_scrolled_window_scroll_step_timeout,
                             scrolled_window);

  return FALSE;
}

static gboolean
gtk_scrolled_window_captured_motion_notify_scrollbar (GtkWidget *widget,
                                                      GdkEvent  *event);

static gboolean
gtk_scrolled_window_captured_button_press_scrollbar (GtkWidget *widget,
                                                     GdkEvent  *event)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  GdkEventButton *bevent = (GdkEventButton *) event;

  if (bevent->button != 1)
    return FALSE;

  if (gtk_scrolled_window_over_child_scroll_areas (scrolled_window, event,
                                                   bevent->x, bevent->y,
                                                   &priv->sb_grab_vscroll,
                                                   &priv->sb_grab_hscroll))
    {
      GdkRectangle vbar_rect;
      GdkRectangle vslider_rect;
      GdkRectangle hbar_rect;
      GdkRectangle hslider_rect;

      priv->sb_pointer_grabbed = TRUE;
      gtk_grab_add (widget);

      gtk_scrolled_window_get_child_scroll_areas (scrolled_window,
                                                  gtk_bin_get_child (GTK_BIN (widget)),
                                                  bevent->window,
                                                  &vbar_rect, &vslider_rect,
                                                  &hbar_rect, &hslider_rect);

      if (priv->sb_grab_vscroll)
        {
          /* we consider the entire width of the scrollbar clickable */
          vslider_rect.x = vbar_rect.x;
          vslider_rect.width = vbar_rect.width;

          if (bevent->x >= vslider_rect.x &&
              bevent->x < (vslider_rect.x + vslider_rect.width) &&
              bevent->y >= vslider_rect.y &&
              bevent->y < (vslider_rect.y + vslider_rect.height))
            {
              priv->sb_drag_slider = TRUE;
              priv->sb_grab_offset_y = bevent->y - vslider_rect.y;
            }
          else
            {
              priv->sb_drag_slider = FALSE;
              priv->sb_grab_offset_y = bevent->y - vbar_rect.y;

              if (bevent->y < vslider_rect.y)
                priv->sb_scroll_direction = -1;
              else
                priv->sb_scroll_direction = 1;
            }
        }
      else if (priv->sb_grab_hscroll)
        {
          /* we consider the entire height of the scrollbar clickable */
          hslider_rect.y = hbar_rect.y;
          hslider_rect.height = hbar_rect.height;

          if (bevent->x >= hslider_rect.x &&
              bevent->x < (hslider_rect.x + hslider_rect.width) &&
              bevent->y >= hslider_rect.y &&
              bevent->y < (hslider_rect.y + hslider_rect.height))
            {
              priv->sb_drag_slider = TRUE;
              priv->sb_grab_offset_x = bevent->x - hslider_rect.x;
            }
          else
            {
              priv->sb_drag_slider = FALSE;
              priv->sb_grab_offset_x = bevent->x - hbar_rect.x;

              if (bevent->x < hslider_rect.x)
                priv->sb_scroll_direction = -1;
              else
                priv->sb_scroll_direction = 1;
            }
        }

      if ((priv->sb_grab_vscroll || priv->sb_grab_hscroll) &&
          !priv->sb_drag_slider)
        {
          gboolean primary_warps;

          g_object_get (gtk_widget_get_settings (widget),
                        "gtk-primary-button-warps-slider", &primary_warps,
                        NULL);

          if (primary_warps)
            {
              GdkEventMotion mevent = { 0, };

              priv->sb_drag_slider = TRUE;
              priv->sb_grab_offset_x = hslider_rect.width / 2;
              priv->sb_grab_offset_y = vslider_rect.height / 2;

              mevent.window = bevent->window;
              mevent.x = bevent->x;
              mevent.y = bevent->y;

              gtk_scrolled_window_captured_motion_notify_scrollbar (widget,
                                                                    (GdkEvent *) &mevent);
            }
          else
            {
              gtk_scrolled_window_scroll_step (scrolled_window);

              priv->sb_scroll_timeout_id =
                gdk_threads_add_timeout (SCROLL_INTERVAL_INITIAL,
                                         gtk_scrolled_window_scroll_step_timeout,
                                         scrolled_window);
            }
        }

      return TRUE;
    }

  return FALSE;
}

static gboolean
gtk_scrolled_window_captured_button_release_scrollbar (GtkWidget *widget,
                                                       GdkEvent  *event)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  GdkEventButton *bevent = (GdkEventButton *) event;

  if (bevent->button != 1)
    return FALSE;

  gtk_grab_remove (widget);
  priv->sb_pointer_grabbed = FALSE;

  if (priv->sb_scroll_timeout_id)
    {
      g_source_remove (priv->sb_scroll_timeout_id);
      priv->sb_scroll_timeout_id = 0;
    }

  return TRUE;
}

static gboolean
gtk_scrolled_window_captured_motion_notify_scrollbar (GtkWidget *widget,
                                                      GdkEvent  *event)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  GdkEventMotion *mevent = (GdkEventMotion *) event;

  if (priv->sb_pointer_grabbed)
    {
      if (priv->sb_drag_slider)
        {
          GdkRectangle vbar_rect;
          GdkRectangle vslider_rect;
          GdkRectangle hbar_rect;
          GdkRectangle hslider_rect;
          GtkAdjustment *adj;
          gint pos;
          gint visible_range;
          gdouble value;

          gtk_scrolled_window_get_child_scroll_areas (scrolled_window,
                                                      gtk_bin_get_child (GTK_BIN (widget)),
                                                      mevent->window,
                                                      &vbar_rect, &vslider_rect,
                                                      &hbar_rect, &hslider_rect);

          if (priv->sb_grab_vscroll)
            {
              adj = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar));
              pos = mevent->y - priv->sb_grab_offset_y - vbar_rect.y;
              visible_range = vbar_rect.height - vslider_rect.height;
            }
          else if (priv->sb_grab_hscroll)
            {
              adj = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar));
              pos = mevent->x - priv->sb_grab_offset_x - hbar_rect.x;
              visible_range = hbar_rect.width - hslider_rect.width;
            }

          pos = CLAMP (pos, 0, visible_range);

          value = (adj->upper - adj->page_size - adj->lower) * pos / visible_range;

          gtk_adjustment_set_value (adj, value);
        }

      return TRUE;
    }
  else
    {
      if (gtk_scrolled_window_over_child_scroll_areas (scrolled_window, event,
                                                       mevent->x, mevent->y,
                                                       NULL, NULL))
        {
          priv->sb_hovering = TRUE;
          priv->sb_visible = TRUE;

          gtk_scrolled_window_start_fade_in_animation (scrolled_window);
          gtk_scrolled_window_stop_fade_out_timeout (scrolled_window);

          /* needed when entering the scrollbar */
          gtk_scrolled_window_expose_scrollbars (NULL, scrolled_window);

          return TRUE;
        }

      priv->sb_hovering = FALSE;

      if (priv->sb_visible || gtk_adjustment_get_value (priv->opacity) > 0.0)
        {
          /* keep visible scrollbars visible while the mouse is moving */
          gtk_scrolled_window_start_fade_in_animation (scrolled_window);
          gtk_scrolled_window_stop_fade_out_timeout (scrolled_window);
          gtk_scrolled_window_start_fade_out_timeout (scrolled_window);
        }

      return FALSE;
    }
}

static gboolean
gtk_scrolled_window_captured_event (GtkWidget *widget,
                                    GdkEvent  *event)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (widget);
  gboolean retval = FALSE;

  switch (event->type)
    {
    case GDK_BUTTON_PRESS:
      retval = gtk_scrolled_window_captured_button_press_scrollbar (widget, event);
      if (!retval)
        retval = gtk_scrolled_window_captured_button_press_kinetic (widget, event);
      break;
    case GDK_BUTTON_RELEASE:
      if (priv->sb_pointer_grabbed)
        retval = gtk_scrolled_window_captured_button_release_scrollbar (widget, event);
      else if (priv->pointer_grabbed)
        retval = gtk_scrolled_window_captured_button_release_kinetic (widget, event);
      else
        priv->last_button_event_valid = FALSE;
      break;
    case GDK_MOTION_NOTIFY:
      if (priv->sb_pointer_grabbed || !priv->pointer_grabbed)
        retval = gtk_scrolled_window_captured_motion_notify_scrollbar (widget, event);
      else if (priv->pointer_grabbed)
        retval = gtk_scrolled_window_captured_motion_notify_kinetic (widget, event);
      break;
    case GDK_LEAVE_NOTIFY:
      if (!priv->in_drag && !priv->sb_pointer_grabbed)
        {
          gtk_scrolled_window_start_fade_out_timeout (scrolled_window);
          priv->sb_hovering = FALSE;
        }
    case GDK_ENTER_NOTIFY:
      if (priv->in_drag &&
          event->crossing.mode != GDK_CROSSING_GRAB)
        retval = TRUE;
      break;
    default:
      break;
    }

  return retval;
}

static gboolean
gtk_scrolled_window_focus (GtkWidget        *widget,
			   GtkDirectionType  direction)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  gboolean had_focus_child = GTK_CONTAINER (widget)->focus_child != NULL;
  
  if (scrolled_window->focus_out)
    {
      scrolled_window->focus_out = FALSE; /* Clear this to catch the wrap-around case */
      return FALSE;
    }
  
  if (gtk_widget_is_focus (widget))
    return FALSE;

  /* We only put the scrolled window itself in the focus chain if it
   * isn't possible to focus any children.
   */
  if (GTK_BIN (widget)->child)
    {
      if (gtk_widget_child_focus (GTK_BIN (widget)->child, direction))
	return TRUE;
    }

  if (!had_focus_child && gtk_widget_get_can_focus (widget))
    {
      gtk_widget_grab_focus (widget);
      return TRUE;
    }
  else
    return FALSE;
}

static void
gtk_scrolled_window_adjustment_changed (GtkAdjustment *adjustment,
					gpointer       data)
{
  GtkScrolledWindow *scrolled_win;
  GtkScrolledWindowPrivate *priv;

  g_return_if_fail (adjustment != NULL);
  g_return_if_fail (data != NULL);

  scrolled_win = GTK_SCROLLED_WINDOW (data);
  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (data);

  if (scrolled_win->hscrollbar &&
      adjustment == gtk_range_get_adjustment (GTK_RANGE (scrolled_win->hscrollbar)))
    {
      if (scrolled_win->hscrollbar_policy == GTK_POLICY_AUTOMATIC)
	{
	  gboolean visible;
	  
	  visible = scrolled_win->hscrollbar_visible;
	  scrolled_win->hscrollbar_visible = (adjustment->upper - adjustment->lower >
					      adjustment->page_size);
	  if (scrolled_win->hscrollbar_visible != visible)
	    gtk_widget_queue_resize (GTK_WIDGET (scrolled_win));
	}
    }
  else if (scrolled_win->vscrollbar &&
	   adjustment == gtk_range_get_adjustment (GTK_RANGE (scrolled_win->vscrollbar)))
    {
      if (scrolled_win->vscrollbar_policy == GTK_POLICY_AUTOMATIC)
	{
	  gboolean visible;

	  visible = scrolled_win->vscrollbar_visible;
	  scrolled_win->vscrollbar_visible = (adjustment->upper - adjustment->lower >
					      adjustment->page_size);
	  if (scrolled_win->vscrollbar_visible != visible)
	    gtk_widget_queue_resize (GTK_WIDGET (scrolled_win));
	}
    }

  if (priv->overlay_scrollbars)
    gtk_scrolled_window_start_fade_in_animation (scrolled_win);
}

static void
gtk_scrolled_window_adjustment_value_changed (GtkAdjustment *adjustment,
                                              gpointer       user_data)
{
  GtkScrolledWindow *scrolled_window = user_data;
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  /* Allow overshooting for kinetic scrolling operations */
  if (priv->pointer_grabbed || priv->deceleration_id)
    return;

  /* Ensure GtkAdjustment and unclamped values are in sync */
  if (scrolled_window->vscrollbar &&
      adjustment == gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar)))
    priv->unclamped_vadj_value = gtk_adjustment_get_value (adjustment);
  else if (scrolled_window->hscrollbar &&
           adjustment == gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar)))
    priv->unclamped_hadj_value = gtk_adjustment_get_value (adjustment);

  if (priv->overlay_scrollbars)
    gtk_scrolled_window_start_fade_in_animation (scrolled_window);
}

static void
gtk_scrolled_window_add (GtkContainer *container,
			 GtkWidget    *child)
{
  GtkScrolledWindow *scrolled_window;
  GtkScrolledWindowPrivate *priv;
  GtkBin *bin;

  bin = GTK_BIN (container);
  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (container);
  g_return_if_fail (bin->child == NULL);

  scrolled_window = GTK_SCROLLED_WINDOW (container);

  if (gtk_widget_get_realized (GTK_WIDGET (bin)))
    gtk_widget_set_parent_window (child, priv->overshoot_window);

  bin->child = child;
  gtk_widget_set_parent (child, GTK_WIDGET (bin));

  /* this is a temporary message */
  if (!gtk_widget_set_scroll_adjustments (child,
                                          gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar)),
                                          gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar))))
    g_warning ("gtk_scrolled_window_add(): cannot add non scrollable widget "
	       "use gtk_scrolled_window_add_with_viewport() instead");

  g_signal_connect_after (child, "expose-event",
                          G_CALLBACK (gtk_scrolled_window_child_expose),
                          container);
}

static void
gtk_scrolled_window_remove (GtkContainer *container,
			    GtkWidget    *child)
{
  GtkScrolledWindowPrivate *priv;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (container));
  g_return_if_fail (child != NULL);
  g_return_if_fail (GTK_BIN (container)->child == child);

  priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (container);

  g_signal_handlers_disconnect_by_func (child,
                                        gtk_scrolled_window_child_expose,
                                        container);

  gtk_widget_set_scroll_adjustments (child, NULL, NULL);

  /* chain parent class handler to remove child */
  GTK_CONTAINER_CLASS (gtk_scrolled_window_parent_class)->remove (container, child);
}

/**
 * gtk_scrolled_window_add_with_viewport:
 * @scrolled_window: a #GtkScrolledWindow
 * @child: the widget you want to scroll
 *
 * Used to add children without native scrolling capabilities. This
 * is simply a convenience function; it is equivalent to adding the
 * unscrollable child to a viewport, then adding the viewport to the
 * scrolled window. If a child has native scrolling, use
 * gtk_container_add() instead of this function.
 *
 * The viewport scrolls the child by moving its #GdkWindow, and takes
 * the size of the child to be the size of its toplevel #GdkWindow. 
 * This will be very wrong for most widgets that support native scrolling;
 * for example, if you add a widget such as #GtkTreeView with a viewport,
 * the whole widget will scroll, including the column headings. Thus, 
 * widgets with native scrolling support should not be used with the 
 * #GtkViewport proxy.
 *
 * A widget supports scrolling natively if the 
 * set_scroll_adjustments_signal field in #GtkWidgetClass is non-zero,
 * i.e. has been filled in with a valid signal identifier.
 */
void
gtk_scrolled_window_add_with_viewport (GtkScrolledWindow *scrolled_window,
				       GtkWidget         *child)
{
  GtkBin *bin;
  GtkWidget *viewport;

  g_return_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window));
  g_return_if_fail (GTK_IS_WIDGET (child));
  g_return_if_fail (child->parent == NULL);

  bin = GTK_BIN (scrolled_window);

  if (bin->child != NULL)
    {
      g_return_if_fail (GTK_IS_VIEWPORT (bin->child));
      g_return_if_fail (GTK_BIN (bin->child)->child == NULL);

      viewport = bin->child;
    }
  else
    {
      viewport =
        gtk_viewport_new (gtk_scrolled_window_get_hadjustment (scrolled_window),
			  gtk_scrolled_window_get_vadjustment (scrolled_window));
      gtk_container_add (GTK_CONTAINER (scrolled_window), viewport);
    }

  gtk_widget_show (viewport);
  gtk_container_add (GTK_CONTAINER (viewport), child);
}

/*
 * _gtk_scrolled_window_get_spacing:
 * @scrolled_window: a scrolled window
 * 
 * Gets the spacing between the scrolled window's scrollbars and
 * the scrolled widget. Used by GtkCombo
 * 
 * Return value: the spacing, in pixels.
 */
gint
_gtk_scrolled_window_get_scrollbar_spacing (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowClass *class;
    
  g_return_val_if_fail (GTK_IS_SCROLLED_WINDOW (scrolled_window), 0);

  class = GTK_SCROLLED_WINDOW_GET_CLASS (scrolled_window);

  if (class->scrollbar_spacing >= 0)
    return class->scrollbar_spacing;
  else
    {
      gint scrollbar_spacing;
      
      gtk_widget_style_get (GTK_WIDGET (scrolled_window),
			    "scrollbar-spacing", &scrollbar_spacing,
			    NULL);

      return scrollbar_spacing;
    }
}

static void
gtk_scrolled_window_realize (GtkWidget *widget)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  GtkAllocation allocation, relative_allocation;
  GdkWindowAttr attributes;
  GtkWidget *child_widget;
  gint attributes_mask;

  gtk_widget_set_realized (widget, TRUE);
  gtk_widget_get_allocation (widget, &allocation);
  gtk_scrolled_window_relative_allocation (widget, &relative_allocation);

  /* Overshoot window */
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.x = allocation.x + relative_allocation.x;
  attributes.y = allocation.y + relative_allocation.y;
  attributes.width = relative_allocation.width;
  attributes.height = relative_allocation.height;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK |
    GDK_BUTTON_MOTION_MASK;

  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

  priv->overshoot_window =
    gdk_window_new (gtk_widget_get_parent_window (widget),
                    &attributes, attributes_mask);

  gdk_window_set_user_data (priv->overshoot_window, widget);

  /* Vertical background window */
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.x = allocation.x + relative_allocation.x;
  attributes.y = allocation.y + relative_allocation.y;
  attributes.width = 0;
  attributes.height = 0;
  attributes.wclass = GDK_INPUT_ONLY;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK |
    GDK_BUTTON_MOTION_MASK | GDK_SCROLL_MASK;

  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

  priv->vbackground_window =
    gdk_window_new (gtk_widget_get_parent_window (widget),
                    &attributes, attributes_mask);

  gdk_window_set_user_data (priv->vbackground_window, widget);

  /* Horizontal background window */
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.x = allocation.x + relative_allocation.x;
  attributes.y = allocation.y + relative_allocation.y;
  attributes.width = 0;
  attributes.height = 0;
  attributes.wclass = GDK_INPUT_ONLY;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK |
    GDK_BUTTON_MOTION_MASK | GDK_SCROLL_MASK;

  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

  priv->hbackground_window =
    gdk_window_new (gtk_widget_get_parent_window (widget),
                    &attributes, attributes_mask);

  gdk_window_set_user_data (priv->hbackground_window, widget);


  child_widget = gtk_bin_get_child (GTK_BIN (widget));

  if (child_widget)
    gtk_widget_set_parent_window (child_widget,
                                  priv->overshoot_window);

  GTK_WIDGET_CLASS (gtk_scrolled_window_parent_class)->realize (widget);
}

static void
gtk_scrolled_window_unrealize (GtkWidget *widget)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  gdk_window_set_user_data (priv->overshoot_window, NULL);
  gdk_window_destroy (priv->overshoot_window);
  priv->overshoot_window = NULL;

  gdk_window_set_user_data (priv->vbackground_window, NULL);
  gdk_window_destroy (priv->vbackground_window);
  priv->vbackground_window = NULL;

  gdk_window_set_user_data (priv->hbackground_window, NULL);
  gdk_window_destroy (priv->hbackground_window);
  priv->hbackground_window = NULL;

  gtk_widget_set_realized (widget, FALSE);

  GTK_WIDGET_CLASS (gtk_scrolled_window_parent_class)->unrealize (widget);
}

static void
gtk_scrolled_window_map (GtkWidget *widget)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  gdk_window_show (priv->overshoot_window);
  if (gdk_window_get_width (priv->vbackground_window) > 1)
    gdk_window_show (priv->vbackground_window);
  else
    gdk_window_hide (priv->vbackground_window);

  if (gdk_window_get_height (priv->hbackground_window) > 1)
    gdk_window_show (priv->hbackground_window);
  else
    gdk_window_hide (priv->hbackground_window);

  GTK_WIDGET_CLASS (gtk_scrolled_window_parent_class)->map (widget);
}

static void
gtk_scrolled_window_unmap (GtkWidget *widget)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  gdk_window_hide (priv->overshoot_window);
  gdk_window_hide (priv->vbackground_window);
  gdk_window_hide (priv->hbackground_window);

  GTK_WIDGET_CLASS (gtk_scrolled_window_parent_class)->unmap (widget);
}

static void
gtk_scrolled_window_grab_notify (GtkWidget *widget,
                                 gboolean   was_grabbed)
{
  GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW (widget);
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  if (priv->pointer_grabbed && !was_grabbed)
    {
      gdk_pointer_ungrab (gtk_get_current_event_time ());
      priv->pointer_grabbed = FALSE;
      priv->in_drag = FALSE;

      if (priv->release_timeout_id)
        {
          g_source_remove (priv->release_timeout_id);
          priv->release_timeout_id = 0;
        }

      if (_gtk_scrolled_window_get_overshoot (scrolled_window, NULL, NULL))
        gtk_scrolled_window_start_deceleration (scrolled_window);
      else
        gtk_scrolled_window_cancel_deceleration (scrolled_window);

      priv->last_button_event_valid = FALSE;
    }

  if (priv->sb_pointer_grabbed && !was_grabbed)
    {
      priv->sb_pointer_grabbed = FALSE;

      if (priv->sb_scroll_timeout_id)
        {
          g_source_remove (priv->sb_scroll_timeout_id);
          priv->sb_scroll_timeout_id = 0;
        }
    }
}

static void
gtk_scrolled_window_rounded_rectangle (cairo_t *cr,
                                       gint     x,
                                       gint     y,
                                       gint     width,
                                       gint     height,
                                       gint     x_radius,
                                       gint     y_radius)
{
  gint x1, x2;
  gint y1, y2;
  gint xr1, xr2;
  gint yr1, yr2;

  x1 = x;
  x2 = x1 + width;
  y1 = y;
  y2 = y1 + height;

  x_radius = MIN (x_radius, width  / 2.0);
  y_radius = MIN (y_radius, height / 2.0);

  xr1 = x_radius;
  xr2 = x_radius / 2.0;
  yr1 = y_radius;
  yr2 = y_radius / 2.0;

  cairo_move_to    (cr, x1 + xr1, y1);
  cairo_line_to    (cr, x2 - xr1, y1);
  cairo_curve_to   (cr, x2 - xr2, y1, x2, y1 + yr2, x2, y1 + yr1);
  cairo_line_to    (cr, x2, y2 - yr1);
  cairo_curve_to   (cr, x2, y2 - yr2, x2 - xr2, y2, x2 - xr1, y2);
  cairo_line_to    (cr, x1 + xr1, y2);
  cairo_curve_to   (cr, x1 + xr2, y2, x1, y2 - yr2, x1, y2 - yr1);
  cairo_line_to    (cr, x1, y1 + yr1);
  cairo_curve_to   (cr, x1, y1 + yr2, x1 + xr2, y1, x1 + xr1, y1);
  cairo_close_path (cr);
}

static gboolean
gtk_scrolled_window_over_child_scroll_areas (GtkScrolledWindow *scrolled_window,
                                             GdkEvent          *event,
                                             gint               x,
                                             gint               y,
                                             gboolean          *over_vscroll,
                                             gboolean          *over_hscroll)
{
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  GtkWidget *child;
  GdkRectangle vbar_rect;
  GdkRectangle hbar_rect;
  gboolean over_v = FALSE;
  gboolean over_h = FALSE;

  child = gtk_bin_get_child (GTK_BIN (scrolled_window));
  if (!child)
    return FALSE;

  if (gtk_get_event_widget (event) != child)
    return FALSE;

  if (gtk_adjustment_get_value (priv->opacity) == 0.0)
    return FALSE;

  gtk_scrolled_window_get_child_scroll_areas (scrolled_window,
                                              child,
                                              ((GdkEventAny *) event)->window,
                                              &vbar_rect, NULL,
                                              &hbar_rect, NULL);

  if (vbar_rect.width > 0 &&
      x >= vbar_rect.x && x < (vbar_rect.x + vbar_rect.width) &&
      y >= vbar_rect.y && y < (vbar_rect.y + vbar_rect.height))
    {
      over_v = TRUE;
    }
  else if (hbar_rect.width > 0 &&
           x >= hbar_rect.x && x < (hbar_rect.x + hbar_rect.width) &&
           y >= hbar_rect.y && y < (hbar_rect.y + hbar_rect.height))
    {
      over_h = TRUE;
    }

  if (over_vscroll) *over_vscroll = over_v;
  if (over_hscroll) *over_hscroll = over_h;

  return over_v || over_h;
}

static void
gtk_scrolled_window_get_child_scroll_areas (GtkScrolledWindow *scrolled_window,
                                            GtkWidget         *child,
                                            GdkWindow         *child_window,
                                            GdkRectangle      *vbar_rect,
                                            GdkRectangle      *vslider_rect,
                                            GdkRectangle      *hbar_rect,
                                            GdkRectangle      *hslider_rect)
{
   GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
   GtkAdjustment *adj;
   GtkAllocation allocation;
   gdouble lower;
   gdouble page_size;
   gdouble upper;
   gdouble value_h = 0.0;
   gdouble value_v = 0.0;
   gdouble ratio;
   gdouble width;
   gdouble height;
   gdouble x;
   gdouble y;
   gint window_width;
   gint window_height;
   gint viewport_width;
   gint viewport_height;
   gint offset_x = 0;
   gint offset_y = 0;

   window_width = gdk_window_get_width (child_window);
   window_height = gdk_window_get_height (child_window);

   gtk_widget_get_allocation (child, &allocation);

   viewport_width = MIN (window_width, allocation.width);
   viewport_height = MIN (window_height, allocation.height);

   if (scrolled_window->vscrollbar)
     {
       adj = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar));

       value_v = gtk_adjustment_get_value (adj);
     }

   if (scrolled_window->hscrollbar)
     {
       adj = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar));

       value_h = gtk_adjustment_get_value (adj);
     }

   if (window_width > allocation.width)
     offset_x = value_h;

   if (window_height > allocation.height)
     offset_y = value_v;

   if ((vbar_rect || vslider_rect) && scrolled_window->vscrollbar)
     {
       adj = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->vscrollbar));

       g_object_get (adj,
                     "lower", &lower,
                     "upper", &upper,
                     "page-size", &page_size,
                     NULL);

       ratio = page_size / (upper - lower);
       if (ratio < 1.0)
         {
           height = ratio * (viewport_height - (2 * priv->sb_padding));
           height = MAX (height, 20);
           height = MIN (height, viewport_height - (2 * priv->sb_padding));

           ratio = (value_v - lower) / (upper - page_size - lower);
           y = ratio * (viewport_height - (2 * priv->sb_padding) - height) + priv->sb_padding;
           x = viewport_width - priv->sb_width - priv->sb_padding;

           x += offset_x;
           y += offset_y;

           if (vbar_rect)
             {
               vbar_rect->x = x - priv->sb_padding;
               vbar_rect->y = offset_y;
               vbar_rect->width = priv->sb_width + 2 * priv->sb_padding;
               vbar_rect->height = viewport_height;
             }

           if (vslider_rect)
             {
               vslider_rect->x = x;
               vslider_rect->y = y;
               vslider_rect->width = priv->sb_width;
               vslider_rect->height = height;
             }
         }
       else
         {
           if (vbar_rect)
             {
               vbar_rect->x = 0;
               vbar_rect->y = 0;
               vbar_rect->width = 0;
               vbar_rect->height = 0;
             }

           if (vslider_rect)
             {
               vslider_rect->x = 0;
               vslider_rect->y = 0;
               vslider_rect->width = 0;
               vslider_rect->height = 0;
             }
         }
     }

   if ((hbar_rect || hslider_rect) && scrolled_window->hscrollbar)
     {
       adj = gtk_range_get_adjustment (GTK_RANGE (scrolled_window->hscrollbar));

       g_object_get (adj,
                     "lower", &lower,
                     "upper", &upper,
                     "page-size", &page_size,
                     NULL);

       ratio = page_size / (upper - lower);
       if (ratio < 1.0)
         {
           width = ratio * (viewport_width - (2 * priv->sb_padding));
           width = MAX (width, 20);
           width = MIN (width, viewport_width - (2 * priv->sb_padding));

           ratio = (value_h - lower) / (upper - page_size - lower);
           x = ratio * (viewport_width - (2 * priv->sb_padding) - width) + priv->sb_padding;
           y = viewport_height - priv->sb_width - priv->sb_padding;

           x += offset_x;
           y += offset_y;

           if (hbar_rect)
             {
               hbar_rect->x = offset_x;
               hbar_rect->y = y - priv->sb_padding;
               hbar_rect->width = viewport_width;
               hbar_rect->height = priv->sb_width + 2 * priv->sb_padding;
             }

           if (hslider_rect)
             {
               hslider_rect->x = x;
               hslider_rect->y = y;
               hslider_rect->width = width;
               hslider_rect->height = priv->sb_width;
             }
         }
       else
         {
           if (hbar_rect)
             {
               hbar_rect->x = 0;
               hbar_rect->y = 0;
               hbar_rect->width = 0;
               hbar_rect->height = 0;
             }

           if (hslider_rect)
             {
               hslider_rect->x = 0;
               hslider_rect->y = 0;
               hslider_rect->width = 0;
               hslider_rect->height = 0;
             }
         }
     }
}

static gboolean
gtk_scrolled_window_child_expose (GtkWidget         *widget,
                                  GdkEventExpose    *eevent,
                                  GtkScrolledWindow *scrolled_window)
{
   GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
   GdkRectangle vbar_rect;
   GdkRectangle vslider_rect;
   GdkRectangle hbar_rect;
   GdkRectangle hslider_rect;
   cairo_t   *cr;

   if (!priv->overlay_scrollbars)
     return FALSE;

   cr = gdk_cairo_create (eevent->window);
   gdk_cairo_region (cr, eevent->region);
   cairo_clip (cr);

   gtk_scrolled_window_get_child_scroll_areas (scrolled_window,
                                               gtk_bin_get_child (GTK_BIN (scrolled_window)),
                                               eevent->window,
                                               &vbar_rect, &vslider_rect,
                                               &hbar_rect, &hslider_rect);

   if (priv->sb_visible)
     {
       if (scrolled_window->vscrollbar && vbar_rect.width > 0)
         gdk_cairo_rectangle (cr, &vbar_rect);

       if (scrolled_window->hscrollbar && hbar_rect.width > 0)
         gdk_cairo_rectangle (cr, &hbar_rect);

       cairo_set_source_rgba (cr, 0, 0, 0, gtk_adjustment_get_value (priv->opacity) / 2.0);
       cairo_fill (cr);
     }

   if (scrolled_window->vscrollbar && vslider_rect.width > 0)
     gtk_scrolled_window_rounded_rectangle (cr,
                                            vslider_rect.x,
                                            vslider_rect.y,
                                            vslider_rect.width,
                                            vslider_rect.height,
                                            priv->sb_radius,
                                            priv->sb_radius);

   if (scrolled_window->hscrollbar && hslider_rect.width > 0)
     gtk_scrolled_window_rounded_rectangle (cr,
                                            hslider_rect.x,
                                            hslider_rect.y,
                                            hslider_rect.width,
                                            hslider_rect.height,
                                            priv->sb_radius,
                                            priv->sb_radius);

   cairo_set_source_rgba (cr, 0, 0, 0, gtk_adjustment_get_value (priv->opacity));
   cairo_fill (cr);

   cairo_destroy (cr);

   return FALSE;
}

static void
gtk_scrolled_window_cancel_animation (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  GbAnimation *anim = priv->opacity_anim;

  if (anim)
    {
      priv->opacity_anim = NULL;
      g_object_remove_weak_pointer (G_OBJECT (anim),
                                    (gpointer *) &priv->opacity_anim);
      _gb_animation_stop (anim);
    }

  gtk_scrolled_window_stop_fade_out_timeout (scrolled_window);

  priv->sb_fading_in = FALSE;
}

static gboolean
gtk_scrolled_window_fade_out_timeout (GtkScrolledWindow *scrolled_window)
{
  gtk_scrolled_window_start_fade_out_animation (scrolled_window);

  return FALSE;
}

static void
gtk_scrolled_window_start_fade_out_timeout (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  if (! priv->sb_fade_out_id)
    priv->sb_fade_out_id =
      gdk_threads_add_timeout (priv->sb_fade_out_delay,
                               (GSourceFunc) gtk_scrolled_window_fade_out_timeout,
                               scrolled_window);
}

static void
gtk_scrolled_window_stop_fade_out_timeout (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  if (priv->sb_fade_out_id)
    {
      g_source_remove (priv->sb_fade_out_id);
      priv->sb_fade_out_id = 0;
    }
}

static void
gtk_scrolled_window_start_fade_in_animation (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);
  gdouble upper;

  if (priv->sb_fading_in)
    return;

  gtk_scrolled_window_cancel_animation (scrolled_window);

  priv->sb_fading_in = TRUE;
  priv->sb_visible = priv->sb_hovering;

  upper = gtk_adjustment_get_upper (priv->opacity);
  priv->opacity_anim = _gb_object_animate (priv->opacity,
                                           GB_ANIMATION_EASE_OUT_QUAD,
                                           100,
                                           "value", upper,
                                           NULL);
  g_object_add_weak_pointer (G_OBJECT (priv->opacity_anim),
                             (gpointer *) &priv->opacity_anim);

  gtk_scrolled_window_start_fade_out_timeout (scrolled_window);
}

static void
gtk_scrolled_window_start_fade_out_animation (GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  gtk_scrolled_window_cancel_animation (scrolled_window);

  priv->opacity_anim = _gb_object_animate (priv->opacity,
                                           GB_ANIMATION_EASE_IN_QUAD,
                                           300,
                                           "value", 0.0,
                                           NULL);
  g_object_add_weak_pointer (G_OBJECT (priv->opacity_anim),
                             (gpointer *) &priv->opacity_anim);
}

static void
gtk_scrolled_window_expose_scrollbars (GtkAdjustment     *adj,
                                       GtkScrolledWindow *scrolled_window)
{
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (scrolled_window);

  if (priv->overlay_scrollbars)
    {
      GtkWidget *child = gtk_bin_get_child (GTK_BIN (scrolled_window));

      if (child && gtk_widget_get_visible (child))
        {
          GtkAllocation alloc;

          gtk_widget_get_allocation (child, &alloc);

          if (scrolled_window->vscrollbar)
            gtk_widget_queue_draw_area (child,
                                        alloc.width - 20,
                                        0,
                                        20,
                                        alloc.height);

          if (scrolled_window->hscrollbar)
            gtk_widget_queue_draw_area (child,
                                        0,
                                        alloc.height - 20,
                                        alloc.width,
                                        20);
        }
    }
}

static void
gtk_scrolled_window_overlay_scrollbars_changed (GtkSettings *settings,
                                                GParamSpec  *arg,
                                                gpointer     user_data)
{
  GtkScrolledWindowPrivate *priv = GTK_SCROLLED_WINDOW_GET_PRIVATE (user_data);

  g_object_get (settings,
                "gtk-enable-overlay-scrollbars",
                &priv->overlay_scrollbars,
                NULL);

  gtk_widget_queue_resize (user_data);
}

#define __GTK_SCROLLED_WINDOW_C__
#include "gtkaliasdef.c"
