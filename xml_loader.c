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
 * @file xml_loader.c
 * @brief XML file loading for the preview pipeline (ROADMAP M2).
 *
 * Reads the whole input file into memory and hands it to the orchestrator as a
 * plain C string. Downstream stages (DOM parser, template expansion, GTK
 * builder, renderer/export) consume the returned buffer.
 */

#include "xml_loader.h"
#include "xml_parser.h"

#include <stdlib.h>
#include <glib.h>

char *load_file(const char *path) {
    gsize length = 0;

    g_autoptr(GError) error = NULL;
    g_autofree char *content = NULL;

    if (!g_file_get_contents(path, &content, &length, &error)) {
        g_printerr("load_file: failed to read %s: %s\n", path, error->message);
        exit(EXIT_FAILURE);
    }

    /* Copy exactly the file bytes so the buffer is guaranteed NUL-terminated
     * regardless of how g_file_get_contents() decided to read the file. */
    return g_strndup(content, length);
}

/**
 * Debug-printer for the parsed DOM (the M7 serializer does not exist yet).
 * Prints element names, attributes and leaf text with indentation.
 */
static void print_indent(int depth) {
    for (int i = 0; i < depth; i++)
        g_print("  ");
}

void print_node(const Node *node, const int depth) {
    for (const Node *current = node; current != NULL; current = current->next) {
        print_indent(depth);
        g_print("<%s", current->name);
        for (const Attr *attribute = current->attrs;
             attribute != NULL;
             attribute = attribute->next)
            g_print(" %s=\"%s\"", attribute->name, attribute->value);

        if (current->content != NULL) {
            g_print(">%s</%s>\n", current->content, current->name);
            continue;
        }
        if (current->children != NULL) {
            g_print(">\n");
            print_node(current->children, depth + 1);
            print_indent(depth);
            g_print("</%s>\n", current->name);
        } else {
            g_print("/>\n");
        }
    }
}

int render_interface(const char *input_path,
                     const char *output_path,
                     const int width,
                     const int height) {
    (void) input_path;
    (void) output_path;
    (void) width;
    (void) height;

    return EXIT_SUCCESS;
}
