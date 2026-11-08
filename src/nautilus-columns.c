/*
 * nautilus-columns.c: Miller columns around a slot's list view
 *
 * Laid out like File Flier's: the folder being looked at stays the real
 * list view, with everything it can do, as the middle column. To its left
 * are the folders above it, each with the way down highlighted; to its
 * right, what's inside the selected folder, or details of the selected
 * file. All columns share the width equally; when they no longer fit, the
 * oldest folders above drop off. Clicking an item in a side column shows
 * it (its folder becomes the middle column, with it selected); a double
 * click goes into it or opens it. Left and Right in the list go up and
 * into the selected folder.
 *
 * The side columns are light: a GtkDirectoryList each, with a few file
 * attributes, no thumbnails and no file monitoring beyond GIO's.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nautilus-columns.h"

#include <adwaita.h>
#include <glib/gi18n.h>

#include "nautilus-file.h"
#include "nautilus-files-view.h"
#include "nautilus-enums.h"
#include "nautilus-icon-info.h"
#include "nautilus-labels.h"
#include "nautilus-metadata.h"
#include "nautilus-mime-actions.h"
#include "nautilus-list-base.h"
#include "nautilus-list-view.h"
#include "nautilus-query.h"
#include "nautilus-view-info.h"
#include "nautilus-window-slot.h"

#define COLUMN_WIDTH 220
#define INPUT_IS_OURS_US (1500 * G_TIME_SPAN_MILLISECOND)
#define ATTRIBUTES G_FILE_ATTRIBUTE_STANDARD_NAME "," \
    G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME "," \
    G_FILE_ATTRIBUTE_STANDARD_TYPE "," \
    G_FILE_ATTRIBUTE_STANDARD_ICON "," \
    "metadata::" NAUTILUS_METADATA_KEY_FOLDER_COLOR "," \
    G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN "," \
    G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP

struct _NautilusColumns
{
    GtkWidget parent_instance;

    NautilusWindowSlot *slot;   /* weak */
    GtkWidget *content;

    GPtrArray *parents;         /* the side columns on the left, outermost first */
    GtkWidget *child_column;    /* the column on the right */
    GFile *child_shown;         /* the folder it lists, if it lists one */
    GFile *child_item;          /* what it shows */
    gint64 last_input;          /* when a click or key last went to the columns */

    GFile *shown_location;
    guint update_id;

    /* The leftmost column: the folder last opened from outside the columns
     * (sidebar, double-click, path bar, history). Moving within the columns
     * keeps it, unless you go above it. */
    GFile *root;
    gboolean moving_inside;   /* the next location change is ours */

    /* Columns mode, per tab. New tabs start the way the last one was set. */
    gboolean enabled;
    gboolean tree;
    guint mode;
    gboolean flat;
    double wheel_amount;
    gint64 wheel_quiet_until;
};

enum
{
    PROP_0,
    PROP_MODE,
    PROP_FLAT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (NautilusColumns, nautilus_columns, GTK_TYPE_WIDGET)

static gboolean
columns_on (NautilusColumns *self)
{
    NautilusFilesView *view;

    if (self->slot == NULL || !self->enabled)
    {
        return FALSE;
    }

    view = nautilus_window_slot_get_current_view (self->slot);

    return view != NULL && nautilus_files_view_get_view_id (view) == NAUTILUS_VIEW_LIST_ID &&
           nautilus_window_slot_get_location (self->slot) != NULL &&
           g_file_is_native (nautilus_window_slot_get_location (self->slot));
}

static gboolean
show_hidden (void)
{
    g_autoptr (GSettings) gtk = g_settings_new ("org.gtk.gtk4.Settings.FileChooser");

    return g_settings_get_boolean (gtk, "show-hidden");
}

/* ------------------------------------------------------------ A column */

typedef struct
{
    NautilusColumns *columns;
    GFile *directory;
    char *highlight;      /* name of the child on the way down, if any */
    GtkSingleSelection *selection;
    GtkDirectoryList *list;
    GtkWidget *view;
    int icon_size;        /* the list view's, when the column was made */
} Column;

static void
column_free (Column *column)
{
    g_object_unref (column->directory);
    g_free (column->highlight);
    g_object_unref (column->list);
    g_free (column);
}

static int
compare_infos (gconstpointer a,
               gconstpointer b,
               gpointer      user_data)
{
    GFileInfo *ia = G_FILE_INFO ((gpointer) a);
    GFileInfo *ib = G_FILE_INFO ((gpointer) b);
    gboolean da = g_file_info_get_file_type (ia) == G_FILE_TYPE_DIRECTORY;
    gboolean db = g_file_info_get_file_type (ib) == G_FILE_TYPE_DIRECTORY;

    if (da != db)
    {
        return da ? -1 : 1;
    }

    return g_utf8_collate (g_file_info_get_display_name (ia), g_file_info_get_display_name (ib));
}

static gboolean
filter_info (gpointer item,
             gpointer user_data)
{
    GFileInfo *info = item;

    return GPOINTER_TO_INT (user_data) ||
           (!g_file_info_get_is_hidden (info) && !g_file_info_get_is_backup (info));
}

static void on_row_pressed (GtkGestureClick *gesture, int n_press, double x, double y, GtkListItem *item);

static void
setup_row (GtkSignalListItemFactory *factory,
           GtkListItem              *item,
           gpointer                  user_data)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    Column *column = user_data;
    GtkWidget *icon = gtk_image_new ();
    GtkWidget *label = gtk_label_new (NULL);
    GtkWidget *arrow = gtk_image_new_from_icon_name ("go-next-symbolic");

