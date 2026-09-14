# 📄 ISSUE-DOC — GTK XML Preprocessor + Renderer (C)

Guia direto para implementação. Sem ambiguidade. Siga na ordem.

---

# 📌 STATUS GERAL

| M            | Módulo                          | Status                                  |
|--------------|---------------------------------|-----------------------------------------|
| M1           | CLI + Bootstrap                 | ✅ pronto (`main.c` + `utils.h`)          |
| M2           | XML Loader                      | ✅ pronto (`xml_loader.c`)                |
| M3           | XML Parser (DOM)                | ✅ pronto (`xml_parser.c`)                |
| M4           | Component Registry              | ✅ pronto (`component_registry.c`)        |
| M5           | Transform (custom expand)       | ✅ pronto (`normalizer.c`)                |
| M6           | Normalização Final              | ✅ pronto (`normalizer.c`)                |
| M7           | Serialização XML                | ✅ pronto (`serializer.c`)                |
| M8           | GTK Loader                      | ✅ embutido em `renderer.c`               |
| M9           | Renderer (Snapshot)             | ✅ pronto (`renderer.c`)                  |
| M10          | Export (PNG)                    | ✅ pronto (`renderer.c`)                  |
| —            | Orquestração (`renderer_render_interface`) | ✅ conectado em `main.c`         |
| —            | Snapshot single-frame (sem tick) | ✅ pronto em `renderer.c`                |
| —            | True headless (zero flash)      | ⏳ próximo passo                          |
| —            | Testes                          | ❌ não iniciado                           |
| —            | Amostras `.ui`                  | ⏳ só `samples/hello.ui`                  |

**Build atual**: ✅ compila no flatpak SDK (`make build`) com `-Wall -Wextra` limpo.

**Pipe principal**: ✅ conectado — `main.c` roda todo o pipeline:

```text
CLI → adw_init → load_file → parse_xml → normalize_templates
   → renderer_render_interface (serialize → GtkBuilder → snapshot → PNG)
```

---

# 🎯 Objetivo

Construir um utilitário em C que:

```text
Input: XML (com templates/custom widgets)
Output: Snapshot renderizado (PNG ou GSK node)

Pipeline:
XML → Expand Templates (inline) → GTK Builder → Render → Export
```

---

# 🧭 Visão Geral dos Módulos

Estrutura **real** do repo (aplicação na raiz, bibliotecas em `src/`):

```text
├── main.c                  ✅  entry point + orquestração (M1)
├── src/utils.h             ✅  Config + parse_config + bootstrap display (M1)
├── src/xml_loader.c/.h     ✅  load_file (M2)
├── src/xml_parser.c/.h     ✅  DOM Node/Attr tree (M3)
├── src/component_registry.c/.h ✅  registry de templates (M4)
├── src/normalizer.c/.h     ✅  template→object + custom expand (M5) + <interface>/<requires> (M6)
├── src/serializer.c/.h     ✅  DOM → XML roundtrip (M7, display-free)
├── src/renderer.c/.h       ✅  GtkBuilder load (M8) + snapshot (M9) + export PNG (M10)
├── src/error_codes.h       ✅  códigos de erro do pipeline de render
├── samples/hello.ui        ✅  amostra manual
├── Makefile                ✅  build/run no flatpak SDK
└── ROADMAP.md              ✅  este documento
```

Desvios do layout original:

* **Não existe** `xml_transform.c` → o papel de M5/M6 é cumprido por
  `normalizer.c`, que também adiciona a passada de expansão de custom widgets.
* **Não existe** `gtk_loader.c` → a carga via `GtkBuilder` (M8) é feita
  dentro de `renderer.c` (é onde o builder é consumido).
* **Não existem** `cli.c`/`utils.c` → parse e bootstrap são header-only.

---

# 🧩 Decisões registradas

* **Backend atual**: `GDK_BACKEND=wayland` + `adw_init()`. A janela é mapeada
  por **um único quadro** para obter um paint real e o PNG é capturado no
  primeiro `GdkFrameClock::after-paint` — sem ticks, sem watchdog.
* **Por que o paint real é obrigatório**: o GTK só rasteriza widget **mapeado**
  (`gtk_widget_snapshot()`/`snapshot_child()` retornam cedo/assertam se não
  mapeado) e `GtkWidgetPaintable` snapshota o `priv->render_node` cacheado
  pelo último paint. Ou seja: para snapshot de conteúdo real, a árvore precisa
  de um ciclo de paint; o que dá para evitar é **ficar** mapeado por vários
  frames.
* **Consequência**: na sessão do usuário a janela fica visível ~1 quadro
  (~16 ms). Zero-flash garantido exige backend headless de verdade (ver
  PRÓXIMOS PASSOS).
