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
 * @file renderer.c
 * @brief ROADMAP M7-M10 — serialize, build, snapshot and export the preview.
 *
 * Takes the normalized Node tree produced by the normalizer and turns it into
 * a PNG snapshot: the tree is round-tripped back to XML (M7), loaded by
 * GtkBuilder (M8), rendered to a GskRenderNode via a GtkWidgetPaintable
 * snapshot (M9) and rasterized into a GdkTexture through a GskRenderer before
 * being saved as PNG (M10).
 */

#include <glib.h>
#include <graphene.h>
#include "renderer.h"
#include "xml_loader.h"
#include "xml_parser.h"

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

        const gchar *name = current->name;
        gboolean is_template = g_str_equal(name, "template");

        // A template is inlined as an <object class="..."> so GtkBuilder
        // instantiates it directly (we never rely on GTK template semantics).
        if (is_template)
            name = "object";

        g_string_append_printf(xml, "<%s", name);

        for (const Attr *attribute = current->attrs;
             attribute != NULL;
             attribute = attribute->next) {
            // The template's class attribute becomes the object's class.
            if (is_template && g_str_equal(attribute->name, "class"))
                continue;

            g_autofree gchar *escaped = g_markup_escape_text(
                attribute->value, -1);
            g_string_append_printf(xml, " %s=\"%s\"", attribute->name,
                                   escaped);
        }

        if (is_template) {
            const gchar *class_name = get_attr(current, "class");
            g_autofree gchar *escaped = g_markup_escape_text(
                class_name != NULL ? class_name : "GtkWidget", -1);
            g_string_append_printf(xml, " class=\"%s\"", escaped);
        }

        if (current->content != NULL) {
            g_autofree gchar *escaped = g_markup_escape_text(current->content,
                                                             -1);
            g_string_append_printf(xml, ">%s</%s>\n", escaped, name);
            continue;
        }

        if (current->children != NULL) {
            g_string_append(xml, ">\n");
            serialize_node(xml, current->children, depth + 1);
            for (int i = 0; i < depth; i++)
                g_string_append(xml, "  ");
            g_string_append_printf(xml, "</%s>\n", name);
        } else {
            g_string_append(xml, "/>\n");
        }
    }
}

/**
 * Round-trip the normalized DOM back to an XML string loadable by GtkBuilder
 * (ROADMAP M7).
 *
 * The output is wrapped in an <interface> root with a <requires> declaration;
 * a root <template> is inlined as an <object> so it is instantiated directly.
 *
 * @param  root Root node of the normalized DOM.
 * @return Newly allocated XML string, or NULL on error. Free with g_free().
 */
static char *serialize_xml(const Node *root) {
    if (root == NULL)
        return NULL;

    GString *xml = g_string_new("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                "<interface>\n"
                                "  <requires lib=\"gtk\" version=\"4.0\"/>\n");

    serialize_node(xml, root, 1);

    g_string_append(xml, "</interface>\n");

    return g_string_free(xml, FALSE);
}

/**
 * Build a widget tree from an XML string (ROADMAP M8).
 *
 * @param  xml     NUL-terminated XML source.
 * @param  builder Output location for the created builder (or NULL).
 * @return The first top-level widget of the loaded interface, or NULL on error.
 */
static GtkWidget *load_builder(const char *xml, GtkBuilder **builder) {
    GtkBuilder *new_builder = gtk_builder_new();
    GError *error = NULL;

    if (gtk_builder_add_from_string(new_builder, xml, -1, &error) == 0) {
        g_printerr("renderer: failed to load interface: %s\n", error->message);
        g_error_free(error);
        g_object_unref(new_builder);
        return NULL;
    }

    GtkWidget *root = NULL;
    GSList *objects = gtk_builder_get_objects(new_builder);
    for (const GSList *link = objects; link != NULL; link = link->next) {
        if (GTK_IS_WIDGET(link->data)) {
            root = GTK_WIDGET(link->data);
            break;
        }
    }
    g_slist_free(objects);

    if (root == NULL) {
        g_printerr("renderer: no widget found in loaded interface\n");
        g_object_unref(new_builder);
        return NULL;
    }

    if (builder != NULL)
        *builder = new_builder;
    else
        g_object_unref(new_builder);

    return root;
}

