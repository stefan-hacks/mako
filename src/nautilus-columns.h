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
    NAUTILUS_COLUMNS_MODE_TREE,
    NAUTILUS_COLUMNS_MODE_COLUMNS,
    NAUTILUS_COLUMNS_MODE_GRID,
} NautilusColumnsMode;

GtkWidget *nautilus_columns_new         (NautilusWindowSlot *slot);
/* The "mode" property: list, tree, columns or grid, the order of Alt+1 to 4. */
guint      nautilus_columns_get_mode    (NautilusColumns    *self);
void       nautilus_columns_set_mode    (NautilusColumns    *self,
                                         guint               mode);
/* One view on (1) or back (-1); in the grid, item sizes come first. */
void       nautilus_columns_step        (NautilusColumns    *self,
                                         int                 direction);
void       nautilus_columns_set_content (NautilusColumns    *self,
                                         GtkWidget          *content);

/* The "flat" property: every file below the folder in one list, as a search
 * whose text filters it. */
gboolean   nautilus_columns_get_flat    (NautilusColumns    *self);
void       nautilus_columns_set_flat    (NautilusColumns    *self,
                                         gboolean            flat);

/* Whether the list shows folders as a tree (Tree mode). The list view asks
 * when it sets up a folder. */
gboolean   nautilus_columns_get_tree    (NautilusColumns    *self);
/* Whether the list shows only names (Columns mode). */
gboolean   nautilus_columns_get_names_only (NautilusColumns *self);

G_END_DECLS
