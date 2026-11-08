/*
 * nautilus-labels-ui.h: menu rows and dialogs for ratings, tags and
 * folder colors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>

#include "nautilus-types.h"

G_BEGIN_DECLS

/* A row of five stars for a context menu. Clicking one activates
 * "view.rate" with the number of stars; the current rating is shown filled. */
GtkWidget *nautilus_rating_row_new          (void);
void       nautilus_rating_row_set_rating   (GtkWidget  *row,
                                             guint       rating);

/* A row of color swatches for a context menu: none, the eight colors of the
 * current scheme, and the rainbow. Activates "view.folder-color". */
GtkWidget *nautilus_folder_color_row_new    (void);

/* Tick tags for @files, make new ones, recolor or delete them. */
void       nautilus_tags_dialog_present     (GList      *files,
                                             GtkWidget  *parent);

/* A small colored dot, as tags are shown next to names. */
GtkWidget *nautilus_tag_dot_new             (const char *tag);

/* The "Organize" page of Preferences: ratings, tags, folder colors and
 * Quick Look. */
GtkWidget *nautilus_labels_preferences_page_new (void);

G_END_DECLS
