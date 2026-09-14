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
// along with this program. If not, see <https://www.gnu.gnu/licenses/>.

/**
 * @file normalizer.c
 * @brief DOM normalization passes for the preview pipeline.
 *
 * Each pass walks the tree and mutates nodes in-place so that downstream
 * consumers (component registry, GTK builder) receive a canonical form.
 */

#include <glib.h>
#include "xml_parser.h"
#include "normalizer.h"
#include "component_registry.h"

/*
 * Resolution stack: tracks the chain of custom classes currently being
 * expanded.  A template cycle (A -> A, or A -> B -> A) is detected the
 * moment a class already on the stack re-appears — the expansion of that
 * branch is stopped with a diagnostic.  Native (GTK/Adwaita) classes are
 * never pushed, so the stack is strictly scoped to non-native templates.
 * See ROADMAP M5 step 10 ("loop protection").
 */
#define MAX_RESOLUTION_DEPTH 64

typedef struct {
    const char *classes[MAX_RESOLUTION_DEPTH];
    int top;
} ResolutionStack;

static void stack_init(ResolutionStack *s) { s->top = 0; }

static gboolean stack_contains(const ResolutionStack *s, const char *name) {
    for (int i = 0; i < s->top; i++)
        if (g_str_equal(s->classes[i], name))
            return TRUE;
    return FALSE;
}

static gboolean stack_push(ResolutionStack *s, const char *name) {
    if (s->top >= MAX_RESOLUTION_DEPTH)
        return FALSE;
    s->classes[s->top++] = g_strdup(name);
    return TRUE;
}

static void stack_pop(ResolutionStack *s) {
    if (s->top > 0)
        g_free((gpointer)s->classes[--s->top]);
}

static Node *normalize_node(Node *node, ResolutionStack *stack);

/**
 * Checks if current node is a custom template and replace it by its parent
 * declaration, allowing a clean transition between parent/children nodes.
 *
 * The class fix-up (parent -> class) and the <template> -> <object> retag
 * happen here, so no <template> element survives normalization: the
 * GtkBuilder-instantiation semantic lives only in this module (ROADMAP M6).
 *
 * @param node current node to normalize
 */
static void normalize_tag_template(Node *node) {
    if (!g_str_equal(node->name, "template")) return;

    const char *parent_val = get_attr(node, "parent");

    if (parent_val != NULL) {
        set_attr(node, "class", parent_val);
        remove_attr(node, "parent");
    }

    g_free(node->name);
    node->name = g_strdup("object");
}

/* ---- final normalization (ROADMAP M6) ----------------------------- */

/**
 * Allocate a fresh, attribute-less element node.
 */
static Node *new_element(const char *name) {
    Node *node = g_new0(Node, 1);
    node->name = g_strdup(name);
    return node;
}

/**
 * Prepend child as the new head of parent's child list.
 */
static void prepend_child(Node *parent, Node *child) {
    child->parent = parent;
    child->next = parent->children;
    parent->children = child;
}

/**
 * Guarantee an <interface> root carries a <requires lib="gtk" version="4.0"/>
 * element as its first child (ROADMAP M6).
 *
 * @return root, unchanged, with <requires> prepended when absent.
 */
static Node *ensure_requires(Node *root) {
    if (find_child(root, "requires") != NULL)
        return root;

    Node *requires = new_element("requires");
    // set_attr prepends, so build in reverse order to keep the attribute
    // chain in the conventional lib="gtk" version="4.0" order.
    set_attr(requires, "version", "4.0");
    set_attr(requires, "lib", "gtk");
    prepend_child(root, requires);
    return root;
}

/**
 * Checks if current node tag is `object` and search for a
 * known class from scanned directory to embed it.
 *
 * The custom <object> is retagged in place with the template's real GTK
 * parent class, its children are replaced by the template's subtree, and
 * usage-site <property> values override the template ones. Nested custom
 * objects inside the adopted template content are expanded recursively by
 * normalize_node().
 *
 * @param node  current node to check
 * @param stack resolution stack (cycle guard for non-native classes)
 */
