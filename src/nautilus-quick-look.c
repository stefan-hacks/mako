/*
 * nautilus-quick-look.c: the built-in Quick Look window
 *
 * Space shows the selection: one picture to zoom into, a video or song to
 * play, a text file with line numbers, or a big thumbnail and the details
 * of anything else. Several selected items are laid out on a canvas to
 * zoom around, rate and tag. Everything works from the left hand:
 *
 *   Space, Esc        close (Esc first goes back from an item to the canvas)
 *   1-5, `            rate the item under the pointer (or all), clear
 *   W, S / + -, Q     zoom in, out, fit   (also scroll and pinch)
 *   A, D / arrows     previous, next item
 *   K  J L  E F  M    play/pause, back/ahead 5 s, next/previous frame, mute
 *   N, T              names, ratings and tags on the canvas
 *   Enter, Ctrl+O     open
 *   Tab               fullscreen
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "nautilus-quick-look"

#include "nautilus-quick-look.h"

#include <adwaita.h>
#include <glib/gi18n.h>

#include "nautilus-file.h"
#include "nautilus-files-view.h"
#include "nautilus-image.h"
#include "nautilus-labels.h"
#include "nautilus-labels-ui.h"
#include "nautilus-mime-actions.h"
#include "nautilus-ql-canvas.h"

/* Text files are shown up to this much. */
#define TEXT_LIMIT (512 * 1024)
#define FRAME_USEC (G_USEC_PER_SEC / 30)

#define NAUTILUS_TYPE_QUICK_LOOK (nautilus_quick_look_get_type ())
G_DECLARE_FINAL_TYPE (NautilusQuickLook, nautilus_quick_look, NAUTILUS, QUICK_LOOK, AdwWindow)

struct _NautilusQuickLook
{
    AdwWindow parent_instance;

    NautilusFilesView *view;   /* weak */
    GList *files;              /* the selection shown */
    NautilusFile *alone;       /* one of several, looked at alone */

    AdwWindowTitle *title;
    GtkWidget *rating_box;
    GtkWidget *stars[5];
    GtkWidget *canvas_buttons;
    GtkWidget *open_button;
    GtkStack *stack;

    NautilusQlCanvas *canvas;

    GtkWidget *media_picture;
    GtkWidget *media_cover;
    GtkMediaControls *media_controls;
    GtkMediaStream *media;

    GtkLabel *line_numbers;
    GtkTextView *text;
    GtkScrolledWindow *text_scroller;
    GCancellable *text_cancellable;

    GtkWidget *info_image;
    GtkLabel *info_name;
    GtkLabel *info_details;
};

G_DEFINE_TYPE (NautilusQuickLook, nautilus_quick_look, ADW_TYPE_WINDOW)

static NautilusQuickLook *quick_look = NULL;

static void show_current (NautilusQuickLook *self);

/* The one item shown alone, if any. */
static NautilusFile *
shown_file (NautilusQuickLook *self)
{
    if (self->alone != NULL)
    {
        return self->alone;
    }

    return self->files != NULL && self->files->next == NULL ? self->files->data : NULL;
}

/* ---------------------------------------------------------- Details */

static char *
file_details (NautilusFile *file,
              int           width,
              int           height)
{
    g_autoptr (GPtrArray) parts = g_ptr_array_new_with_free_func (g_free);
    g_autofree char *type = nautilus_file_get_string_attribute (file, "type");
    g_autofree char *size = nautilus_file_get_string_attribute (file, "size");
    g_autofree char *date = nautilus_file_get_string_attribute (file, "date_modified");

    if (type != NULL && *type != '\0')
    {
        g_ptr_array_add (parts, g_steal_pointer (&type));
    }
    if (size != NULL && *size != '\0' && g_strcmp0 (size, "—") != 0)
    {
        g_ptr_array_add (parts, g_steal_pointer (&size));
    }
    if (width > 0 && height > 0)
    {
        g_ptr_array_add (parts, g_strdup_printf ("%d × %d", width, height));
    }
    if (date != NULL && *date != '\0' && g_strcmp0 (date, "—") != 0)
    {
        g_ptr_array_add (parts, g_steal_pointer (&date));
    }
    g_ptr_array_add (parts, NULL);

    return g_strjoinv ("  ·  ", (char **) parts->pdata);
}

