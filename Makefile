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

# Build mode: debug is the default. Use `make release` for a -O3 -DNDEBUG build.
DEBUG ?= 1

# Build-mode stamp so toggling DEBUG forces a rebuild (make does not track
# CFLAGS changes by itself).
BUILD_MODE_FILE ?= $(BUILD_TREE)/.build-mode
ifeq ($(DEBUG),1)
BUILD_MODE := debug
else
BUILD_MODE := release
endif

TARGET = $(BUILD_TREE)/gtk_embedded_preview
SRCS   = main.c xml_loader.c xml_parser.c component_registry.c normalizer.c renderer.c
OBJS   = $(patsubst %.c,$(BUILD_TREE)/%.o,$(SRCS))
DEPS   = $(OBJS:.o=.d)
PKGS   = libadwaita-1 libxml-2.0

# Extra program arguments, e.g.: make run ARGS="in.ui out.png 800 600 src/"
ARGS   ?=

CFLAGS  += -std=gnu17 -Wall -Wextra $(shell pkg-config --cflags $(PKGS))
ifeq ($(DEBUG),1)
  CFLAGS += -g -O0 -DDEBUG
else
  CFLAGS += -O3 -DNDEBUG
endif
LDLIBS  += $(shell pkg-config --libs $(PKGS))

FBP := flatpak build --bind-mount=/src="$(CURDIR)"

.PHONY: build run release clean

build:
	@test -d "$(BUILD_DIR)" || flatpak build-init "$(BUILD_DIR)" "$(APP_ID)" "$(SDK)" "$(SDK)"
	$(FBP) "$(BUILD_DIR)" make -C /src -f /src/Makefile clean-if-mode-changed DEBUG=$(DEBUG)
	$(FBP) "$(BUILD_DIR)" make -C /src -f /src/Makefile all DEBUG=$(DEBUG)

release:
	$(MAKE) build DEBUG=0

# Wipe the build tree inside the sandbox when the requested build mode differs
# from the last one (or when an object tree exists with no recorded mode).
# Cleaning outside the sandbox would race with its bind-mounted writes.
.PHONY: clean-if-mode-changed
clean-if-mode-changed:
	@if { [ -f "$(BUILD_MODE_FILE)" ] && [ "$$(cat "$(BUILD_MODE_FILE)")" != "$(BUILD_MODE)" ]; } \
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
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD_TREE):
	mkdir -p $@

# The binary links libxml2.so.16 from the SDK, which the host does not have
# (host only ships libxml2.so.2). Run inside the SDK runtime instead; the
# Wayland socket also lets adw_init() succeed against the host compositor.
run: build
	flatpak run --share=ipc --socket=wayland --socket=session-bus \
		--filesystem="$(CURDIR)" \
		--filesystem="home" \
		--command="$(CURDIR)/$(TARGET)" \
		"$(SDK)" $(ARGS)

-include $(DEPS)

clean:
	rm -rf "$(BUILD_DIR)" "$(BUILD_TREE)"