---
title: Tree-sitter for F#
layout: splash
---

An F# grammar for [tree-sitter](https://tree-sitter.github.io/), built for syntax highlighting in Helix, Zed and Neovim.

![Showcase of the grammar in action](/tree-sitter-fsharp/showcase.png)

- Validated against 23 popular F# projects and the F# compiler itself, about 840 000 lines. Over 98% of the files parse without an error.
- A construct the parser does not know loses its colours on that line, not for the rest of the file.
- `///` comments attach to the declaration below, so expand-selection grows from value to binding to documented binding to module.
- Distinct colours for parameters, operators, constructors, function calls and module paths; indentation that follows the offside rule; function and type textobjects; rainbow brackets.
- A second grammar parses signature files (`.fsi`).

## Editors

| Editor | Queries |
|---|---|
| [Helix](users/helix.md) | highlights, injections, locals, textobjects, indents, rainbows |
| [Zed](users/zed.md) | highlights, injections, textobjects, indents, outline, brackets, overrides |
| [Neovim](users/neovim.md) | highlights, injections, indents, folds, textobjects |

## Contributing

Start with [Getting started](contributors/getting-started.md). The source is on [GitHub](https://github.com/MangelMaxime/tree-sitter-fsharp). Licence: Apache 2.0.
