/*
 * nautilus-marks-cell.c: the Rating and Tags columns of the list view
 *
 * Both are drawn rather than built from widgets: a long list has one of
 * these per row, and most rows have neither a rating nor tags.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nautilus-marks-cell.h"

#include <glib/gi18n.h>

#include "nautilus-file.h"
#include "nautilus-labels.h"
#include "nautilus-view-item.h"

#define STAR_SIZE 16
#define DOT_SIZE 10
#define DOT_SPACING 4

struct _NautilusMarksCell
{
    NautilusViewCell parent_instance;

    NautilusMarksCellKind kind;
    GSignalGroup *item_signal_group;
    gboolean hovered;

    /* Tags: the names, ellipsized after the dots. */
    PangoLayout *layout;
};

G_DEFINE_TYPE (NautilusMarksCell, nautilus_marks_cell, NAUTILUS_TYPE_VIEW_CELL)

static NautilusFile *
cell_file (NautilusMarksCell *self)
{
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));

    return item != NULL ? nautilus_view_item_get_file (item) : NULL;
}

static graphene_rect_t
stars_bounds (NautilusMarksCell *self)
{
    int height = gtk_widget_get_height (GTK_WIDGET (self));

    return GRAPHENE_RECT_INIT (0, (height - STAR_SIZE) / 2.0f, 5 * STAR_SIZE, STAR_SIZE);
}

