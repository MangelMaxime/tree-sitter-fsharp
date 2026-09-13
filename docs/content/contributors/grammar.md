---
title: Grammar
order: 5
---

Two grammars share one scanner.

| Grammar | Files | Parses |
|---|---|---|
| `fsharp` | `grammar.js`, `src/scanner.c` | `.fs`, `.fsx`, `.fsscript` |
| `fsharp_signature` | `signature/grammar.js`, `signature/src/scanner.c` | `.fsi` |

`signature/grammar.js` derives from `grammar.js` the way tree-sitter-ocaml derives its interface grammar. It inherits the type language, the attributes and the scanner, replaces member bodies with member signatures and drops every expression rule. Generate and test it from inside `signature/`; `./build.sh generate` and `./build.sh test` do both grammars.

## The scanner

`src/scanner.c` implements the offside rule. It keeps a stack of open layout contexts and emits the block open and close tokens the grammar expects, plus the tokens the lexer cannot produce on its own: interpolated string text, the `(* *)` comment, block openers inside computation expressions and a few zero-width gates. The scanner state is serialized per parse position by tree-sitter; the header comment of the file describes the model.

The scanner may only call `memcpy`, `memcmp`, `strlen`, `strcmp`, `strncpy`, `calloc` and `free`: the Wasm build for Zed has no other libc.

## Parser size

The generated parser has about 15 000 states. Generate time and the size of `src/parser.c` grow with every new conflict; `LIMITATIONS.md` lists the constructs dropped to keep the table small and the techniques that keep it there.

## Trade-offs and known gaps

`LIMITATIONS.md` documents every accepted trade-off with the evidence behind it, the known gaps in both grammars, and how quality is measured. Read it before changing a reserved word or a layout rule.

## Fuzzing

CI fuzzes the scanner on every push with `tree-sitter/fuzz-action`, seeded with `examples/`.
