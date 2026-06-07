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
    INTERP_STRING_TEXT,   // text chunk in $"…"   (external so // isn't a comment)
    INTERP_VERBATIM_TEXT, // text chunk in $@"…" / @$"…"
    INTERP_TRIPLE_TEXT,   // text chunk in $"""…"""
    FOR_OPEN,             // `for … do` body open; suppressed for query-CE operators
    CTOR_ATTR,            // zero-width: attribute on a primary ctor — only when `[<…>]+ (` follows
    TRY_OPEN,             // try/finally body open (S_TRY) — closes before `with`/`finally`
    LABEL_ATTR,           // zero-width: attribute on a labelled param — only when `[<…>]+ ident:` follows
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
//   S_DECL     module/source declaration body — like S_LAYOUT, but a
//              `_layout_semi` is NEVER emitted before a declaration keyword
//              (`let`/`type`/`module`/…). A module body is `repeat(_token)`, so a
//              bare-expression `_token` must not extend into the next declaration
//              as a `sequence_expression` (`ignore x⏎ let y = …` is two decls,
//              whereas a function body — S_LAYOUT — DOES sequence `let` as let-in).
//   S_TRY      try / finally body — like S_EXPR, but ALSO closes before an inline
//              `with`/`finally` (a dedicated sort so the close is try-specific and
//              doesn't fire for a `match … with` inside an enclosing expr body).
typedef enum { S_LAYOUT, S_MATCH, S_BRACKET, S_TYPEBODY, S_EXPR, S_DECL, S_TRY } Sort;

// True for the dedent-closing layout sorts (decl body, type body, expr body, module body, try body).
static inline bool layoutish(uint8_t sort) { return sort == S_LAYOUT || sort == S_TYPEBODY || sort == S_EXPR || sort == S_DECL || sort == S_TRY; }

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

// Consume one identifier segment at the lookahead — a plain ident
// (`Foo`/`foo'`/`x9`) or a ``quoted name``. Caller ensures the first char is an
// identifier start. Used by the RECORD_OPEN field peek so a qualified field name
// (`FunctionDef.Name = …`) is recognised as a field, not a copy-update base.
static void peek_name_segment(TSLexer *lexer) {
    if (lexer->lookahead == '`') {                // ``quoted name``
        lexer->advance(lexer, true);
        if (lexer->lookahead == '`') {
            lexer->advance(lexer, true);
            while (lexer->lookahead != '`' && lexer->lookahead != '\n' &&
                   lexer->lookahead != '\r' && lexer->lookahead != 0) lexer->advance(lexer, true);
            if (lexer->lookahead == '`') { lexer->advance(lexer, true); if (lexer->lookahead == '`') lexer->advance(lexer, true); }
        }
        return;
    }
    while (1) {
        int32_t ch = lexer->lookahead;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '\'') lexer->advance(lexer, true);
        else break;
    }
}

static bool is_name_start(int32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '`';
}

