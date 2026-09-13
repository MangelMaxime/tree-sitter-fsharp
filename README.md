# Tree-sitter for F#

An F# grammar for [tree-sitter](https://tree-sitter.github.io/), built for syntax highlighting
in Helix, Zed and Neovim. A second grammar parses signature files (`.fsi`).

![Showcase of the grammar in action](./assets/showcase.png)

- Validated against 23 popular F# projects and the F# compiler itself, about 840 000 lines.
  Over 98% of the files parse without an error.
- A construct the parser does not know loses its colours on that line, not for the rest of
  the file.
- `///` comments attach to the declaration below, so expand-selection grows from value to
  binding to documented binding to module.
- Distinct colours for parameters, operators, constructors, function calls and module paths;
  indentation that follows the offside rule; function and type textobjects; rainbow brackets.

## Documentation

Everything is on the [documentation site](https://mangelmaxime.github.io/tree-sitter-fsharp/):

| Editor | Install |
|---|---|
| Helix | [users/helix](https://mangelmaxime.github.io/tree-sitter-fsharp/users/helix/) |
| Zed | [users/zed](https://mangelmaxime.github.io/tree-sitter-fsharp/users/zed/) |
| Neovim | [users/neovim](https://mangelmaxime.github.io/tree-sitter-fsharp/users/neovim/) |

Contributors start at
[contributors/getting-started](https://mangelmaxime.github.io/tree-sitter-fsharp/contributors/getting-started/).
In short: `npm ci`, then `./build.sh test-all` (`build.bat` on Windows) runs every local gate,
and `./build.sh --help` lists the commands. `LIMITATIONS.md` records the accepted trade-offs and
the known gaps.

## Licence

Apache 2.0
