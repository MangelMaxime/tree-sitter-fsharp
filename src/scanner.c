#include "tree_sitter/parser.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Order must match externals in grammar.js.
typedef enum {
    BODY_INDENT,       // wraps let_binding bodies; prioritized over INDENT when both valid
    BODY_DEDENT,
    INDENT,            // wraps let_decl_indented bodies inside let_expression
    DEDENT,
    INLINE_OPEN,       // start of inline-body let_decl_indented; pushes body col
    INLINE_CLOSE,      // end of inline body; pops; fires when next line col <= pushed col
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
    IndentStack indents;       // BODY_INDENT / INDENT block columns
    IndentStack inline_cols;   // INLINE_OPEN body columns (popped by INLINE_CLOSE)
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

// Scan forward to find the column of the next non-blank, non-comment line.
// Returns true if found; sets *col to that column.
// Advances the lexer (observation only — caller marked_end before calling).
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

    if (!want_body_indent && !want_body_dedent && !want_indent && !want_dedent
        && !want_inline_open && !want_inline_close) return false;

    // INLINE_OPEN: emitted right after the `=` of a let_decl_indented when the body
    // starts on the same line (not the next). Pushes the body's start column so that
    // INLINE_CLOSE can later compare against it.
    //
    // Suppressed when _body_indent is also valid — that's the body position of a
    // let_binding, where we want let_binding to win over let_decl_indented inline
    // (module-level lets).
    //
    // Also suppressed when there's an `in` keyword on the rest of the current line —
    // that means let_expression Branch B (explicit `let ... = expr in expr`) is
    // intended, and emitting INLINE_OPEN would commit to the wrong branch.
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

    // Mark the token as zero-width at the current position.
    lexer->mark_end(lexer);

    // Skip horizontal whitespace on the current line.
    bool at_newline = (lexer->lookahead == '\n' || lexer->lookahead == '\r');
    if (!at_newline) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            lexer->advance(lexer, true);
        }
        // Inline content — only INDENT-style tokens could fire, but we only emit them
        // for bodies starting on the NEXT line.
        if (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0) {
            return false;
        }
    }

    uint32_t col = 0;
    if (!next_line_indent(lexer, &col)) {
        // EOF — close any open block.
        // INLINE_CLOSE first so inline-bodied lets close before surrounding blocks.
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

    // INLINE_CLOSE: fires when next non-blank line is at col <= the recorded body
    // column. This is the F# offside rule: anything indented at-or-less than the
    // body's start terminates it (sibling let, continuation expression, or end of
    // enclosing block).
    if (want_inline_close && s->inline_cols.size > 0) {
        uint32_t body_col = stack_top(&s->inline_cols);
        if (col <= body_col) {
            stack_pop(&s->inline_cols);
            lexer->result_symbol = INLINE_CLOSE;
            return true;
        }
    }

    if (col > current) {
        // BODY_INDENT is checked first so that let_binding wins over let_decl_indented
        // when both are valid (same "let x =" prefix). The scanner emitting BODY_INDENT
        // instead of INDENT causes the let_decl_indented path to fail immediately.
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

    if (col < current) {
        // DEDENT is checked before BODY_DEDENT so that inner let_decl_indented bodies
        // close before the outer let_binding body closes.
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