static char *
selection_details (GList *files)
{
    guint folders = 0, others = 0;
    goffset total = 0;
    g_autofree char *counts = NULL;

    for (GList *l = files; l != NULL; l = l->next)
    {
        if (nautilus_file_is_directory (l->data))
        {
            folders++;
        }
        else
        {
            others++;
            total += nautilus_file_get_size (l->data);
        }
    }

    if (folders > 0 && others > 0)
    {
        g_autofree char *f = g_strdup_printf (ngettext ("%u folder", "%u folders", folders), folders);
        g_autofree char *o = g_strdup_printf (ngettext ("%u file", "%u files", others), others);

        counts = g_strdup_printf ("%s, %s", f, o);
    }
    else if (folders > 0)
    {
        counts = g_strdup_printf (ngettext ("%u folder", "%u folders", folders), folders);
    }
    else
    {
        counts = g_strdup_printf (ngettext ("%u file", "%u files", others), others);
    }

    if (total > 0)
    {
        g_autofree char *size = g_format_size (total);

        return g_strdup_printf ("%s  ·  %s", counts, size);
    }

    return g_steal_pointer (&counts);
}

static void
update_title (NautilusQuickLook *self)
{
    NautilusFile *file = shown_file (self);

    if (file != NULL)
    {
        int w = 0, h = 0;
        g_autofree char *details = NULL;

        nautilus_ql_canvas_get_picture_size (self->canvas, &w, &h);
        details = file_details (file, w, h);
        adw_window_title_set_title (self->title, nautilus_file_get_display_name (file));
        adw_window_title_set_subtitle (self->title, details);
        gtk_window_set_title (GTK_WINDOW (self), nautilus_file_get_display_name (file));
    }
    else
    {
        guint n = g_list_length (self->files);
        g_autofree char *title = g_strdup_printf (ngettext ("%u Item", "%u Items", n), n);
        g_autofree char *details = selection_details (self->files);

        adw_window_title_set_title (self->title, title);
        adw_window_title_set_subtitle (self->title, details);
        gtk_window_set_title (GTK_WINDOW (self), title);
    }
}

static void
update_rating (NautilusQuickLook *self)
{
    NautilusFile *file = shown_file (self);
    gboolean ratings = g_settings_get_boolean (nautilus_flier_settings (), "ratings");
    guint rating = file != NULL ? nautilus_file_get_rating (file) : 0;

    gtk_widget_set_visible (self->rating_box, ratings && file != NULL &&
                            nautilus_file_can_write (file));
    gtk_widget_set_visible (self->canvas_buttons, file == NULL);

    for (guint i = 0; i < 5; i++)
    {
        gtk_button_set_icon_name (GTK_BUTTON (self->stars[i]),
                                  i < rating ? "starred-symbolic" : "non-starred-symbolic");
        if (i < rating)
        {
            gtk_widget_add_css_class (self->stars[i], "rated");
        }
        else
        {
            gtk_widget_remove_css_class (self->stars[i], "rated");
        }
    }
}

/* ------------------------------------------------------------- Rating */

static void
rate (NautilusQuickLook *self,
      guint              stars)
{
    NautilusFile *target = shown_file (self);

    if (!g_settings_get_boolean (nautilus_flier_settings (), "ratings"))
    {
        return;
    }

    if (target == NULL)
    {
        target = nautilus_ql_canvas_get_hovered (self->canvas);
    }

    if (target != NULL)
    {
        nautilus_labels_set_rating (&(GList){ .data = target }, stars);
    }
    else
    {
        /* Nothing under the pointer: the whole selection. */
        nautilus_labels_set_rating (self->files, stars);
    }
}

static void
on_star_clicked (GtkButton         *button,
                 NautilusQuickLook *self)
{
    NautilusFile *file = shown_file (self);
    guint stars = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (button), "stars"));

    if (file != NULL)
    {
        rate (self, stars == nautilus_file_get_rating (file) ? 0 : stars);
    }
}

/* -------------------------------------------------------------- Media */

static void
stop_media (NautilusQuickLook *self)
{
    if (self->media != NULL)
    {
        gtk_media_stream_pause (self->media);
        gtk_media_controls_set_media_stream (self->media_controls, NULL);
        gtk_picture_set_paintable (GTK_PICTURE (self->media_picture), NULL);
        g_clear_object (&self->media);
    }
}

