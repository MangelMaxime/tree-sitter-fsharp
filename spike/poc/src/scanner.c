#include "tree_sitter/parser.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum { ERROR_SENTINEL, OPEN, SEMI, END };

#define MAXD 256
typedef struct { uint32_t cols[MAXD]; uint16_t size; } Scanner;

void *tree_sitter_layoutpoc_external_scanner_create(void) {
    Scanner *s = calloc(1, sizeof(Scanner)); return s;
}
void tree_sitter_layoutpoc_external_scanner_destroy(void *p) { free(p); }
unsigned tree_sitter_layoutpoc_external_scanner_serialize(void *p, char *buf) {
    Scanner *s = p; unsigned n = sizeof(uint16_t) + s->size * sizeof(uint32_t);
    if (n > TREE_SITTER_SERIALIZATION_BUFFER_SIZE) return 0;
    memcpy(buf, &s->size, sizeof(uint16_t));
    memcpy(buf + sizeof(uint16_t), s->cols, s->size * sizeof(uint32_t));
    return n;
}
void tree_sitter_layoutpoc_external_scanner_deserialize(void *p, const char *buf, unsigned len) {
    Scanner *s = p; s->size = 0;
    if (len == 0) return;
    memcpy(&s->size, buf, sizeof(uint16_t));
    memcpy(s->cols, buf + sizeof(uint16_t), s->size * sizeof(uint32_t));
}

// Compute the indent + first significant char of the next non-blank line.
// Returns false at EOF. (PoC: no comment/directive handling needed.)
static bool next_line_indent(TSLexer *lexer, uint32_t *col) {
    while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0)
        lexer->advance(lexer, true);
    if (lexer->lookahead == 0) return false;
    while (true) {
        if (lexer->lookahead == '\r') lexer->advance(lexer, true);
        if (lexer->lookahead == '\n') lexer->advance(lexer, true);
        uint32_t ind = 0;
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') { ind++; lexer->advance(lexer, true); }
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') continue;
        if (lexer->lookahead == 0) return false;
        *col = ind; return true;
    }
}

bool tree_sitter_layoutpoc_external_scanner_scan(void *p, TSLexer *lexer, const bool *valid) {
    Scanner *s = p;
    lexer->mark_end(lexer);  // zero-width baseline; re-marked only for real tokens
    if (valid[ERROR_SENTINEL]) return false;  // recovery: scanner stays out of the way

    // _open is grammar-driven: emitted right after `=`/`then`/`else`. Push a
    // context at the column of the body's first token (same-line or next-line).
    if (valid[OPEN]) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
        uint32_t col;
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            if (!next_line_indent(lexer, &col)) col = 0;
        } else {
            col = lexer->get_column(lexer);
        }
        if (s->size < MAXD) s->cols[s->size++] = col;
        lexer->result_symbol = OPEN;
        return true;  // zero-width (never marked end)
    }

    // Layout tokens only fire when a NEWLINE separates us from the next token.
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
    bool at_eof = (lexer->lookahead == 0);
    if (!at_eof && lexer->lookahead != '\n' && lexer->lookahead != '\r') return false; // same line

    if (at_eof) {
        // Close every open context at EOF, innermost first.
        if (valid[END] && s->size > 0) { s->size--; lexer->result_symbol = END; return true; }
        return false;
    }

    uint32_t col;
    if (!next_line_indent(lexer, &col)) {
        if (valid[END] && s->size > 0) { s->size--; lexer->result_symbol = END; return true; }
        return false;
    }

    uint32_t top = s->size > 0 ? s->cols[s->size - 1] : 0;
    // _end: a line dedented below the context closes it (one level per call;
    // multi-level handled by repeated calls). Gated on valid(END) — true when
    // the grammar expects an end OR on parse-error recovery (all-symbols-valid).
    if (valid[END] && s->size > 0 && col < top) { s->size--; lexer->result_symbol = END; return true; }
    // _semi: same indent = next item in the same block.
    if (valid[SEMI] && col == top) { lexer->result_symbol = SEMI; return true; }
    return false;
}
