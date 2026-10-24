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
    GtkWidget *dialog; /**< Non-NULL for AdwDialogs (see resolve_render_root). */
    int width;
    int height;
    int frames_waited;
    int max_frames;
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
 * everything else is wrapped in a transient container so GTK can lay it out.
 *
 * AdwDialogs are never presented as dialogs on a stack: the root is a hidden
 * AdwWindow host and the dialog is presented onto it (see
 * initialize_main_loop). AdwDialog content — including AdwShortcutsDialog's
 * internally-constructed AdwToolbarView — is not extractable, so every dialog
 * variant takes this single fallback.
 *
 * @param builder Builder holding the widget tree.
 * @param presented_dialog_out filled with the dialog widget (still owned by
 *        the builder) only when the root is an AdwDialog, so the renderer can
 *        present it onto the hidden host and wait for its content; otherwise
 *        left untouched.
 * @return Renderable root widget, or NULL (with a diagnostic) when none
 *         exists.
 */
static GtkWidget *resolve_render_root(GtkBuilder *builder,
                                      GtkWidget **presented_dialog_out) {
    GtkWidget *root = get_first_widget(builder);
    if (root == NULL) {
        g_printerr("renderer: no widget found in the serialized template\n");
        return NULL;
    }

    if (GTK_IS_WINDOW(root))
        return root;

    if (ADW_IS_DIALOG(root)) {
        /*
         * AdwDialogs build their content through adw_dialog_present — even
         * with declared children (AdwShortcutsDialog constructs its
         * AdwToolbarView internally, and the widget is not extractable:
         * creating a window invalidates it). There is no user widget to hand
         * to a plain window, so every dialog variant is presented onto a
         * hidden AdwWindow host (see initialize_main_loop) and the capture
         * loop waits (bounded) for the content to materialize.
         */
        g_printerr("renderer: template roots at an AdwDialog; "
                   "presenting on a hidden AdwWindow\n");
        GtkWidget *host = GTK_WIDGET(adw_window_new());
        gtk_window_set_decorated(GTK_WINDOW(host), FALSE);
        gtk_window_set_resizable(GTK_WINDOW(host), FALSE);
        if (presented_dialog_out)
            *presented_dialog_out = root;
        return host;
    }

    GtkWidget *wrapper_window = gtk_window_new();
    gtk_window_set_decorated(GTK_WINDOW(wrapper_window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(wrapper_window), FALSE);
    gtk_window_set_child(GTK_WINDOW(wrapper_window), root);
    return wrapper_window;
}

/* ---- capture + export (M9/M10) --------------------------------------- */

/* AdwDialogs build their content across paint frames while presenting; give
 * them a bounded number of frames to materialize. */
static const int MAX_CHILDLESS_DIALOG_FRAMES = 8;

/**
 * Snapshot + render the widget tree into a GdkTexture, save it to PNG.
 *
 * @return TRUE once a texture was snapshotted and saved to disk.
 */
static gboolean capture_and_save(GtkWidget *widget, RenderContext *context) {
    /* The widget tree is allocated to its natural height (the toplevel only
     * constrains the width), so snapshot at the realized extent and fall back
     * to the configured canvas size when nothing is allocated yet. */
    const int width = gtk_widget_get_width(widget);
    const int height = gtk_widget_get_height(widget);
    const int snapshot_width = width > 0 ? width : context->width;
    const int snapshot_height = height > 0 ? height : context->height;

    GtkSnapshot *snapshot = gtk_snapshot_new();
    GdkPaintable *paintable = gtk_widget_paintable_new(widget);
    gdk_paintable_snapshot(paintable, snapshot, snapshot_width,
                           snapshot_height);
    g_object_unref(paintable);

    GskRenderNode *render_node = gtk_snapshot_to_node(snapshot);
    g_object_unref(snapshot);

    if (render_node == NULL) {
        g_printerr("renderer: no render node produced for the widget tree\n");
        return FALSE;
    }

    GdkTexture *texture = NULL;
    {
        GskRenderer *renderer =
            gtk_native_get_renderer(gtk_widget_get_native(widget));
        const graphene_rect_t bounds =
            GRAPHENE_RECT_INIT(0, 0, snapshot_width, snapshot_height);
        texture = gsk_renderer_render_texture(renderer, render_node, &bounds);
    }

    gsk_render_node_unref(render_node);

    if (texture == NULL) {
        g_printerr("renderer: no texture produced for the widget tree\n");
        return FALSE;
    }

    gdk_texture_save_to_png(texture, context->output_path);
    g_object_unref(texture);
    return TRUE;
}

/**
 * Captures the widget tree on the first real paint cycle (hooked via
 * GdkFrameClock::after-paint), then withdraws the toplevel and quits the
 * loop. Only AdwDialogs may defer: their content builds across paint frames
 * while presenting, so capture waits (bounded) for it.
 */
static void capture_and_quit(GdkFrameClock *clock, gpointer user_data) {
    RenderContext *context = user_data;
    (void) clock;

    context->frames_waited++;

    gboolean content_ready = TRUE;
    if (context->dialog != NULL) {
        /* The dialog builds its content asynchronously while presenting;
         * wait until it produced a child that is actually painted. */
        GtkWidget *dialog_child =
            adw_dialog_get_child(ADW_DIALOG(context->dialog));
        content_ready =
            dialog_child != NULL &&
            gtk_widget_get_mapped(dialog_child) &&
            gtk_widget_get_width(dialog_child) > 0 &&
            gtk_widget_get_height(dialog_child) > 0;
    }

    if (!content_ready && context->frames_waited < context->max_frames) {
        g_print("renderer: deferring capture (frame %d/%d) content=%d\n",
                context->frames_waited, context->max_frames, content_ready);
        return;
    }

    if (!content_ready)
        g_printerr("renderer: timed out waiting for dialog content; "
                   "capturing the host frame\n");

    GtkWidget *target = context->root;
    if (context->dialog != NULL) {
        GtkWidget *dialog_child =
            adw_dialog_get_child(ADW_DIALOG(context->dialog));
        if (dialog_child != NULL)
            target = dialog_child;
    }

    context->done = capture_and_save(target, context);

    /* Withdraw the toplevel right away so no window remains mapped on the
     * compositor when we quit. */
    gtk_widget_set_visible(context->root, FALSE);

    g_main_loop_quit(context->loop);
}

/**
 * Realize and show the toplevel, wait for paint cycles (hooked via
 * GdkFrameClock::after-paint) and let it capture + quit.
 *
 * Regular roots only get their width forced (gtk_widget_set_size_request with
 * -1 for the height): the window then allocates its natural height so a
 * compact widget tree fills the pixels instead of sitting on a white canvas.
 * AdwDialogs get the full configured minimum so the host has geometry while
 * the presenting dialog still builds its content.
 */
static void initialize_main_loop(RenderContext *context) {
    /*
     * Regular widgets/windows: constrain only the width so the toplevel
     * allocates its natural height – the preview pixels cover the content
     * instead of a white canvas. AdwDialog hosts keep the full configured
     * minimum so the host has valid geometry while the dialog's content is
     * still constructing.
     */
    if (context->dialog != NULL)
        gtk_widget_set_size_request(context->root, context->width,
                                    context->height);
    else
        gtk_widget_set_size_request(context->root, context->width, -1);
    gtk_widget_realize(context->root);

    /* Dialogs materialize their content only through presentation; the host
     * is realized here so the dialog can present onto a live toplevel. */
    if (context->dialog != NULL)
        adw_dialog_present(ADW_DIALOG(context->dialog), context->root);

    GdkFrameClock *clock = gtk_widget_get_frame_clock(context->root);
    g_signal_connect(clock, "after-paint", G_CALLBACK(capture_and_quit),
                     context);

    gtk_widget_set_visible(context->root, TRUE);

    context->loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(context->loop);
    g_signal_handlers_disconnect_by_func(clock, capture_and_quit, context);
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

    /*
     * Real-headless guard: before any window exists, refuse to render unless
     * the active GDK display is exactly the one pinned by the launcher
     * (WAYLAND_DISPLAY). This makes a window appearing on the user's session
     * display structurally impossible — the renderer fails instead of
     * flashing a window anywhere but the private headless socket.
     */
    const char *expected_display = g_getenv("WAYLAND_DISPLAY");
    GdkDisplay *active_display = gdk_display_get_default();
    if (active_display == NULL || expected_display == NULL ||
        g_strcmp0(gdk_display_get_name(active_display), expected_display) != 0) {
        g_printerr("renderer: refusing to render: active display \"%s\" does "
                   "not match the pinned WAYLAND_DISPLAY \"%s\"\n",
                   active_display != NULL
                       ? gdk_display_get_name(active_display)
                       : "(none)",
                   expected_display);
        return ERROR_DISPLAY_NOT_HEADLESS;
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

    GtkWidget *presented_dialog = NULL;
    GtkWidget *root = resolve_render_root(builder, &presented_dialog);
    if (root == NULL) {
        g_object_unref(builder);
        return ERROR_TEMPLATE_NOT_FOUND;
    }

    RenderContext context = {
        .output_path = config.output_path,
        .root = root,
        .dialog = presented_dialog,
        .width = config.width,
        .height = config.height,
        .frames_waited = 0,
        .max_frames = presented_dialog != NULL ? MAX_CHILDLESS_DIALOG_FRAMES
                                               : 1,
        .done = FALSE,
        .loop = NULL,
    };
    initialize_main_loop(&context);

    g_object_unref(builder);

    return context.done ? EXIT_SUCCESS : EXIT_FAILURE;
}