// A line whose first significant char/word does NOT start a new statement, so a
// LAYOUT_SEMI before it would be wrong (it continues the current construct):
//   * closing delimiters `)` `]` `}` and `|` (match arm / `|>` pipe);
//   * a leading `,` — a tuple / argument-list separator (`f(⏎ a⏎ , b)`), never a
//     statement start;
//   * continuation keywords of an enclosing if/try/let (`else`/`elif`/`then`/
//     `with`/`finally`/`in`/`and`).
// `first` is the leading char (from next_line_indent, where lookahead==first).
static bool semi_blocked(TSLexer *lexer, int32_t first) {
    if (first == ')' || first == ']' || first == '}' || first == '|' || first == ',') return true;
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

// Consume a maximal TEXT run of an interpolated string, stopping (without
// consuming) at the next structural token: `{` interpolation, closing quote, or
// `%` printf/percent. Doubled braces `{{`/`}}` and (per kind) escapes/quotes are
// part of the text. `mark_end` is advanced only over confirmed text, so an
// over-peeked terminator is excluded from the token. Returns true iff ≥1 char of
// text was consumed; on false the caller returns false and tree-sitter lexes the
// structural token itself (resuming from the pre-scan position).
//
// Done in the external scanner — which runs BEFORE extra-skipping — so a leading
// `//` is consumed as text instead of being lexed as a `line_comment` extra.
typedef enum { TX_STRING, TX_VERBATIM, TX_TRIPLE } TextKind;

static bool scan_interp_text(TSLexer *lexer, TextKind kind) {
    bool consumed = false;
    for (;;) {
        int32_t c = lexer->lookahead;
        if (c == 0) break;                 // EOF
        if (c == '%') break;               // percent / printf format terminator
        if (c == '{' || c == '}') {        // single brace = terminator; doubled = text
            lexer->advance(lexer, false);
            if (lexer->lookahead == c) { lexer->advance(lexer, false); consumed = true; lexer->mark_end(lexer); continue; }
            break;                         // single brace: stop (mark_end is before it)
        }
        if (c == '"') {
            if (kind == TX_STRING) break;  // closing quote
            if (kind == TX_VERBATIM) {     // "" is an escaped quote (text), lone " closes
                lexer->advance(lexer, false);
                if (lexer->lookahead == '"') { lexer->advance(lexer, false); consumed = true; lexer->mark_end(lexer); continue; }
                break;
            }
            // TX_TRIPLE: """ closes; a lone " or "" (not part of """) is text.
            lexer->advance(lexer, false);
            if (lexer->lookahead == '"') {
                lexer->advance(lexer, false);
                if (lexer->lookahead == '"') break;   // """ closer (mark_end before 1st ")
                consumed = true; lexer->mark_end(lexer); continue;   // "" text
            }
            consumed = true; lexer->mark_end(lexer); continue;       // lone " text
        }
        if (c == '\\' && kind == TX_STRING) {           // escape: \\ , \n , \uXXXX … (lenient)
            lexer->advance(lexer, false);
            if (lexer->lookahead != 0) lexer->advance(lexer, false);
            consumed = true; lexer->mark_end(lexer); continue;
        }
        lexer->advance(lexer, false);                   // ordinary text char (incl. newline)
        consumed = true; lexer->mark_end(lexer);
    }
    return consumed;
}

// In an S_DECL (module/source) body, a line beginning with one of these keywords
// starts a fresh declaration `_token` — never a continuation of the previous
// bare-expression statement. Blocking LAYOUT_SEMI before them stops the previous
// `_token` from absorbing the declaration into a `sequence_expression` (which then
// fails when, e.g., the `let` has no continuation). `first` is the leading char.
static bool decl_starter(TSLexer *lexer, int32_t first) {
    if (first < 'a' || first > 'z') return false;
    char w[12]; size_t n = 0; int32_t look = lexer->lookahead;
    while (n < 11 && ((look >= 'a' && look <= 'z') || (look >= 'A' && look <= 'Z') ||
                      (look >= '0' && look <= '9') || look == '_' || look == '\'')) {
        w[n++] = (char)look; lexer->advance(lexer, true); look = lexer->lookahead;
    }
    w[n] = '\0';
    return !strcmp(w, "let") || !strcmp(w, "use") || !strcmp(w, "do") ||
           !strcmp(w, "type") || !strcmp(w, "module") || !strcmp(w, "open") ||
           !strcmp(w, "exception") || !strcmp(w, "namespace") || !strcmp(w, "inline") ||
           !strcmp(w, "member") || !strcmp(w, "static") || !strcmp(w, "val") ||
           !strcmp(w, "abstract") || !strcmp(w, "inherit") || !strcmp(w, "override") ||
           !strcmp(w, "default") || !strcmp(w, "interface");
}

// Consume one or more consecutive `[<…>]` attributes, leaving the lexer at the
// first non-whitespace char AFTER them. Skips strings (which may contain `>]`).
// Returns false if not actually at `[<`. Used by the CTOR_ATTR / LABEL_ATTR peeks.
static bool skip_bracket_attrs(TSLexer *lexer) {
    if (lexer->lookahead != '[') return false;
    lexer->advance(lexer, true);
    if (lexer->lookahead != '<') return false;
    lexer->advance(lexer, true);
    for (;;) {
        for (;;) {                                   // scan to the closing `>]`
            int32_t c = lexer->lookahead;
            if (c == 0) return false;
            if (c == '"') {                          // skip a string
                lexer->advance(lexer, true);
                while (lexer->lookahead != '"' && lexer->lookahead != 0) {
                    if (lexer->lookahead == '\\') lexer->advance(lexer, true);
                    if (lexer->lookahead != 0) lexer->advance(lexer, true);
                }
                if (lexer->lookahead == '"') lexer->advance(lexer, true);
                continue;
            }
            if (c == '>') { lexer->advance(lexer, true); if (lexer->lookahead == ']') { lexer->advance(lexer, true); break; } continue; }
            lexer->advance(lexer, true);
        }
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
               lexer->lookahead == '\n' || lexer->lookahead == '\r') lexer->advance(lexer, true);
        if (lexer->lookahead == '[') {               // another `[<…>]`?
            lexer->advance(lexer, true);
            if (lexer->lookahead == '<') { lexer->advance(lexer, true); continue; }
            return false;
        }
        break;
    }
    return true;
}

