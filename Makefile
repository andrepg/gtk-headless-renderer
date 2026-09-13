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
APP_ID     ?= com.example.GtkEmbeddedPreview
CC         ?= cc

TARGET = $(BUILD_TREE)/gtk_embedded_preview
SRCS   = main.c xml_loader.c
OBJS   = $(patsubst %.c,$(BUILD_TREE)/%.o,$(SRCS))
DEPS   = $(OBJS:.o=.d)
PKGS   = libadwaita-1

CFLAGS  += -std=gnu17 -O3 -DNDEBUG -Wall -Wextra $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS))

FBP := flatpak build --bind-mount=/src="$(CURDIR)"

.PHONY: build clean

build:
	@test -d "$(BUILD_DIR)" || flatpak build-init "$(BUILD_DIR)" "$(APP_ID)" "$(SDK)" "$(SDK)"
	$(FBP) "$(BUILD_DIR)" make -C /src -f /src/Makefile all

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILD_TREE)/%.o: %.c | $(BUILD_TREE)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD_TREE):
	mkdir -p $@

-include $(DEPS)

clean:
	rm -rf "$(BUILD_DIR)" "$(BUILD_TREE)"