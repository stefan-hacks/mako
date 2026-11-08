/*
 * nautilus-quick-look.h: the built-in Quick Look window
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "nautilus-types.h"

G_BEGIN_DECLS

/* Whether Quick Look is ours rather than the external previewer's. */
gboolean nautilus_quick_look_enabled    (void);

/* Shows @selection of @view, or closes Quick Look if it's showing. */
void     nautilus_quick_look_toggle     (NautilusFilesView *view,
                                         GList             *selection);
/* Follows a new selection of @view, if Quick Look is showing it. */
void     nautilus_quick_look_update     (NautilusFilesView *view,
                                         GList             *selection);
gboolean nautilus_quick_look_is_visible (void);
void     nautilus_quick_look_close      (void);

G_END_DECLS