/**
 * Allocate a widget to the given size and render it to a GskRenderNode
 * (ROADMAP M9).
 *
 * @param widget Widget to render.
 * @param width  Requested allocation width in pixels.
 * @param height Requested allocation height in pixels.
 * @return A render node describing the widget, or NULL on failure. The caller
 *         owns the returned node and must unref it with gsk_render_node_unref().
 */
static GskRenderNode *render_widget(GtkWidget *widget, const int width,
                                    const int height) {
    if (width <= 0 || height <= 0)
        return NULL;

    gtk_widget_realize(widget);
    gtk_widget_allocate(widget, width, height, -1, NULL);

    GdkPaintable *paintable = gtk_widget_paintable_new(widget);
    GtkSnapshot *snapshot = gtk_snapshot_new();
    gdk_paintable_snapshot(paintable, snapshot, width, height);
    g_object_unref(paintable);

    return gtk_snapshot_free_to_node(snapshot);
}

/**
 * Rasterize a render node into a GdkTexture through the display's renderer.
 *
 * @param node   Render node to rasterize.
 * @param width  Output texture width in pixels.
 * @param height Output texture height in pixels.
 * @return A newly allocated GdkTexture, or NULL on failure.
 */
static GdkTexture *node_to_texture(GskRenderNode *node, const int width,
                                   const int height) {
    GdkDisplay *display = gdk_display_get_default();
    if (display == NULL) {
        g_printerr("renderer: no default display available\n");
        return NULL;
    }

    GskRenderer *renderer = gsk_renderer_new_for_surface(NULL);
    GError *error = NULL;
    if (!gsk_renderer_realize_for_display(renderer, display, &error)) {
        g_printerr("renderer: failed to realize renderer: %s\n",
                   error->message);
        g_error_free(error);
        g_object_unref(renderer);
        return NULL;
    }

    const graphene_rect_t viewport = GRAPHENE_RECT_INIT(0, 0, width, height);
    GdkTexture *texture = gsk_renderer_render_texture(renderer, node,
                                                      &viewport);

    gsk_renderer_unrealize(renderer);
    g_object_unref(renderer);

    return texture;
}

/**
 * Export a GdkTexture to a PNG file (ROADMAP M10).
 *
 * @param texture Texture to save.
 * @param path    Destination PNG path.
 * @return EXIT_SUCCESS on success, EXIT_FAILURE otherwise.
 */
static int export_png(GdkTexture *texture, const char *path) {
    if (!gdk_texture_save_to_png(texture, path)) {
        g_printerr("renderer: failed to write '%s'\n", path);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int renderer_render_interface(const struct Config config,
                              const Node *current_node) {
    if (current_node == NULL) {
        g_printerr("renderer: the template given is null\n");
        return EXIT_FAILURE;
    }

    g_print("Using template %s\n", config.input_path);

#ifdef DEBUG
    g_print("\n\nParsed DOM:\n");
    print_node(current_node, 0);
#endif

    g_autofree char *xml = serialize_xml(current_node);
    if (xml == NULL) {
        g_printerr("renderer: failed to serialize the DOM\n");
        return EXIT_FAILURE;
    }

    g_print("Serialized DOM:\n%s\n", xml);

    GtkBuilder *builder = NULL;
    GtkWidget *root = load_builder(xml, &builder);
    if (root == NULL) {
        g_printerr("renderer: failed to build the widget tree\n");
        return EXIT_FAILURE;
    }

    g_print("Rendering %s (%dx%d)\n", config.output_path, config.width,
            config.height);

    GskRenderNode *node = render_widget(root, config.width, config.height);

    // root and builder are both owned by the builder; free it when done.
    if (builder != NULL)
        g_object_unref(builder);

    if (node == NULL) {
        g_printerr("renderer: failed to render the widget tree\n");
        return EXIT_FAILURE;
    }

    GdkTexture *texture = node_to_texture(node, config.width, config.height);
    gsk_render_node_unref(node);

    if (texture == NULL) {
        g_printerr("renderer: failed to create the snapshot texture\n");
        return EXIT_FAILURE;
    }

    const int status = export_png(texture, config.output_path);
    g_object_unref(texture);

    if (status != EXIT_SUCCESS)
        return status;

    g_print("Snapshot written to %s\n", config.output_path);

    return EXIT_SUCCESS;
}