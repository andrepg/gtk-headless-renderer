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
 * @file main.c
 * @brief CLI entry point and orchestrator of the preview pipeline.
 *
 * Boots the headless Broadway display and the Adwaita/GTK toolkits, parses the
 * positional command-line arguments (see parse_config() in utils.h) and loads
 * the input XML into memory so the parser/normalization pipeline
 * (ROADMAP M2) can consume it. Later milestones attach the DOM parser,
 * template expansion, GTK builder, renderer and PNG export stages.
 */

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <adwaita.h>
#include "utils.h"
#include "xml_loader.h"

/**
 * Program entry point.
 *
 * Requires at least the input and output paths. Selects a headless Broadway
 * display, initializes Adwaita, then loads the input XML into memory ready for
 * the parser/normalization pipeline.
 *
 * @param argument_count number of elements in arguments, never 0.
 * @param arguments     argument vector; argument indexes are defined in
 *                      utils.h (ARG_*_IDX constants).
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on invalid usage.
 */
int main(const int argument_count, char *arguments[]) {
    if (argument_count < 3) {
        g_printerr("Wrong number of arguments.");
        return EXIT_FAILURE;
    }

    // Pin the GDK backend to headless Wayland (see ROADMAP M1). The private
    // Wayland socket is provided by the launcher (WAYLAND_DISPLAY + XDG_RUNTIME_DIR).
    g_print("Setting up headless display\n");
    g_setenv("GDK_BACKEND", "wayland", TRUE);

    // Initialize Adwaita (and Gtk, chained per dependencies)
    g_print("Initializing Adwaita toolkit\n");

    adw_init();

    const struct Config config = parse_config(arguments);

    g_print("Output path: %s\n", config.output_path);

    // Phase 1: load the XML into memory for the parser/normalization pipeline.
    gchar *xml = load_file(config.input_path);
    g_print("Loaded XML from %s (%lu bytes)\n",
            config.input_path, (unsigned long) strlen(xml));

    // Phase 2+: xml_parser -> xml_transform -> gtk_loader -> renderer -> export
    g_free(xml);

    return EXIT_SUCCESS;
}