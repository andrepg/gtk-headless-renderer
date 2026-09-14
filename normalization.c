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
 * @file normalization.c
 * @brief DOM normalization passes for the preview pipeline.
 *
 * Each pass walks the tree and mutates nodes in-place so that downstream
 * consumers (component registry, GTK builder) receive a canonical form.
 */

#include <glib.h>
#include "xml_parser.h"
#include "normalization.h"
#include "component_registry.h"

/**
 * Checks if current node is a custom template and replace it by its parent
 * declaration, allowing a clean transition between parent/children nodes
 * @param node current node to normalize
 */
static void normalize_tag_template(Node *node) {
    if (!g_str_equal(node->name, "template")) return;

    const char *parent_val = get_attr(node, "parent");

    if (parent_val != NULL) {
        set_attr(node, "class", parent_val);
        remove_attr(node, "parent");
    }
}

/**
 * Checks if current node tag is `object` and search for a
 * known class from scanned directory to read it and embed it
 * @param node current node to check
 */
static void replace_object_class(Node *node) {
    if (!g_str_equal(node->name, "object")) return;

    const gchar *class_name = get_attr(node, "class");

    if (class_name == NULL) {
        g_printerr("Failed to find template class at %s\n", node->name);
        return;
    }

    if (component_registry_is_builtin(class_name) == TRUE) {
        g_print("Skipping native widget %s\n", class_name);
        return;
    }

    g_print("Found custom object %s\n", class_name);

    const Node *template = component_registry_get_template(class_name);

    if (template == NULL) {
        g_printerr("Class \"%s\" does not have a valid template name\n", class_name);
        return;
    }

    if (template != NULL) node = template;
}

/**
 * Apply all required normalizations to each node read from XML
 * according to our rules to render GTK interfaces later.
 *
 * @param node current node to normalize
 */
static void normalize_node(Node *node) {
    for (Node *current = node; current != NULL; current = current->next) {
        // Call recursive if we have children, so we do bottom -> up
        if (current->children != NULL)
            normalize_node(current->children);

        normalize_tag_template(current);
        replace_object_class(current);
    }
}

void normalize_templates(Node *root) {
    if (root == NULL) {
        g_printerr("XML does not have a valid root node\n");
        return;
    }

    normalize_node(root);
}
