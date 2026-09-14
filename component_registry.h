/*
 * component_registry.h
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Phase 2 (M4): display-free custom-component registry for gtk-embedded-preview.
 *
 * After Phase 2's xml_parser.h builds the full DOM of a .ui file,
 * this registry lets phase 3 resolve every custom component class to the
 * sibling .ui file that declares its <template class="X">, so the display-free
 * normalizer can later stack components without re-scanning every file.
 *
 * The registry is headless on purpose: it only feeds the m3 node tree and a
 * per-class template path. It never touches GTK and never opens a display.
 *
 * Resolution rules (user-confirmed on phase-2 review 2026-07-13):
 *   - A "custom" component is any class that is not a toolkit builtin
 *     (Gtk-, Adw-, Gdk-, Gio-, Gsk-, Graphene-, Pango- or cairo- prefixed), regardless of how
 *     the owning .ui file spells its name (the class attribute is the only
 *     reliable join key; filenames are never derived from it).
 *   - A class is "resolved" when exactly one sibling .ui file declares a
 *     top-level <template class="X"> for it. The first declaring file wins.
 *   - Unresolved classes are reported with g_warning() and skipped; they are
 *     never fatal.
 *
 * Threading: this registry is single-threaded. It keeps a per-class cache and
 * inspects sibling .ui files once, explicitly, from component_registry_init_scan().
 */

#ifndef GTK_EMBEDDED_PREVIEW_COMPONENT_REGISTRY_H
#define GTK_EMBEDDED_PREVIEW_COMPONENT_REGISTRY_H

#include <glib.h>

#include "xml_parser.h"

/* Scan every sibling *.ui file and index every top-level <template class="X">
 * it declares, mapping class name -> that file.
 *
 * input_path: path to the main .ui file being processed (always required).
 * ui_dir:     explicit scan directory, or NULL to auto-derive from
 *             dirname(input_path).
 *
 * Requires a prior call to component_registry_cleanup() if re-initializing. */
void component_registry_init_scan(const char *input_path, const char *ui_dir);

/* Path of the single sibling .ui file that declares <template class="X">
 * for class_name, or NULL. If the class is duplicated across files, the first
 * template found during the init scan wins (g_warning on later duplicates).
 * Requires a prior call to component_registry_init_scan(). */
const char *component_registry_resolve_path(const char *class_name);

/* Parsed, owned copy of the root <template> node for class_name (already
 * display-free). Requires a prior call to component_registry_init_scan().
 * Returns NULL when the class is unknown. The caller owns the returned tree. */
Node *component_registry_get_template(const char *class_name);

/* Index map: custom class name -> owning .ui file. Freed with
 * component_registry_cleanup(). */
void component_registry_cleanup(void);

/**
 * Checks if a given component is native to GTK/Adwaita toolkit or if
 * it is a custom component written by the developer
 *
 * @param class_name class to search in hash table
 * @return true if native, false if custom
 */
gboolean component_registry_is_builtin(const char *class_name);

#ifdef DEBUG
/* Print every indexed custom component (class name -> owning .ui path) to
 * stdout. Requires a prior call to component_registry_init_scan(). */
void component_registry_dump(void);
#endif

#endif /* GTK_EMBEDDED_PREVIEW_COMPONENT_REGISTRY_H */