static void
show_media (NautilusQuickLook *self,
            NautilusFile      *file,
            gboolean           audio)
{
    g_autoptr (GFile) location = nautilus_file_get_location (file);
    gboolean muted = self->media != NULL && gtk_media_stream_get_muted (self->media);

    stop_media (self);

    self->media = gtk_media_file_new_for_file (location);
    gtk_media_stream_set_muted (self->media, muted);
    gtk_media_controls_set_media_stream (self->media_controls, self->media);
    gtk_picture_set_paintable (GTK_PICTURE (self->media_picture), GDK_PAINTABLE (self->media));
    gtk_widget_set_visible (self->media_picture, !audio);
    gtk_widget_set_visible (self->media_cover, audio);

    if (audio)
    {
        /* Album art, if there's a thumbnail for it. */
        g_autoptr (GdkPaintable) icon = nautilus_file_get_icon_paintable (file, 256,
                                                                          gtk_widget_get_scale_factor (GTK_WIDGET (self)),
                                                                          NAUTILUS_FILE_ICON_FLAGS_NONE);

        nautilus_image_set_fallback (NAUTILUS_IMAGE (self->media_cover), icon);
        nautilus_image_set_source (NAUTILUS_IMAGE (self->media_cover), location);
    }

    gtk_media_stream_play (self->media);
    gtk_stack_set_visible_child_name (self->stack, "media");
}

static void
seek_by (NautilusQuickLook *self,
         gint64             usec)
{
    gint64 to;

    if (self->media == NULL || !gtk_media_stream_is_seekable (self->media))
    {
        return;
    }

    to = CLAMP (gtk_media_stream_get_timestamp (self->media) + usec, 0,
                MAX (gtk_media_stream_get_duration (self->media), 0));
    gtk_media_stream_seek (self->media, to);
}

static void
step_frame (NautilusQuickLook *self,
            int                frames)
{
    if (self->media != NULL)
    {
        gtk_media_stream_pause (self->media);
        seek_by (self, frames * FRAME_USEC);
    }
}

/* --------------------------------------------------------------- Text */

static void
on_text_read (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GBytes) bytes = g_input_stream_read_bytes_finish (G_INPUT_STREAM (source), result, &error);
    NautilusQuickLook *self = user_data;

    if (bytes == NULL)
    {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
            gtk_text_buffer_set_text (gtk_text_view_get_buffer (self->text), error->message, -1);
        }
        g_object_unref (source);
        return;
    }

    gsize size;
    const char *data = g_bytes_get_data (bytes, &size);
    g_autofree char *text = g_utf8_make_valid (data != NULL ? data : "", size);
    g_autoptr (GString) numbers = g_string_new ("1");
    guint lines = 1;

    for (const char *p = text; *p != '\0'; p++)
    {
        if (*p == '\n' && p[1] != '\0')
        {
            g_string_append_printf (numbers, "\n%u", ++lines);
        }
    }

    gtk_text_buffer_set_text (gtk_text_view_get_buffer (self->text), text, -1);
    gtk_label_set_text (self->line_numbers, numbers->str);
    g_object_unref (source);
}

static void
on_text_opened (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
    g_autoptr (GError) error = NULL;
    GFileInputStream *stream = g_file_read_finish (G_FILE (source), result, &error);
    NautilusQuickLook *self = user_data;

    if (stream == NULL)
    {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
            gtk_text_buffer_set_text (gtk_text_view_get_buffer (self->text), error->message, -1);
        }
        return;
    }

    g_input_stream_read_bytes_async (G_INPUT_STREAM (stream), TEXT_LIMIT, G_PRIORITY_DEFAULT,
                                     self->text_cancellable, on_text_read, self);
}

static void
show_text (NautilusQuickLook *self,
           NautilusFile      *file)
{
    g_autoptr (GFile) location = nautilus_file_get_location (file);

    g_cancellable_cancel (self->text_cancellable);
    g_set_object (&self->text_cancellable, g_cancellable_new ());

    gtk_text_buffer_set_text (gtk_text_view_get_buffer (self->text), "", -1);
    gtk_label_set_text (self->line_numbers, "");
    gtk_adjustment_set_value (gtk_scrolled_window_get_vadjustment (self->text_scroller), 0);
    gtk_adjustment_set_value (gtk_scrolled_window_get_hadjustment (self->text_scroller), 0);

    g_file_read_async (location, G_PRIORITY_DEFAULT, self->text_cancellable, on_text_opened, self);
    gtk_stack_set_visible_child_name (self->stack, "text");
}

/* --------------------------------------------------------------- Info */

