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
    RECORD_OPEN,        // `{` record body — peeks `ident =`/`ident :`; suppressed for new/copy-update
    BLOCK_OPEN,         // newline-gated layout open for MODULE bodies (S_LAYOUT, closes via LAYOUT_END)
    TYPE_OPEN,          // newline-gated layout open for TYPE bodies (S_TYPEBODY — also closes before `with`)
    EXPR_OPEN,          // expression body (then/elif body, lambda, let-in value) — S_EXPR
    ELSE_OPEN,          // final-else body — S_EXPR, but SUPPRESSED when next token is `if`
                        //   (`else if` flattens to an elif clause, no nested else-body)
    FLOAT_TRAILING_DOT, // lexical: `1.` trailing-dot float (unrelated to layout)
} Sym;

// Sorts (all dedent-close via LAYOUT_END except as noted):
//   S_LAYOUT   generic decl body (let/member/module body)
//   S_TYPEBODY type body — ALSO closes before a `with` augmentation at body col
//   S_EXPR     expression body (then/elif/else/lambda/let-in value) — ALSO closes
//              before an inline `else`/`elif`/`in`. Crucially a DECL body
//              (S_LAYOUT) does NOT, so `module M =⏎ let f = if a then 1 else 0⏎
//              let g` closes only the then-body at `else`, not the module body.
//   S_MATCH    arm-list (closes on dedent below arm col; no semicolons)
//   S_BRACKET  [ / [| / { … explicit-close
typedef enum { S_LAYOUT, S_MATCH, S_BRACKET, S_TYPEBODY, S_EXPR } Sort;

// True for the dedent-closing layout sorts (decl body, type body, expr body).
static inline bool layoutish(uint8_t sort) { return sort == S_LAYOUT || sort == S_TYPEBODY || sort == S_EXPR; }

typedef struct { uint32_t col; uint8_t sort; } Ctx;
#define MAXD 512
typedef struct { Ctx stk[MAXD]; uint16_t n; } Scanner;

typedef struct { uint32_t col; uint8_t sort; } Ctx;

#define MAXD 512
typedef struct { Ctx stk[MAXD]; uint16_t n; } Scanner;

