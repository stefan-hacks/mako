/*
 * Copyright (C) 2022 The GNOME project contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nautilus-grid-cell.h"

#include "nautilus-file.h"
#include "nautilus-global-preferences.h"
#include "nautilus-icon-info.h"
#include "nautilus-image.h"
#include "nautilus-labels.h"
#include "nautilus-tag-manager.h"
#include "nautilus-thumbnails.h"
#include "nautilus-ui-utilities.h"
#include "nautilus-view-item.h"
#include "nautilus-view-cell.h"

struct _NautilusGridCell
{
    NautilusViewCell parent_instance;

    GSignalGroup *item_signal_group;

    GQuark *caption_attributes;

    GtkWidget *icon;
    GtkWidget *emblems_box;
    GtkWidget *labels_box;
    GtkWidget *first_caption;
    GtkWidget *second_caption;
    GtkWidget *third_caption;

    gboolean in_file_change;
    gboolean hovered;
};

G_DEFINE_TYPE (NautilusGridCell, nautilus_grid_cell, NAUTILUS_TYPE_VIEW_CELL)

static void
update_icon (NautilusGridCell *self)
{
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    gboolean is_cut;

    g_return_if_fail (item != NULL);

    g_object_get (item, "is-cut", &is_cut, NULL);

    if (is_cut)
    {
        gtk_widget_set_visible (self->icon, FALSE);
        gtk_widget_remove_css_class (self->icon, "hidden-file");

        return;
    }

    g_autoptr (GdkPaintable) icon_paintable = NULL;
    NautilusFile *file = nautilus_view_item_get_file (item);
    guint icon_size;
    gint scale_factor = gtk_widget_get_scale_factor (GTK_WIDGET (self));
    NautilusFileIconFlags flags = NAUTILUS_FILE_ICON_FLAGS_NONE;
    gboolean show_thumbnail = nautilus_file_should_show_thumbnail (file);

    g_object_get (self, "icon-size", &icon_size, NULL);
    icon_paintable = nautilus_file_get_icon_paintable (file, icon_size, scale_factor, flags);
    gtk_widget_set_visible (self->icon, TRUE);
    nautilus_image_set_size (NAUTILUS_IMAGE (self->icon), icon_size);
    nautilus_image_set_fallback (NAUTILUS_IMAGE (self->icon), icon_paintable);

    if (self->in_file_change ||
        !show_thumbnail)
    {
        nautilus_image_set_source (NAUTILUS_IMAGE (self->icon), NULL);
    }

    if (show_thumbnail)
    {
        g_autoptr (GFile) location = nautilus_file_get_location (file);

        nautilus_image_set_source (NAUTILUS_IMAGE (self->icon), location);
    }

    if (nautilus_file_is_hidden_file (file))
    {
        gtk_widget_add_css_class (self->icon, "hidden-file");
    }
    else
    {
        gtk_widget_remove_css_class (self->icon, "hidden-file");
    }
}

static void
update_captions (NautilusGridCell *self)
{
    g_autoptr (NautilusViewItem) item = NULL;
    NautilusFile *file;
    GtkWidget * const caption_labels[] =
    {
        self->first_caption,
        self->second_caption,
        self->third_caption
    };
    G_STATIC_ASSERT (G_N_ELEMENTS (caption_labels) == NAUTILUS_GRID_CELL_N_CAPTIONS);

    item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    g_return_if_fail (item != NULL);
    file = nautilus_view_item_get_file (item);
    for (guint i = 0; i < NAUTILUS_GRID_CELL_N_CAPTIONS; i++)
    {
        GQuark attribute_q = self->caption_attributes[i];
        gboolean show_caption;

        show_caption = (attribute_q != 0);
        gtk_widget_set_visible (caption_labels[i], show_caption);
        if (show_caption)
        {
            g_autofree gchar *string = NULL;
            string = nautilus_file_get_string_attribute_q (file, attribute_q);
            gtk_label_set_text (GTK_LABEL (caption_labels[i]), string);
        }
    }
}

static void
update_emblems (NautilusGridCell *self)
{
    g_autoptr (NautilusViewItem) item = NULL;
    NautilusFile *file;
    GtkWidget *child;
    GtkIconTheme *theme;
    g_autolist (GIcon) emblems = NULL;
    g_autofree gchar *file_uri = NULL;

    item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    g_return_if_fail (item != NULL);
    file = nautilus_view_item_get_file (item);
    file_uri = nautilus_file_get_uri (file);

    /* Remove old emblems. */
    while ((child = gtk_widget_get_first_child (self->emblems_box)) != NULL)
    {
        gtk_box_remove (GTK_BOX (self->emblems_box), child);
    }

    if (nautilus_tag_manager_file_is_starred (nautilus_tag_manager_get (), file_uri))
    {
        gtk_box_append (GTK_BOX (self->emblems_box),
                        gtk_image_new_from_icon_name ("starred-symbolic"));
    }

    theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
    emblems = nautilus_file_get_emblem_icons (file);
    for (GList *l = emblems; l != NULL; l = l->next)
    {
        if (!gtk_icon_theme_has_gicon (theme, l->data))
        {
            g_autofree gchar *icon_string = g_icon_to_string (l->data);
            g_warning ("Failed to add emblem. “%s” not found in the icon theme",
                       icon_string);
            continue;
        }

        gtk_box_append (GTK_BOX (self->emblems_box),
                        gtk_image_new_from_gicon (l->data));
    }
}