bool tree_sitter_fsharp_external_scanner_scan(void *p, TSLexer *lexer, const bool *valid) {
    Scanner *s = p;
    lexer->mark_end(lexer);                       // zero-width baseline; re-marked only by real (FLOAT) tokens
    if (valid[ERROR_SENTINEL]) return false;      // parse-error recovery: stay out of tree-sitter's way

    // ---- Interpolated-string text (lexical; before any layout logic) ----------
    // When a text symbol is valid we are inside a string: no layout token applies.
    // Consume the text run, or return false at a structural char so tree-sitter
    // lexes the `{`/`"`/`%` itself.
    if (valid[INTERP_STRING_TEXT])   { bool ok = scan_interp_text(lexer, TX_STRING);   if (ok) lexer->result_symbol = INTERP_STRING_TEXT;   return ok; }
    if (valid[INTERP_VERBATIM_TEXT]) { bool ok = scan_interp_text(lexer, TX_VERBATIM); if (ok) lexer->result_symbol = INTERP_VERBATIM_TEXT; return ok; }
    if (valid[INTERP_TRIPLE_TEXT])   { bool ok = scan_interp_text(lexer, TX_TRIPLE);   if (ok) lexer->result_symbol = INTERP_TRIPLE_TEXT;   return ok; }

    Ctx *top = s->n > 0 ? &s->stk[s->n - 1] : NULL;

    // CTOR_ATTR (zero-width): valid only in the primary-constructor position, after
    // a type name. Look ahead past one or more `[<…>]` attributes; emit ONLY when a
    // `(` (the constructor params) follows. This distinguishes a ctor attribute
    // (`type T [<ParamObject>] (…)`) from a standalone attribute on the NEXT
    // declaration (`[<Measure>] type cm`⏎`[<Measure>] type kg`, where `[<Measure>]`
    // is followed by `type`). Zero-width, so the attributes/`(` are re-lexed after.
    if (valid[CTOR_ATTR]) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
               lexer->lookahead == '\n' || lexer->lookahead == '\r') lexer->advance(lexer, true);
        if (!skip_bracket_attrs(lexer)) return false;
        if (lexer->lookahead == '(') { lexer->result_symbol = CTOR_ATTR; return true; }
        return false;
    }

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
    // FOR_OPEN: the body of a `for … do`. Like LAYOUT_OPEN but SUPPRESSED when the
    // body would not indent past the enclosing context — that's a query-CE
    // `for x in xs do`⏎`where …`/`select …`, where the operators sit at the CE
    // column, not in an indented loop body. Suppressing keeps the for body empty so
    // the operators stay `query_operator` CE siblings (a real loop body always
    // indents past the `for`, so this never suppresses a genuine body). Dedicated
    // (not LAYOUT_OPEN) so only for-do bodies get this rule.
    if (valid[FOR_OPEN]) {
        uint32_t bc = peek_body_col(lexer);
        if (top && bc <= top->col) {
            // No indented body — query-CE for-clause. Emit the enclosing
            // statement separator so the following `where`/`select`/`join`/… is a
            // SIBLING `query_operator`, not absorbed as an application argument of
            // the empty-body `for`. (peek_body_col advanced, but the separator is
            // zero-width at the mark_end baseline, so over-advance is discarded.)
            if (top->sort == S_BRACKET && valid[BRACKET_SEMI]) { lexer->result_symbol = BRACKET_SEMI; return true; }
            if (layoutish(top->sort) && valid[LAYOUT_SEMI])    { lexer->result_symbol = LAYOUT_SEMI; return true; }
            return false;
        }
        push(s, S_LAYOUT, bc); lexer->result_symbol = FOR_OPEN; return true;
    }
    if (valid[EXPR_OPEN])   { push(s, S_EXPR,   peek_body_col(lexer)); lexer->result_symbol = EXPR_OPEN;   return true; }
    if (valid[TRY_OPEN])    { push(s, S_TRY,    peek_body_col(lexer)); lexer->result_symbol = TRY_OPEN;    return true; }
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
            if (next_line_indent(lexer, &col, NULL)) { push(s, S_DECL, col); lexer->result_symbol = BLOCK_OPEN; return true; }
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
        if (is_name_start(c)) {
            peek_name_segment(lexer);
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
            // A qualified field name (`FunctionDef.Name = …`) — consume `.seg`
            // chains so the `=`/`:` check below still fires. A copy-update base
            // (`Foo.bar with …`) is followed by `with`, not `=`/`:`, so it still
            // falls through.
            while (lexer->lookahead == '.') {
                lexer->advance(lexer, true);
                if (!is_name_start(lexer->lookahead)) break;
                peek_name_segment(lexer);
                while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
            }
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
            // Attribute on a labelled (member-sig) param: `[<ParamArray>] xs: obj[]`.
            // Emit a zero-width LABEL_ATTR only when `[<…>]+` is followed by
            // `ident:` (or `?ident:`). Done HERE (mid-line, gated on `c == '['`) so a
            // non-match falls through to the closer logic / `return false` exactly as
            // before — it never pre-empts the layout opens above.
            if (c == '[' && valid[LABEL_ATTR] && skip_bracket_attrs(lexer)) {
                if (lexer->lookahead == '?') lexer->advance(lexer, true);
                int32_t a = lexer->lookahead;
                if ((a >= 'a' && a <= 'z') || (a >= 'A' && a <= 'Z') || a == '_') {
                    while (1) {
                        int32_t ch = lexer->lookahead;
                        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                            (ch >= '0' && ch <= '9') || ch == '_' || ch == '\'') lexer->advance(lexer, true);
                        else break;
                    }
                    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
                    if (lexer->lookahead == ':') { lexer->result_symbol = LABEL_ATTR; return true; }
                }
            }
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
            // `module M =⏎ let f = if a then 1 else 0⏎ let g`. Gated on
            // valid[LAYOUT_END]: once the then-branch has closed and the grammar
            // is ready for the `else`, LAYOUT_END is no longer valid, so an
            // ENCLOSING S_EXPR (e.g. a `let_decl_indented` value wrapping a
            // parenthesised `(if … else …)`) is NOT also closed — which would
            // otherwise orphan the inner `else`.
            if (top && top->sort == S_EXPR && valid[LAYOUT_END] && c >= 'a' && c <= 'z') {
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
            // A try/finally body (S_TRY) closes before an inline `with`/`finally`:
            //   `try e with …` / `try e finally …`. Dedicated sort, so this never
            //   fires for a `match … with` inside an enclosing S_EXPR body.
            if (top && top->sort == S_TRY && valid[LAYOUT_END] && c >= 'a' && c <= 'z') {
                char w[10]; size_t n = 0; int32_t look = lexer->lookahead;
                while (n < 9 && ((look >= 'a' && look <= 'z') || (look >= 'A' && look <= 'Z') ||
                                 (look >= '0' && look <= '9') || look == '_' || look == '\'')) {
                    w[n++] = (char)look; lexer->advance(lexer, true); look = lexer->lookahead;
                }
                w[n] = '\0';
                if (!strcmp(w, "with") || !strcmp(w, "finally")) {
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

    // A leading infix operator continues the previous expression (F#'s
    // leading-operator rule) — UNLESS it DEDENTS below an EXPRESSION body
    // (S_EXPR: then/elif/else/lambda/let-in value), in which case that body must
    // close first and re-invocation continues the OUTER chain. This pipes the
    // whole if in `if c then a else b⏎ |> f` (close the inline else-body, then
    // `|>` applies to the if) instead of binding `|>` to just `b`. Restricted to
    // S_EXPR: a match arm body / decl body (S_LAYOUT) keeps the previous
    // behaviour (the operator continues the inner body, e.g. `match … | B -> 2⏎
    // |> g` keeps `2 |> g`).
    bool infix_continues = !(top->sort == S_EXPR && col < top->col);

    // `|>`/`<|`/`>>` pipe chains, `=`/`<`/`>`/`*`/… arithmetic, `::` cons.
    // `|` alone is a match arm (not infix); only `|>`/`||` are. `&`/`:` count
    // only doubled. Other unary-capable leads (`!` `~`) are excluded.
    if (infix_continues) {
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

        // Leading `+`/`-`/`@` — also continuation, but only in a layout body (a
        // bracket / match arm-list keeps newline-as-element/arm separator). They
        // are unary/prefix-capable. Excluded forms: `->` (lambda/match arrow),
        // `@"…"` (verbatim string), `@>` / `@@>` (code-quotation close).
        if (layoutish(top->sort) && (first == '+' || first == '-' || first == '@')) {
            lexer->advance(lexer, true);
            int32_t c1 = lexer->lookahead;
            if (first == '+') return false;
            if (first == '-' && c1 != '>') return false;
            if (first == '@' && c1 != '"' && c1 != '>' && c1 != '@') return false;
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
        case S_DECL:
        case S_TRY:
            if (valid[LAYOUT_END] && col < top->col) { s->n--; lexer->result_symbol = LAYOUT_END; return true; }
            // A leading close delimiter `)`/`]`/`}` ends the enclosing construct,
            // so an open layout body must close first — even at == body col, where
            // neither the dedent above nor a separator (semi_blocked on closers)
            // fires. E.g. a lambda body whose `)` is on its own line:
            //   `g (fun c ->⏎        x⏎        )` (the `)` aligned with `x`).
            if (valid[LAYOUT_END] && (first == ')' || is_close_bracket(first))) { s->n--; lexer->result_symbol = LAYOUT_END; return true; }
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
            // S_DECL: never separate before a declaration keyword — the line is a
            // new `_token`, not a `sequence_expression` continuation of the prior
            // bare-expression statement.
            if (top->sort == S_DECL && decl_starter(lexer, first)) return false;
            if (valid[LAYOUT_SEMI] && col == top->col && !semi_blocked(lexer, first)) { lexer->result_symbol = LAYOUT_SEMI; return true; }
            return false;
    }
    return false;
}