static void
show_info (NautilusQuickLook *self,
           NautilusFile      *file)
{
    g_autoptr (GFile) location = nautilus_file_get_location (file);
    g_autoptr (GdkPaintable) icon = nautilus_file_get_icon_paintable (file, 256,
                                                                      gtk_widget_get_scale_factor (GTK_WIDGET (self)),
                                                                      NAUTILUS_FILE_ICON_FLAGS_NONE);
    g_autofree char *details = file_details (file, 0, 0);

    /* Documents, fonts and the like have thumbnails worth seeing big. */
    nautilus_image_set_fallback (NAUTILUS_IMAGE (self->info_image), icon);
    nautilus_image_set_source (NAUTILUS_IMAGE (self->info_image),
                               nautilus_file_is_regular_file (file) ? location : NULL);
    gtk_label_set_text (self->info_name, nautilus_file_get_display_name (file));
    gtk_label_set_text (self->info_details, details);
    gtk_stack_set_visible_child_name (self->stack, "info");
}

/* ------------------------------------------------------------ Showing */

static gboolean
is_text (const char *mime)
{
    return g_content_type_is_a (mime, "text/plain") ||
           g_content_type_is_a (mime, "application/json") ||
           g_content_type_is_a (mime, "application/xml") ||
           g_content_type_is_a (mime, "application/x-desktop");
}

static void
show_current (NautilusQuickLook *self)
{
    NautilusFile *file = shown_file (self);

    g_cancellable_cancel (self->text_cancellable);

    if (file == NULL)
    {
        stop_media (self);
        nautilus_ql_canvas_set_files (self->canvas, self->files);
        gtk_stack_set_visible_child_name (self->stack, "canvas");
    }
    else
    {
        const char *mime = nautilus_file_get_mime_type (file);
        gboolean regular = nautilus_file_is_regular_file (file);

        if (regular && g_str_has_prefix (mime, "image/"))
        {
            stop_media (self);
            nautilus_ql_canvas_set_files (self->canvas, &(GList){ .data = file });
            gtk_stack_set_visible_child_name (self->stack, "canvas");
        }
        else if (regular && (g_str_has_prefix (mime, "video/") || g_str_has_prefix (mime, "audio/")))
        {
            show_media (self, file, g_str_has_prefix (mime, "audio/"));
        }
        else if (regular && is_text (mime))
        {
            stop_media (self);
            show_text (self, file);
        }
        else
        {
            stop_media (self);
            show_info (self, file);
        }
    }

    update_title (self);
    update_rating (self);
}

static void
on_item_activated (NautilusQlCanvas  *canvas,
                   NautilusFile      *file,
                   NautilusQuickLook *self)
{
    g_set_object (&self->alone, file);
    show_current (self);
}

static void
on_file_changed (NautilusQuickLook *self)
{
    update_rating (self);
    update_title (self);
}

static void
watch_files (NautilusQuickLook *self,
             gboolean           watch)
{
    for (GList *l = self->files; l != NULL; l = l->next)
    {
        if (watch)
        {
            g_signal_connect_object (l->data, "changed", G_CALLBACK (on_file_changed), self,
                                     G_CONNECT_SWAPPED);
        }
        else
        {
            g_signal_handlers_disconnect_by_func (l->data, on_file_changed, self);
        }
    }
}

static void
set_files (NautilusQuickLook *self,
           GList             *files)
{
    watch_files (self, FALSE);
    g_clear_object (&self->alone);
    g_clear_list (&self->files, g_object_unref);
    self->files = nautilus_file_list_copy (files);
    watch_files (self, TRUE);
    show_current (self);
}

/* ---------------------------------------------------------- Actions */

static void
open_shown (NautilusQuickLook *self)
{
    NautilusFile *file = shown_file (self);

    if (self->view == NULL)
    {
        return;
    }

    if (file != NULL)
    {
        nautilus_files_view_activate_file (self->view, file, 0);
    }
    else
    {
        nautilus_files_view_activate_selection (self->view, FALSE);
    }
    gtk_window_close (GTK_WINDOW (self));
}

static void
move_selection (NautilusQuickLook *self,
                GtkDirectionType   direction)
{
    if (self->view != NULL && self->alone == NULL && shown_file (self) != NULL)
    {
        nautilus_files_view_preview_selection_event (self->view, direction);
    }
}

static void
toggle_setting (const char *key)
{
    GSettings *settings = nautilus_flier_settings ();

    g_settings_set_boolean (settings, key, !g_settings_get_boolean (settings, key));
}

