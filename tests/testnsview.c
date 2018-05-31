/* testnsview.c
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

#include <WebKit/WebKit.h>
#include <gtk/gtk.h>


static void
back_clicked (GtkToolItem *item,
              WebView     *webview)
{
  [webview goBack];
}

static void
forward_clicked (GtkToolItem *item,
                 WebView     *webview)
{
  [webview goForward];
}

gint
main (gint   argc,
      gchar *argv[])
{
  GtkWidget *window;
  GtkWidget *vbox;
  GtkWidget *toolbar;
  GtkToolItem *item;
  GtkWidget *notebook;
  WebView *webview;
  NSRect web_rect = { { 0.0, 0.0 }, { 100.0, 100.0 } };
  NSURL *url;
  NSURLRequest *request;
  GtkWidget *ns_view;

  gtk_init (&argc, &argv);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), "GtkNSView featuring WebView");

  g_signal_connect (window, "destroy",
                    G_CALLBACK (gtk_main_quit),
                    NULL);

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (window), vbox);
  gtk_widget_show (vbox);

  toolbar = gtk_toolbar_new ();
  gtk_box_pack_start (GTK_BOX (vbox), toolbar, FALSE, FALSE, 0);
  gtk_widget_show (toolbar);

  webview = [WebView alloc];

  item = gtk_tool_button_new_from_stock (GTK_STOCK_GO_BACK);
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);
  gtk_widget_show (GTK_WIDGET (item));

  g_signal_connect (item, "clicked",
                    G_CALLBACK (back_clicked),
                    webview);

  item = gtk_tool_button_new_from_stock (GTK_STOCK_GO_FORWARD);
  gtk_toolbar_insert (GTK_TOOLBAR (toolbar), item, -1);
  gtk_widget_show (GTK_WIDGET (item));

  g_signal_connect (item, "clicked",
                    G_CALLBACK (forward_clicked),
                    webview);

  notebook = gtk_notebook_new ();
  gtk_box_pack_end (GTK_BOX (vbox), notebook, TRUE, TRUE, 0);
  gtk_widget_show (notebook);

  [webview initWithFrame:web_rect
               frameName:@"foo"
               groupName:@"bar"];

  url = [NSURL URLWithString:@"http://www.gimp.org/"];
  request = [NSURLRequest requestWithURL:url];

  [[webview mainFrame] loadRequest:request];

  ns_view = gtk_ns_view_new ((NSView *) webview);
  gtk_widget_set_size_request (ns_view, 300, 200);
#if 0
  gtk_box_pack_end (GTK_BOX (vbox), ns_view, TRUE, TRUE, 0);
#else
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), ns_view,
                            gtk_label_new ("WebView"));
#endif
  gtk_widget_show (ns_view);

  [webview release];

  {
    GtkWidget *useless = gtk_label_new ("Useless Label");
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), useless,
                              gtk_label_new ("Useless"));
    gtk_widget_show (useless);
  }

  {
    GtkWidget *button;

    button = gtk_button_new_with_label ("hide webview");
    gtk_box_pack_end (GTK_BOX (vbox), button, FALSE, FALSE, 0);
    gtk_widget_show (button);

    g_signal_connect_swapped (button, "clicked",
                              G_CALLBACK (gtk_widget_hide),
                              ns_view);

    button = gtk_button_new_with_label ("show webview");
    gtk_box_pack_end (GTK_BOX (vbox), button, FALSE, FALSE, 0);
    gtk_widget_show (button);

    g_signal_connect_swapped (button, "clicked",
                              G_CALLBACK (gtk_widget_show),
                              ns_view);
  }

  /* add an entry in an event box to test living inside another gdkwindow */
  {
    GtkWidget *event_box;
    GtkWidget *abox;
    GtkWidget *hbox;
    NSRect label_rect = { { 0.0, 0.0 }, { 100.0, 12.0 } };
    NSRect text_rect = { { 0.0, 0.0 }, { 100.0, 12.0 } };
    NSTextField *text_field;

    event_box = gtk_event_box_new ();
    gtk_widget_set_state (event_box, GTK_STATE_ACTIVE);
    gtk_box_pack_start (GTK_BOX (vbox), event_box, FALSE, FALSE, 0);
    gtk_widget_show (event_box);

    abox = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
    gtk_container_set_border_width (GTK_CONTAINER (abox), 10);
    gtk_container_add (GTK_CONTAINER (event_box), abox);
    gtk_widget_show (abox);

    hbox = gtk_hbox_new (FALSE, 10);
    gtk_container_add (GTK_CONTAINER (abox), hbox);
    gtk_widget_show (hbox);

    /* a non-editable text label */
    text_field = [[NSTextField alloc] initWithFrame:label_rect];
    [text_field setEditable:NO];
    [text_field setDrawsBackground:NO];
    [text_field setBordered:NO];
    [text_field setStringValue:@"A Text Label"];

    ns_view = gtk_ns_view_new ((NSView *) text_field);
    gtk_widget_set_size_request (ns_view, 100, 20);
    gtk_box_pack_start (GTK_BOX (hbox), ns_view, FALSE, FALSE, 0);
    gtk_widget_show (ns_view);

    [text_field release];

    /* an editable text field */
    text_field = [[NSTextField alloc] initWithFrame:text_rect];
    [text_field setEditable:YES];
    [text_field setStringValue:@"An editable text entry"];

    ns_view = gtk_ns_view_new ((NSView *) text_field);
    gtk_widget_set_size_request (ns_view, 100, 20);
    gtk_box_pack_start (GTK_BOX (hbox), ns_view, TRUE, TRUE, 0);
    gtk_widget_show (ns_view);

    [text_field release];
  }

  /* and a normal GtkEntry to check focus */
  {
    GtkWidget *entry;

    entry = gtk_entry_new ();
    gtk_entry_set_text (GTK_ENTRY (entry), "Normal GTK+ entry");
    gtk_box_pack_start (GTK_BOX (vbox), entry, FALSE, FALSE, 0);
    gtk_widget_show (entry);
  }

  gtk_widget_show (window);

  gtk_main ();

  return 0;
}