    gtk_image_set_pixel_size (GTK_IMAGE (icon), column->icon_size);

    gtk_label_set_xalign (GTK_LABEL (label), 0);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand (label, TRUE);
    gtk_widget_add_css_class (arrow, "dim-label");
    gtk_box_append (GTK_BOX (box), icon);
    gtk_box_append (GTK_BOX (box), label);
    gtk_box_append (GTK_BOX (box), arrow);
    gtk_list_item_set_child (item, box);

    GtkGesture *click = gtk_gesture_click_new ();

    g_object_set_data (G_OBJECT (click), "column", column);
    g_signal_connect (click, "pressed", G_CALLBACK (on_row_pressed), item);
    gtk_widget_add_controller (box, GTK_EVENT_CONTROLLER (click));
}

static void
bind_row (GtkSignalListItemFactory *factory,
          GtkListItem              *item,
          gpointer                  user_data)
{
    GFileInfo *info = gtk_list_item_get_item (item);
    GtkWidget *box = gtk_list_item_get_child (item);
    GtkWidget *icon = gtk_widget_get_first_child (box);
    GtkWidget *label = gtk_widget_get_next_sibling (icon);
    GtkWidget *arrow = gtk_widget_get_next_sibling (label);

    Column *column = user_data;
    GIcon *gicon = g_file_info_get_icon (info);
    g_autoptr (GdkPaintable) paintable = NULL;

    /* The same icons as the files: the theme's, in the folder's color. */
    if (gicon != NULL)
    {
        g_autoptr (NautilusIconInfo) icon_info = nautilus_icon_info_lookup (gicon, column->icon_size,
                                                                           gtk_widget_get_scale_factor (icon));
        const char *color = g_file_info_get_attribute_string (info, "metadata::" NAUTILUS_METADATA_KEY_FOLDER_COLOR);

        paintable = nautilus_icon_info_get_paintable (icon_info);
        if (paintable != NULL && g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY &&
            color != NULL && color[0] >= '0' && color[0] <= '7' && color[1] == '\0')
        {
            GdkPaintable *tinted = nautilus_labels_tint_paintable (paintable, color[0] - '0');

            g_object_unref (paintable);
            paintable = tinted;
        }
    }
    gtk_image_set_from_paintable (GTK_IMAGE (icon), paintable);
    gtk_label_set_text (GTK_LABEL (label), g_file_info_get_display_name (info));
    gtk_widget_set_visible (arrow, g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY);
}

/* Shows the item: its folder becomes the middle column, with it selected. */
static void
reveal_info (Column    *column,
             GFileInfo *info)
{
    NautilusColumns *self = column->columns;
    g_autoptr (GFile) location = g_file_get_child (column->directory, g_file_info_get_name (info));
    g_autolist (NautilusFile) selection = g_list_prepend (NULL, nautilus_file_get (location));

    if (self->slot == NULL)
    {
        return;
    }

    self->moving_inside = TRUE;
    if (self->shown_location != NULL && g_file_equal (column->directory, self->shown_location))
    {
        nautilus_files_view_set_selection (nautilus_window_slot_get_current_view (self->slot), selection, FALSE);
        return;
    }
    nautilus_window_slot_open_location_full (self->slot, column->directory, selection);
}

/* Goes into a folder, opens a file. */
static void
open_info (Column    *column,
           GFileInfo *info)
{
    NautilusColumns *self = column->columns;
    g_autoptr (GFile) location = g_file_get_child (column->directory, g_file_info_get_name (info));

    if (self->slot == NULL)
    {
        return;
    }

    self->moving_inside = TRUE;
    if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
    {
        nautilus_window_slot_open_location_full (self->slot, location, NULL);
    }
    else
    {
        g_autolist (NautilusFile) files = g_list_prepend (NULL, nautilus_file_get (location));
        g_autofree char *dir = g_file_get_path (column->directory);

        nautilus_mime_activate_files (GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (self))), self->slot,
                                      files, dir, 0, TRUE);
    }
}

static GFileInfo *
row_info (GtkListItem *item)
{
    return gtk_list_item_get_item (item);
}

static void
on_row_pressed (GtkGestureClick *gesture,
                int              n_press,
                double           x,
                double           y,
                GtkListItem     *item)
{
    Column *column = g_object_get_data (G_OBJECT (gesture), "column");
    GFileInfo *info = row_info (item);

    if (info == NULL || column == NULL)
    {
        return;
    }

    gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    if (n_press == 2)
    {
        open_info (column, info);
    }
    else if (n_press == 1)
    {
        reveal_info (column, info);
    }
}

/* Enter on a row: as a double click. */
static void
on_activate (GtkListView *view,
             guint        position,
             Column      *column)
{
    g_autoptr (GFileInfo) info = g_list_model_get_item (gtk_single_selection_get_model (column->selection),
                                                        position);

    if (info != NULL)
    {
        open_info (column, info);
    }
}

