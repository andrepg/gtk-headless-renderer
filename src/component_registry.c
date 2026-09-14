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

/*
 * @file component_registry.c
 *
 * Display-free custom-component registry for
 * gtk-embedded-preview, built on Phase-2/M3's xml_parser.c DOM.
 *
 * "Custom component" = a class whose name is not a toolkit builtin
 * (Gtk/Adw/Gdk/Gio/Gsk/Graphene/Pango/cairo/...). For every such class the
 * registry scans the target directory's sibling *.ui files once (on
 * component_registry_init_scan()) and indexes the single file whose top-level
 * <template class="X"> declares it.
 *
 * Resolution rules:
 *   - The class attribute is the ONLY reliable join key; filenames are never
 *     derived from the class name.
 *   - A class resolves to the sibling .ui whose <template class="X"> it
 *     matches (first declaring file wins; later duplicates g_warning).
 *   - Unresolved custom classes => g_warning() and skipped (never fatal).
 *
 * The index is a GLib map: class name -> TemplateEntry (path + cached
 * <template> subtree, both owned by the table). Threading: single-threaded;
 * the scan runs once, explicitly, from component_registry_init_scan().
 *
 * Display-free by construction: libxml2 + GLib only, no GTK, no display,
 * same constraint as xml_parser.c. The whole Phase-2 pair compiles headless.
 */

#include "component_registry.h"

#include "xml_parser.h"

#include <glib.h>
#include <stdlib.h>
#include <string.h>

/* Index of custom components: class name -> TemplateEntry. */
static GHashTable *s_templates = NULL;
static char *s_dir = NULL;
static gboolean s_scanned = FALSE;

/* Per-class entry: file path + cached <template> subtree. */
typedef struct {
    char *path; /* owning .ui file path */
    Node *template_node; /* deep-cloned <template> subtree, or NULL */
} TemplateEntry;

static void entry_free(gpointer data) {
    TemplateEntry *entry = data;
    g_free(entry->path);
    free_node(entry->template_node);
    g_free(entry);
}

/* Toolkit builtin class-name prefixes: a class starting with one of these is
 * a GTK/Adwaita component, never a custom component. */
static const char *const k_known_prefixes[] = {
    "Gtk", "Adw", "Gdk", "Gio", "Gsk", "Graphene", "Pango", "cairo",
    "GObject", "GType", NULL
};

/* ---- builtin detection -------------------------------------------- */

gboolean component_registry_is_builtin(const char *class_name) {
    if (class_name == NULL || class_name[0] == '\0')
        return TRUE;

    for (int i = 0; k_known_prefixes[i] != NULL; i++) {
        if (strncmp(class_name, k_known_prefixes[i],
                    strlen(k_known_prefixes[i])) == 0)
            return TRUE;
    }

    return FALSE;
}

/* ---- sibling *.ui scan --------------------------------------------- */

/* Register a <template class="X"> -> TemplateEntry mapping. Takes ownership of
 * entry (freed on duplicate). */
static void register_template(const char *class_name, TemplateEntry *entry) {
    const TemplateEntry *existing = g_hash_table_lookup(s_templates, class_name);

    if (existing != NULL) {
        g_warning("component_registry: duplicate <template class=\"%s\">"
                  " in '%s' (first '%s' wins)",
                  class_name, entry->path, existing->path);
        entry_free(entry);
        return;
    }

    g_hash_table_insert(s_templates, g_strdup(class_name), entry);
}

/* Read one .ui file and index its top-level <template>, if any. Takes
 * ownership of ui_path and frees it on every exit path. */
static void index_ui_file(char *ui_path) {
    gchar *content = NULL;
    gsize length = 0;
    if (!g_file_get_contents(ui_path, &content, &length, NULL)) {
        g_warning("component_registry: cannot read '%s'", ui_path);
        g_free(ui_path);
        return;
    }

    Node *doc = parse_xml(content);
    g_free(content);
    if (doc == NULL) {
        g_free(ui_path);
        return;
    }

    Node *tpl = find_child(doc, "template");
    const char *class_name = tpl != NULL ? get_attr(tpl, "class") : NULL;
    if (class_name != NULL && class_name[0] != '\0'
        && !component_registry_is_builtin(class_name)) {
        TemplateEntry *entry = g_new(TemplateEntry, 1);
        entry->path = ui_path;
        entry->template_node = clone_node(tpl);
        register_template(class_name, entry);
    } else {
        g_free(ui_path);
    }

    free_node(doc);
}

static void do_scan(void) {
    if (s_scanned)
        return;
    s_scanned = TRUE;

    if (s_templates == NULL)
        s_templates = g_hash_table_new_full(g_str_hash, g_str_equal,
                                            g_free, entry_free);

    if (s_dir == NULL)
        return;

    GDir *dir = g_dir_open(s_dir, 0, NULL);
    if (dir == NULL) {
        g_warning("component_registry: cannot open '%s'", s_dir);
        return;
    }

    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (!g_str_has_suffix(name, ".ui"))
            continue;
        index_ui_file(g_build_filename(s_dir, name, NULL));
    }
    g_dir_close(dir);
}

/* ---- public API (matches component_registry.h) -------------------- */

void component_registry_cleanup(void) {
    if (s_templates != NULL) {
        g_hash_table_destroy(s_templates);
        s_templates = NULL;
    }
    g_free(s_dir);
    s_dir = NULL;
    s_scanned = FALSE;
}

void component_registry_init_scan(const char *input_path, const char *ui_dir) {
    g_print("component_registry: initializing component_registry\n");

    if (s_templates != NULL) {
        g_hash_table_destroy(s_templates);
        s_templates = NULL;
    }
    g_free(s_dir);
    s_scanned = FALSE;

    if (ui_dir != NULL) {
        s_dir = g_strdup(ui_dir);
    } else {
        s_dir = g_path_get_dirname(input_path);
    }

    // Register cleanup to run automatically when the process exits so
    // callers do not need to remember component_registry_cleanup().
    static gboolean atexit_registered = FALSE;
    if (!atexit_registered) {
        atexit(component_registry_cleanup);
        atexit_registered = TRUE;
    }

    do_scan();
}

#ifdef DEBUG
void component_registry_dump(void) {
    if (s_templates == NULL || g_hash_table_size(s_templates) == 0) {
        g_print("component_registry: (no template components indexed)\n");
        return;
    }

    g_print("component_registry: %u template component(s) indexed\n",
            g_hash_table_size(s_templates));

    GHashTableIter iter;
    gpointer key;
    gpointer value;
    g_hash_table_iter_init(&iter, s_templates);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        const TemplateEntry *entry = value;
        g_print("  %s -> %s\n", (const char *) key, entry->path);
    }
}
#endif

const char *component_registry_resolve_path(const char *class_name) {
    if (component_registry_is_builtin(class_name))
        return NULL;

    const TemplateEntry *entry = s_templates != NULL
                                     ? g_hash_table_lookup(s_templates, class_name)
                                     : NULL;
    if (entry == NULL) {
        g_warning("component_registry: unresolved custom class '%s' (skipped)",
                  class_name);
        return NULL;
    }
    return entry->path;
}

Node *component_registry_get_template(const char *class_name) {
    if (component_registry_is_builtin(class_name))
        return NULL;

    const TemplateEntry *entry = s_templates != NULL
                                     ? g_hash_table_lookup(s_templates, class_name)
                                     : NULL;
    if (entry == NULL) {
        g_warning("component_registry: unresolved custom class '%s' (skipped)",
                  class_name);
        return NULL;
    }
    return clone_node(entry->template_node);
}
