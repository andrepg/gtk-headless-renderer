# GTK headless renderer

A few lines of C to headless (_not yet_) render a GTK XML interface and generate
a PNG image at given width and height.

## Running

Parameters are positional, meaning you must run CLI it as follows:

```shell
## Directly
./gtk-embedded-preview /path/to/template.xml /path/to/output.png width height /path/to/search
```

```shell
## Or via Make
# clean
make clean
# build in debug
make build
# build in prod
make release
# run directly on Flatpak and org.gnome.Sdk
make run ARGS="{PARAMETERS}"
```

The 5th parameter (`/path/to/search`) is optional, defining where the CLI will search for custom classes and
extra files to expand inside the loaded template file.

# Architecture

There is a few underlying things happening while process the interface, some of them are:

- a normalization, ensuring template starts with interface
- a swap between custom classes and parent classes (`SomeCustomWidget <-> AdwDefaultWidget`)
- a custom object/widget expansion based on sibling files
- a serialization to ensure GObject/GTK compliance while rendering

```text
loader -> parser -> normalizer -> serializer -> renderer
```

## Requirements

This CLI relies on a few system libraries, generally available in development libraries. They are:

- `libxml2`
- `gtk-devel`
- `libadwaita-devel`

We recommend that you install `org.gnome.Sdk` from Flatpak and run the CLI inside it, or using the Make file.