static void expand_custom_object(Node *node, ResolutionStack *stack) {
    if (!g_str_equal(node->name, "object")) return;

    const gchar *class_name = get_attr(node, "class");

    if (class_name == NULL) {
        g_printerr("Failed to find template class at %s\n", node->name);
        return;
    }

    if (component_registry_is_builtin(class_name) == TRUE) {
        g_print("  Skipping native widget %s\n", class_name);
        return;
    }

    g_print("  Found custom object %s\n", class_name);

    Node *template = component_registry_get_template(class_name);

    if (template == NULL) {
        g_printerr("Class \"%s\" does not have a valid template name\n", class_name);
        return;
    }

    const gchar *parent_class = get_attr(template, "parent");
    if (parent_class == NULL) {
        g_printerr("Template \"%s\" does not declare a parent class\n", class_name);
        free_node(template);
        return;
    }

    // Cycle detection: if this custom class is already being expanded
    // we are inside a recursive template and must stop.  The check and
    // push happen BEFORE retag so class_name still points at valid memory
    // (set_attr below frees the old attr value).
    if (stack_contains(stack, class_name)) {
        g_printerr("Template cycle detected: class \"%s\" re-enters "
                   "its own resolution\n",
                   class_name);
        free_node(template);
        return;
    }

    if (!stack_push(stack, class_name)) {
        g_printerr("Template resolution stack overflow for class \"%s\"\n",
                   class_name);
        free_node(template);
        return;
    }

    // Retag this <object> in place with the real GTK class. The node keeps
    // its identity, so id, position and sibling links all survive.
    set_attr(node, "class", parent_class);

    // Adopt the template's subtree as this object's children.
    Node *old_children = node->children;
    node->children = template->children;
    template->children = NULL;
    for (Node *child = node->children; child != NULL; child = child->next)
        child->parent = node;

    // Property merge: a usage-site <property name="k"> either overrides the
    // template's property with the same name or is appended when absent.
    for (const Node *usage = old_children; usage != NULL; usage = usage->next) {
        if (!g_str_equal(usage->name, "property")) continue;

        const gchar *key = get_attr(usage, "name");
        if (key == NULL) continue;

        Node *adopted = NULL;
        for (Node *child = node->children; child != NULL; child = child->next) {
            if (g_str_equal(child->name, "property")
                && g_str_equal(get_attr(child, "name"), key)) {
                adopted = child;
                break;
            }
        }

        if (adopted != NULL) {
            g_free(adopted->content);
            adopted->content = g_strdup(usage->content);
        } else {
            Node *copy = clone_node(usage);
            copy->parent = node;
            copy->next = NULL;
            if (node->children == NULL) {
                node->children = copy;
            } else {
                Node *tail = node->children;
                while (tail->next != NULL)
                    tail = tail->next;
                tail->next = copy;
            }
        }
    }

    // The old usage-site children are dropped (they were conceptually replaced
    // by the template's content); freeing them also avoids a leak.
    free_node(old_children);

    // Recurse into the adopted template children so nested customs expand.
    // The stack is already holding this class — any re-entry will be caught
    // by the cycle check above (ROADMAP M5 loop protection).
    node->children = normalize_node(node->children, stack);
    stack_pop(stack);

    free_node(template);
}

/**
 * Apply all required normalizations to each node read from XML
 * according to our rules to render GTK interfaces later.
 *
 * Walks the sibling list bottom-up (children before their parent) so nested
 * custom objects are resolved before the enclosing element is processed.
 * Custom <object> nodes are expanded in place by expand_custom_object();
 * because the node identity never changes the list head is stable.
 *
 * @param node  current node to normalize
 * @param stack resolution stack (cycle guard for non-native classes)
 * @return the (unchanged) head of the processed list
 */
static Node *normalize_node(Node *node, ResolutionStack *stack) {
    for (Node *current = node; current != NULL; current = current->next) {
        // Call recursive if we have children, so we do bottom -> up
        if (current->children != NULL)
            current->children = normalize_node(current->children, stack);

        normalize_tag_template(current);
        expand_custom_object(current, stack);
    }

    return node;
}

Node *normalize_templates(Node *root) {
    if (root == NULL) {
        g_printerr("XML does not have a valid root node\n");
        return NULL;
    }

    ResolutionStack stack;
    stack_init(&stack);
    normalize_node(root, &stack);

    if (g_str_equal(root->name, "interface"))
        return ensure_requires(root);

    // Wrap the (now canonical) root in an <interface> so the tree is
    // GtkBuilder-ready (ROADMAP M6). The fresh node owns the old root.
    Node *interface = new_element("interface");
    prepend_child(interface, root);
    return ensure_requires(interface);
}
