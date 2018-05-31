#include <gtk/gtk.h>
#include <math.h>


typedef struct
{
  gdouble width;
  gdouble height;

  gdouble angle;

  GtkWidget *widget;
  gint offset_x;
  gint offset_y;
  gdouble progress;
  gboolean increasing;
}
RectangleInfo;



static gboolean
handle_expose_event (GtkWidget      *widget,
                     GdkEventExpose *expose,
                     gpointer        user_data)
{
  cairo_t *cr;
  int center_x, center_y;
  RectangleInfo *rect = (RectangleInfo *)user_data;

  cr = gdk_cairo_create (widget->window);

  cairo_save (cr);

  /* Background */
  cairo_rectangle (cr, 0, 0,
                   widget->allocation.width,
                   widget->allocation.height);
  cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
  cairo_fill (cr);

  cairo_restore (cr);

  /* Rectangle */
  center_x = (widget->allocation.width - rect->width) / 2;
  center_y = (widget->allocation.height - rect->height) / 2;

  if (rect->progress != 0.0f)
    {
      cairo_translate (cr, rect->offset_x * rect->progress,
                       rect->offset_y * rect->progress);
    }

  cairo_save (cr);

  cairo_translate (cr, widget->allocation.width / 2,
                   widget->allocation.height / 2);
  cairo_rotate (cr, rect->angle * M_PI / 180.0);
  cairo_translate (cr, -widget->allocation.width / 2,
                   -widget->allocation.height / 2);


  cairo_rectangle (cr,
                   center_x, center_y,
                   rect->width, rect->height);
  cairo_set_source_rgb (cr, 0.9, 0.0, 0.0);
  cairo_stroke (cr);

  cairo_rectangle (cr,
                   center_x, center_y,
                   rect->width, rect->height);
  cairo_set_source_rgba (cr, 0.9, 0.0, 0.0, 0.3);
  cairo_fill (cr);

  cairo_restore (cr);


  cairo_destroy (cr);

  return FALSE;
}

static gboolean
handle_gesture_magnify_event (GtkWidget              *widget,
                              GdkEventGestureMagnify *magnify,
                              gpointer                user_data)
{
  RectangleInfo *rect = (RectangleInfo *)user_data;

  rect->width += rect->width * magnify->magnification;
  if (rect->width < 5)
    rect->width = 5;

  rect->height += rect->height * magnify->magnification;
  if (rect->height < 5)
    rect->height = 5;

  gtk_widget_queue_draw (widget);

  return TRUE;
}

static gboolean
handle_gesture_rotate_event (GtkWidget             *widget,
                             GdkEventGestureRotate *rotate,
                             gpointer               user_data)
{
  RectangleInfo *rect = (RectangleInfo *)user_data;

  rect->angle -= rotate->rotation;

  gtk_widget_queue_draw (widget);

  return TRUE;
}


static gboolean
bounce_timeout (gpointer user_data)
{
  gboolean retval = TRUE;
  RectangleInfo *rect = (RectangleInfo *)user_data;

  if (rect->increasing)
    rect->progress += 0.10f;
  else
    rect->progress -= 0.10f;

  if (rect->progress > 1.0f)
    {
      rect->progress = 0.90f;
      rect->increasing = FALSE;
    }
  else if (rect->progress <= 0.0f)
    {
      rect->progress = 0.0f;
      retval = FALSE;
    }

  gtk_widget_queue_draw (rect->widget);

  return retval;
}

static void
bounce (RectangleInfo *rect,
        int            offset_x,
        int            offset_y)
{
  if (rect->progress != 0.0f)
    return;

  rect->progress = 0.10f;
  rect->increasing = TRUE;
  rect->offset_x = offset_x;
  rect->offset_y = offset_y;
  gtk_widget_queue_draw (rect->widget);

  gdk_threads_add_timeout (25, bounce_timeout, rect);
}

static gboolean
handle_gesture_swipe_event (GtkWidget             *widget,
                            GdkEventGestureSwipe  *swipe,
                            gpointer               user_data)
{
  int offset_x = 150, offset_y = 150;
  RectangleInfo *rect = (RectangleInfo *)user_data;

  offset_x *= -1.0 * swipe->delta_x;
  offset_y *= -1.0 * swipe->delta_y;

  bounce (rect, offset_x, offset_y);

  return TRUE;
}

int
main (int argc, char **argv)
{
  GtkWidget *window;
  GtkWidget *drawing_area;
  RectangleInfo rect;

  gtk_init (&argc, &argv);

  rect.width = 40.0;
  rect.height = 40.0;
  rect.angle = 0.0;
  rect.progress = 0.0f;

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size (GTK_WINDOW (window), 640, 480);
  g_signal_connect (window, "delete-event",
                    G_CALLBACK (gtk_main_quit), NULL);

  drawing_area = gtk_drawing_area_new ();
  rect.widget = drawing_area;
  g_signal_connect (drawing_area, "expose-event",
                    G_CALLBACK (handle_expose_event), &rect);
  g_signal_connect (drawing_area, "gesture-magnify-event",
                    G_CALLBACK (handle_gesture_magnify_event), &rect);
  g_signal_connect (drawing_area, "gesture-rotate-event",
                    G_CALLBACK (handle_gesture_rotate_event), &rect);
  g_signal_connect (drawing_area, "gesture-swipe-event",
                    G_CALLBACK (handle_gesture_swipe_event), &rect);
  gtk_container_add (GTK_CONTAINER (window), drawing_area);

  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}