static void
on_file_changed (NautilusGridCell *self)
{
    self->in_file_change = TRUE;

    update_icon (self);
    update_emblems (self);
    update_captions (self);

    self->in_file_change = FALSE;
}

static void
on_icon_size_changed (NautilusGridCell *self)
{
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));

    if (item == NULL)
    {
        /* Cell is not bound to an item yet. Do nothing. */
        return;
    }

    update_icon (self);
    update_captions (self);
    gtk_widget_queue_resize (GTK_WIDGET (self));
}

static gboolean
on_label_query_tooltip (GtkWidget  *widget,
                        int         x,
                        int         y,
                        gboolean    keyboard_tip,
                        GtkTooltip *tooltip,
                        gpointer    user_data)
{
    GtkLabel *label = GTK_LABEL (widget);

    if (pango_layout_is_ellipsized (gtk_label_get_layout (label)))
    {
        gtk_tooltip_set_text (tooltip, gtk_label_get_text (label));
        return TRUE;
    }

    return FALSE;
}

static void
on_starred_changed (NautilusTagManager *tag_manager,
                    GList              *changed_files,
                    gpointer            user_data)
{
    NautilusGridCell *self = NAUTILUS_GRID_CELL (user_data);
    g_autoptr (NautilusViewItem) item = NULL;
    NautilusFile *file;

    item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    if (item == NULL)
    {
        return;
    }

    file = nautilus_view_item_get_file (item);
    if (g_list_find (changed_files, file))
    {
        update_emblems (self);
    }
}

static void
nautilus_grid_cell_dispose (GObject *object)
{
    NautilusGridCell *self = (NautilusGridCell *) object;

    gtk_widget_dispose_template (GTK_WIDGET (self), NAUTILUS_TYPE_GRID_CELL);
    g_clear_object (&self->item_signal_group);

    G_OBJECT_CLASS (nautilus_grid_cell_parent_class)->dispose (object);
}

#define EMBLEMS_BOX_WIDTH 18
#define VERTICAL_PADDING 6

/* ------------------------------------------ Ratings and tags on tiles
 *
 * Drawn, not widgets, so big folders stay fast. Rated tiles show their
 * stars; holding Alt over any tile shows them too, to click one. Tags are
 * dots in the picture's top corner.
 */

static gboolean
stars_bounds (NautilusGridCell *self,
              graphene_rect_t  *bounds)
{
    guint icon_size;
    int width = gtk_widget_get_width (GTK_WIDGET (self));

    g_object_get (self, "icon-size", &icon_size, NULL);

    float star = CLAMP (icon_size / 8.0f, 11.0f, 18.0f);

    if (icon_size < 48)
    {
        return FALSE;
    }

    *bounds = GRAPHENE_RECT_INIT ((width - 5 * star) / 2.0f, icon_size - star - 4,
                                  5 * star, star);
    return TRUE;
}

