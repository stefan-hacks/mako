/*
 * nautilus-text-preview.c: the start of a text file, with line numbers
 *
 * The numbers are a label beside the text, in the same scroller, so they
 * stay in line without a source view; lines aren't wrapped for that reason.
 * Only the start of the file is read, in the background.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nautilus-text-preview.h"

#include "nautilus-file.h"

struct _NautilusTextPreview
{
    GtkWidget parent_instance;

    gsize limit;
    GtkScrolledWindow *scroller;
    GtkLabel *numbers;
    GtkTextView *text;
    GCancellable *cancellable;
};

G_DEFINE_TYPE (NautilusTextPreview, nautilus_text_preview, GTK_TYPE_WIDGET)

gboolean
nautilus_text_preview_handles (const char *mime_type)
{
    /* Text by another name: code, data and config formats that
     * shared-mime-info doesn't file under text/plain. */
    static const char *texts[] =
    {
        "application/json", "application/xml", "application/x-desktop", "application/javascript",
        "application/x-yaml", "application/yaml", "application/toml", "application/x-toml",
        "application/sql", "application/x-sql", "application/x-shellscript", "application/x-perl",
        "application/x-python", "application/x-php", "application/x-ruby", "application/x-subrip",
        "application/x-ipynb+json", "application/ld+json", "application/geo+json", "application/x-wine-extension-ini",
        "application/x-ini-file", "application/x-trash", "application/x-log", "application/x-m4",
        "application/x-cmakecache", "application/x-gettext-translation", "application/x-tex", "application/x-latex",
        "application/xhtml+xml", "application/rss+xml", "application/atom+xml", "application/x-csh",
        "application/x-awk", "application/x-zerosize",
    };

    if (mime_type == NULL)
    {
        return FALSE;
    }
    if (g_str_has_prefix (mime_type, "text/") || g_content_type_is_a (mime_type, "text/plain") ||
        g_str_has_suffix (mime_type, "+json") || g_str_has_suffix (mime_type, "+xml"))
    {
        return TRUE;
    }
    for (guint i = 0; i < G_N_ELEMENTS (texts); i++)
    {
        if (g_content_type_is_a (mime_type, texts[i]))
        {
            return TRUE;
        }
    }
    return FALSE;
}

static void
show_text (NautilusTextPreview *self,
           const char          *data,
           gsize                size)
{
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
    gtk_label_set_text (self->numbers, numbers->str);
}

static void
on_read (GObject      *source,
         GAsyncResult *result,
         gpointer      user_data)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GBytes) bytes = g_input_stream_read_bytes_finish (G_INPUT_STREAM (source), result, &error);
    g_autoptr (GObject) stream = G_OBJECT (source);

    if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        return;
    }

    NautilusTextPreview *self = user_data;

    if (bytes == NULL)
    {
        show_text (self, error->message, strlen (error->message));
        return;
    }

    gsize size;
    const char *data = g_bytes_get_data (bytes, &size);

    show_text (self, data, size);
}

static void
on_opened (GObject      *source,
           GAsyncResult *result,
           gpointer      user_data)
{
    g_autoptr (GError) error = NULL;
    GFileInputStream *stream = g_file_read_finish (G_FILE (source), result, &error);

    if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        return;
    }

    NautilusTextPreview *self = user_data;

    if (stream == NULL)
    {
        show_text (self, error->message, strlen (error->message));
        return;
    }

    g_input_stream_read_bytes_async (G_INPUT_STREAM (stream), self->limit, G_PRIORITY_DEFAULT,
                                     self->cancellable, on_read, self);
}

void
nautilus_text_preview_set_file (NautilusTextPreview *self,
                                NautilusFile        *file)
{
    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);

    gtk_text_buffer_set_text (gtk_text_view_get_buffer (self->text), "", -1);
    gtk_label_set_text (self->numbers, "");
    gtk_adjustment_set_value (gtk_scrolled_window_get_vadjustment (self->scroller), 0);
    gtk_adjustment_set_value (gtk_scrolled_window_get_hadjustment (self->scroller), 0);

    if (file == NULL)
    {
        return;
    }

    g_autoptr (GFile) location = nautilus_file_get_location (file);

    self->cancellable = g_cancellable_new ();
    g_file_read_async (location, G_PRIORITY_DEFAULT, self->cancellable, on_opened, self);
}

static void
nautilus_text_preview_init (NautilusTextPreview *self)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

    self->numbers = GTK_LABEL (gtk_label_new (NULL));
    gtk_label_set_xalign (self->numbers, 1.0);
    gtk_label_set_yalign (self->numbers, 0.0);
    gtk_widget_add_css_class (GTK_WIDGET (self->numbers), "monospace");
    gtk_widget_add_css_class (GTK_WIDGET (self->numbers), "dim-label");
    gtk_widget_add_css_class (GTK_WIDGET (self->numbers), "line-numbers");
    gtk_box_append (GTK_BOX (box), GTK_WIDGET (self->numbers));

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

    self->scroller = GTK_SCROLLED_WINDOW (gtk_scrolled_window_new ());
    gtk_scrolled_window_set_child (self->scroller, box);
    gtk_widget_add_css_class (GTK_WIDGET (self->scroller), "view");
    gtk_widget_set_parent (GTK_WIDGET (self->scroller), GTK_WIDGET (self));
}

static void
nautilus_text_preview_dispose (GObject *object)
{
    NautilusTextPreview *self = NAUTILUS_TEXT_PREVIEW (object);

    g_cancellable_cancel (self->cancellable);
    g_clear_object (&self->cancellable);
    if (self->scroller != NULL)
    {
        gtk_widget_unparent (GTK_WIDGET (self->scroller));
        self->scroller = NULL;
    }

    G_OBJECT_CLASS (nautilus_text_preview_parent_class)->dispose (object);
}

static void
nautilus_text_preview_class_init (NautilusTextPreviewClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = nautilus_text_preview_dispose;
    gtk_widget_class_set_layout_manager_type (GTK_WIDGET_CLASS (klass), GTK_TYPE_BIN_LAYOUT);
    gtk_widget_class_set_css_name (GTK_WIDGET_CLASS (klass), "nautilus-text-preview");
}

GtkWidget *
nautilus_text_preview_new (gsize limit)
{
    NautilusTextPreview *self = g_object_new (NAUTILUS_TYPE_TEXT_PREVIEW, NULL);

    self->limit = limit;

    return GTK_WIDGET (self);
}
