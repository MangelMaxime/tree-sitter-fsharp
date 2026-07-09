# Tree Sitter for F#

An F# grammar for [tree-sitter](https://tree-sitter.github.io/).

![Showcase of the grammar in action](./assets/showcase.png)

## Why this grammar

- **Built on real code** - validated against 23 popular F# projects *and the
  compiler itself* (~840 000 lines): over 95 % of files parse without a
  single error.
- **Failures stay local** - if a construct trips the parser, that line loses
  its colors. Not the rest of the file.
- **Docs belong to their code** - `///` comments attach to the declaration
  below, so expand-selection grows value → binding → docs + binding → module.
- **Rich editor experience** - distinct colors for parameters, operators,
  constructors and function calls; auto-indentation that follows the offside
  rule; function/type textobjects; rainbow brackets.

See everything at once in [`examples/layout.fsx`](examples/layout.fsx).

## Editor support

| Editor                            | Status          | Queries                                                                    |
| --------------------------------- | --------------- | -------------------------------------------------------------------------- |
| [Helix](#helix)                   | ✅ Supported    | Full set (highlights, injections, locals, textobjects, indents, rainbows)  |
| [Zed](#zed)                       | ✅ Supported    | Dedicated queries in `queries/zed/` |
| [Neovim](#neovim-experimental)    | 🧪 Experimental | Reuses the Helix queries (capture names map only roughly)                   |

PRs for editor-specific queries are welcome.

## Zed

Zed requires us to install a dev extension.

### Installation

**Requirements**

Rust with the `wasm32-wasip2` target (Zed uses it to compile the extension's
LSP glue):

```bash
rustup target add wasm32-wasip2
# or, on Arch Linux with the packaged Rust toolchain:
sudo pacman -S rust-wasm
```

```bash
git clone https://github.com/MangelMaxime/tree-sitter-fsharp
cd tree-sitter-fsharp
./dev-zed.sh
```

Then in Zed:

1. Command palette → `zed: install dev extension` → select the `zed/` directory.
2. After changing the grammar or queries: rerun `./dev-zed.sh`.
3. Hit **Rebuild** on the extension in Zed's Extensions panel.

### Good to know

- The dev extension reuses the id `fsharp`, so it **overrides** the marketplace F# extension while installed.
- If you want to use LSP features, you need to have `fsautocomplete` installed and on PATH.

    ```bash
    dotnet tool install -g fsautocomplete
    ```

   The local extension honors your `lsp.fsautocomplete` Zed settings (`binary`, `initialization_options`).
- `///` doc-comment (XML) coloring needs [Zed's XML](https://zed.dev/extensions/xml) extension;
  `(** … *)` markdown docs work out of the box.
- Highlighting `TODO:`/`FIXME:` markers inside comments needs [Zed's comment](https://zed.dev/extensions/comment) extension.
- `debug: open syntax tree view` shows the live parse tree useful for debugging.

### Uninstall

1. Command palette → `zed: extensions`
2. Click `Uninstall` for `F# (local dev)`

## Helix

### Installation

In `~/.config/helix/languages.toml`:

```toml
[[grammar]]
name = "fsharp"
source = { git = "https://github.com/MangelMaxime/tree-sitter-fsharp", rev = "main" }
```

Then:

```bash
hx --grammar fetch    # clone the grammar
hx --grammar build    # compile it
```

Install the queries (Helix doesn't auto-install them). One-liner:

```bash
curl -fsSL https://raw.githubusercontent.com/MangelMaxime/tree-sitter-fsharp/main/scripts/install-queries.sh | bash
```

Or pin to a branch / commit (make sure to use the same rev as in `languages.toml`):

```bash
curl -fsSL https://raw.githubusercontent.com/MangelMaxime/tree-sitter-fsharp/main/scripts/install-queries.sh | bash -s -- some-branch
```

The script above copies the queries into `~/.config/helix/runtime/queries/fsharp/` or `$HELIX_RUNTIME/queries/fsharp/` if you have a custom runtime directory.

### Uninstall

The installation touches two places: the compiled grammar (managed by
Helix) and the query files (copied by the script above). Both live under
Helix's runtime directory - `~/.config/helix/runtime` by default, or
`$HELIX_RUNTIME` if you set a custom one. Remove both:

```bash
RUNTIME="${HELIX_RUNTIME:-$HOME/.config/helix/runtime}"

# 1. Remove the queries installed by the script
rm -rf "$RUNTIME/queries/fsharp"

# 2. Remove the compiled grammar + its cloned source
rm -f  "$RUNTIME/grammars/fsharp.so"
rm -rf "$RUNTIME/grammars/sources/fsharp"
```

Then drop the `[[grammar]]` entry - and the `[[language]]` ones, if you added the recommended configuration below - from `~/.config/helix/languages.toml`.

After that, Helix falls back to its built-in F# grammar (if any) on the next launch.

### Editor configuration

Here is my recommended `languages.toml` config for F# support in Helix.

```toml
[language-server.fsharp-ls]
args = [
  "--adaptive-lsp-server-enabled",
  "--project-graph-enabled",
  "--use-fcs-transparent-compiler"
]
environment.FCS_ParallelReferenceResolution = "true"
environment.DOTNET_GCServer = "1"
environment.DOTNET_GCHeapCount = "c"

[language-server.fsharp-ls.config]
AutomaticWorkspaceInit = true
FSharp.unnecessaryParenthesesAnalyzer = false
FSharp.ExternalAutocomplete = false
FSharp.fsac.cachedTypeCheckCount = 400
FSharp.addPrivateAccessModifier = true
FSharp.UnusedOpensAnalyzer = true
FSharp.UnusedDeclarationsAnalyzer = true
FSharp.InterfaceStubGeneration = true
FSharp.AbstractClassStubGeneration = true
FSharp.UnionCaseStubGeneration = true
FSharp.RecordStubGeneration = true
FSharp.TooltipShowDocumentationLink = false
# FSharp.generateBinlog = true

[[language]]
name = "fsharp"
auto-format = false
comment-tokens = ["//", "///"] # Will be the default in next Helix release

[language.auto-pairs]
# Remove auto pairs for single quotes, as they are often used for type annotations in F#
'(' = ')'
'[' = ']'
'{' = '}'
'"' = '"'
# '[<' = ']>' # Helix doesn't support multi-character pairs yet, but it would be nice to have this for F#'s attributes

[[grammar]]
name = "fsharp"
source = { git = "https://github.com/MangelMaxime/tree-sitter-fsharp", rev = "main" }
```

If you also want rainbow brackets (the grammar ships `rainbows.scm`), add this to your `config.toml`:

```toml
[editor]
rainbow-brackets = true
```

> [!NOTE]
> At the time of writing, you need to build Helix from source to get rainbow brackets support

## Neovim (experimental)

> [!NOTE]
> **Neovim support is experimental.** There are no Neovim-specific queries yet:
> the shared `.scm` files target Helix's capture names, which map onto Neovim's
> highlight groups only roughly - highlighting is "ok" today, far from perfect.
>
> **This is where help is most valuable.** PRs adding Neovim-specific queries
> (capture mappings, `injections`, `locals`, `folds`, indentation) are very
> welcome - the grammar is shared, only the queries need editor-specific love.
>
> Historically, the [Ionide grammar](https://github.com/ionide/tree-sitter-fsharp) targets Neovim,
> but it supports fewer coloration features than this one and is more brittle.

### Install

> [!NOTE]
> [`nvim-treesitter`](https://github.com/nvim-treesitter/nvim-treesitter) was **archived on 2026-04-03**
>
> For this reason, the instructions below are using Neovim built-in tree-sitter support, which is stable since 0.9 and mature in 0.11+.
>
> If you prefer using a similar plugin, you can use [`neovim-treesitter/nvim-treesitter`](https://github.com/neovim-treesitter/nvim-treesitter), a community-maintained fork of the original plugin.

The installer script builds the parser and copies it plus the queries into your
Neovim config (`${XDG_CONFIG_HOME:-~/.config}/nvim`):

```bash
curl -fsSL https://raw.githubusercontent.com/MangelMaxime/tree-sitter-fsharp/main/scripts/install-nvim.sh | bash
```

Pin to a branch or commit by appending it:

```bash
curl -fsSL https://raw.githubusercontent.com/MangelMaxime/tree-sitter-fsharp/main/scripts/install-nvim.sh | bash -s -- some-branch
```

It needs `git`, a C compiler, and the `tree-sitter` CLI (or `npx`). Install
somewhere else with `NVIM_CONFIG_DIR=… `, or build from a fork with
`TS_FSHARP_REPO=Owner/repo`.

<details>
<summary>Or do it manually</summary>

```bash
# 1. Build the parser
git clone https://github.com/MangelMaxime/tree-sitter-fsharp
cd tree-sitter-fsharp
npx tree-sitter generate
npx tree-sitter build --output fsharp.so

# 2. Install the parser + queries into ~/.config/nvim
mkdir -p ~/.config/nvim/parser ~/.config/nvim/queries/fsharp
cp fsharp.so ~/.config/nvim/parser/fsharp.so
cp queries/*.scm ~/.config/nvim/queries/fsharp/
```

</details>

Then tell Neovim to use tree-sitter for F# files (in your `init.lua`):

```lua
-- .fs / .fsx / .fsi -> fsharp, with tree-sitter highlighting
vim.filetype.add({ extension = { fs = "fsharp", fsx = "fsharp", fsi = "fsharp" } })
vim.api.nvim_create_autocmd("FileType", {
    pattern = "fsharp",
    callback = function()
        vim.treesitter.start() -- highlighting
    end,
})
```

Open an `.fsx` file and the grammar lights up. `:InspectTree` shows the live
parse tree and `:Inspect` shows the capture under the cursor - handy when
tweaking queries.

#### Try it without installing

The repo ships a throwaway, self-contained Neovim config so you can eyeball the
grammar without touching your own setup. It builds the parser and opens a file
in an isolated `nvim -u nvim/init.lua` (no plugins, no user config):

```bash
./dev-nvim.sh                      # opens examples/references.fsx
./dev-nvim.sh path/to/file.fsx     # opens a specific file
```

This is also the fastest loop when working on the Neovim queries: edit a
`queries/*.scm`, rerun `./dev-nvim.sh`, look.

## Development workflow

1. Edit `grammar.js` and/or `src/scanner.c`.
2. `npx tree-sitter generate` (or just `./dev.sh`).
3. `npx tree-sitter test` to confirm the corpus (460+ tests) still passes.
4. `npx tree-sitter parse examples/layout.fsx | grep -c ERROR` - should be zero.
5. `./dev.sh` to deploy to Helix (`./dev-zed.sh` for Zed).
6. Restart Helix and check the highlights.

For UI-visible changes, render with `tree-sitter highlight`:

```bash
npx tree-sitter highlight path/to/your/file.fsx
```

That shows you the ANSI-tinted output exactly as the queries would apply
in Helix's default theme. It's the fastest way to sanity-check that a
queries change does what you expect before reloading Helix.

## Testing

```bash
npx tree-sitter test                  # run everything
npx tree-sitter test -i some_test     # match by name (substring)
```

### Corpus tests

The standard tree-sitter tests: `test/corpus/*.txt` - one file per topic,
multiple named tests per file. Each test is an F# snippet plus the expected
parse tree:

```
================================================================================
Simple let binding
================================================================================

let answer = 42

--------------------------------------------------------------------------------

(source_file
  (let_binding
    name: (identifier)
    body: (int_literal)))
```

To add one: write the section with the snippet, leave the tree empty, then
generate it from the actual parse and review the result:

```bash
npx tree-sitter test -u -i "Simple let binding"   # writes the expected tree
npx tree-sitter test -i "Simple let binding"      # confirms it passes
```

`-u` overwrites expected trees with whatever the parser currently produces -
only run it filtered (`-i`) on the tests you mean to (re)generate, and read
the diff before committing.

### Highlight tests

`test/highlight/*.fsx` use tree-sitter's native assertion comments - they run
automatically as part of `npx tree-sitter test`:

```fsharp
let add a b = a + b
// <- keyword
//  ^ function
//      ^ variable.parameter
```

`<-` asserts the capture at the start of the line above; `^` asserts at its
own column. The expected name must be one of the captures the queries
produce at that position (e.g. `comment.line.documentation`, not a prefix).

### Expansion tests

Helix's expand-selection walks the parse tree's node *extents* - something
the corpus tests can't assert (they compare structure only). The fixtures in
`test/expansion/*.txt` pin the exact selection text of each expansion step
from a `‸` cursor marker:

```bash
python3 scripts/test-expansion.py             # run all
python3 scripts/test-expansion.py -i multiDoc # filter by substring
```

### Benchmark (real-world coverage)

The numbers in *Why this grammar* come from `scripts/bench.py`: it sweeps
every `.fs`/`.fsx` file of 24 pinned repositories (23 popular projects +
`dotnet/fsharp`'s `src/`, ~3 900 files) and diffs the per-file error counts
against the committed baseline in `test/bench/baseline.txt`. Any file that
parses worse than the baseline fails the run - no grammar change lands
without passing it.

```bash
./scripts/bench.py                    # sweep + regression check (~4 min)
./scripts/bench.py --summary          # add the per-project table
./scripts/bench.py --update-baseline  # accept improvements into the baseline
```

First run clones the corpus (~1.5 GB) into `~/.cache/fsharp-grammar-bench`
(override with `$FSHARP_BENCH_DIR`). Too heavy for CI by design - run it
locally before merging grammar or scanner changes.

## Licence

Apache 2.0
