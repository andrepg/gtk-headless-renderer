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
 * @file xml_parser.c
 * @brief ROADMAP M3 — display-free DOM (Node tree) on libxml2.
 *
 * Implements the pure-DOM parser that ROADMAP M3 specifies: turns the raw
 * XML text produced by xml_loader into our own Node/Attr tree that the
 * component registry (M4) and the transformer (M5+) can clone, walk and
 * mutate. libxml2 does the heavy lifting (xmlReadMemory -> element tree),
 * then every element is copied into a lean GLib-owned tree so the code after
 * this module has zero dependency on libxml2 or on libxml2's document model.
 *
 * No GTK, no widget toolkit, no display: this module runs headless inside the
 * flatpak SDK and can be exercised by the normalization unit test (M3/M4)
 * without ever touching a framebuffer.
 */

#include <string.h>
#include <glib.h>
#include <libxml/parser.h>
#include "xml_parser.h"

/* libxml2 stores element/attribute names and text as xmlChar* (UTF-8). This
 * file normalizes them into our own Node/Attr structs, all of which are UTF-8
 * strings that use GLib's g_strdup/g_str_equal family. */

/* ---- small tree helpers ------------------------------------------- */

static void append_attr(Node *node, Attr *attr) {
    attr->next = NULL;
    if (node->attrs == NULL) {
        node->attrs = attr;
        return;
    }
    Attr *tail = node->attrs;
    while (tail->next != NULL)
        tail = tail->next;
    tail->next = attr;
}

static void append_child(Node *parent, Node *child) {
    child->parent = parent;
    child->next = NULL;
    if (parent->children == NULL) {
        parent->children = child;
        return;
    }
    Node *tail = parent->children;
    while (tail->next != NULL)
        tail = tail->next;
    tail->next = child;
}

/* ---- libxml2 -> Node conversion ----------------------------------- */

static void convert_attrs(xmlNodePtr element, Node *node) {
    for (xmlAttrPtr attribute = element->properties;
         attribute != NULL;
         attribute = attribute->next) {
        if (attribute->type != XML_ATTRIBUTE_NODE)
            continue;

        xmlChar *value = xmlNodeListGetString(element->doc,
                                              attribute->children, 1);
        if (value == NULL)
            continue;

        Attr *attr = g_new0(Attr, 1);
        attr->name = g_strdup((const char *) attribute->name);
        attr->value = g_strdup((const char *) value);
        xmlFree(value);
        append_attr(node, attr);
    }
}

static gboolean has_element_child(xmlNodePtr element) {
    for (xmlNodePtr child = element->children;
         child != NULL;
         child = child->next) {
        if (child->type == XML_ELEMENT_NODE)
            return TRUE;
    }
    return FALSE;
}

/* Leaf text is meaningful; whitespace between elements is dropped. */
static void convert_text_content(xmlNodePtr element, Node *node) {
    xmlChar *content = xmlNodeGetContent(element);
    if (content == NULL)
        return;

    gchar *trimmed = g_strstrip(g_strdup((const char *) content));
    if (trimmed[0] != '\0')
        node->content = trimmed;
    else
        g_free(trimmed);
    xmlFree(content);
}

/**
 * Copy an element (and, recursively, its element children) into our tree.
 */
static Node *convert_element(xmlNodePtr element) {
    Node *node = g_new0(Node, 1);
    node->name = g_strdup((const char *) element->name);
    convert_attrs(element, node);

    if (!has_element_child(element))
        convert_text_content(element, node);

    for (xmlNodePtr child = element->children;
         child != NULL;
         child = child->next) {
        if (child->type == XML_ELEMENT_NODE)
            append_child(node, convert_element(child));
    }

    return node;
}

Node *parse_xml(const char *xml) {
    if (xml == NULL)
        return NULL;

    const xmlDocPtr doc = xmlReadMemory(xml, (int) strlen(xml),
                                  "embedded.ui", NULL,
                                  XML_PARSE_NONET | XML_PARSE_NOERROR);
    if (doc == NULL)
        return NULL;

    xmlNodePtr root = xmlDocGetRootElement(doc);
    Node *node = root != NULL ? convert_element(root) : NULL;
    xmlFreeDoc(doc);
    return node;
}

Node *clone_node(const Node *node) {
    if (node == NULL)
        return NULL;

    Node *copy = g_new0(Node, 1);
    copy->name = g_strdup(node->name);
    copy->content = g_strdup(node->content);

    for (const Attr *a = node->attrs; a != NULL; a = a->next) {
        Attr *attr_copy = g_new0(Attr, 1);
        attr_copy->name = g_strdup(a->name);
        attr_copy->value = g_strdup(a->value);
        append_attr(copy, attr_copy);
    }

    for (const Node *c = node->children; c != NULL; c = c->next)
        append_child(copy, clone_node(c));

    return copy;
}

void free_node(Node *node) {
    while (node != NULL) {
        Node *next_sibling = node->next;

        if (node->children != NULL)
            free_node(node->children);

        while (node->attrs != NULL) {
            Attr *next_attr = node->attrs->next;
            g_free(node->attrs->name);
            g_free(node->attrs->value);
            g_free(node->attrs);
            node->attrs = next_attr;
        }

        g_free(node->name);
        g_free(node->content);
        g_free(node);

        node = next_sibling;
    }
}

Node *find_child(const Node *node, const char *name) {
    if (node == NULL || name == NULL)
        return NULL;

    for (const Node *child = node->children;
         child != NULL;
         child = child->next) {
        if (g_str_equal(child->name, name))
            return (Node *) child;
    }
    return NULL;
}

char *get_attr(const Node *node, const char *key) {
    if (node == NULL || key == NULL)
        return NULL;

    for (const Attr *a = node->attrs; a != NULL; a = a->next) {
        if (g_str_equal(a->name, key))
            return a->value;
    }
    return NULL;
}

void set_attr(Node *node, const char *key, const char *value) {
    if (node == NULL || key == NULL)
        return;

    for (Attr *attr = node->attrs; attr != NULL; attr = attr->next) {
        if (g_str_equal(attr->name, key)) {
            g_free(attr->value);
            attr->value = g_strdup(value);
            return;
        }
    }

    Attr *attr = g_new0(Attr, 1);
    attr->name = g_strdup(key);
    attr->value = g_strdup(value);
    attr->next = node->attrs;
    node->attrs = attr;
}