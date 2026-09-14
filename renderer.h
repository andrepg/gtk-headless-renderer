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

#ifndef GTK_EMBEDDED_PREVIEW_RENDERER_H
#define GTK_EMBEDDED_PREVIEW_RENDERER_H

#include "utils.h"
#include "xml_parser.h"

/**
 * Render a normalized DOM to a PNG snapshot (ROADMAP M7-M10).
 *
 * Serializes the Node tree back to XML, loads it with GtkBuilder, snapshots
 * the root widget into a GskRenderNode and exports it as a GdkTexture to
 * config.output_path at config.width x config.height.
 *
 * @param config       Parsed CLI configuration (output_path/width/height).
 * @param current_node Normalized root node of the input document.
 * @return EXIT_SUCCESS on success, EXIT_FAILURE otherwise.
 */
int renderer_render_interface(const struct Config config,
                              const Node *current_node);

#endif //GTK_EMBEDDED_PREVIEW_RENDERER_H