static NautilusFile *
cell_file (NautilusGridCell *self)
{
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));

    return item != NULL ? nautilus_view_item_get_file (item) : NULL;
}

/* Whether the stars are showing, and so clickable. */

/* The settings the tiles draw by, read once per change rather than per
 * frame and tile. */
static struct
{
    gboolean ready, ratings, tags, marks_on_tiles;
} marks_settings;

static void
read_marks_settings (void)
{
    GSettings *flier = nautilus_flier_settings ();

    marks_settings.ratings = g_settings_get_boolean (flier, "ratings");
    marks_settings.tags = g_settings_get_boolean (flier, "tags");
    marks_settings.marks_on_tiles = g_settings_get_boolean (flier, "marks-on-tiles");
}

static void
ensure_marks_settings (void)
{
    if (!marks_settings.ready)
    {
        GSettings *flier = nautilus_flier_settings ();

        marks_settings.ready = TRUE;
        read_marks_settings ();
        g_signal_connect (flier, "changed::ratings", G_CALLBACK (read_marks_settings), NULL);
        g_signal_connect (flier, "changed::tags", G_CALLBACK (read_marks_settings), NULL);
        g_signal_connect (flier, "changed::marks-on-tiles", G_CALLBACK (read_marks_settings), NULL);
    }
}

static gboolean
stars_active (NautilusGridCell *self,
              NautilusFile     *file)
{
    ensure_marks_settings ();
    if (file == NULL || !marks_settings.ratings)
    {
        return FALSE;
    }

    if (self->hovered && nautilus_labels_get_alt_held ())
    {
        return TRUE;
    }

    return nautilus_file_get_rating (file) > 0 && marks_settings.marks_on_tiles;
}

static void
snapshot_marks (NautilusGridCell *self,
                GtkSnapshot      *snapshot)
{
    NautilusFile *file = cell_file (self);
    graphene_rect_t stars;

    if (file == NULL)
    {
        return;
    }

    if (stars_active (self, file) && stars_bounds (self, &stars))
    {
        GskRoundedRect pill;
        graphene_rect_t back;
        GdkRGBA fg, bg = { 0, 0, 0, 0.55f };

        gtk_widget_get_color (GTK_WIDGET (self), &fg);
        graphene_rect_inset_r (&stars, -4, -2, &back);
        gsk_rounded_rect_init_from_rect (&pill, &back, back.size.height / 2);
        gtk_snapshot_push_rounded_clip (snapshot, &pill);
        gtk_snapshot_append_color (snapshot, &bg, &back);
        gtk_snapshot_pop (snapshot);

        nautilus_labels_draw_stars (snapshot, &stars, nautilus_file_get_rating (file),
                                    &(GdkRGBA){ 1, 1, 1, 0.75f },
                                    gtk_widget_get_scale_factor (GTK_WIDGET (self)));
    }

    const char * const *tags = nautilus_file_get_tags (file);

    ensure_marks_settings ();
    if (tags != NULL && marks_settings.tags && marks_settings.marks_on_tiles)
    {
        guint icon_size;

        g_object_get (self, "icon-size", &icon_size, NULL);

        float dot = CLAMP (icon_size / 12.0f, 7.0f, 12.0f);
        float x = EMBLEMS_BOX_WIDTH + 4;

        for (guint i = 0; tags[i] != NULL && i < 6; i++, x += dot * 0.75f)
        {
            GdkRGBA color;
            GskRoundedRect circle;
            graphene_rect_t r = GRAPHENE_RECT_INIT (x, 4, dot, dot);
            const GdkRGBA ring = { 1, 1, 1, 0.9f };

            nautilus_labels_get_tag_color (tags[i], &color);
            gsk_rounded_rect_init_from_rect (&circle, &r, dot / 2);
            gtk_snapshot_push_rounded_clip (snapshot, &circle);
            gtk_snapshot_append_color (snapshot, &color, &r);
            gtk_snapshot_pop (snapshot);
            gtk_snapshot_append_border (snapshot, &circle,
                                        (float[4]) { 1, 1, 1, 1 },
                                        (GdkRGBA[4]) { ring, ring, ring, ring });
        }
    }
}

