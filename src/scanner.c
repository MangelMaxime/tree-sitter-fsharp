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
    INLINE_LET_END,    // terminates inline-body let_decl_indented at sibling/continuation col
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
    IndentStack indents;
} Scanner;

void *tree_sitter_fsharp_external_scanner_create(void) {
    return calloc(1, sizeof(Scanner));
}

void tree_sitter_fsharp_external_scanner_destroy(void *payload) {
    Scanner *s = payload;
    free(s->indents.data);
    free(s);
}

unsigned tree_sitter_fsharp_external_scanner_serialize(void *payload, char *buf) {
    Scanner *s = payload;
    unsigned n = 0;
    uint32_t sz = s->indents.size;
    if (n + 4 > TREE_SITTER_SERIALIZATION_BUFFER_SIZE) return n;
    memcpy(buf + n, &sz, 4); n += 4;
    for (uint32_t i = 0; i < sz && n + 4 <= TREE_SITTER_SERIALIZATION_BUFFER_SIZE; i++) {
        memcpy(buf + n, &s->indents.data[i], 4); n += 4;
    }
    return n;
}

void tree_sitter_fsharp_external_scanner_deserialize(void *payload, const char *buf, unsigned length) {
    Scanner *s = payload;
    s->indents.size = 0;
    unsigned n = 0;
    if (n + 4 > length) return;
    uint32_t sz; memcpy(&sz, buf + n, 4); n += 4;
    for (uint32_t i = 0; i < sz && n + 4 <= length; i++) {
        uint32_t v; memcpy(&v, buf + n, 4); n += 4;
        stack_push(&s->indents, v);
    }
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
    bool want_inline_end = valid_symbols[INLINE_LET_END];

    if (!want_body_indent && !want_body_dedent && !want_indent && !want_dedent
        && !want_inline_end) return false;

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

    // INLINE_LET_END is only valid INSIDE an open block — i.e., when at least one
    // BODY_INDENT or INDENT has been pushed. At module level (indents.size == 0)
    // `let` is always a let_binding, never a let_decl_indented inline, so we never
    // emit INLINE_LET_END there.
    bool inline_end_eligible = want_inline_end && s->indents.size >= 1;

    uint32_t col = 0;
    if (!next_line_indent(lexer, &col)) {
        // EOF — close any open block.
        // INLINE_LET_END first so inline-bodied lets close before surrounding blocks.
        if (inline_end_eligible) {
            lexer->result_symbol = INLINE_LET_END;
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

    // INLINE_LET_END: fires when next non-blank line is at col <= the surrounding
    // block's column (top of indents stack). This is the F# offside rule: a sibling
    // `let` or a continuation expression at the same column terminates the body.
    if (inline_end_eligible && col <= current) {
        lexer->result_symbol = INLINE_LET_END;
        return true;
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
