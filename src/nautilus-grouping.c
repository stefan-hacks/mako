/*
 * nautilus-grouping.c: group a folder's items by date, name, rating or kind
 *
 * Each item gets a group key, an integer whose order is the groups' order.
 * Comparing keys is all the sorter does, so grouping a big folder costs
 * about as much as sorting it.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nautilus-grouping.h"

#include <glib/gi18n.h>

#include "nautilus-file.h"
#include "nautilus-view-item.h"

static const char *mode_names[] = { "none", "date", "name", "rating", "kind" };

NautilusGroupBy
nautilus_group_by_from_string (const char *name)
{
    for (guint i = 0; i < G_N_ELEMENTS (mode_names); i++)
    {
        if (g_strcmp0 (name, mode_names[i]) == 0)
        {
            return i;
        }
    }

    return NAUTILUS_GROUP_BY_NONE;
}

const char *
nautilus_group_by_to_string (NautilusGroupBy mode)
{
    return mode < G_N_ELEMENTS (mode_names) ? mode_names[mode] : "none";
}

/* --------------------------------------------------------------- Date */

/* Newest first: today, yesterday, the 7 days before, the rest of this
 * month, then this year's months, then the years before. */
enum
{
    DATE_TODAY,
    DATE_YESTERDAY,
    DATE_WEEK,
    DATE_MONTH,
    DATE_MONTHS = 100,     /* + 12 - month */
    DATE_YEARS = 1000,     /* + this year - year */
    DATE_UNKNOWN = G_MAXINT,
};

#define YEARS_BACK 80

typedef struct
{
    gint64 today, yesterday, week, month;
    gint64 month_starts[13];          /* [m]: 1st of month m of this year */
    gint64 year_starts[YEARS_BACK];   /* [i]: Jan 1st, i years ago */
    int year, month_of_year;
} DateMarks;

static void
date_marks_init (DateMarks *marks)
{
    g_autoptr (GDateTime) now = g_date_time_new_now_local ();
    g_autoptr (GDateTime) today = g_date_time_new_local (g_date_time_get_year (now),
                                                         g_date_time_get_month (now),
                                                         g_date_time_get_day_of_month (now), 0, 0, 0);
    g_autoptr (GDateTime) yesterday = g_date_time_add_days (today, -1);
    g_autoptr (GDateTime) week = g_date_time_add_days (today, -7);
    g_autoptr (GDateTime) month = g_date_time_new_local (g_date_time_get_year (now),
                                                         g_date_time_get_month (now), 1, 0, 0, 0);

    marks->today = g_date_time_to_unix (today);
    marks->yesterday = g_date_time_to_unix (yesterday);
    marks->week = g_date_time_to_unix (week);
    marks->month = g_date_time_to_unix (month);
    marks->year = g_date_time_get_year (now);
    marks->month_of_year = g_date_time_get_month (now);

    /* So placing a date is comparing numbers, with nothing made per item. */
    for (int m = 1; m <= 12; m++)
    {
        g_autoptr (GDateTime) start = g_date_time_new_local (marks->year, m, 1, 0, 0, 0);

        marks->month_starts[m] = g_date_time_to_unix (start);
    }
    for (int i = 0; i < YEARS_BACK; i++)
    {
        g_autoptr (GDateTime) start = g_date_time_new_local (marks->year - i, 1, 1, 0, 0, 0);

        marks->year_starts[i] = g_date_time_to_unix (start);
    }
}

static int
date_key (const DateMarks *marks,
          NautilusFile    *file)
{
    time_t mtime = nautilus_file_get_mtime (file);

    if (mtime <= 0)
    {
        return DATE_UNKNOWN;
    }
    if (mtime >= marks->today)
    {
        return DATE_TODAY;
    }
    if (mtime >= marks->yesterday)
    {
        return DATE_YESTERDAY;
    }
    if (mtime >= marks->week)
    {
        return DATE_WEEK;
    }
    if (mtime >= marks->month)
    {
        return DATE_MONTH;
    }

    if (mtime >= marks->year_starts[0])
    {
        for (int m = 12; m >= 1; m--)
        {
            if (mtime >= marks->month_starts[m])
            {
                return DATE_MONTHS + 12 - m;
            }
        }
    }

    for (int i = 1; i < YEARS_BACK; i++)
    {
        if (mtime >= marks->year_starts[i])
        {
            return DATE_YEARS + i;
        }
    }

    return DATE_YEARS + YEARS_BACK;
}

