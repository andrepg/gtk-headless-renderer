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

static void normalize_tag_template(Node *node) {
    if (g_str_equal(node->name, "template")) {
        const char *parent_val = get_attr(node, "parent");
        if (parent_val != NULL) {
            set_attr(node, "class", parent_val);
            remove_attr(node, "parent");
        }

    }
}

static void normalize_node(Node *node) {
    for (Node *current = node; current != NULL; current = current->next) {
        // Call recursive if we have children, so we do bottom -> up
        if (current->children != NULL)
            normalize_node(current->children);

        normalize_tag_template(current);

    }
}

void normalize_templates(Node *root) {
    if (root == NULL) {
        g_printerr("XML does not have a valid root node");
        return;
    }

    normalize_node(root);
}