static gboolean
on_key_pressed (GtkEventControllerKey *controller,
                guint                  keyval,
                guint                  keycode,
                GdkModifierType        state,
                NautilusQuickLook     *self)
{
    GdkModifierType mods = state & gtk_accelerator_get_default_mod_mask ();
    gboolean on_text = g_strcmp0 (gtk_stack_get_visible_child_name (self->stack), "text") == 0;
    guint key = gdk_keyval_to_lower (keyval);

    if (mods == GDK_CONTROL_MASK && key == GDK_KEY_o)
    {
        open_shown (self);
        return GDK_EVENT_STOP;
    }

    if ((mods & ~GDK_SHIFT_MASK) != 0)
    {
        return GDK_EVENT_PROPAGATE;
    }

    switch (key)
    {
        case GDK_KEY_space:
        {
            gtk_window_close (GTK_WINDOW (self));
        }
        break;

        case GDK_KEY_Escape:
        {
            if (self->alone != NULL)
            {
                g_clear_object (&self->alone);
                show_current (self);
            }
            else if (gtk_window_is_fullscreen (GTK_WINDOW (self)))
            {
                gtk_window_unfullscreen (GTK_WINDOW (self));
            }
            else
            {
                gtk_window_close (GTK_WINDOW (self));
            }
        }
        break;

        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
        {
            open_shown (self);
        }
        break;

        case GDK_KEY_Tab:
        case GDK_KEY_F11:
        {
            if (gtk_window_is_fullscreen (GTK_WINDOW (self)))
            {
                gtk_window_unfullscreen (GTK_WINDOW (self));
            }
            else
            {
                gtk_window_fullscreen (GTK_WINDOW (self));
            }
        }
        break;

        case GDK_KEY_1:
        case GDK_KEY_2:
        case GDK_KEY_3:
        case GDK_KEY_4:
        case GDK_KEY_5:
        {
            rate (self, key - GDK_KEY_0);
        }
        break;

        case GDK_KEY_grave:
        case GDK_KEY_0:
        {
            rate (self, 0);
        }
        break;

        /* Zoom */
        case GDK_KEY_w:
        case GDK_KEY_plus:
        case GDK_KEY_equal:
        case GDK_KEY_KP_Add:
        {
            nautilus_ql_canvas_zoom (self->canvas, 1.25);
        }
        break;

        case GDK_KEY_s:
        case GDK_KEY_minus:
        case GDK_KEY_KP_Subtract:
        {
            nautilus_ql_canvas_zoom (self->canvas, 0.8);
        }
        break;

        case GDK_KEY_q:
        {
            nautilus_ql_canvas_fit (self->canvas);
        }
        break;

        case GDK_KEY_z:
        {
            nautilus_ql_canvas_actual_size (self->canvas);
        }
        break;

        /* Moving between items, or around the canvas */
        case GDK_KEY_a:
        case GDK_KEY_Left:
        {
            if (shown_file (self) != NULL)
            {
                move_selection (self, GTK_DIR_LEFT);
            }
            else
            {
                nautilus_ql_canvas_pan (self->canvas, 80, 0);
            }
        }
        break;

        case GDK_KEY_d:
        case GDK_KEY_Right:
        {
            if (shown_file (self) != NULL)
            {
                move_selection (self, GTK_DIR_RIGHT);
            }
            else
            {
                nautilus_ql_canvas_pan (self->canvas, -80, 0);
            }
        }
        break;

        case GDK_KEY_Up:
        case GDK_KEY_Down:
        {
            if (on_text)
            {
                return GDK_EVENT_PROPAGATE;
            }
            if (shown_file (self) != NULL)
            {
                move_selection (self, key == GDK_KEY_Up ? GTK_DIR_UP : GTK_DIR_DOWN);
            }
            else
            {
                nautilus_ql_canvas_pan (self->canvas, 0, key == GDK_KEY_Up ? 80 : -80);
            }
        }
        break;

        /* Playing */
        case GDK_KEY_k:
        {
            if (self->media != NULL)
            {
                gtk_media_stream_set_playing (self->media, !gtk_media_stream_get_playing (self->media));
            }
        }
        break;

        case GDK_KEY_j:
        {
            seek_by (self, -5 * G_USEC_PER_SEC);
        }
        break;

        case GDK_KEY_l:
        {
            seek_by (self, 5 * G_USEC_PER_SEC);
        }
        break;

        case GDK_KEY_e:
        {
            step_frame (self, 1);
        }
        break;

        case GDK_KEY_f:
        {
            step_frame (self, -1);
        }
        break;

        case GDK_KEY_m:
        {
            if (self->media != NULL)
            {
                gtk_media_stream_set_muted (self->media, !gtk_media_stream_get_muted (self->media));
            }
        }
        break;

        /* The canvas */
        case GDK_KEY_n:
        {
            toggle_setting ("quick-look-names");
        }
        break;

        case GDK_KEY_t:
        {
            toggle_setting ("quick-look-marks");
        }
        break;

        default:
        {
            return GDK_EVENT_PROPAGATE;
        }
    }

    return GDK_EVENT_STOP;
}