static char *
date_label (const DateMarks *marks,
            int              key)
{
    switch (key)
    {
        case DATE_TODAY:
        {
            return g_strdup (_("Today"));
        }

        case DATE_YESTERDAY:
        {
            return g_strdup (_("Yesterday"));
        }

        case DATE_WEEK:
        {
            return g_strdup (_("Previous 7 Days"));
        }

        case DATE_MONTH:
        {
            return g_strdup (_("Earlier This Month"));
        }

        case DATE_UNKNOWN:
        {
            return g_strdup (_("Unknown Date"));
        }

        default:
        {
        }
        break;
    }

    if (key >= DATE_YEARS)
    {
        return g_strdup_printf ("%d", marks->year - (key - DATE_YEARS));
    }

    g_autoptr (GDateTime) month = g_date_time_new_local (marks->year, 12 - (key - DATE_MONTHS), 1, 0, 0, 0);

    /* The month's name, standing alone. */
    return g_date_time_format (month, "%OB");
}

/* --------------------------------------------------------------- Name */

/* The first letter, upper case and without accents; '#' for digits and
 * the like, which come first. */
static int
name_key (NautilusFile *file)
{
    const char *name = nautilus_file_get_display_name (file);
    gunichar c = name != NULL ? g_utf8_get_char_validated (name, -1) : 0;
    gunichar base[4];

    /* Hidden files are grouped by what follows the dot. */
    if (c == '.' && name[1] != '\0')
    {
        c = g_utf8_get_char_validated (name + 1, -1);
    }

    if (c == (gunichar) -1 || c == (gunichar) -2 || !g_unichar_isalpha (c))
    {
        return '#';
    }

    if (g_unichar_fully_decompose (c, FALSE, base, G_N_ELEMENTS (base)) > 0)
    {
        c = base[0];
    }

    return g_unichar_toupper (c);
}

/* -------------------------------------------------------------- Kind */

enum
{
    KIND_FOLDER,
    KIND_DOCUMENT,
    KIND_IMAGE,
    KIND_VIDEO,
    KIND_AUDIO,
    KIND_ARCHIVE,
    KIND_CODE,
    KIND_OTHER,
};

static int
kind_key (NautilusFile *file)
{
    const char *mime;

    if (nautilus_file_is_directory (file))
    {
        return KIND_FOLDER;
    }

    mime = nautilus_file_get_mime_type (file);
    if (mime == NULL)
    {
        return KIND_OTHER;
    }
    if (g_str_has_prefix (mime, "image/"))
    {
        return KIND_IMAGE;
    }
    if (g_str_has_prefix (mime, "video/"))
    {
        return KIND_VIDEO;
    }
    if (g_str_has_prefix (mime, "audio/"))
    {
        return KIND_AUDIO;
    }
    if (strstr (mime, "zip") != NULL || strstr (mime, "tar") != NULL ||
        strstr (mime, "compressed") != NULL || strstr (mime, "7z") != NULL ||
        strstr (mime, "rar") != NULL || g_str_has_suffix (mime, "-xz") ||
        g_str_has_suffix (mime, "bzip2") || g_str_has_suffix (mime, "zstd"))
    {
        return KIND_ARCHIVE;
    }
    if (g_str_has_prefix (mime, "application/pdf") || strstr (mime, "opendocument") != NULL ||
        strstr (mime, "officedocument") != NULL || strstr (mime, "msword") != NULL ||
        g_str_has_prefix (mime, "application/epub") || g_str_has_prefix (mime, "text/markdown") ||
        g_str_has_prefix (mime, "application/rtf"))
    {
        return KIND_DOCUMENT;
    }
    if (g_str_has_prefix (mime, "text/") || strstr (mime, "script") != NULL ||
        strstr (mime, "json") != NULL || strstr (mime, "xml") != NULL)
    {
        return KIND_CODE;
    }

    return KIND_OTHER;
}

