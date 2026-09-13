# GTK headless renderer

A few lines of C to render a GTK template and its children, without events and signals. 
This tool aims to render a simple interface based on a XML template and return a generated image.

# Architecture

The main arch is composed of: **orchestrator**, **parser/loader**, **transformer** and **generator**.

```text
(orchestrator) -> load file -> inline custom templates -> render
```