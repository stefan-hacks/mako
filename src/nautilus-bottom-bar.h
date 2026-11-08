/*
 * nautilus-bottom-bar.h: the bar under each tab's files
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "nautilus-types.h"

G_BEGIN_DECLS

#define NAUTILUS_TYPE_BOTTOM_BAR (nautilus_bottom_bar_get_type ())

G_DECLARE_FINAL_TYPE (NautilusBottomBar, nautilus_bottom_bar, NAUTILUS, BOTTOM_BAR, GtkWidget)

/* The views (list, columns, grid), a filter for the folder, search, flat
 * view and the view menu of @slot. Shown while the "bottom-bar" setting is
 * on; the window's header bar then leaves search and the view menu out. */
GtkWidget *nautilus_bottom_bar_new          (NautilusWindowSlot *slot);

/* Typing in the files goes to the filter. Returns whether it took @keyval. */
gboolean   nautilus_bottom_bar_handle_event (NautilusBottomBar     *self,
                                             GtkEventControllerKey *controller,
                                             guint                  keyval,
                                             GdkModifierType        state);

/* Types @text into the filter, as if typed. */
void       nautilus_bottom_bar_set_filter_text (NautilusBottomBar *self,
                                                const char        *text);

/* Whether the bar is on (the "bottom-bar" setting). */
gboolean   nautilus_bottom_bar_enabled      (void);

G_END_DECLS
