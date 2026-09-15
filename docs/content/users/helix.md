---
title: Helix
order: 1
---

Helix compiles the grammar itself. The queries are copied by a script. Three steps.

## 1. Declare the grammars

Add both grammars to `~/.config/helix/languages.toml`. The second one is for signature files (`.fsi`).

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

## 2. Install the queries

Helix compiles external grammars but does not install their queries. This script (it needs `curl`) copies them into `~/.config/helix/runtime/queries/fsharp/` and `queries/fsharp-signature/`:

```bash
curl -fsSL https://raw.githubusercontent.com/MangelMaxime/tree-sitter-fsharp/main/scripts/install-queries.sh | bash
```

To pin a branch, a tag or a commit, pass it as the argument, and use the same `rev` in `languages.toml`:

```bash
curl -fsSL https://raw.githubusercontent.com/MangelMaxime/tree-sitter-fsharp/main/scripts/install-queries.sh | bash -s -- v0.1.0
```

:::note
Rerun the script after every `hx --grammar fetch` that changes the revision. The queries and the grammar must come from the same commit.
:::

When `HELIX_RUNTIME` is set, the script installs under `$HELIX_RUNTIME/queries/` instead. It writes an empty file for every query kind the repository does not ship, which keeps Helix from falling back to its built-in F# queries.

## 3. Configure the languages

Helix's built-in `fsharp` entry claims `fs`, `fsi` and `fsx`. Give `fsi` to `fsharp-signature`, and set the comment tokens and auto-pairs for both:

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

The single quote is left out of the auto-pairs: it starts type parameters such as `'T`.

Restart Helix and open an `.fsx` file: it is coloured. Open buffers pick up a query change after `:reload`.

## Language server

Helix already defines `fsharp-ls` as `fsautocomplete` with `AutomaticWorkspaceInit`. Install the tool:

```bash
dotnet tool install -g fsautocomplete
```

To tune it, redefine the server. This is the maintainer's configuration:

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

## Rainbow brackets

The grammar ships `rainbows.scm`. Turn the feature on in `config.toml`:

```toml
[editor]
rainbow-brackets = true
```

:::note
You need Helix built from the `master` branch: no release includes rainbow brackets yet.
:::

## Uninstall

The grammar and the queries both live under the runtime directory:

```bash
RUNTIME="${HELIX_RUNTIME:-$HOME/.config/helix/runtime}"
rm -rf "$RUNTIME/queries/fsharp" "$RUNTIME/queries/fsharp-signature"
rm -f  "$RUNTIME/grammars/fsharp.so" "$RUNTIME/grammars/fsharp-signature.so"
rm -rf "$RUNTIME/grammars/sources/fsharp" "$RUNTIME/grammars/sources/fsharp-signature"
```

Then remove the `[[grammar]]` and `[[language]]` entries from `languages.toml`. Helix uses its built-in F# grammar again on the next launch.
