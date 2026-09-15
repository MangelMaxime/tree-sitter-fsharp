---
title: Zed
order: 2
---

[Zed's F# extension](https://zed.dev/extensions/fsharp) ships this grammar. Nothing to build.

## Install

1. Open the Extensions panel (`zed: extensions`).
2. Search for **F#** and click **Install**.

Open an `.fs` file: it is coloured.

## Language server

The extension downloads FsAutoComplete on first use and configures it. Its [README](https://github.com/nathanjcollins/zed-fsharp#configuration) documents the settings.

## Companion extensions

Two parts of the colouring are injected from other grammars, so they need their extension installed:

| Extension | What it colours |
|---|---|
| [XML](https://zed.dev/extensions/xml) | `///` doc comments, which are XML. `(** *)` doc comments are Markdown and need nothing. |
| [Comment](https://zed.dev/extensions/comment) | `TODO:` and `FIXME:` markers inside comments. |

## Looking at the parse tree

`debug: open syntax tree view` shows the live tree of the current file. It is the quickest way to see what the grammar makes of a line before reporting an issue.

## Development extension

The repository ships a second extension, for trying a grammar or query change before it reaches the F# extension. Installing it replaces the F# extension until you uninstall it: both use the id `fsharp`.

It differs from the F# extension in two ways: it does not download FsAutoComplete, and it adds the [netcoredbg](https://github.com/Samsung/netcoredbg) debug adapter.

### Install

You need Node.js, the .NET SDK version in `global.json`, and Rust with the `wasm32-wasip2` target, which Zed uses to compile the extension:

```bash
rustup target add wasm32-wasip2
# Arch Linux with the packaged toolchain:
sudo pacman -S rust-wasm
```

Then:

```bash
git clone https://github.com/MangelMaxime/tree-sitter-fsharp
cd tree-sitter-fsharp
npm ci
./build.sh dev zed
```

1. Command palette, `zed: install dev extension`, select the `zed/` directory.
2. After every change to the grammar or the queries: rerun `./build.sh dev zed`, then click **Rebuild** on the extension in the Extensions panel.

To go back to the F# extension: `zed: extensions`, then **Uninstall** on `F# (local dev)`.

### Language server

FsAutoComplete must be on `PATH`, or named in the settings:

```bash
dotnet tool install -g fsautocomplete
```

```json
"lsp": { "fsautocomplete": { "binary": { "path": "/path/to/fsautocomplete" } } }
```

### Debugging

netcoredbg must be on `PATH`, from a recent [release](https://github.com/Samsung/netcoredbg/releases) or a package manager (AUR on Arch Linux), or named in the settings:

```json
"dap": { "netcoredbg": { "binary": { "path": "/path/to/netcoredbg" } } }
```

:::note
Old netcoredbg releases predate current .NET runtimes and fail to start.
:::

Declare the scenarios in `.zed/debug.json` at the project root, then start one from the debug panel or with `debugger: start`:

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

`program` is the built assembly, not the project: the `build` step runs `dotnet build` before each session. `$ZED_PICK_PID` opens Zed's process picker. A launched process starts suspended, so a breakpoint on the first line is hit.