/* Highlights the way down once the folder has been read. */
static void
highlight (Column *column)
{
    GListModel *model = gtk_single_selection_get_model (column->selection);
    guint n = g_list_model_get_n_items (model);

    if (column->highlight == NULL)
    {
        return;
    }

    for (guint i = 0; i < n; i++)
    {
        g_autoptr (GFileInfo) info = g_list_model_get_item (model, i);

        if (g_strcmp0 (g_file_info_get_name (info), column->highlight) == 0)
        {
            gtk_single_selection_set_selected (column->selection, i);
            gtk_list_view_scroll_to (GTK_LIST_VIEW (column->view), i, GTK_LIST_SCROLL_NONE, NULL);
            return;
        }
    }
}

static void
on_loading (GtkDirectoryList *list,
            GParamSpec       *pspec,
            Column           *column)
{
    if (!gtk_directory_list_is_loading (list))
    {
        highlight (column);
    }
}

/* The size of the list view's icons, so the columns match it. */
static int
list_icon_size (NautilusColumns *self)
{
    NautilusFilesView *view = self->slot != NULL ? nautilus_window_slot_get_current_view (self->slot) : NULL;

    if (view == NULL)
    {
        return NAUTILUS_LIST_ICON_SIZE_SMALL;
    }

    switch (nautilus_list_base_get_zoom_level (nautilus_files_view_get_private_list_base (view)))
    {
        case NAUTILUS_LIST_ZOOM_LEVEL_MEDIUM:
        {
            return NAUTILUS_LIST_ICON_SIZE_MEDIUM;
        }

        case NAUTILUS_LIST_ZOOM_LEVEL_LARGE:
        {
            return NAUTILUS_LIST_ICON_SIZE_LARGE;
        }

        default:
        {
            return NAUTILUS_LIST_ICON_SIZE_SMALL;
        }
    }
}

static GtkWidget *
column_new (NautilusColumns *self,
            GFile           *directory,
            const char      *highlight_name)
{
    Column *column = g_new0 (Column, 1);
    GtkSorter *sorter = GTK_SORTER (gtk_custom_sorter_new (compare_infos, NULL, NULL));
    GtkFilter *filter = GTK_FILTER (gtk_custom_filter_new (filter_info, GINT_TO_POINTER (show_hidden ()), NULL));
    GtkSortListModel *sorted;
    GtkFilterListModel *filtered;
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
    GtkWidget *view;
    GtkWidget *scroller = gtk_scrolled_window_new ();
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *title = gtk_label_new (NULL);
    g_autofree char *basename = g_file_get_basename (directory);
    g_autoptr (GFile) home = g_file_new_for_path (g_get_home_dir ());

    column->columns = self;
    column->directory = g_object_ref (directory);
    column->highlight = g_strdup (highlight_name);
    column->list = gtk_directory_list_new (ATTRIBUTES, directory);
    gtk_directory_list_set_monitored (column->list, TRUE);
    g_object_ref (column->list);

    filtered = gtk_filter_list_model_new (G_LIST_MODEL (column->list), filter);
    sorted = gtk_sort_list_model_new (G_LIST_MODEL (filtered), sorter);
    column->selection = gtk_single_selection_new (G_LIST_MODEL (sorted));
    gtk_single_selection_set_autoselect (column->selection, FALSE);
    gtk_single_selection_set_can_unselect (column->selection, TRUE);
    gtk_single_selection_set_selected (column->selection, GTK_INVALID_LIST_POSITION);
    g_signal_connect (column->list, "notify::loading", G_CALLBACK (on_loading), column);

    column->icon_size = list_icon_size (self);
    g_signal_connect (factory, "setup", G_CALLBACK (setup_row), column);
    g_signal_connect (factory, "bind", G_CALLBACK (bind_row), column);
    view = gtk_list_view_new (GTK_SELECTION_MODEL (column->selection), factory);
    column->view = view;
    g_signal_connect (view, "activate", G_CALLBACK (on_activate), column);
    gtk_widget_add_css_class (view, "navigation-sidebar");
    gtk_widget_add_css_class (view, "miller-column");

    gtk_label_set_text (GTK_LABEL (title), g_file_equal (directory, home) ? _("Home")
                                           : g_utf8_validate (basename, -1, NULL) ? basename : "?");
    gtk_label_set_ellipsize (GTK_LABEL (title), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_xalign (GTK_LABEL (title), 0);
    gtk_widget_add_css_class (title, "heading");
    gtk_widget_add_css_class (title, "miller-column-title");

    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroller), view);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand (scroller, TRUE);

    gtk_box_append (GTK_BOX (box), title);
    gtk_box_append (GTK_BOX (box), scroller);
    gtk_widget_add_css_class (box, "miller-column-box");
    g_object_set_data_full (G_OBJECT (box), "column", column, (GDestroyNotify) column_free);

    return box;
}

/* ------------------------------------------------------------ Updating */

static void
clear_box (GtkWidget *box)
{
    GtkWidget *child;

    while ((child = gtk_widget_get_first_child (box)) != NULL)
    {
        gtk_box_remove (GTK_BOX (box), child);
    }
}