static gboolean
on_close_request (NautilusQuickLook *self)
{
    stop_media (self);
    g_cancellable_cancel (self->text_cancellable);
    watch_files (self, FALSE);
    g_clear_object (&self->alone);
    g_clear_list (&self->files, g_object_unref);
    nautilus_ql_canvas_set_files (self->canvas, NULL);
    gtk_window_unfullscreen (GTK_WINDOW (self));

    /* Hidden, to be shown again quickly. */
    gtk_widget_set_visible (GTK_WIDGET (self), FALSE);

    return TRUE;
}

/* --------------------------------------------------------- Building */

static GtkWidget *
build_header (NautilusQuickLook *self)
{
    GtkWidget *header = adw_header_bar_new ();
    GtkWidget *button;

    self->title = ADW_WINDOW_TITLE (adw_window_title_new ("", ""));
    adw_header_bar_set_title_widget (ADW_HEADER_BAR (header), GTK_WIDGET (self->title));

    /* Stars of the item shown alone. */
    self->rating_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class (self->rating_box, "quick-look-rating");
    for (guint i = 0; i < 5; i++)
    {
        g_autofree char *tooltip = g_strdup_printf (ngettext ("%u Star (%u)", "%u Stars (%u)", i + 1), i + 1, i + 1);

        self->stars[i] = gtk_button_new_from_icon_name ("non-starred-symbolic");
        gtk_widget_add_css_class (self->stars[i], "flat");
        gtk_widget_add_css_class (self->stars[i], "star");
        gtk_widget_set_tooltip_text (self->stars[i], tooltip);
        gtk_widget_set_focus_on_click (self->stars[i], FALSE);
        g_object_set_data (G_OBJECT (self->stars[i]), "stars", GUINT_TO_POINTER (i + 1));
        g_signal_connect (self->stars[i], "clicked", G_CALLBACK (on_star_clicked), self);
        gtk_box_append (GTK_BOX (self->rating_box), self->stars[i]);
    }
    adw_header_bar_pack_start (ADW_HEADER_BAR (header), self->rating_box);

    /* Switches of the canvas. */
    self->canvas_buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);

    button = gtk_toggle_button_new ();
    gtk_button_set_icon_name (GTK_BUTTON (button), "text-x-generic-symbolic");
    gtk_widget_set_tooltip_text (button, _("Show Names (N)"));
    gtk_widget_add_css_class (button, "flat");
    gtk_widget_set_focus_on_click (button, FALSE);
    g_settings_bind (nautilus_flier_settings (), "quick-look-names", button, "active", G_SETTINGS_BIND_DEFAULT);
    gtk_box_append (GTK_BOX (self->canvas_buttons), button);

    button = gtk_toggle_button_new ();
    gtk_button_set_icon_name (GTK_BUTTON (button), "starred-symbolic");
    gtk_widget_set_tooltip_text (button, _("Show Ratings and Tags (T)"));
    gtk_widget_add_css_class (button, "flat");
    gtk_widget_set_focus_on_click (button, FALSE);
    g_settings_bind (nautilus_flier_settings (), "quick-look-marks", button, "active", G_SETTINGS_BIND_DEFAULT);
    gtk_box_append (GTK_BOX (self->canvas_buttons), button);

    button = gtk_button_new_from_icon_name ("zoom-fit-best-symbolic");
    gtk_widget_set_tooltip_text (button, _("Show Everything (Q)"));
    gtk_widget_add_css_class (button, "flat");
    gtk_widget_set_focus_on_click (button, FALSE);
    g_signal_connect_swapped (button, "clicked", G_CALLBACK (nautilus_ql_canvas_fit), self->canvas);
    gtk_box_append (GTK_BOX (self->canvas_buttons), button);

    adw_header_bar_pack_start (ADW_HEADER_BAR (header), self->canvas_buttons);

    self->open_button = gtk_button_new_with_mnemonic (_("_Open"));
    gtk_widget_add_css_class (self->open_button, "suggested-action");
    gtk_widget_set_tooltip_text (self->open_button, _("Open (Enter)"));
    gtk_widget_set_focus_on_click (self->open_button, FALSE);
    g_signal_connect_swapped (self->open_button, "clicked", G_CALLBACK (open_shown), self);
    adw_header_bar_pack_end (ADW_HEADER_BAR (header), self->open_button);

    return header;
}

