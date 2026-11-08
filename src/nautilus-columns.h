/*
 * nautilus-columns.h: Miller columns around a slot's list view
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "nautilus-types.h"

G_BEGIN_DECLS

#define NAUTILUS_TYPE_COLUMNS (nautilus_columns_get_type ())

G_DECLARE_FINAL_TYPE (NautilusColumns, nautilus_columns, NAUTILUS, COLUMNS, GtkWidget)

/* Holds @slot's files view; in Columns mode, with the folders above it as
 * columns on the left and the selected folder's contents on the right. */
typedef enum
{
    NAUTILUS_COLUMNS_MODE_LIST = 1,
    NAUTILUS_COLUMNS_MODE_COLUMNS,
    NAUTILUS_COLUMNS_MODE_GRID,
} NautilusColumnsMode;

GtkWidget *nautilus_columns_new         (NautilusWindowSlot *slot);
/* The "mode" property: list, columns or grid, the order of Alt+1, 2, 3. */
guint      nautilus_columns_get_mode    (NautilusColumns    *self);
void       nautilus_columns_set_mode    (NautilusColumns    *self,
                                         guint               mode);
/* One view on (1) or back (-1); in the grid, item sizes come first. */
void       nautilus_columns_step        (NautilusColumns    *self,
                                         int                 direction);
void       nautilus_columns_set_content (NautilusColumns    *self,
                                         GtkWidget          *content);

G_END_DECLS