static void
on_marks_pressed (GtkGestureClick  *gesture,
                  int               n_press,
                  double            x,
                  double            y,
                  NautilusGridCell *self)
{
    NautilusFile *file = cell_file (self);
    graphene_rect_t stars;
    guint star;

    if (n_press != 1 || !stars_active (self, file) || !stars_bounds (self, &stars) ||
        y < stars.origin.y - 2 || y > stars.origin.y + stars.size.height + 2 ||
        (star = nautilus_labels_star_at (&stars, x)) == 0)
    {
        gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }

    /* Clicking the current rating clears it. */
    if (star == nautilus_file_get_rating (file))
    {
        star = 0;
    }

    gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    nautilus_labels_set_rating (&(GList){ .data = file }, star);
}

static void
on_hover_changed (NautilusGridCell *self,
                  gboolean          hovered)
{
    self->hovered = hovered;
    if (nautilus_labels_get_alt_held ())
    {
        gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

static void
on_pointer_enter (GtkEventControllerMotion *motion,
                  double                    x,
                  double                    y,
                  NautilusGridCell         *self)
{
    on_hover_changed (self, TRUE);
}

static void
on_pointer_leave (GtkEventControllerMotion *motion,
                  NautilusGridCell         *self)
{
    on_hover_changed (self, FALSE);
}

static void
on_alt_held_changed (NautilusGridCell *self)
{
    if (self->hovered)
    {
        gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

static void
nautilus_grid_cell_measure (GtkWidget      *widget,
                            GtkOrientation  orientation,
                            int             for_size,
                            int            *minimum,
                            int            *natural,
                            int            *min_baseline,
                            int            *nat_baseline)
{
    NautilusGridCell *self = NAUTILUS_GRID_CELL (widget);
    guint icon_size;
    int width, child_min, child_nat;

    g_object_get (self, "icon-size", &icon_size, NULL);
    width = EMBLEMS_BOX_WIDTH + icon_size + EMBLEMS_BOX_WIDTH;

    /* We always expect our layout to fit into this width. Check that
     * this is indeed the case. This also shuts up the
     *     Allocating size to ... without calling gtk_widget_measure().
     *     How does the code know the size to allocate?
     * warning that we get in debug builds of GTK otherwise. The answer
     * to that question is: we know that the icon is a GtkPicture, and
     * we set the icon_paintable of the specific size on it.
     */
    if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        gtk_widget_measure (self->labels_box, orientation, -1,
                            &child_min, NULL, NULL, NULL);
        if (G_UNLIKELY (child_min > width))
        {
            g_warning ("%s %p unexpectedly doesn't fit into width of %d",
                       gtk_widget_get_name (self->labels_box), self->labels_box, width);
            width = child_min;
        }
        gtk_widget_measure (self->icon, orientation, -1,
                            &child_min, NULL, NULL, NULL);
        if (G_UNLIKELY (child_min > (int) icon_size))
        {
            g_warning ("%s %p unexpectedly doesn't fit into width of %d",
                       gtk_widget_get_name (self->icon), self->icon, icon_size);
            width += child_min - icon_size;
        }
        gtk_widget_measure (self->emblems_box, orientation, -1,
                            &child_min, NULL, NULL, NULL);
        if (G_UNLIKELY (child_min > EMBLEMS_BOX_WIDTH))
        {
            g_warning ("%s %p unexpectedly doesn't fit into width of %d",
                       gtk_widget_get_name (self->emblems_box), self->emblems_box, EMBLEMS_BOX_WIDTH);
            width += 2 * (child_min - EMBLEMS_BOX_WIDTH);
        }
    }
    else /* GTK_ORIENTATION_VERTICAL */
    {
        gtk_widget_measure (self->icon, orientation, icon_size,
                            &child_min, NULL, NULL, NULL);
        if (G_UNLIKELY (child_min > (int) icon_size))
        {
            g_warning ("%s %p unexpectedly doesn't fit into height of %d",
                       gtk_widget_get_name (self->icon), self->icon, icon_size);
        }
        gtk_widget_measure (self->emblems_box, orientation, EMBLEMS_BOX_WIDTH,
                            &child_min, NULL, NULL, NULL);
        if (G_UNLIKELY (child_min > (int) icon_size))
        {
            g_warning ("%s %p unexpectedly doesn't fit into height of %d",
                       gtk_widget_get_name (self->emblems_box), self->emblems_box, icon_size);
        }
    }

    if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        *natural = *minimum = width;
        *min_baseline = *nat_baseline = -1;
        return;
    }

    gtk_widget_measure (self->labels_box, GTK_ORIENTATION_VERTICAL, width,
                        &child_min, &child_nat,
                        min_baseline, nat_baseline);

    *minimum = icon_size + VERTICAL_PADDING + child_min;
    *natural = icon_size + VERTICAL_PADDING + child_nat;
    if (*min_baseline != -1)
    {
        *min_baseline += icon_size + VERTICAL_PADDING;
    }
    if (*nat_baseline != -1)
    {
        *nat_baseline += icon_size + VERTICAL_PADDING;
    }
}

static void
nautilus_grid_cell_size_allocate (GtkWidget *widget,
                                  int        width,
                                  int        height,
                                  int        baseline)
{
    NautilusGridCell *self = NAUTILUS_GRID_CELL (widget);
    GtkAllocation child_allocation;
    guint icon_size;

    g_object_get (self, "icon-size", &icon_size, NULL);

    /* Put the icon at the top. Center it horizontally, with
     * EMBLEMS_BOX_WIDTH horizontal margins on both sides.
     * Note that the icon is expected to center itself further
     * inside the allocation that we give it.
     */
    child_allocation = (GtkAllocation) {
        EMBLEMS_BOX_WIDTH, 0,
        width - EMBLEMS_BOX_WIDTH * 2, icon_size
    };
    gtk_widget_size_allocate (self->icon, &child_allocation, -1);

    /* Put the emblems_box into the "end" margin. */
    child_allocation.width = EMBLEMS_BOX_WIDTH;
    if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_LTR)
    {
        child_allocation.x = width - EMBLEMS_BOX_WIDTH;
    }
    else
    {
        child_allocation.x = 0;
    }
    gtk_widget_size_allocate (self->emblems_box, &child_allocation, -1);

    /* Give the remaining space to labels box. */
    child_allocation = (GtkAllocation) {
        0, icon_size + VERTICAL_PADDING,
        width, height - (icon_size + VERTICAL_PADDING)
    };
    if (baseline != -1)
    {
        baseline -= icon_size + VERTICAL_PADDING;
    }
    gtk_widget_size_allocate (self->labels_box, &child_allocation, baseline);
}

static void
snapshot (GtkWidget   *widget,
          GtkSnapshot *snapshot)
{
    NautilusGridCell *self = NAUTILUS_GRID_CELL (widget);
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    gboolean is_cut;

    g_object_get (item, "is-cut", &is_cut, NULL);

    if (is_cut)
    {
        AdwStyleManager *style_manager = adw_style_manager_get_default ();
        gboolean is_high_contrast = adw_style_manager_get_high_contrast (style_manager);
        guint icon_size;
        graphene_rect_t dash_bounds, icon_bounds;
        GdkRGBA color, dashed_border_color, icon_color;
        const double border_opacity = is_high_contrast ? 0.5 : 0.15;
        const double dim_opacity = is_high_contrast ? 0.9 : 0.55;

        g_object_get (self, "icon-size", &icon_size, NULL);
        dash_bounds = GRAPHENE_RECT_INIT (EMBLEMS_BOX_WIDTH, 0, icon_size, icon_size);
        graphene_rect_inset_r (&dash_bounds, 0.2 * icon_size, 0.2 * icon_size, &icon_bounds);
        gtk_widget_get_color (widget, &color);

        dashed_border_color = color;
        dashed_border_color.alpha *= border_opacity;
        nautilus_ui_draw_icon_dashed_border (snapshot, &dash_bounds, dashed_border_color);

        icon_color = color;
        icon_color.alpha *= dim_opacity;
        nautilus_ui_draw_symbolic_icon (snapshot,
                                        "cut-large-symbolic",
                                        &icon_bounds,
                                        icon_color,
                                        gtk_widget_get_scale_factor (widget));
    }

    GTK_WIDGET_CLASS (nautilus_grid_cell_parent_class)->snapshot (widget, snapshot);

    snapshot_marks (self, snapshot);
}

static void
nautilus_grid_cell_class_init (NautilusGridCellClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = nautilus_grid_cell_dispose;

    widget_class->measure = nautilus_grid_cell_measure;
    widget_class->size_allocate = nautilus_grid_cell_size_allocate;

    widget_class->snapshot = snapshot;

    g_type_ensure (NAUTILUS_TYPE_IMAGE);

    gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/nautilus/ui/nautilus-grid-cell.ui");

    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, icon);
    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, emblems_box);
    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, labels_box);
    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, first_caption);
    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, second_caption);
    gtk_widget_class_bind_template_child (widget_class, NautilusGridCell, third_caption);

    gtk_widget_class_bind_template_callback (widget_class, on_label_query_tooltip);

    gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_NONE);
}