* **DOM próprio**: libxml2 faz só o parsing; o resultado é copiado para uma
  árvore `Node/Attr` independente (header `xml_parser.h`). O transformer (M5+)
  opera **sem** dependência de libxml2.
* **Registry é independente**: a classe é a **única** chave de junção; nome de
  arquivo nunca é derivado do nome da classe.
* **API do registry**:
  `component_registry_init_scan()` / `component_registry_is_builtin()` /
  `component_registry_resolve_path()` / `component_registry_get_template()` /
  `component_registry_cleanup()`.
* **Captura (M9/M10)**: `GtkWidgetPaintable` → `gdk_paintable_snapshot()` →
  `gtk_snapshot_to_node()` → `gsk_renderer_render_texture()` (renderer nativo)
  → `gdk_texture_save_to_png()`. `done` só é setado se o PNG foi salvo.

---

# 🧭 MILESTONES

---

## 🚩 M1 — CLI + Bootstrap ✅

### Objetivo

Receber parâmetros e inicializar GTK headless.

### Entrada esperada

```bash
app input.xml output.png 800 600 [src_dir]
```

### Situação atual

* `utils.h` expõe `struct Config`, constantes `ARG_*_IDX`,
  `parse_config(arguments)` (width/height opcionais, fallback 800x600) e
  `initialize_extra_libraries()` (`GDK_BACKEND=wayland`, `adw_init()`,
  scan do registry).
* `main.c` valida argc >= 3, inicializa as libs, carrega o XML, parseia,
  normaliza e chama `renderer_render_interface()`.
* `src/error_codes.h` define os códigos de erro usados pelo renderer.

---

## 🚩 M2 — XML Loader ✅

#### `xml_loader.c`

```c
char* load_file(const char* path);
```

Regras (cumpridas):

* ler completo
* null-terminated (`g_strndup` sobre os bytes lidos)
* erro → aborta com `EXIT_FAILURE`

---

## 🚩 M3 — XML Parser (DOM) ✅

Representação manipulável do XML:

* Parsing via **libxml2** (`xmlReadMemory`), copiado para árvore própria
  (`Node`/`Attr` com `parent/children/next`), sem dependência de libxml2 após
  `parse_xml()`.
* Implementado: `parse_xml`, `clone_node`, `free_node`, `find_child`,
  `get_attr`, `set_attr`.
* `content` só é preenchido em elementos folha (texto útil); whitespace entre
  elementos é descartado.

---

## 🚩 M4 — Component Registry ✅

#### `component_registry.c`

```c
int component_registry_init_scan(const char *dir);
int component_registry_is_builtin(const char *class_name);
const char *component_registry_resolve_path(const char *class_name);
Node *component_registry_get_template(const char *class_name);
void component_registry_cleanup(void);
```

Regras:

* Custom = classe que **não** é builtin do toolkit (prefixos
  `Gtk/Adw/Gdk/Gio/Gsk/Graphene/Pango/cairo/GObject/GType`).
* Scan **lazy** dos `.ui` irmãos do diretório informado; indexa
  `<template class="X">` de topo (primeiro arquivo vence; duplicatas →
  `g_warning`).
* Classe não resolvida → `g_warning()` e o normalizador a ignora; nunca fatal.
* `cleanup()` libera cache/estado.

---

## 🚩 M5+M6 — Transform + Normalização Final ✅ (`normalizer.c`)

### Objetivo

Expandir TODOS os custom widgets e garantir XML válido para GtkBuilder.

### Passadas implementadas em `normalize_templates(dom)`

1. **`<template>` → `<object>`**: copia `parent` para `class` e retaga o
   elemento (herança de widget refletida no build).
2. **Expandir custom**: `<object class="Custom">` é substituído pelo template
   registrado (classe `parent` do `<template>`) com a sub-árvore clonada,
   mesclando `<property>` do ponto de uso sobre o template.
3. **Recursão**: expande aninhamentos até não restar custom (protegido contra
   loop de classes).
4. **Forma final (M6)**: raiz `<interface>` (envolve se não for), com
   `<requires lib="gtk" version="4.0"/>` garantido.

### Pendência

* **IDs únicos** da checklist M6 ainda não são aplicados pelo normalizador.

---

## 🚩 M7 — Serialização XML ✅ (`serializer.c`)

```c
char *serialize_xml(const Node *root);
```

* Round-trip da árvore `Node` de volta para texto (entrada do `GtkBuilder`).
* Display-free: nenhuma dependência de GTK — testável headless.
* Raiz `<interface>` é emitida no lugar; qualquer outra raiz é envolvida em
  `<interface>`. Um DOC normalizado já leva seu próprio `<requires>`; esta
  função nunca adiciona um.

