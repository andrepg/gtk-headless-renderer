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
 * @file utils.h
 * @brief Shared CLI parameter helpers for the preview pipeline.
 *
 * Defines the Config struct and the parse_config() helper used by the
 * orchestrator to interpret positional command-line arguments (ROADMAP M1).
 */

#ifndef GTK_EMBEDDED_PREVIEW_UTILS_H
#define GTK_EMBEDDED_PREVIEW_UTILS_H

#include <stdlib.h>

static const int ARG_INPUT_PATH_IDX = 1;
static const int ARG_OUTPUT_PATH_IDX = 2;
static const int ARG_WIDTH_IDX = 3;
static const int ARG_HEIGHT_IDX = 4;
static const int ARG_SRC_DIR_IDX = 5;

/**
 * Command-line configuration parsed from the argument vector.
 */
struct Config {
    char *input_path;  /**< Input XML file given to parse/process. */
    char *output_path; /**< Destination PNG path. */
    int width;         /**< Canvas width in pixels for the generated image. */
    int height;        /**< Canvas height in pixels for the generated image. */
    char *src_dir;     /**< Folder to scan for sibling .ui interfaces (optional; NULL = use input's directory). */
};

/**
 * Parse CLI arguments and transform them into a well-known struct to allow
 * C's parameter handling.
 *
 * Width and height are optional: a missing or zero value falls back to 800x600.
 *
 * @param argument_count number of elements in arguments, never 0.
 * @param arguments CLI arguments to parse.
 * @return Config struct with defined values.
 */
static struct Config parse_config(const int argument_count, char *arguments[]) {
    struct Config config;

    const int width = atoi(arguments[ARG_WIDTH_IDX]);
    const int height = atoi(arguments[ARG_HEIGHT_IDX]);

    config.input_path = arguments[ARG_INPUT_PATH_IDX];
    config.output_path = arguments[ARG_OUTPUT_PATH_IDX];

    config.src_dir = argument_count > ARG_SRC_DIR_IDX ? arguments[ARG_SRC_DIR_IDX] : NULL;

    config.width = width ? width : 800;
    config.height = height ? height : 600;

    return config;
}

#endif //GTK_EMBEDDED_PREVIEW_UTILS_H