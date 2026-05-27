#include "tree_sitter/parser.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// External scanner for F# offside-rule tokens.
//
// Tree-sitter only supports LALR(1) over a fixed token grammar, but F# uses
// indentation to delimit several constructs that have no syntactic terminator.
// The scanner emits zero-width tokens to bracket those constructs.
//
//   BODY_INDENT/BODY_DEDENT  — wrap indented bodies: module-level let_binding
//                              bodies, class-style type_decl/module_decl
//                              bodies, record-type/record-expression field
//                              lists, and if/elif/else body branches.
//   INDENT/DEDENT            — wrap indented let_decl_indented bodies inside
//                              let_expression.
//   INLINE_OPEN/INLINE_CLOSE — wrap same-line let_decl_indented bodies. The
//                              scanner records the body's start column on OPEN
//                              and emits CLOSE at the first line whose column
//                              is <= that recorded column.
//   VIRTUAL_SEMI             — virtual semicolon between sibling expressions on
//                              separate lines (F#'s implicit sequence). Emitted
//                              when the parser accepts it AND the next line
//                              sits at the current body column AND its first
//                              significant token isn't a blocker keyword (see
//                              `blockers[]` below). GLR sorts out the
//                              sequence-vs-continuation ambiguity for the
//                              cases the scanner can't fully disambiguate.
//
// The enum order must match the externals: array in grammar.js.
typedef enum {
    BODY_INDENT,
    BODY_DEDENT,
    INDENT,
    DEDENT,
    INLINE_OPEN,
    INLINE_CLOSE,
    VIRTUAL_SEMI,
} TokenType;

typedef struct {
    uint32_t *data;
    uint32_t size;
    uint32_t capacity;
} IndentStack;

static void stack_push(IndentStack *s, uint32_t val) {
    if (s->size == s->capacity) {
        s->capacity = s->capacity ? s->capacity * 2 : 8;
        s->data = realloc(s->data, s->capacity * sizeof(uint32_t));
    }
    s->data[s->size++] = val;
}

static uint32_t stack_top(const IndentStack *s) {
    return s->size > 0 ? s->data[s->size - 1] : 0;
}

static void stack_pop(IndentStack *s) {
    if (s->size > 0) s->size--;
}

typedef struct {
    IndentStack indents;      // pushed by BODY_INDENT / INDENT, popped by their DEDENT
    IndentStack inline_cols;  // pushed by INLINE_OPEN, popped by INLINE_CLOSE
} Scanner;

void *tree_sitter_fsharp_external_scanner_create(void) {
    return calloc(1, sizeof(Scanner));
}

void tree_sitter_fsharp_external_scanner_destroy(void *payload) {
    Scanner *s = payload;
    free(s->indents.data);
    free(s->inline_cols.data);
    free(s);
}

static unsigned serialize_stack(const IndentStack *st, char *buf, unsigned n) {
    uint32_t sz = st->size;
    if (n + 4 > TREE_SITTER_SERIALIZATION_BUFFER_SIZE) return n;
    memcpy(buf + n, &sz, 4); n += 4;
    for (uint32_t i = 0; i < sz && n + 4 <= TREE_SITTER_SERIALIZATION_BUFFER_SIZE; i++) {
        memcpy(buf + n, &st->data[i], 4); n += 4;
    }
    return n;
}

static unsigned deserialize_stack(IndentStack *st, const char *buf, unsigned length, unsigned n) {
    st->size = 0;
    if (n + 4 > length) return n;
    uint32_t sz; memcpy(&sz, buf + n, 4); n += 4;
    for (uint32_t i = 0; i < sz && n + 4 <= length; i++) {
        uint32_t v; memcpy(&v, buf + n, 4); n += 4;
        stack_push(st, v);
    }
    return n;
}

unsigned tree_sitter_fsharp_external_scanner_serialize(void *payload, char *buf) {
    Scanner *s = payload;
    unsigned n = 0;
    n = serialize_stack(&s->indents, buf, n);
    n = serialize_stack(&s->inline_cols, buf, n);
    return n;
}

void tree_sitter_fsharp_external_scanner_deserialize(void *payload, const char *buf, unsigned length) {
    Scanner *s = payload;
    unsigned n = 0;
    n = deserialize_stack(&s->indents, buf, length, n);
    n = deserialize_stack(&s->inline_cols, buf, length, n);
}