static void
nautilus_grid_cell_init (NautilusGridCell *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));

    g_signal_connect (self, "notify::icon-size",
                      G_CALLBACK (on_icon_size_changed), NULL);
    g_signal_connect (self, "notify::scale-factor", G_CALLBACK (on_icon_size_changed), NULL);

    g_signal_connect_object (nautilus_tag_manager_get (), "starred-changed",
                             G_CALLBACK (on_starred_changed), self, G_CONNECT_DEFAULT);

    g_signal_connect_object (nautilus_preferences, "changed::" NAUTILUS_PREFERENCES_DATE_TIME_FORMAT,
                             G_CALLBACK (update_captions), self,
                             G_CONNECT_SWAPPED);

    /* Ratings and tags drawn on the tile. */
    GtkEventController *motion = gtk_event_controller_motion_new ();
    GtkGesture *click = gtk_gesture_click_new ();

    g_signal_connect (motion, "enter", G_CALLBACK (on_pointer_enter), self);
    g_signal_connect (motion, "leave", G_CALLBACK (on_pointer_leave), self);
    gtk_widget_add_controller (GTK_WIDGET (self), motion);
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), GDK_BUTTON_PRIMARY);
    gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (click), GTK_PHASE_CAPTURE);
    g_signal_connect (click, "pressed", G_CALLBACK (on_marks_pressed), self);
    gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (click));

    g_signal_connect_object (nautilus_labels_get (), "notify::alt-held",
                             G_CALLBACK (on_alt_held_changed), self, G_CONNECT_SWAPPED);
    g_signal_connect_object (nautilus_labels_get (), "tags-changed",
                             G_CALLBACK (gtk_widget_queue_draw), self, G_CONNECT_SWAPPED);
    for (guint i = 0; i < 3; i++)
    {
        static const char *keys[] = { "changed::marks-on-tiles", "changed::ratings", "changed::tags" };

        g_signal_connect_object (nautilus_flier_settings (), keys[i],
                                 G_CALLBACK (gtk_widget_queue_draw), self, G_CONNECT_SWAPPED);
    }

    /* Connect automatically to an item. */
    self->item_signal_group = g_signal_group_new (NAUTILUS_TYPE_VIEW_ITEM);
    g_signal_group_connect_swapped (self->item_signal_group, "notify::is-cut",
                                    (GCallback) update_icon, self);
    g_signal_group_connect_swapped (self->item_signal_group, "notify::is-cut",
                                    (GCallback) gtk_widget_queue_draw, self);
    g_signal_group_connect_swapped (self->item_signal_group, "file-changed",
                                    (GCallback) on_file_changed, self);
    g_signal_connect_object (self->item_signal_group, "bind",
                             (GCallback) on_file_changed, self,
                             G_CONNECT_SWAPPED);

    g_object_bind_property (self, "item",
                            self->item_signal_group, "target",
                            G_BINDING_SYNC_CREATE);
}

NautilusGridCell *
nautilus_grid_cell_new (NautilusListBase *view)
{
    return g_object_new (NAUTILUS_TYPE_GRID_CELL,
                         "view", view,
                         NULL);
}

void
nautilus_grid_cell_set_caption_attributes (NautilusGridCell *self,
                                           GQuark           *attrs)
{
    self->caption_attributes = attrs;
}