static GtkWidget *
build_media_page (NautilusQuickLook *self)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

    self->media_picture = gtk_picture_new ();
    gtk_picture_set_content_fit (GTK_PICTURE (self->media_picture), GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_vexpand (self->media_picture, TRUE);
    gtk_box_append (GTK_BOX (box), self->media_picture);

    self->media_cover = GTK_WIDGET (nautilus_image_new ());
    nautilus_image_set_size (NAUTILUS_IMAGE (self->media_cover), 256);
    gtk_widget_set_halign (self->media_cover, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (self->media_cover, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand (self->media_cover, TRUE);
    gtk_box_append (GTK_BOX (box), self->media_cover);

    self->media_controls = GTK_MEDIA_CONTROLS (gtk_media_controls_new (NULL));
    gtk_widget_add_css_class (GTK_WIDGET (self->media_controls), "toolbar");
    gtk_box_append (GTK_BOX (box), GTK_WIDGET (self->media_controls));

    return box;
}

static GtkWidget *
build_text_page (NautilusQuickLook *self)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

    /* Line numbers beside the text, in one scroller, so they stay in line
     * without a source view. Lines aren't wrapped for the same reason. */
    self->line_numbers = GTK_LABEL (gtk_label_new (NULL));
    gtk_label_set_xalign (self->line_numbers, 1.0);
    gtk_label_set_yalign (self->line_numbers, 0.0);
    gtk_widget_add_css_class (GTK_WIDGET (self->line_numbers), "monospace");
    gtk_widget_add_css_class (GTK_WIDGET (self->line_numbers), "dim-label");
    gtk_widget_add_css_class (GTK_WIDGET (self->line_numbers), "quick-look-line-numbers");
    gtk_box_append (GTK_BOX (box), GTK_WIDGET (self->line_numbers));

    self->text = GTK_TEXT_VIEW (gtk_text_view_new ());
    gtk_text_view_set_editable (self->text, FALSE);
    gtk_text_view_set_cursor_visible (self->text, FALSE);
    gtk_text_view_set_monospace (self->text, TRUE);
    gtk_text_view_set_wrap_mode (self->text, GTK_WRAP_NONE);
    gtk_text_view_set_left_margin (self->text, 12);
    gtk_text_view_set_top_margin (self->text, 12);
    gtk_text_view_set_bottom_margin (self->text, 12);
    gtk_text_view_set_right_margin (self->text, 12);
    gtk_widget_set_hexpand (GTK_WIDGET (self->text), TRUE);
    gtk_widget_set_vexpand (GTK_WIDGET (self->text), TRUE);
    gtk_box_append (GTK_BOX (box), GTK_WIDGET (self->text));

    self->text_scroller = GTK_SCROLLED_WINDOW (gtk_scrolled_window_new ());
    gtk_scrolled_window_set_child (self->text_scroller, box);
    gtk_widget_add_css_class (GTK_WIDGET (self->text_scroller), "view");

    return GTK_WIDGET (self->text_scroller);
}

static GtkWidget *
build_info_page (NautilusQuickLook *self)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);

    gtk_widget_set_valign (box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign (box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start (box, 24);
    gtk_widget_set_margin_end (box, 24);

    self->info_image = GTK_WIDGET (nautilus_image_new ());
    nautilus_image_set_size (NAUTILUS_IMAGE (self->info_image), 384);
    gtk_widget_set_halign (self->info_image, GTK_ALIGN_CENTER);
    gtk_box_append (GTK_BOX (box), self->info_image);

    self->info_name = GTK_LABEL (gtk_label_new (NULL));
    gtk_label_set_wrap (self->info_name, TRUE);
    gtk_label_set_justify (self->info_name, GTK_JUSTIFY_CENTER);
    gtk_widget_add_css_class (GTK_WIDGET (self->info_name), "title-2");
    gtk_box_append (GTK_BOX (box), GTK_WIDGET (self->info_name));

    self->info_details = GTK_LABEL (gtk_label_new (NULL));
    gtk_label_set_wrap (self->info_details, TRUE);
    gtk_label_set_justify (self->info_details, GTK_JUSTIFY_CENTER);
    gtk_widget_add_css_class (GTK_WIDGET (self->info_details), "dim-label");
    gtk_box_append (GTK_BOX (box), GTK_WIDGET (self->info_details));

    return box;
}

static void
nautilus_quick_look_init (NautilusQuickLook *self)
{
    GtkWidget *toolbar = adw_toolbar_view_new ();
    GtkEventController *keys;

    gtk_window_set_default_size (GTK_WINDOW (self), 960, 680);
    gtk_window_set_hide_on_close (GTK_WINDOW (self), FALSE);
    gtk_widget_add_css_class (GTK_WIDGET (self), "quick-look");

    self->canvas = NAUTILUS_QL_CANVAS (nautilus_ql_canvas_new ());
    g_signal_connect (self->canvas, "item-activated", G_CALLBACK (on_item_activated), self);
    g_settings_bind (nautilus_flier_settings (), "quick-look-names", self->canvas, "show-names",
                     G_SETTINGS_BIND_GET);
    g_settings_bind (nautilus_flier_settings (), "quick-look-marks", self->canvas, "show-marks",
                     G_SETTINGS_BIND_GET);

    adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar), build_header (self));

    self->stack = GTK_STACK (gtk_stack_new ());
    gtk_stack_add_named (self->stack, GTK_WIDGET (self->canvas), "canvas");
    gtk_stack_add_named (self->stack, build_media_page (self), "media");
    gtk_stack_add_named (self->stack, build_text_page (self), "text");
    gtk_stack_add_named (self->stack, build_info_page (self), "info");
    adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar), GTK_WIDGET (self->stack));
    adw_window_set_content (ADW_WINDOW (self), toolbar);

    keys = gtk_event_controller_key_new ();
    gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);
    g_signal_connect (keys, "key-pressed", G_CALLBACK (on_key_pressed), self);
    gtk_widget_add_controller (GTK_WIDGET (self), keys);

    g_signal_connect (self, "close-request", G_CALLBACK (on_close_request), NULL);
    g_signal_connect_object (nautilus_flier_settings (), "changed::ratings",
                             G_CALLBACK (update_rating), self, G_CONNECT_SWAPPED);
}