// Skip to the next non-blank, non-comment line and return its indent column in
// *col. Returns false on EOF. Advances the lexer; the caller must have already
// called mark_end at the position where the token should appear.
//
// "Comment" here means line comments (`// …`) AND block comments (`(* … *)`),
// including F#'s nested-block-comment form. Without this, an outdented block
// comment between two declarations would be reported as the next significant
// line and (for inside-an-indented-body scanner calls) trigger a spurious
// BODY_DEDENT that closes the surrounding block.
static bool next_line_indent(TSLexer *lexer, uint32_t *col) {
    // Skip the rest of the current line (non-newline chars).
    while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0) {
        lexer->advance(lexer, true);
    }
    if (lexer->lookahead == 0) return false; // EOF

    // Scan lines until we find a non-blank, non-comment line.
    while (true) {
        // Consume the newline(s).
        if (lexer->lookahead == '\r') lexer->advance(lexer, true);
        if (lexer->lookahead == '\n') lexer->advance(lexer, true);

        // Count leading spaces/tabs.
        uint32_t indent = 0;
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            indent += (lexer->lookahead == '\t') ? 4 : 1;
            lexer->advance(lexer, true);
        }

        // Skip blank lines.
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') continue;
        // Skip line comments (// ...).
        if (lexer->lookahead == '/') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == '/') {
                while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0)
                    lexer->advance(lexer, true);
                if (lexer->lookahead == 0) return false;
                continue;
            }
            // Single slash — treat as content at `indent` columns.
            *col = indent;
            return true;
        }
        // Skip block comments (* ... *)  — supports F#'s nested form.
        if (lexer->lookahead == '(') {
            lexer->advance(lexer, true);
            if (lexer->lookahead != '*') {
                // Lone `(` — significant content at `indent`.
                *col = indent;
                return true;
            }
            lexer->advance(lexer, true);
            int depth = 1;
            while (depth > 0) {
                if (lexer->lookahead == 0) return false;
                if (lexer->lookahead == '(') {
                    lexer->advance(lexer, true);
                    if (lexer->lookahead == '*') {
                        depth++;
                        lexer->advance(lexer, true);
                    }
                } else if (lexer->lookahead == '*') {
                    lexer->advance(lexer, true);
                    if (lexer->lookahead == ')') {
                        depth--;
                        lexer->advance(lexer, true);
                    }
                } else {
                    lexer->advance(lexer, true);
                }
            }
            // Block comment consumed. Skip any trailing content on this line
            // (e.g. `(* foo *) more`) and let the loop iterate to the next.
            while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0) {
                lexer->advance(lexer, true);
            }
            if (lexer->lookahead == 0) return false;
            continue;
        }
        if (lexer->lookahead == 0) return false; // EOF after blank lines

        *col = indent;
        return true;
    }
}

