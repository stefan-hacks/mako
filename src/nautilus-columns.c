/*
 * nautilus-columns.c: Miller columns around a slot's list view
 *
 * The folder being looked at stays the real list view, with everything it
 * can do. Columns mode adds the folders above it as columns on its left,
 * each with the way down highlighted, and the contents of a selected
 * folder as a column on its right. Clicking in a column goes there; Left
 * and Right in the list go up and into the selected folder. Alt+scroll
 * slides the columns sideways.
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
#include "nautilus-labels.h"
#include "nautilus-view-info.h"
#include "nautilus-window-slot.h"

#define COLUMN_WIDTH 220
#define MAX_PARENT_COLUMNS_SHOWN 3
#define ATTRIBUTES G_FILE_ATTRIBUTE_STANDARD_NAME "," \
    G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME "," \
    G_FILE_ATTRIBUTE_STANDARD_TYPE "," \
    G_FILE_ATTRIBUTE_STANDARD_SYMBOLIC_ICON "," \
    G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN "," \
    G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP

struct _NautilusColumns
{
    GtkWidget parent_instance;

    NautilusWindowSlot *slot;   /* weak */
    GtkWidget *content;

    GtkWidget *parents_scroller;
    GtkWidget *parents_box;
    GtkWidget *child_column;
    GFile *child_shown;

    GFile *shown_location;
    guint update_id;
};

G_DEFINE_TYPE (NautilusColumns, nautilus_columns, GTK_TYPE_WIDGET)

static gboolean
columns_on (NautilusColumns *self)
{
    NautilusFilesView *view;

    if (self->slot == NULL || !g_settings_get_boolean (nautilus_flier_settings (), "columns-view"))
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

static void
setup_row (GtkSignalListItemFactory *factory,
           GtkListItem              *item,
           gpointer                  user_data)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *icon = gtk_image_new ();
    GtkWidget *label = gtk_label_new (NULL);
    GtkWidget *arrow = gtk_image_new_from_icon_name ("go-next-symbolic");

    gtk_label_set_xalign (GTK_LABEL (label), 0);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand (label, TRUE);
    gtk_widget_add_css_class (arrow, "dim-label");
    gtk_box_append (GTK_BOX (box), icon);
    gtk_box_append (GTK_BOX (box), label);
    gtk_box_append (GTK_BOX (box), arrow);
    gtk_list_item_set_child (item, box);
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

    gtk_image_set_from_gicon (GTK_IMAGE (icon), g_file_info_get_symbolic_icon (info));
    gtk_label_set_text (GTK_LABEL (label), g_file_info_get_display_name (info));
    gtk_widget_set_visible (arrow, g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY);
}

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

    if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
    {
        nautilus_window_slot_open_location_full (self->slot, location, NULL);
    }
    else
    {
        /* A file: to its folder, with it selected. */
        g_autolist (NautilusFile) selection = g_list_prepend (NULL, nautilus_file_get (location));

        nautilus_window_slot_open_location_full (self->slot, column->directory, selection);
    }
}

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

    g_signal_connect (factory, "setup", G_CALLBACK (setup_row), NULL);
    g_signal_connect (factory, "bind", G_CALLBACK (bind_row), NULL);
    view = gtk_list_view_new (GTK_SELECTION_MODEL (column->selection), factory);
    gtk_list_view_set_single_click_activate (GTK_LIST_VIEW (view), TRUE);
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
    gtk_widget_set_size_request (box, COLUMN_WIDTH, -1);
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

static gboolean
scroll_to_end (gpointer user_data)
{
    NautilusColumns *self = user_data;
    GtkAdjustment *adj = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (self->parents_scroller));

    gtk_adjustment_set_value (adj, gtk_adjustment_get_upper (adj));
    return G_SOURCE_REMOVE;
}

