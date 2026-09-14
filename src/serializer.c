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
 * @file serializer.c
 * @brief Serialize the normalized DOM back to XML text.
 *
 * Walks the Node/Attr tree built by xml_parser.c and writes a
 * GtkBuilder-loadable XML document. Display-free on purpose: GLib only, no
 * GTK, no cairo. The structural guarantees (root <interface>, <requires>
 * element, no <template> nodes left in the tree) are enforced by the
 * normalizer (M6) before this module ever sees the tree, so the writer here
 * stays a plain, template-agnostic round-trip.
 */

#include <glib.h>
#include "serializer.h"

/**
 * Serialize a single node and its subtree into xml, indenting for readability.
 *
 * @param xml   GString receiving the serialized output.
 * @param node  Node to serialize.
 * @param depth Current indentation depth.
 */
static void serialize_node(GString *xml, const Node *node, const int depth) {
    for (const Node *current = node; current != NULL; current = current->next) {
        for (int i = 0; i < depth; i++)
            g_string_append(xml, "  ");

        g_string_append_printf(xml, "<%s", current->name);

        for (const Attr *attribute = current->attrs;
             attribute != NULL;
             attribute = attribute->next) {
            g_autofree gchar *escaped = g_markup_escape_text(
                attribute->value, -1);
            g_string_append_printf(xml, " %s=\"%s\"", attribute->name,
                                   escaped);
        }

        if (current->content != NULL) {
            g_autofree gchar *escaped = g_markup_escape_text(current->content,
                                                             -1);
            g_string_append_printf(xml, ">%s</%s>\n", escaped, current->name);
            continue;
        }

        if (current->children != NULL) {
            g_string_append(xml, ">\n");
            serialize_node(xml, current->children, depth + 1);
            for (int i = 0; i < depth; i++)
                g_string_append(xml, "  ");
            g_string_append_printf(xml, "</%s>\n", current->name);
        } else {
            g_string_append(xml, "/>\n");
        }
    }
}

char *serialize_xml(const Node *root) {
    if (root == NULL)
        return NULL;

    GString *xml = g_string_new(NULL);

    if (g_str_equal(root->name, "interface")) {
        g_string_append(xml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                             "<interface>\n");
        serialize_node(xml, root->children, 1);
    } else {
        g_string_append(xml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                             "<interface>\n");
        serialize_node(xml, root, 1);
    }

    g_string_append(xml, "</interface>\n");

    return g_string_free(xml, FALSE);
}