static void
clear_parents (NautilusColumns *self,
               guint            from)
{
    while (self->parents->len > from)
    {
        gtk_widget_unparent (g_ptr_array_steal_index (self->parents, self->parents->len - 1));
    }
}

static Column *
column_of (GtkWidget *box)
{
    return g_object_get_data (G_OBJECT (box), "column");
}

static void
update_parents (NautilusColumns *self)
{
    GFile *location = nautilus_window_slot_get_location (self->slot);
    g_autoptr (GPtrArray) chain = g_ptr_array_new_with_free_func (g_object_unref);
    g_autoptr (GFile) child = g_object_ref (location);
    GFile *parent;
    guint keep = 0;

    if (self->shown_location != NULL && g_file_equal (self->shown_location, location))
    {
        return;
    }
    g_set_object (&self->shown_location, location);

    /* Up to the leftmost column's folder. */
    while (self->root != NULL && !g_file_equal (child, self->root) &&
           (parent = g_file_get_parent (child)) != NULL)
    {
        g_ptr_array_insert (chain, 0, g_object_ref (child));
        g_set_object (&child, parent);
        g_object_unref (parent);
    }

    /* chain: from the top's child down to the location; child: the top.
     * Columns that stay the same are kept, with where they were scrolled. */
    GFile *dir = child;

    for (guint i = 0; i < chain->len; i++)
    {
        g_autofree char *name = g_file_get_basename (g_ptr_array_index (chain, i));
        Column *column = i < self->parents->len ? column_of (g_ptr_array_index (self->parents, i)) : NULL;

        if (column != NULL && keep == i && g_file_equal (column->directory, dir) &&
            g_strcmp0 (column->highlight, name) == 0)
        {
            keep++;
        }
        else
        {
            if (keep == i)
            {
                clear_parents (self, i);
            }

            GtkWidget *box = column_new (self, dir, name);

            gtk_widget_add_css_class (box, "miller-parent");
            gtk_widget_insert_before (box, GTK_WIDGET (self),
                                      self->content != NULL ? self->content : self->child_column);
            g_ptr_array_add (self->parents, box);
        }
        dir = g_ptr_array_index (chain, i);
    }
    clear_parents (self, chain->len);
    gtk_widget_queue_allocate (GTK_WIDGET (self));
}

