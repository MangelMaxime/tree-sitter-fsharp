---
title: Neovim
order: 3
---

Neovim's built-in tree-sitter support runs this grammar. It is stable since 0.9 and mature in 0.11. No plugin is needed.

:::note
The original `nvim-treesitter` plugin was archived on 2026-04-03. The community fork [neovim-treesitter/nvim-treesitter](https://github.com/neovim-treesitter/nvim-treesitter) works with this grammar too.
:::

## Install

One script builds both parsers - `fsharp` for `.fs` and `.fsx`, `fsharp_signature` for `.fsi` - and copies them with the queries into `${XDG_CONFIG_HOME:-~/.config}/nvim`. It needs `git`, a C compiler (`cc`, `gcc` or `clang`), and the `tree-sitter` CLI or `npx`:

```bash
curl -fsSL https://raw.githubusercontent.com/MangelMaxime/tree-sitter-fsharp/main/scripts/install-nvim.sh | bash
```

To pin a branch, a tag or a commit, pass it as the argument:

```bash
curl -fsSL https://raw.githubusercontent.com/MangelMaxime/tree-sitter-fsharp/main/scripts/install-nvim.sh | bash -s -- v0.1.0
```

Two variables change where it looks and where it writes:

| Variable | Effect |
|---|---|
| `NVIM_CONFIG_DIR` | Install somewhere other than the Neovim config directory. |
| `TS_FSHARP_REPO` | Build from a fork, as `Owner/repo`. |

Run from a clone of the repository, the script builds that clone instead of downloading one.

:::details By hand instead
```bash
git clone https://github.com/MangelMaxime/tree-sitter-fsharp
cd tree-sitter-fsharp
npx tree-sitter build --output fsharp.so
npx tree-sitter build --output fsharp_signature.so signature

mkdir -p ~/.config/nvim/parser ~/.config/nvim/queries/fsharp ~/.config/nvim/queries/fsharp_signature
cp fsharp.so ~/.config/nvim/parser/fsharp.so
cp queries/*.scm ~/.config/nvim/queries/fsharp/
cp queries/nvim/*.scm ~/.config/nvim/queries/fsharp/

cp fsharp_signature.so ~/.config/nvim/parser/fsharp_signature.so
cp queries/signature/*.scm ~/.config/nvim/queries/fsharp_signature/
cp queries/nvim/signature/*.scm ~/.config/nvim/queries/fsharp_signature/
```

The second `cp` of each pair puts the Neovim versions of `highlights`, `indents`, `folds` and `textobjects` over the Helix ones.
:::

## Turn it on

In `init.lua`, map the extensions to the `fsharp` filetype and start tree-sitter for it. `.fsi` files keep that filetype and are parsed by the signature grammar:

```lua
vim.filetype.add({ extension = { fs = "fsharp", fsx = "fsharp", fsi = "fsharp" } })
vim.api.nvim_create_autocmd("FileType", {
    pattern = "fsharp",
    callback = function(args)
        local lang = args.file:match("%.fsi$") and "fsharp_signature" or "fsharp"
        vim.treesitter.start(args.buf, lang)
    end,
})
```

Open an `.fsx` file: it is coloured. `:InspectTree` shows the parse tree and `:Inspect` the capture under the cursor.

## Try it without installing

The repository ships an isolated Neovim config. This builds the parser and opens a file with `nvim -u nvim/init.lua`, without plugins or your own config:

```bash
./build.sh dev nvim
./build.sh dev nvim path/to/file.fsx
```

It is also the fastest loop when working on the Neovim queries in `queries/nvim/` and `queries/nvim/signature/`: edit, rerun, look.