static const char *
kind_label (int key)
{
    switch (key)
    {
        case KIND_FOLDER:
        {
            return _("Folders");
        }

        case KIND_DOCUMENT:
        {
            return _("Documents");
        }

        case KIND_IMAGE:
        {
            return _("Pictures");
        }

        case KIND_VIDEO:
        {
            return _("Videos");
        }

        case KIND_AUDIO:
        {
            return _("Music");
        }

        case KIND_ARCHIVE:
        {
            return _("Archives");
        }

        case KIND_CODE:
        {
            return _("Text and Code");
        }

        default:
        {
            return _("Other");
        }
    }
}

/* ------------------------------------------------------------ Sorter */

typedef struct
{
    NautilusGroupBy mode;
    DateMarks marks;
} GroupData;

static int
group_key (NautilusGroupBy  mode,
           const DateMarks *marks,
           NautilusFile    *file)
{
    switch (mode)
    {
        case NAUTILUS_GROUP_BY_DATE:
        {
            return date_key (marks, file);
        }

        case NAUTILUS_GROUP_BY_NAME:
        {
            return name_key (file);
        }

        case NAUTILUS_GROUP_BY_RATING:
        {
            /* Most stars first, unrated last. */
            guint rating = nautilus_file_get_rating (file);

            return rating > 0 ? 5 - (int) rating : 6;
        }

        case NAUTILUS_GROUP_BY_KIND:
        {
            return kind_key (file);
        }

        default:
        {
            return 0;
        }
    }
}

/* Expanded folders' contents stay with their folder. */
static NautilusFile *
row_file (gpointer row)
{
    GtkTreeListRow *root = g_object_ref (row);
    GtkTreeListRow *parent;

    while ((parent = gtk_tree_list_row_get_parent (root)) != NULL)
    {
        g_object_unref (root);
        root = parent;
    }

    g_autoptr (NautilusViewItem) item = gtk_tree_list_row_get_item (root);

    g_object_unref (root);

    return item != NULL ? nautilus_view_item_get_file (item) : NULL;
}

static int
compare_groups (gconstpointer a,
                gconstpointer b,
                gpointer      user_data)
{
    GroupData *data = user_data;
    NautilusFile *file_a = row_file ((gpointer) a);
    NautilusFile *file_b = row_file ((gpointer) b);

    if (file_a == NULL || file_b == NULL)
    {
        return (file_a == NULL) - (file_b == NULL);
    }

    int key_a = group_key (data->mode, &data->marks, file_a);
    int key_b = group_key (data->mode, &data->marks, file_b);

    return (key_a > key_b) - (key_a < key_b);
}

GtkSorter *
nautilus_group_sorter_new (NautilusGroupBy mode)
{
    GroupData *data;

    if (mode == NAUTILUS_GROUP_BY_NONE || mode >= NAUTILUS_GROUP_BY_N_MODES)
    {
        return NULL;
    }

    data = g_new0 (GroupData, 1);
    data->mode = mode;
    date_marks_init (&data->marks);

    return GTK_SORTER (gtk_custom_sorter_new (compare_groups, data, g_free));
}

char *
nautilus_group_get_label (NautilusGroupBy  mode,
                          NautilusFile    *file)
{
    DateMarks marks;
    int key;

    date_marks_init (&marks);
    key = group_key (mode, &marks, file);

    switch (mode)
    {
        case NAUTILUS_GROUP_BY_DATE:
        {
            return date_label (&marks, key);
        }

        case NAUTILUS_GROUP_BY_NAME:
        {
            char letter[8] = { 0 };

            g_unichar_to_utf8 (key, letter);
            return g_strdup (letter);
        }

        case NAUTILUS_GROUP_BY_RATING:
        {
            if (key == 6)
            {
                return g_strdup (_("Not Rated"));
            }

            guint stars = 5 - key;

            return g_strdup_printf (ngettext ("%u Star", "%u Stars", stars), stars);
        }

        case NAUTILUS_GROUP_BY_KIND:
        {
            return g_strdup (kind_label (key));
        }

        default:
        {
            return g_strdup ("");
        }
    }
}