static void
nautilus_quick_look_dispose (GObject *object)
{
    NautilusQuickLook *self = NAUTILUS_QUICK_LOOK (object);

    stop_media (self);
    g_cancellable_cancel (self->text_cancellable);
    g_clear_object (&self->text_cancellable);
    watch_files (self, FALSE);
    g_clear_object (&self->alone);
    g_clear_list (&self->files, g_object_unref);
    g_clear_weak_pointer (&self->view);

    G_OBJECT_CLASS (nautilus_quick_look_parent_class)->dispose (object);
}

static void
nautilus_quick_look_class_init (NautilusQuickLookClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = nautilus_quick_look_dispose;
}

/* --------------------------------------------------------------- API */

gboolean
nautilus_quick_look_enabled (void)
{
    return g_settings_get_boolean (nautilus_flier_settings (), "own-quick-look");
}

gboolean
nautilus_quick_look_is_visible (void)
{
    return quick_look != NULL && gtk_widget_get_visible (GTK_WIDGET (quick_look));
}

void
nautilus_quick_look_close (void)
{
    if (nautilus_quick_look_is_visible ())
    {
        gtk_window_close (GTK_WINDOW (quick_look));
    }
}

void
nautilus_quick_look_toggle (NautilusFilesView *view,
                            GList             *selection)
{
    GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (view));

    if (nautilus_quick_look_is_visible ())
    {
        nautilus_quick_look_close ();
        return;
    }

    if (selection == NULL || !GTK_IS_WINDOW (root))
    {
        return;
    }

    if (quick_look == NULL)
    {
        quick_look = g_object_new (NAUTILUS_TYPE_QUICK_LOOK, NULL);
        g_object_add_weak_pointer (G_OBJECT (quick_look), (gpointer *) &quick_look);
    }

    /* Over the window it was opened from, going away with it. */
    gtk_window_set_transient_for (GTK_WINDOW (quick_look), GTK_WINDOW (root));
    gtk_window_set_destroy_with_parent (GTK_WINDOW (quick_look), TRUE);
    g_set_weak_pointer (&quick_look->view, view);

    set_files (quick_look, selection);
    gtk_window_present (GTK_WINDOW (quick_look));
}

void
nautilus_quick_look_update (NautilusFilesView *view,
                            GList             *selection)
{
    if (!nautilus_quick_look_is_visible () || quick_look->view != view || selection == NULL)
    {
        return;
    }

    /* The same items again (a file of them changed): nothing to redo. */
    GList *a = selection, *b = quick_look->files;

    for (; a != NULL && b != NULL && a->data == b->data; a = a->next, b = b->next)
    {
    }
    if (a == NULL && b == NULL)
    {
        return;
    }

    set_files (quick_look, selection);
}
