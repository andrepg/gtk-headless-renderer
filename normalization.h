// Copyright (C) 2026 apg
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

/**
 * @file normalization.h
 * @brief DOM normalization passes for the preview pipeline.
 */

#ifndef GTK_EMBEDDED_PREVIEW_NORMALIZATION_H
#define GTK_EMBEDDED_PREVIEW_NORMALIZATION_H

#include "xml_parser.h"

/**
 * Apply all normalization passes to the parsed DOM in-place.
 *
 * Currently handles:
 *  - <template class="X" parent="Y"> → copies parent value into class.
 *  - <object class="Custom"> → expands in place to the registered template's
 *    parent class (<template parent="Y">) with its subtree, merging usage-site
 *    <property> overrides and recursing into nested custom objects (cycle
 *    guarded).
 *
 * @param root Root of the DOM tree to normalize.
 */
void normalize_templates(Node *root);

#endif /* GTK_EMBEDDED_PREVIEW_NORMALIZATION_H */
