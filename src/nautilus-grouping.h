/*
 * nautilus-grouping.h: group a folder's items by date, name, rating or kind
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "nautilus-types.h"

G_BEGIN_DECLS

typedef enum
{
    NAUTILUS_GROUP_BY_NONE,
    NAUTILUS_GROUP_BY_DATE,
    NAUTILUS_GROUP_BY_NAME,
    NAUTILUS_GROUP_BY_RATING,
    NAUTILUS_GROUP_BY_KIND,
    NAUTILUS_GROUP_BY_N_MODES,
} NautilusGroupBy;

/* As stored in a folder's metadata and used as action targets. */
NautilusGroupBy  nautilus_group_by_from_string (const char      *name);
const char      *nautilus_group_by_to_string   (NautilusGroupBy  mode);

/* Orders GtkTreeListRows of NautilusViewItems into groups; NULL for none. */
GtkSorter       *nautilus_group_sorter_new     (NautilusGroupBy  mode);

/* The heading of the group @file is in. */
char            *nautilus_group_get_label      (NautilusGroupBy  mode,
                                                NautilusFile    *file);

G_END_DECLS
