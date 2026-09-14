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
 * @file xml_parser.h
 * @brief ROADMAP M3 — display-free DOM representation of a GTK .ui document.
 *
 * Parses the XML produced by xml_loader (M2) into a small libxml2-independent
 * tree of @c Node objects that the component registry (M4) and transformer (M5)
 * can walk, clone and mutate. Nothing in this module touches GTK, so it can be
 * unit-tested headless inside the flatpak SDK without a display.
 */

#ifndef GTK_EMBEDDED_PREVIEW_XML_PARSER_H
#define GTK_EMBEDDED_PREVIEW_XML_PARSER_H

/**
 * A single XML attribute (name/value pair).
 */
typedef struct Attr {
    char *name;        /**< Attribute name (owned). */
    char *value;       /**< Attribute value (owned). */
    struct Attr *next; /**< Next attribute in the same element (owned). */
} Attr;

/**
 * A single node of the normalized XML DOM.
 *
 * The tree lives entirely in memory owned by these structs; there is no
 * dependency on the originating libxml2 document once @c parse_xml returns.
 */
typedef struct Node {
    char *name;        /**< Element name, e.g. "object" or "template" (owned). */
    char *content;     /**< Text content of a leaf element, or NULL (owned). */
    Attr *attrs;       /**< Attribute chain (owned). */
    struct Node *children; /**< First child (owned). */
    struct Node *next; /**< Next sibling (owned). */
    struct Node *parent;  /**< Back-pointer to the parent (not owned). */
} Node;

/**
 * Parse XML text into a Node tree.
 *
 * @param xml NUL-terminated XML source.
 * @return Root node (an element; the document node itself is not modeled), or
 *         NULL on a hard parse error. Free with free_node().
 */
Node *parse_xml(const char *xml);

/**
 * Deep copy a node and its entire subtree (children but not siblings).
 *
 * @param node Source node.
 * @return Newly allocated, independent copy. Free with free_node().
 */
Node *clone_node(const Node *node);

/**
 * Recursively free a node and all its children.
 *
 * @param node Node to free (may be NULL).
 */
void free_node(Node *node);

/**
 * Find the first direct child whose element name matches.
 *
 * @param node Parent to search.
 * @param name Element name to look for.
 * @return Matching child, or NULL.
 */
Node *find_child(const Node *node, const char *name);

/**
 * Read an attribute value.
 *
 * @param node Node to inspect.
 * @param key  Attribute name.
 * @return Attribute value, or NULL if absent. Points into the node — do not free.
 */
char *get_attr(const Node *node, const char *key);

/**
 * Set an attribute, creating or replacing its value.
 *
 * @param node  Node to modify.
 * @param key   Attribute name.
 * @param value New value (copied).
 */
void set_attr(Node *node, const char *key, const char *value);

/**
 * Remove an attribute by name, freeing its memory.
 *
 * @param node Node to modify.
 * @param key  Attribute name.
 */
void remove_attr(Node *node, const char *key);

#endif /* GTK_EMBEDDED_PREVIEW_XML_PARSER_H */
