/*
 * nautilus-labels.h: star ratings, tags and folder colors
 *
 * Ratings and tags live on the files, as the extended attributes Dolphin
 * and File Flier use (user.baloo.rating, user.xdg.tags). Tag names and
 * colors are kept in ~/.config/nautilus/tags.json. Folder colors are a slot
 * (0-7) in the folder's metadata, drawn in the chosen scheme.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#include "nautilus-types.h"

G_BEGIN_DECLS

#define NAUTILUS_FOLDER_COLOR_SLOTS 8
/* For nautilus_labels_set_folder_color(): give each folder the next color. */
#define NAUTILUS_FOLDER_COLOR_RAINBOW 100

#define NAUTILUS_TYPE_LABELS (nautilus_labels_get_type ())

G_DECLARE_FINAL_TYPE (NautilusLabels, nautilus_labels, NAUTILUS, LABELS, GObject)

/* The shared instance. Signals: "tags-changed" (the tag list or a tag's
 * color), "colors-changed" (the folder color scheme or the accent).
 * Property "alt-held": whether Alt is down in a Files window. */
NautilusLabels *nautilus_labels_get              (void);

GSettings      *nautilus_flier_settings          (void);

/* @size, the zoom's icon size, as the density draws it: as it is in
 * "comfortable", one standard size smaller in "compact" and "dense", two
 * in "extra-dense" (not below 16). */
guint           nautilus_density_icon_size       (guint         size);

/* Ratings and tags. The change shows at once; the files are written to in
 * the background. */
void            nautilus_labels_set_rating       (GList        *files,
                                                  guint         stars);
void            nautilus_labels_set_tag          (GList        *files,
                                                  const char   *tag,
                                                  gboolean      on);

gboolean        nautilus_labels_get_alt_held     (void);
void            nautilus_labels_set_alt_held     (gboolean      held);

/* Known tags, in the order they were made. */
guint           nautilus_labels_get_n_tags       (void);
const char     *nautilus_labels_get_tag_name     (guint         i);
/* A tag's color; unknown tags are gray. */
void            nautilus_labels_get_tag_color    (const char   *tag,
                                                  GdkRGBA      *color);
/* Adds a tag (with the next color) if there's none of that name. */
void            nautilus_labels_ensure_tag       (const char   *tag);
void            nautilus_labels_set_tag_color    (const char   *tag,
                                                  const GdkRGBA *color);
void            nautilus_labels_rename_tag       (const char   *tag,
                                                  const char   *new_name);
void            nautilus_labels_remove_tag       (const char   *tag);

/* Folder colors. -1 means none. */
int             nautilus_labels_get_folder_color (NautilusFile *file);
void            nautilus_labels_set_folder_color (GList        *files,
                                                  int           slot);
void            nautilus_labels_get_slot_color   (int           slot,
                                                  GdkRGBA      *color);
const char     *nautilus_labels_get_slot_name    (int           slot);
/* The colors of a scheme (an org.gnome.nautilus.flier.FolderColorScheme
 * value), for previews in Preferences. */
void            nautilus_labels_get_scheme_colors (int          scheme,
                                                   GdkRGBA      colors[NAUTILUS_FOLDER_COLOR_SLOTS]);
const char     *nautilus_labels_get_scheme_name  (int           scheme);
guint           nautilus_labels_get_n_schemes    (void);

/* @icon drawn in the color of @slot, following scheme changes. */
GdkPaintable   *nautilus_labels_tint_paintable   (GdkPaintable *icon,
                                                  int           slot);

/* Draws 5 stars, @rating of them filled, in @bounds (a row of squares). */
void            nautilus_labels_draw_stars       (GtkSnapshot           *snapshot,
                                                  const graphene_rect_t *bounds,
                                                  guint                  rating,
                                                  const GdkRGBA         *empty_color,
                                                  int                    scale);
/* Which star (1-5) is at @x within @bounds, or 0. */
guint           nautilus_labels_star_at          (const graphene_rect_t *bounds,
                                                  double                 x);

/* Brings over File Flier's folder colors, tags and scheme, once. */
void            nautilus_labels_import_file_flier (void);

G_END_DECLS
