/*
 * nautilus-text-preview.h: the start of a text file, with line numbers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "nautilus-types.h"

G_BEGIN_DECLS

#define NAUTILUS_TYPE_TEXT_PREVIEW (nautilus_text_preview_get_type ())

G_DECLARE_FINAL_TYPE (NautilusTextPreview, nautilus_text_preview, NAUTILUS, TEXT_PREVIEW, GtkWidget)

/* Shows up to @limit bytes of the files it's given. */
GtkWidget *nautilus_text_preview_new      (gsize                limit);
void       nautilus_text_preview_set_file (NautilusTextPreview *self,
                                           NautilusFile        *file);

/* Whether a file of @mime_type reads as text. */
gboolean   nautilus_text_preview_handles  (const char          *mime_type);

G_END_DECLS
