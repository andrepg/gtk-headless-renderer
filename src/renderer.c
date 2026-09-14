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
 * @brief ROADMAP M8-M10 — build, snapshot and export the preview.
 *
 * Takes the canonical Node tree produced by the normalizer, serializes it
 * back to XML through the display-free serializer module (M7), loads it with
 * GtkBuilder (M8) and then renders/snapshots the widget tree (M9/M10):
 * the root widget is presented in the headless Wayland display, after a
 * real paint cycle a GskRenderNode is rasterized with the native GskRenderer
 * into a GdkTexture and exported to PNG (gdk_texture_save_to_png).
 */

#include <glib.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include <graphene.h>
#include "utils.h"
#include "error_codes.h"
#include "renderer.h"
#include "serializer.h"

/* ---- internal types -------------------------------------------------- */

typedef struct {
    const char *output_path;
    GtkWidget *root;
    int width;
    int height;
    int tick_count;
    gboolean done;
    GMainLoop *loop;
} RenderContext;

/* ---- root resolution (M8) -------------------------------------------- */

/**
 * Return the first renderable toplevel widget from the builder, preferring
 * a GtkWindow when one exists (it can be presented standalone without
 * a wrapper). Parented widgets (GtkBuilder child objects reachable through
 * gtk_builder_get_objects) are skipped, so the picked root is always
 * parentless and safe to set as a window child.
 */
static GtkWidget *get_first_widget(GtkBuilder *builder) {
    GSList *object_list = gtk_builder_get_objects(builder);
    GtkWidget *first_widget = NULL;

    for (GSList *iter = object_list; iter; iter = g_slist_next(iter)) {
        if (!GTK_IS_WIDGET(iter->data)) continue;
        GtkWidget *widget = GTK_WIDGET(iter->data);
        if (gtk_widget_get_parent(widget))
            continue;
        if (!first_widget)
            first_widget = widget;
        if (GTK_IS_WINDOW(widget)) {
            first_widget = widget;
            break;
        }
    }

    g_slist_free(object_list);
    return first_widget;
}

/**
 * Resolve the root widget for rendering. Windows are returned directly;
 * everything else is wrapped in a transient GtkWindow so GTK can lay it
 * out. Adwaita presenter dialogs are unwrapped to their child.
 *
 * @param builder Builder holding the widget tree.
 * @return Renderable root widget, or NULL (with a diagnostic) when none
 *         exists.
 */
