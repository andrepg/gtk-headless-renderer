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
 * @file serializer.h
 * @brief ROADMAP M7 — round-trip the normalized Node tree back to XML.
 *
 * Display-free counterpart of xml_parser.h: turns the canonical DOM produced
 * by the normalizer (M6) back into a GtkBuilder-loadable XML string. No GTK,
 * no widget toolkit — this module can be unit-tested headless.
 */

#ifndef GTK_EMBEDDED_PREVIEW_SERIALIZER_H
#define GTK_EMBEDDED_PREVIEW_SERIALIZER_H

#include "xml_parser.h"

/**
 * Serialize a Node tree back into an XML document loadable by GtkBuilder
 * (ROADMAP M7).
 *
 * An <interface> root is emitted in place; any other root (e.g. a single
 * <object> produced by normalizing a <template>) is wrapped in an
 * <interface> element. A normalized tree already carries its own <requires>
 * element (guaranteed by normalize_templates()); this function never adds
 * one.
 *
 * @param  root Root node of the tree to serialize.
 * @return Newly allocated XML string, or NULL on error. Free with g_free().
 */
char *serialize_xml(const Node *root);

#endif /* GTK_EMBEDDED_PREVIEW_SERIALIZER_H */