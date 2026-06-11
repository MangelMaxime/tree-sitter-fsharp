# Coverage comparison: this grammar vs ionide/tree-sitter-fsharp

*2026-06-11 — ionide @ 5916cf8 (2026-06-02), our grammar @ 099f181 (fix 25).*

## Methodology

Both parsers ran over the same two corpora, one `tree-sitter parse` per file,
counting `ERROR`+`MISSING` nodes (a file "passes" at zero):

- **23-repo suite**: 3654 .fs/.fsx files (fantomas, Fable, Paket, FAKE,
  FSharp.Data, fparsec, FSharpPlus, farmer, Hopac, FsToolkit, Giraffe, Saturn,
  Feliz×2, elmish, FsCheck, expecto, Thoth.Json, Fulma, EasyBuild×3, Scriptorium).
- **dotnet/fsharp `src/`**: 260 files (compiler + FSharp.Core). `tests/` was
  excluded — it contains intentionally-invalid compiler fixtures.

⚠ Both grammars are named `fsharp`, and the tree-sitter CLI caches compiled
parsers BY NAME — switching repos silently serves the other parser (tell:
ionide's root node is `(file …)`, ours `(source_file …)`). Every switch
requires `rm -rf ~/.cache/tree-sitter` + a re-warm; all numbers below were
taken with verified parser identity.

## Headline numbers

| Suite | Metric | **ours** | **ionide** |
|---|---|---|---|
| 23-repo (3654 files) | files clean | **3475 (95.1%)** | 2835 (77.6%) |
| | error nodes | **975** | 14 479 |
| dotnet/fsharp src (260) | files clean | **229 (88.1%)** | 124 (47.7%) |
| | error nodes | **159** | 3 273 |

Overlap on the 23-repo suite: **62 files only we fail** (195 nodes),
**702 files only ionide fails** (11 022 nodes), 117 files both fail.
On fsharp-src: **2 / 107 / 29**.

## Gaps WE had (ionide parsed, we didn't)

The comparison directly produced **fix 25** (committed):
- `{ X.Default("A").Settings with … }` — copy-update with a dot-expression base (Paket tests).
- Ctor attributes with trailing `//` comments before the params (Feliz POJO docs).
- `lazy` with an indented multi-statement block body (FCS FxResolver) — new `_lazy_open` external.

Still open after fix 25:
| Gap | Where | Nodes | Status |
|---|---|---|---|
| `struct {\| … \|}` anonymous records | Fable test trios | ~24 | **Parked** — two encodings broke `struct (a, b)` tuples via silent GLR resolution; needs an explicit-conflicts session |
| Pipe into match arms mid-chain compound | FAKE Chocolatey.fs | 24 | compound (pieces pass in isolation) |
| 50k/10k generated benchmark scripts | fable-standalone | 26 | low value (generated) |
| FSC "undentation" (arm body dedented to arm col) | NameResolution (both copies) | 17+17 | ionide handles this — worth studying their approach |
| `(​(\|App\|_\|) : _ -> _ voption)` active-pattern param | LowerComputedCollections | 2 | exotic |
| Return ascription w/ generic tuple args | TransparentCompiler | 2 | tiny |

## Gaps IONIDE has (we parse, they don't)

Their failures span **every repo** (Fable 251 files, FSharpPlus 94, Paket 44,
FSharp.Data 39, FAKE 39, Hopac 27, farmer 26, FsToolkit 25 …). Probed
constructs they fail and we handle:

- Hopac's `^` right-apply operator (`memos ^ MVar.read xM.mvar`)
- `extern` P/Invoke declarations
- Over-indented match continuation arms
- Multi-line FSharp.Core static-optimization equation chains
- Compound `while … && p arr.[i] do` blocks (components pass; combination fails)
- Their biggest single failures: TypedTree.fs 570 nodes, CompilerDiagnostics 278,
  ServiceLexing 235, FSharp.Core/array.fs 142, FSharpPlus Foldable 398, Seq 378

They DO handle: simple `#if/#else` duals, `#nowarn`, anonymous-record
copy-update, and the undentation idiom.

Their error DENSITY is the starker difference: 14.8× our node count on the
23-repo suite — their failures cascade through whole files, ours stay local.

## Structural comparison

| | ours | ionide |
|---|---|---|
| grammar.js | 3 513 lines | 2 377 lines |
| external scanner | 1 589 lines (29 externals, offside-layout model) | ~1 256 lines |
| corpus tests | 460 | ~420 |
| queries | tuned for Helix (highlights/locals last-wins) | nvim-oriented |

## Verdict

On real-world code our coverage is substantially ahead on both suites
(95.1% vs 77.6%, 88.1% vs 47.7%), and where we fail, damage stays local.
The comparison was still profitable: it surfaced three fixable gaps (landed as
fix 25), one parked feature (`struct {\| \|}`), and one idiom worth studying in
their grammar (undentation).