static void
update_parents (NautilusColumns *self)
{
    GFile *location = nautilus_window_slot_get_location (self->slot);
    g_autoptr (GFile) home = g_file_new_for_path (g_get_home_dir ());
    g_autoptr (GPtrArray) chain = g_ptr_array_new_with_free_func (g_object_unref);
    g_autoptr (GFile) child = g_object_ref (location);
    GFile *parent;

    if (self->shown_location != NULL && g_file_equal (self->shown_location, location))
    {
        return;
    }
    g_set_object (&self->shown_location, location);
    clear_box (self->parents_box);

    /* Up to home, if under it; else up to the root. */
    while ((parent = g_file_get_parent (child)) != NULL)
    {
        g_ptr_array_insert (chain, 0, g_object_ref (child));
        g_set_object (&child, parent);
        g_object_unref (parent);
        if (g_file_equal (child, home))
        {
            break;
        }
    }

    /* chain: from the top's child down to the location; child: the top. */
    GFile *dir = child;

    for (guint i = 0; i < chain->len; i++)
    {
        g_autofree char *name = g_file_get_basename (g_ptr_array_index (chain, i));

        gtk_box_append (GTK_BOX (self->parents_box), column_new (self, dir, name));
        gtk_box_append (GTK_BOX (self->parents_box), gtk_separator_new (GTK_ORIENTATION_VERTICAL));
        dir = g_ptr_array_index (chain, i);
    }

    gtk_widget_set_visible (self->parents_scroller, chain->len > 0);
    g_idle_add_full (G_PRIORITY_LOW, scroll_to_end, g_object_ref (self), g_object_unref);
}

static void
update_child (NautilusColumns *self)
{
    GList *selection = nautilus_window_slot_get_selection (self->slot);
    g_autoptr (GFile) folder = NULL;

    if (selection != NULL && selection->next == NULL && nautilus_file_is_directory (selection->data))
    {
        folder = nautilus_file_get_location (selection->data);
    }

    if (folder != NULL && self->child_shown != NULL && g_file_equal (folder, self->child_shown))
    {
        return;
    }

    g_set_object (&self->child_shown, folder);
    clear_box (self->child_column);
    if (folder != NULL)
    {
        gtk_box_append (GTK_BOX (self->child_column), gtk_separator_new (GTK_ORIENTATION_VERTICAL));
        gtk_box_append (GTK_BOX (self->child_column), column_new (self, folder, NULL));
    }
    gtk_widget_set_visible (self->child_column, folder != NULL);
}

static gboolean
update (gpointer user_data)
{
    NautilusColumns *self = user_data;

    self->update_id = 0;

    if (!columns_on (self))
    {
        gtk_widget_set_visible (self->parents_scroller, FALSE);
        gtk_widget_set_visible (self->child_column, FALSE);
        g_clear_object (&self->shown_location);
        g_clear_object (&self->child_shown);
        clear_box (self->parents_box);
        clear_box (self->child_column);
        return G_SOURCE_REMOVE;
    }

    update_parents (self);
    update_child (self);

    return G_SOURCE_REMOVE;
}

static void
schedule_update (NautilusColumns *self)
{
    if (self->update_id == 0)
    {
        self->update_id = g_timeout_add (60, update, self);
    }
}

static void
on_columns_setting (NautilusColumns *self)
{
    NautilusFilesView *view = self->slot != NULL ? nautilus_window_slot_get_current_view (self->slot) : NULL;

    /* Columns go with the list. */
    if (g_settings_get_boolean (nautilus_flier_settings (), "columns-view") && view != NULL &&
        nautilus_files_view_get_view_id (view) == NAUTILUS_VIEW_GRID_ID &&
        gtk_widget_get_mapped (GTK_WIDGET (self)))
    {
        gtk_widget_activate_action (GTK_WIDGET (self->slot), "slot.files-view-mode", "u",
                                    NAUTILUS_VIEW_LIST_ID);
    }

    g_clear_object (&self->shown_location);
    schedule_update (self);
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

    if (!columns_on (self) || (state & gtk_accelerator_get_default_mod_mask ()) != 0 ||
        focus == NULL || GTK_IS_EDITABLE (focus) || GTK_IS_TEXT_VIEW (focus) ||
        !gtk_widget_is_ancestor (focus, self->content))
    {
        return GDK_EVENT_PROPAGATE;
    }

    if (keyval == GDK_KEY_Left)
    {
        gtk_widget_activate_action (GTK_WIDGET (self->slot), "slot.up", NULL);
        return GDK_EVENT_STOP;
    }

    if (keyval == GDK_KEY_Right && self->child_shown != NULL)
    {
        nautilus_window_slot_open_location_full (self->slot, self->child_shown, NULL);
        return GDK_EVENT_STOP;
    }

    return GDK_EVENT_PROPAGATE;
}

