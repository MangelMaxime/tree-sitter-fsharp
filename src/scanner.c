#include "tree_sitter/parser.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Uniform-layout external scanner for F# (rewrite — see LAYOUT_REWRITE.md).
//
// Model (from tree-sitter-haskell, validated by spike/poc):
//   * OPENS are GRAMMAR-driven: the grammar emits a zero-width open token right
//     after the layout keyword (`=` / `then` / `->` / `do` / `with` / `[` / `{`).
//     The scanner reacts by pushing a context at the body's first-token column.
//     It does NOT guess which construct opens.
//   * CLOSES / SEPARATORS are SCANNER-driven by column comparison, but GATED on
//     `valid(...)` — true when the grammar expects that token OR on parse-error
//     recovery (all-symbols-valid). The scanner decides only WHETHER to close at
//     this indent, never WHICH construct.
//   * One context stack, three sorts. Multi-level dedent = one close per scan
//     call; tree-sitter re-invokes at the same (mark_end-restored) position.
//
// Token enum MUST match the `externals:` order in grammar.js.
// ============================================================================
typedef enum {
    ERROR_SENTINEL,     // unused in grammar; valid only on all-symbols-valid (recovery)
    LAYOUT_OPEN,        // generic layout open (Decl/Then/Do/Let bodies)
    LAYOUT_SEMI,        // generic layout separator (next line at == body col)
    LAYOUT_END,         // generic layout close (next line dedents below body col)
    MATCH_OPEN,         // match/try/function arm-list open (after `with`/`function`/`->`)
    MATCH_END,          // close arm-list (dedent below arm col, or == col & not `|`)
    BRACKET_OPEN,       // [ / [| / { block body on its own line(s)
    BRACKET_SEMI,       // newline-aligned element/field separator
    BRACKET_CLOSE,      // ] / |] / } closing a block bracket
    FLOAT_TRAILING_DOT, // lexical: `1.` trailing-dot float (unrelated to layout)
} Sym;

typedef enum { S_LAYOUT, S_MATCH, S_BRACKET } Sort;

typedef struct { uint32_t col; uint8_t sort; } Ctx;

#define MAXD 512
typedef struct { Ctx stk[MAXD]; uint16_t n; } Scanner;

void *tree_sitter_fsharp_external_scanner_create(void) { return calloc(1, sizeof(Scanner)); }
void tree_sitter_fsharp_external_scanner_destroy(void *p) { free(p); }

unsigned tree_sitter_fsharp_external_scanner_serialize(void *p, char *buf) {
    Scanner *s = p;
    unsigned size = sizeof(uint16_t) + (unsigned)s->n * sizeof(Ctx);
    if (size > TREE_SITTER_SERIALIZATION_BUFFER_SIZE) return 0;
    memcpy(buf, &s->n, sizeof(uint16_t));
    memcpy(buf + sizeof(uint16_t), s->stk, (size_t)s->n * sizeof(Ctx));
    return size;
}
void tree_sitter_fsharp_external_scanner_deserialize(void *p, const char *buf, unsigned len) {
    Scanner *s = p; s->n = 0;
    if (len == 0) return;
    memcpy(&s->n, buf, sizeof(uint16_t));
    memcpy(s->stk, buf + sizeof(uint16_t), (size_t)s->n * sizeof(Ctx));
}

static void push(Scanner *s, uint8_t sort, uint32_t col) {
    if (s->n < MAXD) { s->stk[s->n].sort = sort; s->stk[s->n].col = col; s->n++; }
}

