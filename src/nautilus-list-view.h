/*
 * Copyright (C) 2000 Eazel, Inc.
 * Copyright (C) 2001, 2002 Anders Carlsson <andersca@gnu.org>
 * Copyright (C) 2022 GNOME project contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "nautilus-grouping.h"
#include "nautilus-list-base.h"

G_BEGIN_DECLS

#define NAUTILUS_TYPE_LIST_VIEW (nautilus_list_view_get_type())

G_DECLARE_FINAL_TYPE (NautilusListView, nautilus_list_view, NAUTILUS, LIST_VIEW, NautilusListBase)

NautilusListView *nautilus_list_view_new (void);

void nautilus_list_view_present_column_editor (NautilusListView *self);
/* Again, e.g. on entering or leaving Columns mode, which shows names only. */
void nautilus_list_view_refresh_visible_columns (NautilusListView *self);

/* Headings over the groups, when the view's items are grouped. */
void nautilus_list_view_set_group_by (NautilusListView *self,
                                      NautilusGroupBy   group_by);

G_END_DECLS
