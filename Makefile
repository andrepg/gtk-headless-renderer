#
# Copyright (C) 2026 apg
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#

SDK        ?= org.gnome.Sdk//50
BUILD_DIR  ?= .flatpak-build
BUILD_TREE ?= _build
APP_ID     ?= io.github.andrepg.GtkEmbeddedPreview
CC         ?= cc

# Build mode: debug is the default.
# Use `make release-local` or `make release` for a release build.
DEBUG ?= 1

# Build-mode stamp so toggling DEBUG forces a rebuild.
BUILD_MODE_FILE ?= $(BUILD_TREE)/.build-mode

ifeq ($(DEBUG),1)
BUILD_MODE := debug
else
BUILD_MODE := release
endif

TARGET = $(BUILD_TREE)/gtk_embedded_preview
OBJS   = $(patsubst %.c,$(BUILD_TREE)/%.o,$(SRCS))
DEPS   = $(OBJS:.o=.d)
PKGS   = libadwaita-1 libxml-2.0

SRCS   = main.c \
         src/xml_loader.c \
         src/xml_parser.c \
         src/component_registry.c \
         src/normalizer.c \
         src/serializer.c \
         src/renderer.c

# Extra program arguments, e.g.:
# make run ARGS="in.ui out.png 800 600 src/"
ARGS   ?=

CFLAGS  += -std=gnu17 -Isrc -Wall -Wextra $(shell pkg-config --cflags $(PKGS))

ifeq ($(DEBUG),1)
CFLAGS += -g -O0 -DDEBUG
else
CFLAGS += -O3 -DNDEBUG
endif

LDLIBS += $(shell pkg-config --libs $(PKGS))

.PHONY: \
	build \
	build-local \
	build-flatpak \
	release \
	release-local \
	run \
	all \
	clean \
	clean-if-mode-changed

# ============================================================
# LOCAL BUILD
# ============================================================

# Backwards-compatible default build target.
build: build-local

build-local: all

release: release-local

release-local:
	$(MAKE) build-local DEBUG=0

# ============================================================
# FLATPAK SANDBOX BUILD
# ============================================================
#
# This target MUST be executed from inside a Flatpak sandbox.
#
# Example:
#
#   flatpak run --command=make org.gnome.Sdk//50 \
#       -C /path/to/project build-flatpak
#
# The build artifacts are written directly to BUILD_TREE in the
# current project directory, e.g.:
#
#   ./_build/gtk_embedded_preview
#
# No `flatpak build` or `flatpak build-init` is invoked here.
#

build-flatpak:
	$(MAKE) all DEBUG=$(DEBUG)

# ============================================================
# INTERNAL BUILD GRAPH
# ============================================================

clean-if-mode-changed:
	@if { [ -f "$(BUILD_MODE_FILE)" ] && \
	      [ "$$(cat "$(BUILD_MODE_FILE)")" != "$(BUILD_MODE)" ]; } \
	    || { [ ! -f "$(BUILD_MODE_FILE)" ] && [ -f "$(TARGET)" ]; }; then \
		echo "Setting up a $(BUILD_MODE) build tree"; \
		rm -rf "$(BUILD_TREE)"; \
	fi

all: $(TARGET) | $(BUILD_MODE_FILE)

$(BUILD_MODE_FILE): | $(BUILD_TREE)
	@printf '%s\n' "$(BUILD_MODE)" > "$@"

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILD_TREE)/%.o: %.c | $(BUILD_TREE)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD_TREE):
	mkdir -p $@

# ============================================================
# RUN
# ============================================================
#
# Runtime execution still happens inside the GNOME SDK/runtime,
# because the resulting binary is linked against the SDK libraries.
#

run: build-flatpak
	flatpak run \
		--share=ipc \
		--socket=wayland \
		--socket=session-bus \
		--filesystem="$(CURDIR)" \
		--filesystem="home" \
		--command="$(CURDIR)/$(TARGET)" \
		"$(SDK)" \
		$(ARGS)

-include $(DEPS)

clean:
	rm -rf "$(BUILD_DIR)" "$(BUILD_TREE)"