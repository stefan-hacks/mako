/*
 * nautilus-shortcuts.h: every command with its keys, the shortcuts
 * overview (F1), changeable keys and the left-hand layout
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Adds the keys of the commands to @window (a NautilusWindow): the
 * left-hand ones and any changed in the overview. */
void nautilus_shortcuts_attach         (GtkWindow *window);

/* The overview: search commands and keys, run a command, change its keys. */
void nautilus_shortcuts_dialog_present (GtkWindow *window);

/* The key to show next to @detailed_action in menus ("<alt>1"), the
 * left-hand one if it has one, or NULL. */
char *nautilus_shortcuts_get_accel     (const char *detailed_action);

G_END_DECLS
