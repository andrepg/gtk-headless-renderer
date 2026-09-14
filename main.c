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
#include <glib.h>
#include <adwaita.h>
#include "utils.h"
#include "xml_loader.h"
#include "xml_parser.h"
#include "component_registry.h"
#include "normalization.h"

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
    g_print("\n\n\n");

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

    g_print("Parsing CLI arguments\n\n");
    const struct Config config = parse_config(argument_count, arguments);

    // Phase 1: load the XML into memory for the parser/normalization pipeline.
    gchar *xml = load_file(config.input_path);
    g_print("Loaded XML from %s\n", config.input_path);

    // Phase 2: parse the raw XML into our display-free DOM (ROADMAP M3).
    Node *template_file = parse_xml(xml);
    if (template_file == NULL) {
        g_printerr("main: failed to parse %s\n", config.input_path);
        g_free(xml);
        return EXIT_FAILURE;
    }

    // Phase 3: index sibling .ui interfaces (ROADMAP M4). The scan folder comes
    // from the new optional positional argument; when absent the registry
    // derives it from the input file's own directory.
    component_registry_init_scan(config.input_path, config.src_dir);
#ifdef DEBUG
    component_registry_dump();
#endif

    // Phase 2.5: normalize the DOM (copy template parent into class).
    normalize_templates(template_file);

#ifdef DEBUG
    g_print("\n\nParsed DOM:\n");
    print_node(template_file, 0);
#endif

    component_registry_cleanup();

    free_node(template_file);
    g_free(xml);

    return EXIT_SUCCESS;
}
