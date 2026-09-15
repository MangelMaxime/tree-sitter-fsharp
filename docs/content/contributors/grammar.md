---
title: Grammar
order: 5
---

The repository holds two grammars that share one scanner.

| Grammar | Files | Parses |
|---|---|---|
| `fsharp` | `grammar.js`, `src/scanner.c` | `.fs`, `.fsx` |
| `fsharp_signature` | `signature/grammar.js`, `signature/src/scanner.c` | `.fsi` |

`signature/grammar.js` builds on `grammar.js`. It keeps the types, attributes and scanner, replaces member bodies with member signatures, and removes every expression. `signature/src/scanner.c` only includes `src/scanner.c`. `./build.sh generate` and `./build.sh test` handle both grammars.

## The scanner

F# decides where a block ends from indentation. `src/scanner.c` implements that offside rule: it tracks the open blocks by column and emits the zero-width tokens the grammar uses to open and close them. It also produces the few tokens a regular lexer cannot, such as the text between interpolation holes and nested `(* *)` comments. The comment at the top of the file describes the model.

:::warning
The Wasm build used by Zed links against a small libc. The scanner calls only `calloc`, `free`, `memcpy`, `memset` and `strcmp`; test any new C library call in Zed before relying on it. A missing function, `strcpy` for example, does not fail the build: Zed refuses to load the grammar.
:::

## Parser size

The generated parser has about 16 000 states. Each new conflict or rule variant can grow it, and with it `src/parser.c` and the generate time. `LIMITATIONS.md` lists the constructs dropped to keep it small and the techniques that keep it there.

## Before changing a rule

`LIMITATIONS.md` records every accepted trade-off with the evidence behind it, the known gaps in both grammars, and how quality is measured. Check it before reserving a keyword or changing a layout rule: many of those changes were measured and rejected already.

## Fuzzing

CI fuzzes the scanner on every push and pull request with `tree-sitter/fuzz-action`, starting from the files in `examples/`.