// Compute the indent + first significant char of the NEXT non-blank, non-comment
// line. Returns false at EOF. Reused verbatim from the old scanner (handles `//`
// line comments, `(* *)` nested block comments, and `#if/#elif/#else/#endif`
// conditional-compilation lines which are extras transparent to the offside rule).
static bool next_line_indent(TSLexer *lexer, uint32_t *col, int32_t *first) {
    while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0)
        lexer->advance(lexer, true);
    if (lexer->lookahead == 0) return false;
    while (true) {
        if (lexer->lookahead == '\r') lexer->advance(lexer, true);
        if (lexer->lookahead == '\n') lexer->advance(lexer, true);
        uint32_t indent = 0;
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') { indent++; lexer->advance(lexer, true); }
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') continue;
        if (lexer->lookahead == '/') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == '/') {
                while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0)
                    lexer->advance(lexer, true);
                if (lexer->lookahead == 0) return false;
                continue;
            }
            if (first) *first = '/'; *col = indent; return true;
        }
        if (lexer->lookahead == '(') {
            lexer->advance(lexer, true);
            if (lexer->lookahead != '*') { if (first) *first = '('; *col = indent; return true; }
            lexer->advance(lexer, true);
            int depth = 1;
            while (depth > 0) {
                if (lexer->lookahead == 0) return false;
                if (lexer->lookahead == '(') { lexer->advance(lexer, true); if (lexer->lookahead == '*') { depth++; lexer->advance(lexer, true); } }
                else if (lexer->lookahead == '*') { lexer->advance(lexer, true); if (lexer->lookahead == ')') { depth--; lexer->advance(lexer, true); } }
                else lexer->advance(lexer, true);
            }
            while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0) lexer->advance(lexer, true);
            if (lexer->lookahead == 0) return false;
            continue;
        }
        if (lexer->lookahead == 0) return false;
        if (lexer->lookahead == '#') {
            lexer->advance(lexer, true);
            char w[6]; size_t wi = 0;
            while (wi < 5 && lexer->lookahead >= 'a' && lexer->lookahead <= 'z') { w[wi++] = (char)lexer->lookahead; lexer->advance(lexer, true); }
            w[wi] = '\0';
            if (strcmp(w, "if") == 0 || strcmp(w, "elif") == 0 || strcmp(w, "else") == 0 || strcmp(w, "endif") == 0) {
                while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0) lexer->advance(lexer, true);
                if (lexer->lookahead == 0) return false;
                continue;
            }
            if (first) *first = '#'; *col = indent; return true;
        }
        if (first) *first = lexer->lookahead; *col = indent; return true;
    }
}

// Peek the column where a body's first token sits: skip horizontal whitespace;
// if at a newline the body is on the next line (use next_line_indent), else it
// is inline on the current line (use get_column). This collapses F#'s old
// inline-vs-own-line distinction into one decision.
static uint32_t peek_body_col(TSLexer *lexer) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        uint32_t col;
        if (next_line_indent(lexer, &col, NULL)) return col;
        return 0;
    }
    return lexer->get_column(lexer);
}

// Match a trailing-dot float literal (`1.`, `20.`) at the current position.
// (Verbatim from the old scanner — lexical, independent of layout.)
static bool scan_trailing_dot_float(TSLexer *lexer) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
    if (lexer->lookahead < '0' || lexer->lookahead > '9') return false;
    lexer->advance(lexer, false);
    while ((lexer->lookahead >= '0' && lexer->lookahead <= '9') || lexer->lookahead == '_') lexer->advance(lexer, false);
    if (lexer->lookahead != '.') return false;
    lexer->advance(lexer, false);
    int32_t after = lexer->lookahead;
    if (after == '.' || (after >= '0' && after <= '9') || after == 'e' || after == 'E') return false;
    lexer->mark_end(lexer);
    lexer->result_symbol = FLOAT_TRAILING_DOT;
    return true;
}

static bool is_close_bracket(int32_t c) { return c == ']' || c == '}'; }

// A line whose first significant char/word does NOT start a new statement, so a
// LAYOUT_SEMI before it would be wrong (it continues the current construct):
//   * closing delimiters `)` `]` `}` and `|` (match arm / `|>` pipe);
//   * continuation keywords of an enclosing if/try/let (`else`/`elif`/`then`/
//     `with`/`finally`/`in`/`and`).
// `first` is the leading char (from next_line_indent, where lookahead==first).
static bool semi_blocked(TSLexer *lexer, int32_t first) {
    if (first == ')' || first == ']' || first == '}' || first == '|') return true;
    if (first >= 'a' && first <= 'z') {
        char w[10]; size_t n = 0; int32_t look = lexer->lookahead;
        while (n < 9 && ((look >= 'a' && look <= 'z') || (look >= 'A' && look <= 'Z') ||
                         (look >= '0' && look <= '9') || look == '_' || look == '\'')) {
            w[n++] = (char)look; lexer->advance(lexer, true); look = lexer->lookahead;
        }
        w[n] = '\0';
        if (!strcmp(w, "else") || !strcmp(w, "elif") || !strcmp(w, "then") ||
            !strcmp(w, "with") || !strcmp(w, "finally") || !strcmp(w, "in") || !strcmp(w, "and"))
            return true;
    }
    return false;
}