/* The right column for a file: what it is. */
static GtkWidget *
file_details_new (NautilusColumns *self,
                  NautilusFile    *file)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *image = gtk_image_new ();
    GtkWidget *name = gtk_label_new (nautilus_file_get_display_name (file));
    g_autoptr (GdkPaintable) paintable = nautilus_file_get_icon_paintable (file, 96,
                                                                           gtk_widget_get_scale_factor (GTK_WIDGET (self)),
                                                                           NAUTILUS_FILE_ICON_FLAGS_USE_THUMBNAILS);
    const char *attributes[] = { "type", "size", "date_modified" };

    gtk_widget_add_css_class (box, "miller-file-details");
    gtk_image_set_from_paintable (GTK_IMAGE (image), paintable);
    gtk_image_set_pixel_size (GTK_IMAGE (image), 96);
    gtk_widget_set_margin_top (image, 36);
    gtk_widget_set_margin_bottom (image, 12);
    gtk_box_append (GTK_BOX (box), image);

    gtk_label_set_wrap (GTK_LABEL (name), TRUE);
    gtk_label_set_wrap_mode (GTK_LABEL (name), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_justify (GTK_LABEL (name), GTK_JUSTIFY_CENTER);
    gtk_label_set_lines (GTK_LABEL (name), 3);
    gtk_label_set_ellipsize (GTK_LABEL (name), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class (name, "heading");
    gtk_widget_set_margin_start (name, 12);
    gtk_widget_set_margin_end (name, 12);
    gtk_widget_set_margin_bottom (name, 6);
    gtk_box_append (GTK_BOX (box), name);

    for (guint i = 0; i < G_N_ELEMENTS (attributes); i++)
    {
        g_autofree char *value = nautilus_file_get_string_attribute (file, attributes[i]);
        g_autofree char *text = NULL;
        GtkWidget *label;

        if (value == NULL || value[0] == '\0')
        {
            continue;
        }
        text = i == 2 ? g_strdup_printf (_("Modified %s"), value) : g_strdup (value);
        label = gtk_label_new (text);
        gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
        gtk_widget_add_css_class (label, "dim-label");
        gtk_box_append (GTK_BOX (box), label);
    }

    return box;
}

static void
update_child (NautilusColumns *self)
{
    GList *selection = nautilus_window_slot_get_selection (self->slot);
    NautilusFile *file = selection != NULL && selection->next == NULL ? selection->data : NULL;
    g_autoptr (GFile) item = file != NULL ? nautilus_file_get_location (file) : NULL;
    gboolean folder = file != NULL && nautilus_file_is_directory (file);

    if ((item == NULL && self->child_item == NULL) ||
        (item != NULL && self->child_item != NULL && g_file_equal (item, self->child_item)))
    {
        return;
    }

    g_set_object (&self->child_item, item);
    g_clear_object (&self->child_shown);
    if (folder)
    {
        self->child_shown = g_object_ref (item);
    }
    clear_box (self->child_column);
    if (folder)
    {
        GtkWidget *column = column_new (self, item, NULL);

        gtk_widget_set_hexpand (column, TRUE);
        gtk_box_append (GTK_BOX (self->child_column), column);
    }
    else if (file != NULL)
    {
        GtkWidget *details = file_details_new (self, file);

        gtk_widget_set_hexpand (details, TRUE);
        gtk_box_append (GTK_BOX (self->child_column), details);
    }
}

static gboolean
update (gpointer user_data)
{
    NautilusColumns *self = user_data;

    self->update_id = 0;

    if (!columns_on (self))
    {
        gtk_widget_set_visible (self->child_column, FALSE);
        g_clear_object (&self->shown_location);
        g_clear_object (&self->child_shown);
        g_clear_object (&self->child_item);
        clear_parents (self, 0);
        clear_box (self->child_column);
        gtk_widget_queue_allocate (GTK_WIDGET (self));
        return G_SOURCE_REMOVE;
    }

    gtk_widget_set_visible (self->child_column, TRUE);
    update_parents (self);
    update_child (self);

    return G_SOURCE_REMOVE;
}

static void
on_location_for_root (NautilusColumns *self)
{
    GFile *location = self->slot != NULL ? nautilus_window_slot_get_location (self->slot) : NULL;
    /* Ours: from the side columns, or a double click, Enter or Backspace in
     * the middle one. The sidebar, path bar and so on start a new chain. */
    gboolean inside = self->moving_inside ||
                      g_get_monotonic_time () - self->last_input < INPUT_IS_OURS_US;

    self->moving_inside = FALSE;
    self->last_input = 0;
    if (location == NULL)
    {
        return;
    }

    if (!inside || self->root == NULL ||
        !(g_file_equal (location, self->root) || g_file_has_prefix (location, self->root)))
    {
        g_set_object (&self->root, location);
    }
}

static void
schedule_update (NautilusColumns *self)
{
    if (self->update_id == 0)
    {
        self->update_id = g_timeout_add (60, update, self);
    }
}

static NautilusFilesView *
files_view (NautilusColumns *self)
{
    return self->slot != NULL ? nautilus_window_slot_get_current_view (self->slot) : NULL;
}

static guint
compute_mode (NautilusColumns *self)
{
    NautilusFilesView *view = files_view (self);

    if (view != NULL && nautilus_files_view_get_view_id (view) == NAUTILUS_VIEW_GRID_ID)
    {
        return NAUTILUS_COLUMNS_MODE_GRID;
    }

    if (self->enabled)
    {
        return NAUTILUS_COLUMNS_MODE_COLUMNS;
    }

    return self->tree ? NAUTILUS_COLUMNS_MODE_TREE : NAUTILUS_COLUMNS_MODE_LIST;
}

gboolean
nautilus_columns_get_names_only (NautilusColumns *self)
{
    return self->enabled;
}

gboolean
nautilus_columns_get_tree (NautilusColumns *self)
{
    return self->tree && !self->enabled;
}

static void
on_view_changed (NautilusColumns *self)
{
    NautilusFilesView *view = files_view (self);
    guint mode;

    /* Columns and tree go with the list: back from the grid is the plain list. */
    if (view != NULL && nautilus_files_view_get_view_id (view) == NAUTILUS_VIEW_GRID_ID)
    {
        self->enabled = FALSE;
        self->tree = FALSE;
    }

    mode = compute_mode (self);
    if (mode != self->mode)
    {
        self->mode = mode;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODE]);
    }

    schedule_update (self);
}

guint
nautilus_columns_get_mode (NautilusColumns *self)
{
    return self->mode;
}

void
nautilus_columns_set_mode (NautilusColumns *self,
                           guint            mode)
{
    NautilusFilesView *view = files_view (self);
    guint view_id = mode == NAUTILUS_COLUMNS_MODE_GRID ? NAUTILUS_VIEW_GRID_ID : NAUTILUS_VIEW_LIST_ID;

    g_return_if_fail (mode >= NAUTILUS_COLUMNS_MODE_LIST && mode <= NAUTILUS_COLUMNS_MODE_GRID);

    if (view == NULL)
    {
        return;
    }

    gboolean was_tree = nautilus_columns_get_tree (self);
    gboolean was_names_only = self->enabled;
    GSettings *flier = nautilus_flier_settings ();

    self->enabled = mode == NAUTILUS_COLUMNS_MODE_COLUMNS;
    self->tree = mode == NAUTILUS_COLUMNS_MODE_TREE;
    /* New tabs start this way. */
    if (g_settings_get_boolean (flier, "columns-view") != self->enabled)
    {
        g_settings_set_boolean (flier, "columns-view", self->enabled);
    }
    if (g_settings_get_boolean (flier, "tree-view") != self->tree)
    {
        g_settings_set_boolean (flier, "tree-view", self->tree);
    }

    g_clear_object (&self->shown_location);
    if (nautilus_files_view_get_view_id (view) != view_id)
    {
        /* Notifies the icon name, which lands in on_view_changed (). The new
         * list view asks for the tree as it sets up. */
        gtk_widget_activate_action (GTK_WIDGET (self->slot), "slot.files-view-mode", "u", view_id);
    }
    else if (was_tree != nautilus_columns_get_tree (self))
    {
        /* The list view takes the tree on when it loads the folder. */
        gtk_widget_activate_action (GTK_WIDGET (self->slot), "slot.reload", NULL);
    }
    on_view_changed (self);

    /* Columns mode shows names only in the list; back out, the others. */
    view = files_view (self);
    if (view != NULL && was_names_only != self->enabled &&
        NAUTILUS_IS_LIST_VIEW (nautilus_files_view_get_private_list_base (view)))
    {
        nautilus_list_view_refresh_visible_columns (NAUTILUS_LIST_VIEW (nautilus_files_view_get_private_list_base (view)));
    }
}