// Is there a match/try/function arm-list (S_MATCH) anywhere on the stack? Used to
// tell a real match-arm `|` (close the inline arm body first) from a UNION case
// separator `type X = A | B` (no arm-list — must NOT close the enclosing body).
static bool has_match_ctx(Scanner *s) {
    for (int i = (int)s->n - 1; i >= 0; i--) if (s->stk[i].sort == S_MATCH) return true;
    return false;
}

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
    uint32_t col = lexer->get_column(lexer);
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') { lexer->advance(lexer, true); col++; }
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        uint32_t nl;
        if (next_line_indent(lexer, &nl, NULL)) return nl;
        return 0;
    }
    return col;
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

    Ctx *top = s->n > 0 ? &s->stk[s->n - 1] : NULL;

    // ---- Grammar-driven OPENS (zero-width; push a context) --------------------
    // Checked BEFORE the float probe: these only peek (and restore position via
    // mark_end on return), whereas `scan_trailing_dot_float` advances over digits
    // DESTRUCTIVELY even on failure — running it first would corrupt the body
    // column for an inline body like `let a = 1` (peek would see the newline → 0).
    // When RECORD_OPEN is also valid we're right after a `{`; the RECORD_OPEN
    // block below owns that decision (field → record; own-line base → layout;
    // same-line `new`/`x with` → fall through to object-expr/copy-update). So the
    // generic LAYOUT_OPEN must NOT pre-empt it.
    if (valid[LAYOUT_OPEN] && !valid[RECORD_OPEN]) { push(s, S_LAYOUT, peek_body_col(lexer)); lexer->result_symbol = LAYOUT_OPEN; return true; }
    if (valid[EXPR_OPEN])   { push(s, S_EXPR,   peek_body_col(lexer)); lexer->result_symbol = EXPR_OPEN;   return true; }
    if (valid[ELSE_OPEN]) {
        // Final-else body. If it starts with `if`, this is `else if` — DON'T open a
        // nested else-body; return false so the grammar's flat `else if`→elif clause
        // matches (and its elif/else stay at the chain's level instead of nesting an
        // if inside the else-body, whose layout would over-close at a later `elif`).
        uint32_t col = peek_body_col(lexer);  // positions lexer at the body's first char
        if (lexer->lookahead == 'i') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == 'f') {
                lexer->advance(lexer, true);
                int32_t a = lexer->lookahead;
                bool word = (a >= 'a' && a <= 'z') || (a >= 'A' && a <= 'Z') ||
                            (a >= '0' && a <= '9') || a == '_' || a == '\'';
                if (!word) return false;  // `else if …` → flat elif clause
            }
        }
        push(s, S_EXPR, col); lexer->result_symbol = ELSE_OPEN; return true;
    }
    if (valid[MATCH_OPEN])  { push(s, S_MATCH,  peek_body_col(lexer)); lexer->result_symbol = MATCH_OPEN;  return true; }
    // BLOCK_OPEN: a type/module body is a layout ONLY when its members are on the
    // NEXT line (`type X =⏎ members`, `module M =⏎ decls`). For an inline body
    // (`type X = {…}` / `type X = int` / `module L = Lib` abbrev) it must NOT fire,
    // so the grammar's inline alternative matches. Newline-gated like BRACKET_OPEN,
    // but pushes S_LAYOUT (dedent-close via LAYOUT_END).
    if (valid[BLOCK_OPEN]) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            uint32_t col;
            if (next_line_indent(lexer, &col, NULL)) { push(s, S_LAYOUT, col); lexer->result_symbol = BLOCK_OPEN; return true; }
        }
        return false; // inline body — let the grammar's inline alternative match
    }
    // TYPE_OPEN: like BLOCK_OPEN but the context is S_TYPEBODY so a `with`
    // augmentation at the body column closes it (see the S_TYPEBODY boundary case).
    if (valid[TYPE_OPEN]) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            uint32_t col;
            if (next_line_indent(lexer, &col, NULL)) { push(s, S_TYPEBODY, col); lexer->result_symbol = TYPE_OPEN; return true; }
        }
        return false; // inline type body (record/alias/inline DU) — let it match
    }
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

    // RECORD_OPEN: a `{` record body whose first field starts here (same line as
    // `{`, or the next line). Peeks to confirm a field shape (`ident =` for a
    // record_field, `ident :` for a record_type_field) and captures the field
    // column. SUPPRESSED (return false → fall through) for `{ new … }` (object
    // expression) and `{ base with … }` (copy-update), whose first word is NOT
    // followed by `=`/`:` — letting the grammar's other `{`-branches match.
    if (valid[RECORD_OPEN]) {
        uint32_t col = lexer->get_column(lexer);
        bool nl = false;
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') { lexer->advance(lexer, true); col++; }
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            nl = true;
            if (!next_line_indent(lexer, &col, NULL)) return false;
        }
        int32_t c = lexer->lookahead;
        bool ok = false;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '`') {
            if (c == '`') {                       // ``quoted name``
                lexer->advance(lexer, true);
                if (lexer->lookahead == '`') {
                    lexer->advance(lexer, true);
                    while (lexer->lookahead != '`' && lexer->lookahead != '\n' &&
                           lexer->lookahead != '\r' && lexer->lookahead != 0) lexer->advance(lexer, true);
                    if (lexer->lookahead == '`') { lexer->advance(lexer, true); if (lexer->lookahead == '`') lexer->advance(lexer, true); }
                }
            } else {
                while (1) {
                    int32_t ch = lexer->lookahead;
                    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') || ch == '_' || ch == '\'') lexer->advance(lexer, true);
                    else break;
                }
            }
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
            int32_t sep = lexer->lookahead;
            if (sep == '=' || sep == ':') ok = true;   // record_field / record_type_field
        }
        if (ok) { push(s, S_BRACKET, col); lexer->result_symbol = RECORD_OPEN; return true; }
        // Not a field. If the base is on its OWN line after `{` (`{⏎ base with ⏎
        // field }` copy-update), open a layout at the base's column. Otherwise
        // (same-line `{ new …}` / `{ x with …}`) fall through so object-expression
        // / inline copy-update match.
        if (nl && valid[LAYOUT_OPEN]) { push(s, S_LAYOUT, col); lexer->result_symbol = LAYOUT_OPEN; return true; }
        return false;
    }

    // Lexical trailing-dot float (mid-line). After the opens. The probe advances
    // over digits DESTRUCTIVELY even on failure, which would corrupt the position
    // for the layout logic below (e.g. `{ X = abs 3 }` would then see `}` and emit
    // a spurious BRACKET_CLOSE, ending the field at `abs`). So: only probe when the
    // current-line char is a digit, and if it's a plain int (not `1.`) RETURN
    // FALSE — a mid-line digit never needs a layout token, so resetting to
    // mark_end (and letting tree-sitter lex the int) is correct. At a line
    // boundary the first char is a newline, so we fall through to the layout logic.
    if (valid[FLOAT_TRAILING_DOT]) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
        if (lexer->lookahead >= '0' && lexer->lookahead <= '9') {
            if (scan_trailing_dot_float(lexer)) return true;
            return false;
        }
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
            if (!closer && c == '@') {            // `@>` / `@@>` code-quotation close
                lexer->advance(lexer, true);
                if (lexer->lookahead == '>') closer = true;
                else if (lexer->lookahead == '@') { lexer->advance(lexer, true); if (lexer->lookahead == '>') closer = true; }
            }
            if (!closer && c == '|') {            // `|]` array / `|}` anon-record close
                lexer->advance(lexer, true);
                int32_t c1 = lexer->lookahead;
                if (is_close_bracket(c1)) closer = true;
                else if (c1 != '>' && c1 != '|' && top && layoutish(top->sort) && valid[LAYOUT_END] && has_match_ctx(s)) {
                    // A bare same-line `|` is the next match arm; close the inline
                    // arm body first (`function | 0 -> "a" | _ -> "b"`). Gated on an
                    // S_MATCH being on the stack so a UNION case separator
                    // `type X = A | B` (no arm-list) does NOT close the enclosing body.
                    s->n--; lexer->result_symbol = LAYOUT_END; return true;
                }
            }
            if (closer && top) {
                if (layoutish(top->sort)  && valid[LAYOUT_END])    { s->n--; lexer->result_symbol = LAYOUT_END;    return true; }
                if (top->sort == S_MATCH   && valid[MATCH_END])     { s->n--; lexer->result_symbol = MATCH_END;     return true; }
                if (top->sort == S_BRACKET && valid[BRACKET_CLOSE]) { s->n--; lexer->result_symbol = BRACKET_CLOSE; return true; }
            }
            // A same-line continuation keyword ends an inline layout body so it
            // attaches to the enclosing construct rather than being absorbed:
            //   `let … = e in body`  ·  `if c then a else b`  ·  `if c then a elif …`
            //   `try e with …` / `… finally …`.
            // Gated on valid[LAYOUT_END] (true only when a layout body is open and
            // complete) — so the `in` of `for x in xs` (no open body) is unaffected.
            // Only an EXPRESSION body (S_EXPR) closes before an inline `else`/
            // `elif`/`in` — and it's the INNERMOST one, so exactly the then-branch
            // (or let-in value) closes; the enclosing DECL body (S_LAYOUT module/
            // let) does NOT, which stops the over-close that ate the module body in
            // `module M =⏎ let f = if a then 1 else 0⏎ let g`. Not gated on
            // valid[LAYOUT_END] (GLR keeps it true for outer layouts).
            if (top && top->sort == S_EXPR && c >= 'a' && c <= 'z') {
                char w[10]; size_t n = 0; int32_t look = lexer->lookahead;
                while (n < 9 && ((look >= 'a' && look <= 'z') || (look >= 'A' && look <= 'Z') ||
                                 (look >= '0' && look <= '9') || look == '_' || look == '\'')) {
                    w[n++] = (char)look; lexer->advance(lexer, true); look = lexer->lookahead;
                }
                w[n] = '\0';
                if (!strcmp(w, "else") || !strcmp(w, "elif") || !strcmp(w, "in")) {
                    s->n--; lexer->result_symbol = LAYOUT_END; return true;
                }
            }
            return false; // other same-line content: layout doesn't apply
        }
    }

    // ---- Line boundary / EOF --------------------------------------------------
    bool at_eof = (lexer->lookahead == 0);
    if (at_eof) {
        if (valid[BRACKET_CLOSE] && top && top->sort == S_BRACKET) { s->n--; lexer->result_symbol = BRACKET_CLOSE; return true; }
        if (valid[MATCH_END]    && top && top->sort == S_MATCH)    { s->n--; lexer->result_symbol = MATCH_END;    return true; }
        if (valid[LAYOUT_END]   && top && layoutish(top->sort))   { s->n--; lexer->result_symbol = LAYOUT_END;   return true; }
        return false;
    }

    uint32_t col; int32_t first = 0;
    if (!next_line_indent(lexer, &col, &first)) {           // EOF after trailing blanks
        if (valid[BRACKET_CLOSE] && top && top->sort == S_BRACKET) { s->n--; lexer->result_symbol = BRACKET_CLOSE; return true; }
        if (valid[MATCH_END]    && top && top->sort == S_MATCH)    { s->n--; lexer->result_symbol = MATCH_END;    return true; }
        if (valid[LAYOUT_END]   && top && layoutish(top->sort))   { s->n--; lexer->result_symbol = LAYOUT_END;   return true; }
        return false;
    }
    if (!top) return false;

    // A leading `|` is a match arm UNLESS it is `|]` (array close) or `|}` (anon
    // record close). We can't cheaply peek the 2nd char here (next_line_indent
    // already advanced), so treat `|` as an arm marker; the bracket cases are
    // handled by BRACKET_CLOSE above/below via valid-gating.
    bool bar_arm = (first == '|');

    // Leading-infix continuation: a line that BEGINS with an infix operator
    // continues the previous expression (F#'s leading-operator rule), so NO
    // layout token fires — even when the operator sits at or below the body
    // column (`|>`/`<|`/`>>` pipe chains, `+`/`*`/… arithmetic, `::` cons).
    // `|` alone is a match arm (not infix); only `|>`/`||` are. `&`/`:` count
    // only doubled. Unary-capable leads (`-` `+` `!` `~` `@`) are excluded.
    {
        int32_t c0 = first;
        if (c0 == '|' || c0 == '<' || c0 == '>' || c0 == '=' ||
            c0 == '*' || c0 == '/' || c0 == '%' || c0 == '^' || c0 == '&' || c0 == ':') {
            lexer->advance(lexer, true);
            int32_t c1 = lexer->lookahead;
            bool infix = false;
            if (c0 == '|')      infix = (c1 == '>' || c1 == '|');           // |> ||
            else if (c0 == '&') infix = (c1 == '&');                        // &&
            else if (c0 == ':') infix = (c1 == ':' || c1 == '>' || c1 == '?'); // :: :> :?
            else                infix = true;                              // = < > * / % ^
            if (infix) return false;
        }
    }

    switch (top->sort) {
        case S_BRACKET:
            if (valid[BRACKET_CLOSE] && (is_close_bracket(first) || first == '|')) { s->n--; lexer->result_symbol = BRACKET_CLOSE; return true; }
            // Same blocker as LAYOUT_SEMI: a CE statement separator must not fire
            // before `else`/`elif`/… — otherwise `if c then return a`⏎`else …`
            // inside a CE detaches the else (banked-fix #3, in the new model).
            if (valid[BRACKET_SEMI] && col == top->col && !semi_blocked(lexer, first)) { lexer->result_symbol = BRACKET_SEMI; return true; }
            return false;
        case S_MATCH:
            // Close the arm-list when a line dedents below the arm column, or sits
            // at the arm column but does NOT start a new `|` arm.
            if (valid[MATCH_END] && (col < top->col || (col == top->col && !bar_arm))) { s->n--; lexer->result_symbol = MATCH_END; return true; }
            return false;
        case S_LAYOUT:
        case S_TYPEBODY:
        case S_EXPR:
            if (valid[LAYOUT_END] && col < top->col) { s->n--; lexer->result_symbol = LAYOUT_END; return true; }
            // A `with` type-augmentation aligned AT the body column closes the
            // TYPE body (S_TYPEBODY only) so the augmentation attaches:
            //   type D =⏎    | A⏎    | B⏎    with⏎        member …
            // A module body (S_LAYOUT) at == col must NOT close before `with`.
            if (top->sort == S_TYPEBODY && valid[LAYOUT_END] && col == top->col && first == 'w') {
                lexer->advance(lexer, true);            // 'w'
                if (lexer->lookahead == 'i') { lexer->advance(lexer, true);
                if (lexer->lookahead == 't') { lexer->advance(lexer, true);
                if (lexer->lookahead == 'h') { lexer->advance(lexer, true);
                    int32_t a = lexer->lookahead;
                    bool word = (a >= 'a' && a <= 'z') || (a >= 'A' && a <= 'Z') ||
                                (a >= '0' && a <= '9') || a == '_' || a == '\'';
                    if (!word) { s->n--; lexer->result_symbol = LAYOUT_END; return true; }
                }}}
            }
            if (valid[LAYOUT_SEMI] && col == top->col && !semi_blocked(lexer, first)) { lexer->result_symbol = LAYOUT_SEMI; return true; }
            return false;
    }
    return false;
}
