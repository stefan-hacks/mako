/*
 * nautilus-inspector.h: a panel with a preview and the details of the
 * selection
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "nautilus-types.h"

G_BEGIN_DECLS

#define NAUTILUS_TYPE_INSPECTOR (nautilus_inspector_get_type ())

G_DECLARE_FINAL_TYPE (NautilusInspector, nautilus_inspector, NAUTILUS, INSPECTOR, GtkWidget)

GtkWidget *nautilus_inspector_new      (void);

/* Follows the selection of @slot; with nothing selected, shows its folder. */
void       nautilus_inspector_set_slot (NautilusInspector  *self,
                                        NautilusWindowSlot *slot);

/* The header that can be dragged to move the panel. */
GtkWidget *nautilus_inspector_get_grip (NautilusInspector *self);

/* Whether the panel is in its own window (no grip, a dock button). */
void       nautilus_inspector_set_detached (NautilusInspector *self,
                                            gboolean           detached);

G_END_DECLS
