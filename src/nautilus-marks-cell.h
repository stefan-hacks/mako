/*
 * nautilus-marks-cell.h: the Rating and Tags columns of the list view
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "nautilus-view-cell.h"

G_BEGIN_DECLS

#define NAUTILUS_TYPE_MARKS_CELL (nautilus_marks_cell_get_type ())

G_DECLARE_FINAL_TYPE (NautilusMarksCell, nautilus_marks_cell, NAUTILUS, MARKS_CELL, NautilusViewCell)

typedef enum
{
    NAUTILUS_MARKS_CELL_RATING,
    NAUTILUS_MARKS_CELL_TAGS,
} NautilusMarksCellKind;

NautilusViewCell *nautilus_marks_cell_new (NautilusListBase      *view,
                                           NautilusMarksCellKind  kind);

G_END_DECLS