---

## 🚩 M8 — GTK Loader ✅ (embutido em `renderer.c`)

```c
GtkBuilder *builder = gtk_builder_new();
gtk_builder_add_from_string(builder, xml, -1, &error);
GtkWidget *root = resolve_render_root(builder);   // janela preferida; senão wrapper
```

* O `resolve_render_root()` devolve a primeira toplevel sem pai (prioriza
  `GtkWindow`; `AdwDialog` é desembrulhado no child); o resto ganha uma
  `GtkWindow` não decorada como wrapper.

---

## 🚩 M9+M10 — Renderer + Export ✅ (`renderer.c`)

### Snapshot single-frame

```text
set_size_request(w, h) → realize → gtk_widget_get_frame_clock
  → connect GdkFrameClock::after-paint  → set_visible(TRUE)
  → primeiro after-paint → capture_and_quit()
```

`capture_and_save()` (dentro do after-paint, 1º quadro):

```c
GdkPaintable *p = gtk_widget_paintable_new(widget);
gdk_paintable_snapshot(p, snapshot, w, h);          // node real do último paint
GskRenderNode *node = gtk_snapshot_to_node(snapshot);
GdkTexture *tex = gsk_renderer_render_texture(
    gtk_native_get_renderer(gtk_widget_get_native(widget)), node, &bounds);
gdk_texture_save_to_png(tex, output_path);          // M10
```

* O loop sai no primeiro `after-paint`: nada de tick counting nem watchdog.
* A janela é retirada imediatamente após a captura.

---

# 🚫 FALTAS (resumo aberto)

1. **True headless** — hoje a janela mapeia ~1 quadro na sessão do usuário
   (`make run` usa `--socket=wayland`). Para zero flash: rodar contra backend
   com vista nenhuma — o SDK tem `broadwayd` (não tem `weston`).
2. **Testes** — nenhum (harness + 4 cenários previstos abaixo).
3. **IDs únicos** na normalização (M6).
4. **Fidelidade/ruído de render** — ✅ resolvido: `GSK_RENDERER=cairo` é
   forçado em `utils.h`, eliminando os avisos `libEGL/MESA` no fallback do
   renderer GL (o SDK só tem Mesa Vulkan/zink, sem DRI GL).
5. **Amostras `.ui` adicionais** (só existe `samples/hello.ui`).
6. **Export formatos** — hoje só PNG via `gdk_texture_save_to_png` (há
   `save_to_png_bytes`/`save_to_tiff*` disponíveis).

---

# 🧭 PRÓXIMOS PASSOS (ordem recomendada)

```text
1. true headless (broadway/broadwayd ou compositor privado) → zero flash
2. testes: harness + 4 cenários (GTK puro, 1 nível, nested, loop)
3. IDs únicos no normalizador (M6)
4. avaliar GSK_RENDERER=cairo (limpa warnings EGL/MESA) — ✅ feito
5. mais amostras .ui (ex.: task.ui, task-form.ui) p/ dev manual
6. decidir export extra (PNG bytes / TIFF) se fizer sentido
```

---

# ⚠️ REGRAS CRÍTICAS

---

## ❌ Nunca deixar passar

```xml
<object class="CustomWidget">
</object>
```

---

## ❌ Nunca depender de GTK Template

Sempre expandir antes (via `normalize_templates`).

---

## ⚠️ Sempre garantir

* IDs únicos
* classes válidas
* estrutura `<interface>`

---

# 🧪 Estratégia de Testes

---

## Teste 1 — XML puro GTK

* deve renderizar direto

## Teste 2 — 1 nível de custom

* deve expandir corretamente

## Teste 3 — nested custom

* recursão válida

## Teste 4 — loop

* erro detectado

---

# 🧠 Extensões futuras (não implementar agora)

* True headless (backend sem janela visível)
* GObject Introspection
* suporte real a signals
* CSS Adwaita completo
* preview interativo

---

# 🧩 Ordem real de implementação (atualizada)

```text
1.  CLI + loader                      ✅
2.  parser DOM                        ✅
3.  component registry                ✅
4.  normalização + expand (M5/M6)     ✅ normalizer.c
5.  serialização (roundtrip)          ✅ M7
6.  gtk builder + snapshot + export   ✅ M8/M9/M10 (renderer.c)
7.  orquestração em main.c            ✅
8.  snapshot single-frame (sem tick)  ✅
9.  true headless                     ⏳
10. testes                            ⏳
```

---

# 🧠 Insight final

Você está construindo:

```text
GTK UI Compiler (XML → Runtime Renderable)
```

Não trate como parser.
Trate como **pipeline de compilação**.