/* ---------------------------------------------------------------- Flat view */

static NautilusQuery *
search_query (NautilusColumns *self)
{
    NautilusFilesView *view = files_view (self);

    if (view == NULL || !nautilus_window_slot_get_search_visible (self->slot))
    {
        return NULL;
    }

    return nautilus_files_view_get_search_query (view);
}

static void
update_flat (NautilusColumns *self)
{
    NautilusQuery *query = search_query (self);
    gboolean flat = query != NULL && nautilus_query_get_flat (query);

    if (flat != self->flat)
    {
        self->flat = flat;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLAT]);
    }
}

gboolean
nautilus_columns_get_flat (NautilusColumns *self)
{
    return self->flat;
}

void
nautilus_columns_set_flat (NautilusColumns *self,
                           gboolean         flat)
{
    NautilusQuery *current = search_query (self);
    g_autoptr (NautilusQuery) query = NULL;
    g_autoptr (GFile) folder = NULL;

    if (self->slot == NULL || flat == self->flat)
    {
        return;
    }

    if (!flat)
    {
        /* Back to the folder. */
        g_object_set (self->slot, "search-visible", FALSE, NULL);
        return;
    }

    /* A search running already keeps its text, as the filter. */
    if (current != NULL)
    {
        query = nautilus_query_copy (current);
    }
    else
    {
        query = nautilus_query_new ();
        nautilus_query_set_location (query, nautilus_window_slot_get_location (self->slot));
    }

    folder = nautilus_query_get_location (query);
    if (folder == NULL)
    {
        /* Search everywhere has no folder to flatten. */
        return;
    }

    nautilus_query_set_flat (query, TRUE);
    nautilus_window_slot_search (self->slot, query);
    /* The search shows before the view has the query. */
    update_flat (self);
}

/* Every file under Home with @tag, as a flat view. */
void
nautilus_columns_show_tag (NautilusColumns *self,
                           const char      *tag)
{
    g_autoptr (NautilusQuery) query = nautilus_query_new ();
    g_autoptr (GFile) home = g_file_new_for_path (g_get_home_dir ());

    if (self->slot == NULL)
    {
        return;
    }

    nautilus_query_set_location (query, home);
    nautilus_query_set_flat (query, TRUE);
    nautilus_query_set_tag (query, tag);
    nautilus_window_slot_search (self->slot, query);
    update_flat (self);
}

/* Like the wheel: list, columns, grid, then bigger grid items; and back. */
void
nautilus_columns_step (NautilusColumns *self,
                       int              direction)
{
    NautilusFilesView *view = files_view (self);
    guint mode = self->mode;

    if (view == NULL || direction == 0)
    {
        return;
    }

    if (mode == NAUTILUS_COLUMNS_MODE_GRID)
    {
        NautilusListBase *base = nautilus_files_view_get_private_list_base (view);
        NautilusViewInfo info = nautilus_list_base_get_view_info (base);
        int zoom = nautilus_list_base_get_zoom_level (base);

        if (direction > 0 && zoom < info.zoom_level_max)
        {
            gtk_widget_activate_action (GTK_WIDGET (view), "view.zoom-in", NULL);
            return;
        }
        /* Smaller than standard stays with Ctrl+minus. */
        if (direction < 0 && zoom > info.zoom_level_standard)
        {
            gtk_widget_activate_action (GTK_WIDGET (view), "view.zoom-out", NULL);
            return;
        }
    }

    mode = CLAMP ((int) mode + (direction > 0 ? 1 : -1),
                  NAUTILUS_COLUMNS_MODE_LIST, NAUTILUS_COLUMNS_MODE_GRID);
    if (mode != self->mode)
    {
        nautilus_columns_set_mode (self, mode);
    }
}

/* Ctrl+wheel steps through the views; Ctrl+Shift+wheel is left to zoom. */
static gboolean
on_wheel (GtkEventControllerScroll *controller,
          double                    dx,
          double                    dy,
          NautilusColumns          *self)
{
    GdkModifierType state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (controller));
    double threshold;
    gint64 now;

    if ((state & gtk_accelerator_get_default_mod_mask ()) != GDK_CONTROL_MASK || files_view (self) == NULL)
    {
        return GDK_EVENT_PROPAGATE;
    }

    /* A notch is 1; touchpads send pixels. */
    threshold = gtk_event_controller_scroll_get_unit (controller) == GDK_SCROLL_UNIT_WHEEL ? 1.0 : 40.0;
    now = g_get_monotonic_time ();
    if (now < self->wheel_quiet_until)
    {
        return GDK_EVENT_STOP;
    }

    self->wheel_amount += dy;
    if (ABS (self->wheel_amount) >= threshold)
    {
        /* Wheel up: the next view, as in Alt+1 to 4. */
        nautilus_columns_step (self, self->wheel_amount < 0 ? 1 : -1);
        self->wheel_amount = 0;
        /* One step per notch, also for smoothed and fast wheels. */
        self->wheel_quiet_until = now + 220 * G_TIME_SPAN_MILLISECOND;
    }

    return GDK_EVENT_STOP;
}

