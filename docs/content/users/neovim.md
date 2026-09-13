---
title: Neovim
order: 3
---

Neovim's built-in tree-sitter support is used. It is stable since 0.9 and mature in 0.11. The original `nvim-treesitter` plugin was archived on 2026-04-03; the community fork [neovim-treesitter/nvim-treesitter](https://github.com/neovim-treesitter/nvim-treesitter) also works with this grammar.

`queries/nvim/` holds the Neovim-specific queries: `highlights.scm` is derived from the Helix one, `indents.scm`, `folds.scm` and `textobjects.scm` are written for Neovim. The other queries are shared with Helix.

## Install

The script builds the parser and copies it with the queries into `${XDG_CONFIG_HOME:-~/.config}/nvim`. It needs `git`, a C compiler and the `tree-sitter` CLI or `npx`.

```bash
curl -fsSL https://raw.githubusercontent.com/MangelMaxime/tree-sitter-fsharp/main/scripts/install-nvim.sh | bash
```

Pin a branch or commit by appending it:

```bash
curl -fsSL https://raw.githubusercontent.com/MangelMaxime/tree-sitter-fsharp/main/scripts/install-nvim.sh | bash -s -- some-branch
```

`NVIM_CONFIG_DIR` changes the target directory. `TS_FSHARP_REPO=Owner/repo` builds from a fork.

### Manual install

```bash
git clone https://github.com/MangelMaxime/tree-sitter-fsharp
cd tree-sitter-fsharp
npx tree-sitter generate
npx tree-sitter build --output fsharp.so

mkdir -p ~/.config/nvim/parser ~/.config/nvim/queries/fsharp
cp fsharp.so ~/.config/nvim/parser/fsharp.so
cp queries/*.scm ~/.config/nvim/queries/fsharp/
cp queries/nvim/*.scm ~/.config/nvim/queries/fsharp/
```

## Configuration

In `init.lua`:

```lua
vim.filetype.add({ extension = { fs = "fsharp", fsx = "fsharp", fsi = "fsharp" } })
vim.api.nvim_create_autocmd("FileType", {
    pattern = "fsharp",
    callback = function()
        vim.treesitter.start()
    end,
})
```

`:InspectTree` shows the live parse tree. `:Inspect` shows the capture under the cursor.

## Try it without installing

The repository ships an isolated Neovim config. The command builds the parser and opens a file with `nvim -u nvim/init.lua`, without plugins or your own config:

```bash
./build.sh dev nvim
./build.sh dev nvim path/to/file.fsx
```