/* Alt+scroll slides the columns of the folders above sideways. */
static gboolean
on_parents_scroll (GtkEventControllerScroll *controller,
                   double                    dx,
                   double                    dy,
                   NautilusColumns          *self)
{
    GdkModifierType state = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (controller));
    GtkAdjustment *adj = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (self->parents_scroller));
    double step = gtk_event_controller_scroll_get_unit (controller) == GDK_SCROLL_UNIT_WHEEL ? COLUMN_WIDTH / 2.0 : 1.0;

    if (!(state & GDK_ALT_MASK))
    {
        return GDK_EVENT_PROPAGATE;
    }

    gtk_adjustment_set_value (adj, gtk_adjustment_get_value (adj) + (dy + dx) * step);
    return GDK_EVENT_STOP;
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
        gtk_widget_insert_after (content, GTK_WIDGET (self), self->parents_scroller);
        gtk_widget_set_hexpand (content, TRUE);
    }

    g_clear_object (&self->shown_location);
    schedule_update (self);
}

static void
nautilus_columns_init (NautilusColumns *self)
{
    GtkEventController *controller;

    gtk_widget_add_css_class (GTK_WIDGET (self), "miller-columns");

    self->parents_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    self->parents_scroller = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (self->parents_scroller), self->parents_box);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (self->parents_scroller),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_propagate_natural_width (GTK_SCROLLED_WINDOW (self->parents_scroller), TRUE);
    gtk_scrolled_window_set_max_content_width (GTK_SCROLLED_WINDOW (self->parents_scroller),
                                               MAX_PARENT_COLUMNS_SHOWN * (COLUMN_WIDTH + 1));
    gtk_widget_set_visible (self->parents_scroller, FALSE);
    gtk_widget_set_parent (self->parents_scroller, GTK_WIDGET (self));

    controller = gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
    g_signal_connect (controller, "scroll", G_CALLBACK (on_parents_scroll), self);
    gtk_widget_add_controller (self->parents_scroller, controller);

    self->child_column = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_visible (self->child_column, FALSE);
    gtk_widget_set_parent (self->child_column, GTK_WIDGET (self));

    controller = gtk_event_controller_key_new ();
    gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
    g_signal_connect (controller, "key-pressed", G_CALLBACK (on_key_pressed), self);
    gtk_widget_add_controller (GTK_WIDGET (self), controller);

    g_signal_connect_object (nautilus_flier_settings (), "changed::columns-view",
                             G_CALLBACK (on_columns_setting), self, G_CONNECT_SWAPPED);
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
    gtk_widget_class_set_layout_manager_type (GTK_WIDGET_CLASS (klass), GTK_TYPE_BOX_LAYOUT);
    gtk_widget_class_set_css_name (GTK_WIDGET_CLASS (klass), "nautilus-columns");
}

GtkWidget *
nautilus_columns_new (NautilusWindowSlot *slot)
{
    NautilusColumns *self = g_object_new (NAUTILUS_TYPE_COLUMNS, NULL);

    g_set_weak_pointer (&self->slot, slot);
    g_signal_connect_object (slot, "notify::location", G_CALLBACK (schedule_update), self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (slot, "notify::selection", G_CALLBACK (schedule_update), self,
                             G_CONNECT_SWAPPED);
    /* Switching between list and grid. */
    g_signal_connect_object (slot, "notify::icon-name", G_CALLBACK (schedule_update), self,
                             G_CONNECT_SWAPPED);

    return GTK_WIDGET (self);
}
