---
title: Zed
order: 2
---

The repository contains a Zed extension under `zed/`. It is installed as a dev extension.

## Requirements

- Node.js. `npm ci` installs the pinned tree-sitter CLI.
- The .NET SDK version listed in `global.json`.
- Rust with the `wasm32-wasip2` target. Zed compiles the extension's language server glue with it.

```bash
rustup target add wasm32-wasip2
# Arch Linux with the packaged toolchain:
sudo pacman -S rust-wasm
```

## Install

```bash
git clone https://github.com/MangelMaxime/tree-sitter-fsharp
cd tree-sitter-fsharp
npm ci
./build.sh dev zed
```

Then in Zed:

1. Command palette, `zed: install dev extension`, select the `zed/` directory.
2. After every change to the grammar or the queries: rerun `./build.sh dev zed`, then click **Rebuild** on the extension in the Extensions panel.

The dev extension uses the id `fsharp`, so it replaces the marketplace F# extension while it is installed.

## Language server

The extension registers FsAutoComplete. Install it and keep it on `PATH`:

```bash
dotnet tool install -g fsautocomplete
```

The `lsp.fsautocomplete` settings (`binary`, `initialization_options`) apply to it.

## Settings

- Rainbow brackets: `"colorize_brackets": true`, globally or under `"languages": { "FSharp": ... }`. Every pair the grammar declares is coloured, including `[| |]`, `{| |}` and `[< >]`.
- `///` doc comments are XML. Their colouring needs the [XML extension](https://zed.dev/extensions/xml). `(** *)` doc comments are Markdown and work without it.
- `TODO:` and `FIXME:` markers inside comments need the [comment extension](https://zed.dev/extensions/comment).
- `debug: open syntax tree view` shows the live parse tree.

## Debugging

The extension registers [netcoredbg](https://github.com/Samsung/netcoredbg) as a debug adapter.

**Requirements**

- `netcoredbg` on `PATH`, from a recent [release](https://github.com/Samsung/netcoredbg/releases) or a package manager (AUR on Arch Linux). Old releases predate current .NET runtimes.
- Or point Zed at the binary: `"dap": { "netcoredbg": { "binary": "/path/to/netcoredbg" } }`.

**Usage**

Declare the scenarios in `.zed/debug.json` at the project root:

```json
[
  {
    "label": "Debug MyApp",
    "adapter": "netcoredbg",
    "request": "launch",
    "program": "$ZED_WORKTREE_ROOT/bin/Debug/net10.0/MyApp.dll",
    "cwd": "$ZED_WORKTREE_ROOT",
    "build": { "command": "dotnet", "args": ["build"] }
  },
  {
    "label": "Attach to .NET process",
    "adapter": "netcoredbg",
    "request": "attach",
    "processId": "$ZED_PICK_PID"
  }
]
```

Start a session from the debug panel or with `debugger: start`.

- `program` is the built assembly, not the project. The `build` step runs `dotnet build` before each session.
- `"processId": "$ZED_PICK_PID"` opens Zed's process picker when the session starts.
- Launched processes start suspended, so breakpoints on the first line are hit.

## Uninstall

Command palette, `zed: extensions`, then **Uninstall** on `F# (local dev)`.
