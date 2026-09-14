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
 * Selects the headless Wayland display, initializes the Adwaita/GTK toolkits,
 * parses the positional command-line arguments (see parse_config() in
 * utils.h), loads the input XML, runs the parser/normalization pipeline
 * (ROADMAP M2-M6) and finally renders the normalized DOM to a PNG snapshot
 * through the renderer module (ROADMAP M7-M10).
 */

#include <stdlib.h>
#include <glib.h>
#include <adwaita.h>
#include "utils.h"
#include "xml_loader.h"
#include "xml_parser.h"
#include "normalizer.h"
#include "renderer.h"

/**
 * Requires at least the input and output paths. Selects a headless Wayland
 * display, initializes Adwaita, then loads the input XML and runs the whole
 * preview pipeline (parse, normalize, render to PNG).
 *
 * @brief The program main loop and entrypoint
 * @param argument_count number of elements in arguments, never 0.
 * @param arguments     argument vector; argument indexes are defined in
 *                      utils.h (ARG_*_IDX constants).
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on invalid usage or when the
 *         rendering pipeline reports an error.
 */
int main(const int argument_count, char *arguments[]) {
    g_print("\n\n\n");

    if (argument_count < 3) {
        g_printerr("Wrong number of arguments.");
        return EXIT_FAILURE;
    }

    g_print("Parsing CLI arguments\n\n");
    const struct Config config = parse_config(argument_count, arguments);

    // Initialize ADW, GTK, custom libraries and other required tools
    initialize_extra_libraries(config);

    // Load XML file given by argument and parse its main node
    gchar *xml = load_file(config.input_path);
    g_print("Loaded XML from %s\n", config.input_path);
    Node *template_file = parse_xml(xml);

    if (template_file == NULL) {
        g_printerr("main: failed to parse %s\n", config.input_path);
        g_free(xml);
        return EXIT_FAILURE;
    }

    // normalize the DOM (copy template parent into class) and canonicalize it
    // (root <interface> + <requires>), returning the owned root.
    template_file = normalize_templates(template_file);

    const int render_status = renderer_render_interface(config, template_file);

    free_node(template_file);
    g_free(xml);

    return render_status;
}