static void
update_tags_text (NautilusMarksCell *self)
{
    NautilusFile *file = cell_file (self);
    const char * const *tags = file != NULL ? nautilus_file_get_tags (file) : NULL;
    g_autofree char *text = tags != NULL ? g_strjoinv (", ", (char **) tags) : NULL;

    if (g_strcmp0 (pango_layout_get_text (self->layout), text != NULL ? text : "") != 0)
    {
        pango_layout_set_text (self->layout, text != NULL ? text : "", -1);
        gtk_widget_set_tooltip_text (GTK_WIDGET (self), text);
        gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

static void
on_file_changed (NautilusMarksCell *self)
{
    if (self->kind == NAUTILUS_MARKS_CELL_TAGS)
    {
        update_tags_text (self);
    }
    else
    {
        gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

static void
nautilus_marks_cell_measure (GtkWidget      *widget,
                             GtkOrientation  orientation,
                             int             for_size,
                             int            *minimum,
                             int            *natural,
                             int            *minimum_baseline,
                             int            *natural_baseline)
{
    NautilusMarksCell *self = NAUTILUS_MARKS_CELL (widget);

    if (orientation == GTK_ORIENTATION_VERTICAL)
    {
        *minimum = *natural = STAR_SIZE;
    }
    else if (self->kind == NAUTILUS_MARKS_CELL_RATING)
    {
        *minimum = *natural = 5 * STAR_SIZE;
    }
    else
    {
        /* The column sets the width; the names ellipsize. */
        *minimum = DOT_SIZE;
        *natural = 120;
    }
}

static void
snapshot_tags (NautilusMarksCell *self,
               GtkSnapshot       *snapshot,
               NautilusFile      *file)
{
    const char * const *tags = nautilus_file_get_tags (file);
    int width = gtk_widget_get_width (GTK_WIDGET (self));
    int height = gtk_widget_get_height (GTK_WIDGET (self));
    float x = 0;
    GdkRGBA fg;
    int text_height;

    if (tags == NULL)
    {
        return;
    }

    for (guint i = 0; tags[i] != NULL && x + DOT_SIZE <= width; i++)
    {
        GdkRGBA color;
        GskRoundedRect circle;
        graphene_rect_t r = GRAPHENE_RECT_INIT (x, (height - DOT_SIZE) / 2.0f, DOT_SIZE, DOT_SIZE);

        nautilus_labels_get_tag_color (tags[i], &color);
        gsk_rounded_rect_init_from_rect (&circle, &r, DOT_SIZE / 2.0f);
        gtk_snapshot_push_rounded_clip (snapshot, &circle);
        gtk_snapshot_append_color (snapshot, &color, &r);
        gtk_snapshot_pop (snapshot);

        /* Several tags overlap a little, like File Flier's. */
        x += tags[i + 1] != NULL ? DOT_SIZE * 0.7f : DOT_SIZE + DOT_SPACING;
    }

    if (x >= width)
    {
        return;
    }

    pango_layout_set_width (self->layout, (width - x) * PANGO_SCALE);
    pango_layout_get_pixel_size (self->layout, NULL, &text_height);
    gtk_widget_get_color (GTK_WIDGET (self), &fg);
    fg.alpha *= 0.7f;

    gtk_snapshot_save (snapshot);
    gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (x, (height - text_height) / 2.0f));
    gtk_snapshot_append_layout (snapshot, self->layout, &fg);
    gtk_snapshot_restore (snapshot);
}

static void
nautilus_marks_cell_snapshot (GtkWidget   *widget,
                              GtkSnapshot *snapshot)
{
    NautilusMarksCell *self = NAUTILUS_MARKS_CELL (widget);
    NautilusFile *file = cell_file (self);

    if (file == NULL)
    {
        return;
    }

    if (self->kind == NAUTILUS_MARKS_CELL_TAGS)
    {
        snapshot_tags (self, snapshot, file);
        return;
    }

    guint rating = nautilus_file_get_rating (file);

    /* Unrated rows stay empty until hovered, to keep the list calm. */
    if (rating == 0 && !self->hovered)
    {
        return;
    }

    GdkRGBA empty;
    graphene_rect_t bounds = stars_bounds (self);

    gtk_widget_get_color (widget, &empty);
    empty.alpha *= self->hovered ? 0.45f : 0.25f;
    nautilus_labels_draw_stars (snapshot, &bounds, rating, &empty,
                                gtk_widget_get_scale_factor (widget));
}

static void
on_pressed (GtkGestureClick   *gesture,
            int                n_press,
            double             x,
            double             y,
            NautilusMarksCell *self)
{
    NautilusFile *file = cell_file (self);
    graphene_rect_t bounds = stars_bounds (self);
    guint star;

    if (n_press != 1 || file == NULL || !nautilus_file_can_write (file) ||
        (star = nautilus_labels_star_at (&bounds, x)) == 0)
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
on_enter (GtkEventControllerMotion *motion,
          double                    x,
          double                    y,
          NautilusMarksCell        *self)
{
    self->hovered = TRUE;
    gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
on_leave (GtkEventControllerMotion *motion,
          NautilusMarksCell        *self)
{
    self->hovered = FALSE;
    gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
setup_kind (NautilusMarksCell *self)
{
    if (self->kind == NAUTILUS_MARKS_CELL_RATING)
    {
        GtkEventController *motion = gtk_event_controller_motion_new ();
        GtkGesture *click = gtk_gesture_click_new ();

        g_signal_connect (motion, "enter", G_CALLBACK (on_enter), self);
        g_signal_connect (motion, "leave", G_CALLBACK (on_leave), self);
        gtk_widget_add_controller (GTK_WIDGET (self), motion);

        gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), GDK_BUTTON_PRIMARY);
        gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (click), GTK_PHASE_CAPTURE);
        g_signal_connect (click, "pressed", G_CALLBACK (on_pressed), self);
        gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (click));
    }
    else
    {
        self->layout = gtk_widget_create_pango_layout (GTK_WIDGET (self), NULL);
        pango_layout_set_ellipsize (self->layout, PANGO_ELLIPSIZE_END);
        g_signal_connect_object (nautilus_labels_get (), "tags-changed",
                                 G_CALLBACK (gtk_widget_queue_draw), self, G_CONNECT_SWAPPED);
    }
}

static void
nautilus_marks_cell_init (NautilusMarksCell *self)
{
    self->item_signal_group = g_signal_group_new (NAUTILUS_TYPE_VIEW_ITEM);
    g_signal_group_connect_swapped (self->item_signal_group, "file-changed",
                                    (GCallback) on_file_changed, self);
    g_signal_connect_object (self->item_signal_group, "bind",
                             (GCallback) on_file_changed, self,
                             G_CONNECT_SWAPPED);
    g_object_bind_property (self, "item",
                            self->item_signal_group, "target",
                            G_BINDING_SYNC_CREATE);
}

static void
nautilus_marks_cell_dispose (GObject *object)
{
    NautilusMarksCell *self = NAUTILUS_MARKS_CELL (object);

    g_clear_object (&self->item_signal_group);
    g_clear_object (&self->layout);

    G_OBJECT_CLASS (nautilus_marks_cell_parent_class)->dispose (object);
}

static void
nautilus_marks_cell_class_init (NautilusMarksCellClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = nautilus_marks_cell_dispose;
    widget_class->measure = nautilus_marks_cell_measure;
    widget_class->snapshot = nautilus_marks_cell_snapshot;
}

NautilusViewCell *
nautilus_marks_cell_new (NautilusListBase      *view,
                         NautilusMarksCellKind  kind)
{
    NautilusMarksCell *self = g_object_new (NAUTILUS_TYPE_MARKS_CELL,
                                            "view", view,
                                            NULL);

    self->kind = kind;
    setup_kind (self);

    return NAUTILUS_VIEW_CELL (self);
}
