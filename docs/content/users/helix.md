---
title: Helix
order: 1
---

Helix fetches and compiles the grammar itself. The queries are copied by a script.

## Install the grammar

Add both grammars to `~/.config/helix/languages.toml`. The second one parses signature files (`.fsi`).

```toml
[[grammar]]
name = "fsharp"
source = { git = "https://github.com/MangelMaxime/tree-sitter-fsharp", rev = "main" }

[[grammar]]
name = "fsharp-signature"
source = { git = "https://github.com/MangelMaxime/tree-sitter-fsharp", rev = "main", subpath = "signature" }
```

Then fetch and compile:

```bash
hx --grammar fetch
hx --grammar build
```

## Install the queries

Helix does not install queries for external grammars. The script copies them into `~/.config/helix/runtime/queries/fsharp/` and `queries/fsharp-signature/`, or under `$HELIX_RUNTIME/queries/` when that variable is set.

```bash
curl -fsSL https://raw.githubusercontent.com/MangelMaxime/tree-sitter-fsharp/main/scripts/install-queries.sh | bash
```

To pin a branch or commit, pass it as an argument. Use the same `rev` as in `languages.toml`.

```bash
curl -fsSL https://raw.githubusercontent.com/MangelMaxime/tree-sitter-fsharp/main/scripts/install-queries.sh | bash -s -- some-branch
```

Rerun the script after every `hx --grammar fetch` that changes the revision.

## Language configuration

Helix's built-in `fsharp` entry claims `.fsi`. Give that extension to `fsharp-signature` and configure both languages in `languages.toml`:

```toml
[[language]]
name = "fsharp"
file-types = ["fs", "fsx", "fsscript"]
auto-format = false
comment-tokens = ["//", "///"]

[language.auto-pairs]
'(' = ')'
'[' = ']'
'{' = '}'
'"' = '"'

[[language]]
name = "fsharp-signature"
scope = "source.fsharp.signature"
file-types = ["fsi"]
roots = ["*.fsproj", "*.sln"]
comment-tokens = ["//", "///"]
language-servers = ["fsharp-ls"]
indent = { tab-width = 4, unit = "    " }
```

The single quote is left out of `auto-pairs` because it starts type parameters (`'T`).

## Language server

A `languages.toml` entry for FsAutoComplete with the options used by the maintainer:

```toml
[language-server.fsharp-ls]
command = "fsautocomplete"
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
```

Install the server with `dotnet tool install -g fsautocomplete`.

## Rainbow brackets

The grammar ships `rainbows.scm`. Enable it in `config.toml`:

```toml
[editor]
rainbow-brackets = true
```

Rainbow brackets need a Helix built from source at the time of writing.

## Uninstall

The grammar and the queries both live under the runtime directory.

```bash
RUNTIME="${HELIX_RUNTIME:-$HOME/.config/helix/runtime}"
rm -rf "$RUNTIME/queries/fsharp" "$RUNTIME/queries/fsharp-signature"
rm -f  "$RUNTIME/grammars/fsharp.so" "$RUNTIME/grammars/fsharp-signature.so"
rm -rf "$RUNTIME/grammars/sources/fsharp" "$RUNTIME/grammars/sources/fsharp-signature"
```

Then remove the `[[grammar]]` and `[[language]]` entries from `languages.toml`. Helix falls back to its built-in F# grammar on the next launch.