static GtkWidget *resolve_render_root(GtkBuilder *builder) {
    GtkWidget *root = get_first_widget(builder);
    if (root == NULL) {
        g_printerr("renderer: no widget found in the serialized template\n");
        return NULL;
    }

    if (GTK_IS_WINDOW(root))
        return root;

    if (ADW_IS_DIALOG(root)) {
        GtkWidget *content = adw_dialog_get_child(ADW_DIALOG(root));
        if (content == NULL) {
            g_printerr("renderer: the template roots at an empty AdwDialog\n");
            return NULL;
        }
        g_printerr("renderer: template roots at an AdwDialog; "
                   "rendering its child (best-effort)\n");
        root = content;
    }

    GtkWidget *wrapper_window = gtk_window_new();
    gtk_window_set_decorated(GTK_WINDOW(wrapper_window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(wrapper_window), FALSE);
    gtk_window_set_child(GTK_WINDOW(wrapper_window), root);
    return wrapper_window;
}

/* ---- capture + export (M9/M10) --------------------------------------- */

/**
 * Snapshot + render the widget tree into a GdkTexture, save it to PNG, and
 * quit the main loop. Sets context->done only once a texture was saved.
 */
static void capture_and_save(GtkWidget *widget, RenderContext *context) {
    GtkSnapshot *snapshot = gtk_snapshot_new();
    GdkPaintable *paintable = gtk_widget_paintable_new(widget);
    gdk_paintable_snapshot(paintable, snapshot, context->width, context->height);
    g_object_unref(paintable);

    GskRenderNode *render_node = gtk_snapshot_to_node(snapshot);
    g_object_unref(snapshot);

    if (render_node == NULL) {
        g_printerr("renderer: no render node produced for the widget tree\n");
        return;
    }

    GdkTexture *texture = NULL;
    {
        GskRenderer *renderer =
            gtk_native_get_renderer(gtk_widget_get_native(widget));
        const graphene_rect_t bounds =
            GRAPHENE_RECT_INIT(0, 0, context->width, context->height);
        texture = gsk_renderer_render_texture(renderer, render_node, &bounds);
    }

    gsk_render_node_unref(render_node);

    if (texture == NULL) {
        g_printerr("renderer: no texture produced for the widget tree\n");
        return;
    }

    gdk_texture_save_to_png(texture, context->output_path);
    g_object_unref(texture);
    context->done = TRUE;
}

/**
 * Captures the widget tree synchronously with the frame clock's after-paint
 * phase (i.e. after a real paint cycle laid out and rendered the tree), then
 * withdraws the toplevel window and quits the main loop.
 */
static void render_after_paint(GdkFrameClock *clock, gpointer user_data) {
    RenderContext *context = user_data;
    g_signal_handlers_disconnect_by_func(clock, render_after_paint, context);

    capture_and_save(context->root, context);

    /* Withdraw the toplevel right away so no window remains mapped on the
     * headless compositor when we quit. */
    gtk_widget_set_visible(context->root, FALSE);

    g_main_loop_quit(context->loop);
}

/**
 * Frame-clock tick: let two frames go by so the initial layout + paint cycle
 * completes, then hook the capture into the after-paint phase of the next
 * frame and stop ticking.
 */
static gboolean render_frame_callback(GtkWidget *widget,
                                      GdkFrameClock *clock,
                                      gpointer user_data) {
    RenderContext *context = user_data;
    (void) widget;

    if (context->tick_count++ < 2)
        return G_SOURCE_CONTINUE;

    g_signal_connect(clock, "after-paint",
                     G_CALLBACK(render_after_paint), context);
    return G_SOURCE_REMOVE;
}

static gboolean watchdog_timeout(gpointer user_data) {
    g_printerr("renderer: timed out waiting for a frame; "
               "nothing could be rendered\n");
    g_main_loop_quit((GMainLoop *) user_data);
    return G_SOURCE_REMOVE;
}

static void initialize_main_loop(RenderContext *context) {
    gtk_widget_add_tick_callback(context->root, render_frame_callback, context,
                                 NULL);

    gtk_widget_set_size_request(context->root, context->width, context->height);
    gtk_widget_set_visible(context->root, TRUE);

    context->loop = g_main_loop_new(NULL, FALSE);
    guint watchdog_id = g_timeout_add_seconds(5, watchdog_timeout, context->loop);
    g_main_loop_run(context->loop);
    g_source_remove(watchdog_id);
    g_main_loop_unref(context->loop);
    context->loop = NULL;
}

/* ---- public API ------------------------------------------------------- */

int renderer_render_interface(const struct Config config,
                              const Node *current_node) {
    if (current_node == NULL) {
        g_printerr("renderer: the template passed is null or invalid.");
        return ERROR_TEMPLATE_NOT_FOUND;
    }

    g_autofree char *serialized_template = serialize_xml(current_node);

    if (serialized_template == NULL) {
        g_printerr("renderer: the template could not be serialized.");
        return ERROR_TEMPLATE_INVALID;
    }

#ifdef DEBUG
    g_print("Serialized template:\n%s\n", serialized_template);
#endif

    GError *g_error = NULL;
    GtkBuilder *builder = gtk_builder_new();
    if (!gtk_builder_add_from_string(builder, serialized_template, -1,
                                     &g_error)) {
        g_printerr("renderer: failed to build the widget tree: %s\n",
                   g_error != NULL ? g_error->message : "unknown error");
        g_clear_error(&g_error);
        g_object_unref(builder);
        return ERROR_TEMPLATE_INVALID;
    }

    GtkWidget *root = resolve_render_root(builder);
    if (root == NULL) {
        g_object_unref(builder);
        return ERROR_TEMPLATE_NOT_FOUND;
    }

    RenderContext context = {
        .output_path = config.output_path,
        .root = root,
        .width = config.width,
        .height = config.height,
        .tick_count = 0,
        .done = FALSE,
        .loop = NULL,
    };
    initialize_main_loop(&context);

    g_object_unref(builder);

    return context.done ? EXIT_SUCCESS : EXIT_FAILURE;
}