static void
on_wheel_end (NautilusColumns *self)
{
    self->wheel_amount = 0;
}

/* ---------------------------------------------------------------- Keys */

static gboolean
on_key_pressed (GtkEventControllerKey *controller,
                guint                  keyval,
                guint                  keycode,
                GdkModifierType        state,
                NautilusColumns       *self)
{
    GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (self));
    GtkWidget *focus = root != NULL ? gtk_root_get_focus (root) : NULL;

    if (focus != NULL && gtk_widget_is_ancestor (focus, GTK_WIDGET (self)) &&
        !GTK_IS_EDITABLE (focus))
    {
        self->last_input = g_get_monotonic_time ();
    }

    if (!columns_on (self) || (state & gtk_accelerator_get_default_mod_mask ()) != 0 ||
        focus == NULL || GTK_IS_EDITABLE (focus) || GTK_IS_TEXT_VIEW (focus) ||
        self->content == NULL || !gtk_widget_is_ancestor (focus, self->content))
    {
        return GDK_EVENT_PROPAGATE;
    }

    if (keyval == GDK_KEY_Left)
    {
        self->moving_inside = TRUE;
        gtk_widget_activate_action (GTK_WIDGET (self->slot), "slot.up", NULL);
        return GDK_EVENT_STOP;
    }

    if (keyval == GDK_KEY_Right && self->child_shown != NULL)
    {
        self->moving_inside = TRUE;
        nautilus_window_slot_open_location_full (self->slot, self->child_shown, NULL);
        return GDK_EVENT_STOP;
    }

    return GDK_EVENT_PROPAGATE;
}

/* ------------------------------------------------------------- GObject */

void
nautilus_columns_set_content (NautilusColumns *self,
                              GtkWidget       *content)
{
    if (self->content != NULL)
    {
        gtk_widget_unparent (self->content);
    }

    self->content = content;

    if (content != NULL)
    {
        gtk_widget_insert_before (content, GTK_WIDGET (self), self->child_column);
        gtk_widget_set_hexpand (content, TRUE);
    }

    g_clear_object (&self->shown_location);
    clear_parents (self, 0);
    schedule_update (self);
}

static void
on_any_press (NautilusColumns *self)
{
    self->last_input = g_get_monotonic_time ();
}

/* Like File Flier's: every column the same width, the oldest folders above
 * dropping off when they don't fit. */
static void
nautilus_columns_measure (GtkWidget      *widget,
                          GtkOrientation  orientation,
                          int             for_size,
                          int            *minimum,
                          int            *natural,
                          int            *minimum_baseline,
                          int            *natural_baseline)
{
    NautilusColumns *self = NAUTILUS_COLUMNS (widget);

    *minimum = *natural = 0;
    if (self->content != NULL && gtk_widget_get_visible (self->content))
    {
        gtk_widget_measure (self->content, orientation, -1, minimum, natural, NULL, NULL);
    }
    if (orientation == GTK_ORIENTATION_VERTICAL)
    {
        for (GtkWidget *child = gtk_widget_get_first_child (widget); child != NULL;
             child = gtk_widget_get_next_sibling (child))
        {
            int min = 0, nat = 0;

            if (child != self->content && gtk_widget_get_visible (child))
            {
                gtk_widget_measure (child, orientation, -1, &min, &nat, NULL, NULL);
                *minimum = MAX (*minimum, min);
                *natural = MAX (*natural, nat);
            }
        }
    }
}

static void
nautilus_columns_size_allocate (GtkWidget *widget,
                                int        width,
                                int        height,
                                int        baseline)
{
    NautilusColumns *self = NAUTILUS_COLUMNS (widget);
    int content_min = 0;
    int fit = width / COLUMN_WIDTH;
    gboolean side = gtk_widget_get_visible (self->child_column) && fit >= 2;
    guint n_parents = self->parents->len;
    guint n_left = side ? MIN (n_parents, (guint) fit - 2) : 0;
    guint n = side ? n_left + 2 : 1;
    guint k = 0;

    if (self->content != NULL && gtk_widget_get_visible (self->content))
    {
        gtk_widget_measure (self->content, GTK_ORIENTATION_HORIZONTAL, -1, &content_min, NULL, NULL, NULL);
    }

#define COL_X(k) ((int) ((double) width * (k) / n + 0.5))
    for (guint i = 0; i < n_parents; i++)
    {
        GtkWidget *box = g_ptr_array_index (self->parents, i);
        gboolean shown = i >= n_parents - n_left;

        gtk_widget_set_child_visible (box, shown);
        if (shown)
        {
            gtk_widget_size_allocate (box, &(GtkAllocation) { COL_X (k), 0, COL_X (k + 1) - COL_X (k), height },
                                      -1);
            k++;
        }
    }

    if (self->content != NULL && gtk_widget_get_visible (self->content))
    {
        int x = COL_X (k), w = MAX (COL_X (k + 1) - x, content_min);

        if (x + w > width && side)
        {
            x = MAX (0, width - w - (COL_X (n) - COL_X (n - 1)));
        }
        gtk_widget_size_allocate (self->content, &(GtkAllocation) { x, 0, w, height }, -1);
        k++;
    }

    gtk_widget_set_child_visible (self->child_column, side);
    if (side)
    {
        gtk_widget_size_allocate (self->child_column,
                                  &(GtkAllocation) { COL_X (k), 0, width - COL_X (k), height }, -1);
    }
#undef COL_X
}