bool tree_sitter_fsharp_external_scanner_scan(void *p, TSLexer *lexer, const bool *valid) {
    Scanner *s = p;
    lexer->mark_end(lexer);                       // zero-width baseline; re-marked only by real (FLOAT) tokens
    if (valid[ERROR_SENTINEL]) return false;      // parse-error recovery: stay out of tree-sitter's way

    // Lexical trailing-dot float (mid-line); handle before any layout logic.
    if (valid[FLOAT_TRAILING_DOT] && scan_trailing_dot_float(lexer)) return true;

    Ctx *top = s->n > 0 ? &s->stk[s->n - 1] : NULL;

    // ---- Grammar-driven OPENS (zero-width; push a context) --------------------
    if (valid[LAYOUT_OPEN]) { push(s, S_LAYOUT, peek_body_col(lexer)); lexer->result_symbol = LAYOUT_OPEN; return true; }
    if (valid[MATCH_OPEN])  { push(s, S_MATCH,  peek_body_col(lexer)); lexer->result_symbol = MATCH_OPEN;  return true; }
    if (valid[BRACKET_OPEN]) {
        // Only open a bracket CONTEXT for the block form (body on the next line).
        // Inline `[ a; b ]` uses literal `;` + `]` and needs no scanner context.
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            uint32_t col;
            if (next_line_indent(lexer, &col, NULL)) { push(s, S_BRACKET, col); lexer->result_symbol = BRACKET_OPEN; return true; }
        }
        return false; // inline bracket
    }

    // ---- Mid-line closes ------------------------------------------------------
    // An inline body / arm-list / block bracket can close on the SAME line before
    // a closing delimiter `)` `]` `}` (and `|]`/`|}`):
    //   (fun x -> body)   (match v with … | _ -> k)   [ … ]   { … }
    // Fire one close per call (gated by valid + the top sort); tree-sitter
    // re-invokes for multi-level (arm body, then arm-list, then `)`).
    {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
        int32_t c = lexer->lookahead;
        if (c != '\n' && c != '\r' && c != 0) {
            bool closer = (c == ')' || c == ']' || c == '}');
            if (!closer && c == '|') {            // `|]` array / `|}` anon-record close
                lexer->advance(lexer, true);
                closer = is_close_bracket(lexer->lookahead);
            }
            if (closer && top) {
                if (top->sort == S_LAYOUT  && valid[LAYOUT_END])    { s->n--; lexer->result_symbol = LAYOUT_END;    return true; }
                if (top->sort == S_MATCH   && valid[MATCH_END])     { s->n--; lexer->result_symbol = MATCH_END;     return true; }
                if (top->sort == S_BRACKET && valid[BRACKET_CLOSE]) { s->n--; lexer->result_symbol = BRACKET_CLOSE; return true; }
            }
            return false; // other same-line content: layout doesn't apply
        }
    }

    // ---- Line boundary / EOF --------------------------------------------------
    bool at_eof = (lexer->lookahead == 0);
    if (at_eof) {
        if (valid[BRACKET_CLOSE] && top && top->sort == S_BRACKET) { s->n--; lexer->result_symbol = BRACKET_CLOSE; return true; }
        if (valid[MATCH_END]    && top && top->sort == S_MATCH)    { s->n--; lexer->result_symbol = MATCH_END;    return true; }
        if (valid[LAYOUT_END]   && top && top->sort == S_LAYOUT)   { s->n--; lexer->result_symbol = LAYOUT_END;   return true; }
        return false;
    }

    uint32_t col; int32_t first = 0;
    if (!next_line_indent(lexer, &col, &first)) {           // EOF after trailing blanks
        if (valid[BRACKET_CLOSE] && top && top->sort == S_BRACKET) { s->n--; lexer->result_symbol = BRACKET_CLOSE; return true; }
        if (valid[MATCH_END]    && top && top->sort == S_MATCH)    { s->n--; lexer->result_symbol = MATCH_END;    return true; }
        if (valid[LAYOUT_END]   && top && top->sort == S_LAYOUT)   { s->n--; lexer->result_symbol = LAYOUT_END;   return true; }
        return false;
    }
    if (!top) return false;

    // A leading `|` is a match arm UNLESS it is `|]` (array close) or `|}` (anon
    // record close). We can't cheaply peek the 2nd char here (next_line_indent
    // already advanced), so treat `|` as an arm marker; the bracket cases are
    // handled by BRACKET_CLOSE above/below via valid-gating.
    bool bar_arm = (first == '|');

    switch (top->sort) {
        case S_BRACKET:
            if (valid[BRACKET_CLOSE] && (is_close_bracket(first) || first == '|')) { s->n--; lexer->result_symbol = BRACKET_CLOSE; return true; }
            if (valid[BRACKET_SEMI] && col == top->col) { lexer->result_symbol = BRACKET_SEMI; return true; }
            return false;
        case S_MATCH:
            // Close the arm-list when a line dedents below the arm column, or sits
            // at the arm column but does NOT start a new `|` arm.
            if (valid[MATCH_END] && (col < top->col || (col == top->col && !bar_arm))) { s->n--; lexer->result_symbol = MATCH_END; return true; }
            return false;
        case S_LAYOUT:
            if (valid[LAYOUT_END] && col < top->col) { s->n--; lexer->result_symbol = LAYOUT_END; return true; }
            if (valid[LAYOUT_SEMI] && col == top->col && !semi_blocked(lexer, first)) { lexer->result_symbol = LAYOUT_SEMI; return true; }
            return false;
    }
    return false;
}