bool tree_sitter_fsharp_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    Scanner *s = payload;
    bool want_body_indent = valid_symbols[BODY_INDENT];
    bool want_body_dedent = valid_symbols[BODY_DEDENT];
    bool want_indent = valid_symbols[INDENT];
    bool want_dedent = valid_symbols[DEDENT];
    bool want_inline_open = valid_symbols[INLINE_OPEN];
    bool want_inline_close = valid_symbols[INLINE_CLOSE];
    bool want_virtual_semi = valid_symbols[VIRTUAL_SEMI];

    if (!want_body_indent && !want_body_dedent && !want_indent && !want_dedent
        && !want_inline_open && !want_inline_close && !want_virtual_semi) return false;

    // INLINE_OPEN: emitted right after `=` when a let_decl_indented body starts on
    // the same line. Pushes the body's start column so INLINE_CLOSE can compare.
    //
    // Suppressed in two cases:
    //   1. BODY_INDENT is also valid — let_binding owns this position; we want it
    //      to win over let_decl_indented inline (module-level lets).
    //   2. An `in` keyword appears on the rest of the current line — that means
    //      let_expression Branch B (explicit `let ... = expr in expr`) is intended,
    //      and committing to inline here would dead-end the parse.
    if (want_inline_open && !want_body_indent) {
        // Skip horizontal whitespace to find the start of the body.
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            lexer->advance(lexer, true);
        }
        // Same-line body? (Not newline / EOF.)
        if (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0) {
            uint32_t body_col = lexer->get_column(lexer);
            lexer->mark_end(lexer);

            // Peek for `in` on the rest of the current line.
            bool prev_word = false;
            bool found_in = false;
            while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0) {
                int32_t c = lexer->lookahead;
                bool c_word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9') || c == '_' || c == '\'';
                if (!prev_word && c == 'i') {
                    lexer->advance(lexer, true);
                    if (lexer->lookahead == 'n') {
                        lexer->advance(lexer, true);
                        int32_t n = lexer->lookahead;
                        bool n_word = (n >= 'a' && n <= 'z') || (n >= 'A' && n <= 'Z') ||
                                      (n >= '0' && n <= '9') || n == '_' || n == '\'';
                        if (!n_word) { found_in = true; break; }
                        prev_word = true;
                    } else {
                        prev_word = c_word;
                    }
                } else {
                    lexer->advance(lexer, true);
                    prev_word = c_word;
                }
            }

            if (!found_in) {
                stack_push(&s->inline_cols, body_col);
                lexer->result_symbol = INLINE_OPEN;
                return true;
            }
            // `in` found on rest of line — let Branch B match. Fall through.
            return false;
        }
        // Body on next line — fall through; INDENT will handle it.
    }

    // From here we're handling BODY_INDENT / BODY_DEDENT / INDENT / DEDENT /
    // INLINE_CLOSE — these normally only fire at a line boundary, with one
    // exception: BODY_DEDENT also fires inline when the next non-whitespace
    // char is a closing delimiter (`}` or the `end` keyword). That lets blocks
    // whose body ends on the same line as the close still parse — e.g.
    //   `{ new IFoo with member _.X = 1 }`
    // where the `}` sits right after the last member instead of on its own
    // line. Without this, `interface_impl`/`type_extension`/etc. require the
    // user to break the close onto a new line.
    lexer->mark_end(lexer);

    bool at_newline = (lexer->lookahead == '\n' || lexer->lookahead == '\r');
    if (!at_newline) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            lexer->advance(lexer, true);
        }
        if (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0) {
            // Mid-line non-whitespace ahead. If it's a recognised closing
            // delimiter and the parser is expecting BODY_DEDENT (i.e. we are
            // inside an indented body that needs to close before the
            // delimiter), pop one indent level and emit BODY_DEDENT.
            if (want_body_dedent && s->indents.size > 0) {
                if (lexer->lookahead == '}') {
                    stack_pop(&s->indents);
                    lexer->result_symbol = BODY_DEDENT;
                    return true;
                }
                // Match the `end` keyword (must be followed by a non-word
                // char, so we don't mistake identifiers like `enderror`).
                if (lexer->lookahead == 'e') {
                    lexer->advance(lexer, true);
                    if (lexer->lookahead == 'n') {
                        lexer->advance(lexer, true);
                        if (lexer->lookahead == 'd') {
                            lexer->advance(lexer, true);
                            int32_t after = lexer->lookahead;
                            bool word_continues =
                                (after >= 'a' && after <= 'z') ||
                                (after >= 'A' && after <= 'Z') ||
                                (after >= '0' && after <= '9') ||
                                after == '_' || after == '\'';
                            if (!word_continues) {
                                stack_pop(&s->indents);
                                lexer->result_symbol = BODY_DEDENT;
                                return true;
                            }
                        }
                    }
                }
            }
            return false;
        }
    }

    uint32_t col = 0;
    if (!next_line_indent(lexer, &col)) {
        // EOF: close any open block. INLINE_CLOSE first so inline-bodied lets
        // close before any surrounding BODY_/INDENT blocks.
        if (want_inline_close && s->inline_cols.size > 0) {
            stack_pop(&s->inline_cols);
            lexer->result_symbol = INLINE_CLOSE;
            return true;
        }
        if (s->indents.size > 0) {
            if (want_dedent) {
                stack_pop(&s->indents);
                lexer->result_symbol = DEDENT;
                return true;
            }
            if (want_body_dedent) {
                stack_pop(&s->indents);
                lexer->result_symbol = BODY_DEDENT;
                return true;
            }
        }
        return false;
    }

    uint32_t current = stack_top(&s->indents);

    // INLINE_CLOSE: any line at column <= the recorded body column ends the inline
    // body (sibling let, continuation expression, or end of enclosing block).
    if (want_inline_close && s->inline_cols.size > 0) {
        uint32_t body_col = stack_top(&s->inline_cols);
        if (col <= body_col) {
            stack_pop(&s->inline_cols);
            lexer->result_symbol = INLINE_CLOSE;
            return true;
        }
    }

    // Indent — push a new block.
    // BODY_INDENT is preferred over INDENT so that at the body position of a
    // module-level let_binding, the parser commits to let_binding rather than
    // exploring let_decl_indented (which would fail without _indent).
    if (col > current) {
        if (want_body_indent) {
            stack_push(&s->indents, col);
            lexer->result_symbol = BODY_INDENT;
            return true;
        }
        if (want_indent) {
            stack_push(&s->indents, col);
            lexer->result_symbol = INDENT;
            return true;
        }
    }

    // Dedent — pop. DEDENT is preferred over BODY_DEDENT so inner let_decl_indented
    // bodies close before the outer let_binding body.
    if (col < current) {
        if (want_dedent) {
            stack_pop(&s->indents);
            lexer->result_symbol = DEDENT;
            return true;
        }
        if (want_body_dedent) {
            stack_pop(&s->indents);
            lexer->result_symbol = BODY_DEDENT;
            return true;
        }
    }

    // VIRTUAL_SEMI: lowest priority — fires only when no INLINE_CLOSE / INDENT /
    // DEDENT applied. Emitted when the next line sits at exactly the current
    // body column (the top of the indents stack — which is pushed by
    // BODY_INDENT when we enter any indented body, let or if-then or
    // for/while/lambda etc.). Top-level _token siblings have an empty stack
    // so this naturally skips them.
    //
    // We also block emission when the next char can't start a new expression
    // (`|`, `)`, `]`, `}`, or continuation keywords like `else`/`elif`/`with`).
    // Without those guards the parser commits to a sequence path that fails
    // once it reaches the closer.
    if (want_virtual_semi && col == current && s->indents.size > 0) {
        int32_t c = lexer->lookahead;
        bool blocked = (c == '|' || c == ')' || c == ']' || c == '}');
        // Punctuation/sigil blockers — non-identifier sequences that ALSO start
        // a new declaration rather than continuing an expression. Peeking two
        // characters: `[<` opens an attribute, `//` opens a `///` doc comment.
        if (!blocked && (c == '[' || c == '/')) {
            int32_t first = c;
            lexer->advance(lexer, true);
            int32_t second = lexer->lookahead;
            if (first == '[' && second == '<') blocked = true;
            else if (first == '/' && second == '/') blocked = true;
        }
        // Read the next identifier-shaped word so we can match against a list
        // of "blocker" keywords. Any keyword that starts a sibling declaration
        // inside a class/module/let body, OR continues the enclosing construct
        // (else/elif/with/etc.), means we should NOT emit a virtual_semi for
        // an expression-sequence continuation here.
        if (!blocked) {
            int32_t buf[16];
            size_t i = 0;
            int32_t look = lexer->lookahead;
            while (i < 15 && ((look >= 'a' && look <= 'z') || (look >= 'A' && look <= 'Z') ||
                              (look >= '0' && look <= '9') || look == '_' || look == '\'')) {
                buf[i++] = look;
                lexer->advance(lexer, true);
                look = lexer->lookahead;
            }
            // Any keyword that either continues the enclosing construct
            // (else/elif/with/then/etc.), closes a block (end), or starts a
            // sibling declaration inside a class/module body (member/let/
            // type/etc.) must NOT trigger a virtual_semi — otherwise the
            // parser would commit to an expression-sequence continuation it
            // can't recover from.
            //
            // Keep this list in sync with the grammar: every string-literal
            // keyword that begins a class_body_member or _token belongs here.
            static const char *blockers[] = {
                // Continuation keywords for the enclosing construct
                "else", "elif", "with", "then", "do", "in", "and", "finally", "of",
                // Closes a `class`/`struct`/`interface` block
                "end",
                // Class-body sibling declaration starters
                "member", "abstract", "override", "default", "inherit",
                "interface", "val", "new", "static",
                // Module-body / top-level declaration starters
                "let", "type", "module", "namespace", "exception", "open",
                NULL,
            };
            for (const char **k = blockers; *k; k++) {
                size_t kl = strlen(*k);
                if (kl != i) continue;
                bool eq = true;
                for (size_t j = 0; j < kl; j++) {
                    if (buf[j] != (int32_t)(*k)[j]) { eq = false; break; }
                }
                if (eq) { blocked = true; break; }
            }
        }
        if (!blocked) {
            lexer->result_symbol = VIRTUAL_SEMI;
            return true;
        }
    }

    return false;
}