static void
nautilus_columns_init (NautilusColumns *self)
{
    GtkEventController *controller;

    gtk_widget_add_css_class (GTK_WIDGET (self), "miller-columns");

    self->parents = g_ptr_array_new ();

    controller = GTK_EVENT_CONTROLLER (gtk_gesture_click_new ());
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (controller), 0);
    gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
    g_signal_connect_swapped (controller, "pressed", G_CALLBACK (on_any_press), self);
    gtk_widget_add_controller (GTK_WIDGET (self), controller);

    self->child_column = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class (self->child_column, "miller-child");
    gtk_widget_set_visible (self->child_column, FALSE);
    gtk_widget_set_parent (self->child_column, GTK_WIDGET (self));

    controller = gtk_event_controller_key_new ();
    gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
    g_signal_connect (controller, "key-pressed", G_CALLBACK (on_key_pressed), self);
    gtk_widget_add_controller (GTK_WIDGET (self), controller);

    controller = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
    g_signal_connect (controller, "scroll", G_CALLBACK (on_wheel), self);
    g_signal_connect_swapped (controller, "scroll-end", G_CALLBACK (on_wheel_end), self);
    gtk_widget_add_controller (GTK_WIDGET (self), controller);

    self->enabled = g_settings_get_boolean (nautilus_flier_settings (), "columns-view");
    self->tree = !self->enabled && g_settings_get_boolean (nautilus_flier_settings (), "tree-view");
    self->mode = self->enabled ? NAUTILUS_COLUMNS_MODE_COLUMNS :
                 self->tree ? NAUTILUS_COLUMNS_MODE_TREE : NAUTILUS_COLUMNS_MODE_LIST;
}

static void
nautilus_columns_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    switch (prop_id)
    {
        case PROP_MODE:
        {
            g_value_set_uint (value, NAUTILUS_COLUMNS (object)->mode);
        }
        break;

        case PROP_FLAT:
        {
            g_value_set_boolean (value, NAUTILUS_COLUMNS (object)->flat);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static void
nautilus_columns_dispose (GObject *object)
{
    NautilusColumns *self = NAUTILUS_COLUMNS (object);
    GtkWidget *child;

    g_clear_handle_id (&self->update_id, g_source_remove);
    g_clear_weak_pointer (&self->slot);
    g_clear_object (&self->shown_location);
    g_clear_object (&self->child_shown);
    g_clear_object (&self->child_item);
    g_clear_object (&self->root);
    g_clear_pointer (&self->parents, g_ptr_array_unref);
    self->content = NULL;
    while ((child = gtk_widget_get_first_child (GTK_WIDGET (object))) != NULL)
    {
        gtk_widget_unparent (child);
    }

    G_OBJECT_CLASS (nautilus_columns_parent_class)->dispose (object);
}

static void
nautilus_columns_class_init (NautilusColumnsClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = nautilus_columns_dispose;
    G_OBJECT_CLASS (klass)->get_property = nautilus_columns_get_property;

    properties[PROP_MODE] = g_param_spec_uint ("mode", NULL, NULL,
                                               NAUTILUS_COLUMNS_MODE_LIST, NAUTILUS_COLUMNS_MODE_GRID,
                                               NAUTILUS_COLUMNS_MODE_LIST,
                                               G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
    properties[PROP_FLAT] = g_param_spec_boolean ("flat", NULL, NULL, FALSE,
                                                  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
    g_object_class_install_properties (G_OBJECT_CLASS (klass), N_PROPS, properties);
    GTK_WIDGET_CLASS (klass)->measure = nautilus_columns_measure;
    GTK_WIDGET_CLASS (klass)->size_allocate = nautilus_columns_size_allocate;
    gtk_widget_class_set_css_name (GTK_WIDGET_CLASS (klass), "nautilus-columns");
}

GtkWidget *
nautilus_columns_new (NautilusWindowSlot *slot)
{
    NautilusColumns *self = g_object_new (NAUTILUS_TYPE_COLUMNS, NULL);

    g_set_weak_pointer (&self->slot, slot);
    g_signal_connect_object (slot, "notify::location", G_CALLBACK (on_location_for_root), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (slot, "notify::location", G_CALLBACK (schedule_update), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (slot, "notify::location", G_CALLBACK (update_flat), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (slot, "notify::search-visible", G_CALLBACK (update_flat), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (slot, "notify::selection", G_CALLBACK (schedule_update), self,
                             G_CONNECT_SWAPPED);
    /* Switching between list and grid. */
    g_signal_connect_object (slot, "notify::icon-name", G_CALLBACK (on_view_changed), self,
                             G_CONNECT_SWAPPED);

    return GTK_WIDGET (self);
}
