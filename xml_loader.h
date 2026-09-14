/*
 * Copyright (C) 2026 apg
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file xml_loader.h
 * @brief Public interface of the XML loading / rendering dispatch module.
 */

#ifndef GTK_EMBEDDED_PREVIEW_RENDERER_H
#define GTK_EMBEDDED_PREVIEW_RENDERER_H

#include "xml_parser.h"

/**
 * Read the entire contents of an XML file into a NUL-terminated string.
 *
 * The caller owns the returned buffer and must release it with g_free(). On
 * any read error the underlying implementation prints the cause to stderr and
 * exits the process with EXIT_FAILURE (ROADMAP M2: "erro -> abortar").
 *
 * @param path filesystem path of the file to load.
 * @return Newly allocated, NUL-terminated file contents; never NULL.
 */
char *load_file(const char *path);

/**
 * Debug-print the parsed DOM tree with indentation.
 *
 * @param current_node  Root node to start printing from.
 * @param depth Initial indentation depth (usually 0).
 */
void print_node(const Node *current_node, int depth);

#endif //GTK_EMBEDDED_PREVIEW_RENDERER_H