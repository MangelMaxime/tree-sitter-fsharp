#include "tree_sitter/parser.h"
#include <stdbool.h>
#include <stdint.h>
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
    ELEMENT_DSL_OPEN,     // zero-width: Oxpecker element-DSL builder — only when `ident ( … ) {` follows
    AND_DOCS_OPEN,        // zero-width: `///` doc lines followed by the word `and` — docs attach to the and-clause
    CASE_DOCS_OPEN,       // zero-width: `///` doc lines followed by `|` — docs attach to the union/enum case
    PAREN_FIELD_OPEN,     // named-field-pattern body open `Foo(ident = …)` — S_BRACKET context for newline fields
    CE_BRACE_OPEN,        // the `{` of a computation_expression body — consumed+emitted ONLY when brace content is a CE body (not record/object/copy-update)
    PREPROC_INACTIVE,     // RESERVED (never emitted; kept so enum indexes match the externals array)
    BLOCK_COMMENT,        // `(* … *)` NESTED (regex can't nest)
    BLOCK_DOC_COMMENT,    // `(** … *)` doc form
    THEN_OPEN,            // then/elif body open — S_EXPR with thn=1 (mid-line `else` may close it)
    LAZY_OPEN,            // lazy block-body open — S_EXPR, declines INLINE bodies
    CTOR_TUPLE_GATE,      // zero-width: `let Ctor(a, b), rest` — only when `ident ( … ) ,` follows
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

typedef struct { uint32_t col; uint8_t sort; uint8_t inl; uint8_t thn; } Ctx;  // inl: body opened INLINE; thn: then/elif body (closeable at mid-line else)

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
    if (s->n < MAXD) { s->stk[s->n].sort = sort; s->stk[s->n].col = col; s->stk[s->n].inl = 0; s->stk[s->n].thn = 0; s->n++; }
}

// Compute the indent + first significant char of the NEXT non-blank, non-comment
// line. Returns false at EOF. Reused verbatim from the old scanner (handles `//`
// line comments, `(* *)` nested block comments, and `#if/#elif/#else/#endif`
// conditional-compilation lines which are extras transparent to the offside rule).
// Consume a block comment from just AFTER its `(*` (already advanced with
// advance(false), so the token starts at the `(`) through the MATCHING `*)`
// (nesting-aware) and emit BLOCK_COMMENT / BLOCK_DOC_COMMENT. External because
// a token regex cannot nest. Returns false on EOF (unterminated) or when
// neither symbol is valid — the reset internal lexer takes over.
static bool finish_block_comment(TSLexer *lexer, const bool *valid) {
    if (!valid[BLOCK_COMMENT] && !valid[BLOCK_DOC_COMMENT]) return false;
    if (lexer->lookahead == ')') return false;   // `(*)` = the multiply operator value, not a comment
    bool doc = false;
    if (lexer->lookahead == '*') {                 // `(**` — doc form…
        lexer->advance(lexer, false);
        doc = (lexer->lookahead != ')');           // …unless `(**)`: EMPTY normal comment
    }
    int cdepth = 1;
    while (cdepth > 0) {
        if (lexer->lookahead == 0) return false;   // unterminated
        if (lexer->lookahead == '(') {
            lexer->advance(lexer, false);
            if (lexer->lookahead == '*') { cdepth++; lexer->advance(lexer, false); }
        } else if (lexer->lookahead == '*') {
            lexer->advance(lexer, false);
            if (lexer->lookahead == ')') { cdepth--; lexer->advance(lexer, false); }
        } else lexer->advance(lexer, false);
    }
    lexer->mark_end(lexer);
    lexer->result_symbol = (doc && valid[BLOCK_DOC_COMMENT]) ? BLOCK_DOC_COMMENT
                         : (valid[BLOCK_COMMENT] ? BLOCK_COMMENT : BLOCK_DOC_COMMENT);
    return true;
}

// When set (main line-boundary call only), next_line_indent STOPS at a
// line-start block comment (sentinel *first = 2) so the boundary path can
// emit it as one token. Peek callers (body-col probes, open helpers) leave
// it false and get plain skipping geometry.
static bool g_region_stop = false;
// Set per-scan before the MAIN next_line_indent call: a doc-attachment gate
// (CASE_DOCS_OPEN / AND_DOCS_OPEN) is valid, so the doc-line baseline anchor
// is worth taking. Ordinary closes must NOT anchor at the docs (it drags the
// CLOSED node's extent forward to the next declaration's doc block).
static bool g_doc_gate_possible = false;
// Indent of the FIRST skipped `///` line (valid when g_skipped_doc_lines).
static uint32_t g_doc_indent = 0;
// The layout-stack top column at the time of the MAIN call: docs INDENTED AT
// OR PAST it can decorate a case/and at that level (mark-worthy); docs LEFT of
// it belong to a declaration after a dedent-close, which must keep its tight
// old-baseline anchor.
static uint32_t g_top_col_for_docs = 0;
static bool g_comment_doc = false;   // set by the stop-mode comment scan: `(**` doc form

// Set when next_line_indent skips one or more `///` doc-comment lines on its
// way to the next real line — transient (read within the same scan only).
static bool g_skipped_doc_lines = false;
// The word read by the docs probe (try_and_docs) right after skipped doc
// lines — used by the LAYOUT_SEMI check to suppress a sequence separator
// before `/// docs⏎ <decl keyword>` (the docs belong to a DECLARATION; a
// separator would let the decl nest inside the previous statement's body).
static char g_post_doc_word[10] = {0};

static bool word_is_decl_kw(const char *w) {
    return !strcmp(w, "let") || !strcmp(w, "type") || !strcmp(w, "open") ||
           !strcmp(w, "module") || !strcmp(w, "exception") || !strcmp(w, "member") ||
           !strcmp(w, "static") || !strcmp(w, "override") || !strcmp(w, "abstract") ||
           !strcmp(w, "val") || !strcmp(w, "interface") || !strcmp(w, "new");
}

static bool next_line_indent(TSLexer *lexer, uint32_t *col, int32_t *first) {
    g_skipped_doc_lines = false;
    bool marked_line_start = false;
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
            // MAIN boundary call: move the zero-width baseline to the first
            // comment/doc line's start. Tokens this scan emits (CASE/AND doc
            // gates, closes) then anchor AT the `///` block, so a documented
            // case/and-clause node STARTS at its docs (expand-selection
            // extents). The scan RESUMES from here afterwards — the mid-line
            // doc-resume dispatch in the scan body handles that position.
            if (g_region_stop && g_doc_gate_possible && indent >= g_top_col_for_docs && !marked_line_start) { lexer->mark_end(lexer); marked_line_start = true; }
            lexer->advance(lexer, true);
            if (lexer->lookahead == '/') {
                lexer->advance(lexer, true);
                if (lexer->lookahead == '/') {
                    if (!g_skipped_doc_lines) g_doc_indent = indent;        // first doc line
                    g_skipped_doc_lines = true;                             // a `///` doc line
                }
                while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0)
                    lexer->advance(lexer, true);
                if (lexer->lookahead == 0) return false;
                continue;
            }
            if (first) *first = '/'; *col = indent; return true;
        }
        if (lexer->lookahead == '(') {
            lexer->advance(lexer, !g_region_stop);
            if (lexer->lookahead != '*') { if (first) *first = '('; *col = indent; return true; }
            if (g_region_stop) {
                // Line-start block comment, MAIN boundary call. Consume it with
                // advance(false) — the token (if emitted) starts at the `(`; for
                // any OTHER outcome those advances are harmless (zero-width
                // layout tokens never re-mark past the baseline).
                lexer->advance(lexer, false);          // the `*`
                if (lexer->lookahead == ')') {         // `(*)` multiply-op value line
                    if (first) *first = '('; *col = indent; return true;
                }
                g_comment_doc = (lexer->lookahead == '*');
                int sdepth = 1;
                while (sdepth > 0) {
                    if (lexer->lookahead == 0) return false;
                    if (lexer->lookahead == '(') {
                        lexer->advance(lexer, false);
                        if (lexer->lookahead == '*') { sdepth++; lexer->advance(lexer, false); }
                    } else if (lexer->lookahead == '*') {
                        lexer->advance(lexer, false);
                        if (lexer->lookahead == ')') { sdepth--; lexer->advance(lexer, false); }
                    } else lexer->advance(lexer, false);
                }
                while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
                if (lexer->lookahead == '\n' || lexer->lookahead == '\r' || lexer->lookahead == 0) {
                    lexer->mark_end(lexer);            // full comment span (+trailing ws)
                    // mark_end ONLY here: in the comment-LED branch below the
                    // baseline must stay at the scan start, or the next
                    // zero-width close/semi would SWALLOW the comment text.
                    // Comment-ONLY line: emit it as ONE token BEFORE any close
                    // (extras are transparent; closes fire on re-scan with
                    // post-comment geometry). Handles NESTING — the reason the
                    // internal regex fallback can't do this one.
                    if (first) *first = 2; *col = indent; return true;
                }
                // Comment-LED line (`(* 4 *) 7`, aligned arrays): geometry first
                // — col is the COMMENT's start indent, first the real char; the
                // comment itself lexes via the internal-regex fallback later.
                // (KNOWN GAP: a NESTED comment here truncates in the fallback.)
                if (first) *first = lexer->lookahead;
                *col = indent;
                return true;
            }
            lexer->advance(lexer, true);
            if (lexer->lookahead == ')') { if (first) *first = '('; *col = indent; return true; }  // `(*)`
            int depth = 1;
            while (depth > 0) {
                if (lexer->lookahead == 0) return false;
                if (lexer->lookahead == '(') { lexer->advance(lexer, true); if (lexer->lookahead == '*') { depth++; lexer->advance(lexer, true); } }
                else if (lexer->lookahead == '*') { lexer->advance(lexer, true); if (lexer->lookahead == ')') { depth--; lexer->advance(lexer, true); } }
                else lexer->advance(lexer, true);
            }
            // CONTENT may follow the comment on the same line — a comment-LED
            // element (`(* 4 *) 7`, PriorityQueue-style aligned arrays). The
            // line then counts: its column is the COMMENT's start indent (where
            // the element visually begins) and `first` is the first real char.
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
            if (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0) {
                if (first) *first = lexer->lookahead;
                *col = indent;
                return true;
            }
            if (lexer->lookahead == 0) return false;
            continue;
        }
        if (lexer->lookahead == 0) return false;
        if (lexer->lookahead == '#') {
            lexer->advance(lexer, true);
            char w[8]; size_t wi = 0;
            while (wi < 7 && lexer->lookahead >= 'a' && lexer->lookahead <= 'z') { w[wi++] = (char)lexer->lookahead; lexer->advance(lexer, true); }
            w[wi] = '\0';
            // `#nowarn`/`#warnon` are skipped like the `#if` family so they
            // don't dedent-close an open body when interspersed (e.g. between
            // union cases, Argu style); the directive tokens themselves are
            // consumed by the grammar where it allows `preproc_directive`.
            // NOT `#load`/`#r`: those are top-level statements that RELY on
            // the dedent-close firing at their line.
            // `#if`-family / `#nowarn` / `#line` lines are skipped like
            // comment lines so they never dedent-close an open body.
            // `#elif`/`#else`: BOTH branches parse as real code (user choice:
            // Fable-style dual-path projects carry full-sized #else branches
            // that deserve real highlighting). The directive LINE is skipped
            // for geometry, exactly like the `#if` family below. Known cost:
            // exotic keyword-splices (`#if A⏎let⏎#else⏎use⏎#endif`, FParsec)
            // don't parse — rare and accepted.
            if (strcmp(w, "if") == 0 || strcmp(w, "endif") == 0 ||
                strcmp(w, "elif") == 0 || strcmp(w, "else") == 0 ||
                strcmp(w, "nowarn") == 0 || strcmp(w, "warnon") == 0 ||
                strcmp(w, "line") == 0) {
                while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0) lexer->advance(lexer, true);
                if (lexer->lookahead == 0) return false;
                continue;
            }
            // `# 14 "pars.fs"` — fsyacc/fslex line directive: trivia, skip the line.
            if (wi == 0) {
                int32_t dl = lexer->lookahead;
                while (dl == ' ' || dl == '\t') { lexer->advance(lexer, true); dl = lexer->lookahead; }
                if (dl >= '0' && dl <= '9') {
                    while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0) lexer->advance(lexer, true);
                    if (lexer->lookahead == 0) return false;
                    continue;
                }
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
    // A trailing comment after the opener keyword (`match x with // …`,
    // `let x = (* … *)`) means the body/arms start on a later line — defer to
    // next_line_indent (which skips comment-only lines) instead of taking the
    // comment's column as the body column.
    if (lexer->lookahead == '/') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '/') { uint32_t nl; return next_line_indent(lexer, &nl, NULL) ? nl : 0; }
        return col;  // a lone `/` is inline (operator), body sits at its column
    }
    if (lexer->lookahead == '(') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '*') {
            // Block comment. CONTENT may follow it on the SAME line
            // (`| A -> (* tailcall *) f res`, FCS DiagnosticsLogger style):
            // skip the comment (depth-aware) and check — inline content keeps
            // the comment's start column as the body column (mirrors
            // next_line_indent's comment-led-element rule); otherwise the body
            // is on a later line.
            lexer->advance(lexer, true);
            int cdepth = 1;
            while (cdepth > 0) {
                if (lexer->lookahead == 0) return 0;
                if (lexer->lookahead == '(') { lexer->advance(lexer, true); if (lexer->lookahead == '*') { cdepth++; lexer->advance(lexer, true); } }
                else if (lexer->lookahead == '*') { lexer->advance(lexer, true); if (lexer->lookahead == ')') { cdepth--; lexer->advance(lexer, true); } }
                else lexer->advance(lexer, true);
            }
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
            if (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0)
                return col;                       // inline body after the comment
            uint32_t nl; return next_line_indent(lexer, &nl, NULL) ? nl : 0;
        }
        return col;  // `(` inline (parenthesised pattern / expression)
    }
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

// Like peek_name_segment but copies the (plain-identifier) segment into buf,
// NUL-terminated and truncated to cap. A backtick segment yields "`" (which
// never matches a plain keyword). Lets the RECORD_OPEN peek tell an object
// expression (`{ new … }`) from a copy-update base on its own line.
static void peek_name_capture(TSLexer *lexer, char *buf, int cap) {
    int n = 0;
    if (lexer->lookahead == '`') { if (cap > 1) { buf[0] = '`'; buf[1] = 0; } else if (cap > 0) buf[0] = 0; peek_name_segment(lexer); return; }
    while (1) {
        int32_t ch = lexer->lookahead;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '\'') {
            if (n < cap - 1) buf[n++] = (char)ch;
            lexer->advance(lexer, true);
        } else break;
    }
    if (cap > 0) buf[n < cap ? n : cap - 1] = 0;
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
            !strcmp(w, "with") || !strcmp(w, "finally") || !strcmp(w, "in") || !strcmp(w, "and") ||
            !strcmp(w, "when"))   // static-optimization equations / arm-guard continuations
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

// Skip a `"`-initiated string at the opening quote: triple `"""…"""` or regular
// `"…"` (with `\"` escape). Lexer ends just past the closing quote(s).
static void edsl_skip_dquote(TSLexer *lexer) {
    lexer->advance(lexer, true);                          // opening "
    if (lexer->lookahead == '"') {
        lexer->advance(lexer, true);
        if (lexer->lookahead != '"') return;             // empty "" string
        lexer->advance(lexer, true);                     // triple """ … """
        int q = 0;
        while (lexer->lookahead != 0) {
            if (lexer->lookahead == '"') { q++; lexer->advance(lexer, true); if (q == 3) break; }
            else { q = 0; lexer->advance(lexer, true); }
        }
        return;
    }
    while (lexer->lookahead != '"' && lexer->lookahead != 0) {
        if (lexer->lookahead == '\\') { lexer->advance(lexer, true); if (lexer->lookahead != 0) lexer->advance(lexer, true); continue; }
        lexer->advance(lexer, true);
    }
    if (lexer->lookahead == '"') lexer->advance(lexer, true);
}

// Skip a verbatim `@"…"` body at the opening quote; `""` is an escaped quote.
static void edsl_skip_verbatim(TSLexer *lexer) {
    lexer->advance(lexer, true);                          // opening "
    while (lexer->lookahead != 0) {
        if (lexer->lookahead == '"') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == '"') { lexer->advance(lexer, true); continue; }  // "" escape
            break;                                        // closing "
        }
        lexer->advance(lexer, true);
    }
}

// Consume a balanced `( … )` group with the lexer positioned at the opening `(`.
// Args MAY span lines (`div(class'="a"⏎ , id="b")`), so every string/comment form
// is skipped to keep the paren depth exact — a `)`, `{` or `"` inside a string
// must not miscount (this is what made an earlier newline-allowing version
// mis-fire on an emoticon `:^)` inside a triple string in a multi-line `(fun … )`
// arg). Returns false on EOF / runaway.
static bool edsl_skip_balanced_parens(TSLexer *lexer) {
    int depth = 0, guard = 0;
    for (;;) {
        if (++guard > 8192) return false;                // runaway guard
        int32_t c = lexer->lookahead;
        if (c == 0) return false;
        if (c == '"') { edsl_skip_dquote(lexer); continue; }
        if (c == '@') {                                  // @"verbatim" / @$"…"
            lexer->advance(lexer, true);
            if (lexer->lookahead == '$') lexer->advance(lexer, true);
            if (lexer->lookahead == '"') edsl_skip_verbatim(lexer);
            continue;
        }
        if (c == '$') {                                  // $"interp" / $@"…" / $"""…"""
            lexer->advance(lexer, true);
            if (lexer->lookahead == '@') { lexer->advance(lexer, true); if (lexer->lookahead == '"') edsl_skip_verbatim(lexer); }
            else if (lexer->lookahead == '"') edsl_skip_dquote(lexer);
            continue;
        }
        if (c == '/') {                                  // // line comment
            lexer->advance(lexer, true);
            if (lexer->lookahead == '/') { while (lexer->lookahead != '\n' && lexer->lookahead != 0) lexer->advance(lexer, true); }
            continue;
        }
        if (c == '(') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == '*') {               // (* block comment *) — not a paren
                lexer->advance(lexer, true);
                int32_t prev = 0;
                while (lexer->lookahead != 0) {
                    int32_t cc = lexer->lookahead; lexer->advance(lexer, true);
                    if (prev == '*' && cc == ')') break;
                    prev = cc;
                }
                continue;
            }
            depth++;
            continue;
        }
        if (c == ')') { lexer->advance(lexer, true); depth--; if (depth == 0) return true; continue; }
        lexer->advance(lexer, true);
    }
}

// Skip the rest of a brace-balanced `{| … |}` anonymous-record argument — the caller
// has ALREADY consumed the opening `{` (depth starts at 1). Counts `{`/`}` (so
// `{|`/`|}` and nested records balance) and skips strings/comments. Used for the
// Oxpecker.Solid component DSL arg form `Component {| props |} { children }`.
static bool edsl_skip_braces_after_open(TSLexer *lexer) {
    int depth = 1, guard = 0;
    for (;;) {
        if (++guard > 8192) return false;
        int32_t c = lexer->lookahead;
        if (c == 0) return false;
        if (c == '"') { edsl_skip_dquote(lexer); continue; }
        if (c == '@') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == '$') lexer->advance(lexer, true);
            if (lexer->lookahead == '"') edsl_skip_verbatim(lexer);
            continue;
        }
        if (c == '$') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == '@') { lexer->advance(lexer, true); if (lexer->lookahead == '"') edsl_skip_verbatim(lexer); }
            else if (lexer->lookahead == '"') edsl_skip_dquote(lexer);
            continue;
        }
        if (c == '{') { lexer->advance(lexer, true); depth++; continue; }
        if (c == '}') { lexer->advance(lexer, true); depth--; if (depth == 0) return true; continue; }
        lexer->advance(lexer, true);
    }
}

// Consume a (possibly qualified) identifier with the lexer at its first char.
static bool edsl_skip_name(TSLexer *lexer) {
    if (!is_name_start(lexer->lookahead)) return false;
    for (;;) {
        int32_t ch = lexer->lookahead;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '\'') { lexer->advance(lexer, true); continue; }
        break;
    }
    return true;
}

// Lookahead for the Oxpecker element-DSL head: the CE builder is an APPLICATION,
// optionally extended by a fluent method chain —
//   `div() {`  ·  `div(attrs) {`  ·  `div(attrs).hxTarget("#x").hxSwap("y") {`
// (chain links may sit on their own lines). The lexer is positioned at the
// builder's first char. Advances DESTRUCTIVELY; the caller emits a ZERO-WIDTH
// token so the over-advance is discarded and the real tokens are re-lexed. This
// scanner-side lookahead is what lets the parser tell `div() { … }` (element DSL)
// from `a()`⏎`b()` (two applications): the LR table can't peek past the args/chain
// Forward decl: classify the content after a `{` as a CE body vs record/object/copy-update.
static bool ce_brace_content_is_ce_body(TSLexer *lexer);

// for the `{`. Tail of element_dsl_ahead: the caller has consumed the first name
// segment; check the optional `.seg` qualification, then `( … )( .m( … ) )* {`.
static bool element_dsl_parens_brace(TSLexer *lexer) {
    while (lexer->lookahead == '.') {                     // qualified `A.B.div`
        lexer->advance(lexer, true);
        if (!edsl_skip_name(lexer)) return false;
    }
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
    // The builder argument is either a parenthesised group (`div(attrs)`) or a
    // single string literal (`stage "x"` / `pipeline "Build"`, Fun.Build-style).
    if (lexer->lookahead == '(') {
        if (!edsl_skip_balanced_parens(lexer)) return false;
    } else if (lexer->lookahead == '"') {
        edsl_skip_dquote(lexer);                          // regular or """triple"""
    } else if (lexer->lookahead == '@') {
        lexer->advance(lexer, true);
        if (lexer->lookahead != '"') return false;
        edsl_skip_verbatim(lexer);                        // @"verbatim"
    } else if (lexer->lookahead == '{') {
        // Oxpecker.Solid component: `Component {| props |} { children }` — the
        // builder argument is an anonymous record. Require `{|` (not a plain `{`,
        // which would be the body).
        lexer->advance(lexer, true);
        if (lexer->lookahead != '|') return false;
        if (!edsl_skip_braces_after_open(lexer)) return false;
    } else {
        return false;
    }
    // After each arg: the body `{` must be SAME-line (only spaces/tabs between, so
    // `foo(a, b)`⏎`{ record }` isn't read as one DSL); otherwise a fluent
    // `.method( … )` chain link may continue (those CAN sit on their own lines).
    for (;;) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
        if (lexer->lookahead == '{') {
            // Only an element-DSL if the brace holds a CE body — NOT a record /
            // object-expr / copy-update. Otherwise `f "msg" { x with … }` /
            // `f "s" { name = 1 }` must stay application(f, "msg", record/copy-update).
            lexer->advance(lexer, true);                  // past `{`
            if (lexer->lookahead == '|') return false;    // `{|` anon record
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
                   lexer->lookahead == '\n' || lexer->lookahead == '\r') lexer->advance(lexer, true);
            return ce_brace_content_is_ce_body(lexer);
        }
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
               lexer->lookahead == '\n' || lexer->lookahead == '\r') lexer->advance(lexer, true);
        if (lexer->lookahead != '.') return false;        // not a chain link → not a DSL
        lexer->advance(lexer, true);
        if (!edsl_skip_name(lexer)) return false;
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
        if (lexer->lookahead != '(') return false;        // method must be CALLED
        if (!edsl_skip_balanced_parens(lexer)) return false;
    }
}

// Full element-DSL head probe (lexer at the builder's first char): read the first
// name segment, then defer to element_dsl_parens_brace.
static bool element_dsl_ahead(TSLexer *lexer) {
    if (!is_name_start(lexer->lookahead)) return false;
    for (;;) {
        int32_t ch = lexer->lookahead;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '\'') { lexer->advance(lexer, true); continue; }
        break;
    }
    return element_dsl_parens_brace(lexer);
}

// Emit the zero-width element-DSL marker if `valid` and the pattern is ahead.
// Call right before a `return false` where an expression/statement can begin
// (lexer at the candidate builder's first char). On no-match the destructive
// advance is discarded by the caller's `return false`.
static inline bool try_element_dsl(TSLexer *lexer, const bool *valid) {
    if (valid[ELEMENT_DSL_OPEN] && element_dsl_ahead(lexer)) { lexer->result_symbol = ELEMENT_DSL_OPEN; return true; }
    return false;
}

// Classify the content right after a `{` (lexer positioned at the first non-ws char)
// as a computation-expression BODY (true) vs a record / object-expression / copy-update
// (false). Used to decide whether to emit CE_BRACE_OPEN so `head { … }` forks to a CE
// vs application(head, record/object). Destructive peek — only ever called right before a
// `return false` (on no-match) or a zero-width `return true`, so the advances are
// discarded by the caller's mark_end baseline.
//
//   CE body   → true:  `return …`, `let x = …`, `yield …`, `if …`, `for …`, `x`,
//                       `f x`, `x :: xs`, `(…)`, `[ … ]`, `1`, …  (and empty `{}`)
//   record    → false: `ident = …`  /  `ident : ty`  (NOT `::`)
//   object    → false: `new …`
//   copy-update → false: `base with …`
static bool ce_brace_content_is_ce_body(TSLexer *lexer) {
    int32_t c = lexer->lookahead;
    if (c == '}') return true;                 // empty CE body `{ }`
    if (!is_name_start(c)) return true;        // literal / paren / bracket / operator → CE expr
    char w0[12] = {0};
    peek_name_capture(lexer, w0, sizeof(w0));
    if (!strcmp(w0, "new")) return false;      // object expression `{ new T … }`
    // CE statement keywords (reserved → can never be a record field name). `let!`,
    // `use!`, `do!`, `match!`, `yield!`, `return!` share the base word read here.
    static const char *kw[] = {"let","use","do","return","yield","if","for","while",
                               "match","try","fun","function","lazy","assert", NULL};
    for (int i = 0; kw[i]; i++) if (!strcmp(w0, kw[i])) return true;
    // Otherwise scan the leading expression: `=`/`:` (record field) or a later `with`
    // (copy-update) ⇒ NOT a CE; anything else ⇒ CE bare-expression body.
    for (int guard = 0; guard < 64; guard++) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
        int32_t d = lexer->lookahead;
        if (d == '=') return false;                 // record field `name = …`
        if (d == ':') {                             // `:` field type, but `::` is cons (CE)
            lexer->advance(lexer, true);
            return lexer->lookahead == ':';         // `::` → CE ; `:` → record field
        }
        if (d == '.') { lexer->advance(lexer, true); continue; }   // qualified name / member access
        // A bracketed/parenthesised APPLICATION ARGUMENT before a possible
        // `with` (`{ createEx [] doQuery with X = h }` — copy-update whose base
        // is an application): skip the balanced group and keep scanning.
        if (d == '[' || d == '(') {
            int32_t open = d, close = (d == '[') ? ']' : ')';
            int depth = 0, bguard = 0;
            for (;;) {
                if (++bguard > 4096) return true;
                int32_t e = lexer->lookahead;
                if (e == 0) return true;
                if (e == '"') { edsl_skip_dquote(lexer); continue; }
                if (e == open) depth++;
                else if (e == close && --depth == 0) { lexer->advance(lexer, true); break; }
                lexer->advance(lexer, true);
            }
            continue;
        }
        if (is_name_start(d)) {
            char w[8] = {0};
            peek_name_capture(lexer, w, sizeof(w));
            if (!strcmp(w, "with")) return false;   // copy-update `base with …`
            continue;                               // application arg / next path segment
        }
        return true;                                // `}`/`;`/newline/`(`/`[`/literal/op → CE
    }
    return true;
}

// Probe for AND_DOCS_OPEN: at a line boundary where next_line_indent SKIPPED
// `///` doc lines (g_skipped_doc_lines) and the next real line starts with the
// word `and`, emit the zero-width marker that lets the docs shift into the
// and-clause slot (`_and_docs`). The lexer sits AT the first char after
// next_line_indent; reading the word only consumes peeked lookahead (the
// marker is zero-width — mark_end stays at the baseline). Call LAST.
// Dispatch for `///` doc lines at a layout boundary (g_skipped_doc_lines):
// the post-doc word decides where the docs belong. Reads the word
// destructively — every taken branch RETURNS a token, and the lone
// fall-through (no branch applies) only loses position for checks that
// re-derive it themselves.
static bool try_and_docs(Scanner *s, TSLexer *lexer, const bool *valid,
                         int32_t first, uint32_t col, Ctx *top) {
    g_post_doc_word[0] = '\0';
    if (!g_skipped_doc_lines) return false;
    // Docs followed by a `|` case — union/enum case attachment.
    if (valid[CASE_DOCS_OPEN] && first == '|') {
        lexer->result_symbol = CASE_DOCS_OPEN; return true;
    }

    // Word-led dispatch (`and` / decl keywords / member words).
    if (first >= 'a' && first <= 'z') {
        char w[10]; size_t n = 0; int32_t lk = lexer->lookahead;
        while (n < 9 && ((lk >= 'a' && lk <= 'z') || (lk >= 'A' && lk <= 'Z') ||
                         (lk >= '0' && lk <= '9') || lk == '_' || lk == '\'')) {
            w[n++] = (char)lk; lexer->advance(lexer, true); lk = lexer->lookahead;
        }
        w[n] = '\0';
        { size_t i = 0; for (; w[i] && i < 9; i++) g_post_doc_word[i] = w[i]; g_post_doc_word[i] = '\0'; }
        // docs + `and` — the and-clause attachment marker.
        if (valid[AND_DOCS_OPEN] && !strcmp(w, "and")) {
            lexer->result_symbol = AND_DOCS_OPEN; return true;
        }
        // Inside a TYPE body at the body column, docs + a DECL keyword mean
        // the body ends here and the docs decorate the NEXT declaration —
        // close the body BEFORE the docs (`type V =⏎| A⏎⏎/// d⏎[<A>]⏎type W`).
        // NOT `let`: class bodies legitimately contain let-members.
        if (top && top->sort == S_TYPEBODY && valid[LAYOUT_END] && col <= top->col &&
            (!strcmp(w, "type") || !strcmp(w, "open") ||
             !strcmp(w, "module") || !strcmp(w, "namespace") || !strcmp(w, "exception"))) {
            s->n--; lexer->result_symbol = LAYOUT_END; return true;
        }
        return false;
    }
    // docs + `[<` attribute: could decorate a MEMBER (body continues) or the
    // NEXT declaration (body ends). At the body column inside a TYPE body the
    // attribute alone is ambiguous — peek PAST the attr group to the word.
    if (first == '[' && top && top->sort == S_TYPEBODY && col <= top->col &&
        (valid[LAYOUT_END] || valid[LAYOUT_SEMI])) {
        if (!skip_bracket_attrs(lexer)) return false;
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
               lexer->lookahead == '\n' || lexer->lookahead == '\r') lexer->advance(lexer, true);
        char w[10]; size_t n = 0; int32_t lk = lexer->lookahead;
        while (n < 9 && lk >= 'a' && lk <= 'z') { w[n++] = (char)lk; lexer->advance(lexer, true); lk = lexer->lookahead; }
        w[n] = '\0';
        if (valid[LAYOUT_END] &&
            (!strcmp(w, "type") || !strcmp(w, "open") ||
             !strcmp(w, "module") || !strcmp(w, "namespace") || !strcmp(w, "exception"))) {
            s->n--; lexer->result_symbol = LAYOUT_END; return true;
        }
        return false;
    }
    return false;
}

// Probe a LEADING `{` (lookahead == first == '{') for the CE-body
// classification — the line-boundary twin of the mid-line CE_BRACE_OPEN
// dispatch, for a builder whose `{` sits on the NEXT line (`seq`⏎`    {`).
// Zero-width (mark_end stays at the baseline): advances only peek. Call it
// LAST before `return false` — it consumes lookahead even on a miss.
static bool try_ce_brace(TSLexer *lexer, const bool *valid, int32_t first) {
    if (first != '{' || !valid[CE_BRACE_OPEN]) return false;
    lexer->advance(lexer, true);                  // past `{`
    if (lexer->lookahead == '|') return false;    // `{|` anonymous record
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
           lexer->lookahead == '\n' || lexer->lookahead == '\r') lexer->advance(lexer, true);
    if (ce_brace_content_is_ce_body(lexer)) { lexer->result_symbol = CE_BRACE_OPEN; return true; }
    return false;
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
        // Any mix of `[<attr>]` rows, `///` doc lines and `//` comments may
        // precede a primary ctor's `(` (`type StringSyntaxAttribute⏎ ///<param
        // …>⏎ (syntax: string, …) =` — Feliz StringSyntax; `[<ParamObject>] //
        // …⏎ (params) =` — Feliz POJO). Require at least ONE such row so a
        // PLAIN ctor `type T (x) =` keeps its ordinary ungated path.
        bool seen_row = false;
        for (;;) {
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
                   lexer->lookahead == '\n' || lexer->lookahead == '\r') lexer->advance(lexer, true);
            if (lexer->lookahead == '/') {
                lexer->advance(lexer, true);
                if (lexer->lookahead != '/') return false;
                while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0)
                    lexer->advance(lexer, true);
                seen_row = true;
                continue;
            }
            if (lexer->lookahead == '[') {        // an attribute row
                if (!skip_bracket_attrs(lexer)) return false;
                seen_row = true;
                continue;
            }
            break;
        }
        if (!seen_row) return false;
        // Optional access modifier between the attrs and the `(`:
        // `type T [<ParamObject; Emit("$0")>]⏎ private (…)`.
        if (lexer->lookahead == 'p' || lexer->lookahead == 'i') {
            char aw[10]; size_t an = 0;
            while (an < 9 && lexer->lookahead >= 'a' && lexer->lookahead <= 'z') {
                aw[an++] = (char)lexer->lookahead; lexer->advance(lexer, true);
            }
            aw[an] = '\0';
            if (strcmp(aw, "private") && strcmp(aw, "internal") && strcmp(aw, "public")) return false;
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
                   lexer->lookahead == '\n' || lexer->lookahead == '\r') lexer->advance(lexer, true);
        }
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
    if (valid[EXPR_OPEN])   {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
        bool inl = lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
                   lexer->lookahead != '/'  && lexer->lookahead != 0;
        push(s, S_EXPR, peek_body_col(lexer));
        if (inl && s->n) s->stk[s->n - 1].inl = 1;
        lexer->result_symbol = EXPR_OPEN;   return true;
    }
    if (valid[THEN_OPEN])   {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
        bool inl = lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
                   lexer->lookahead != '/'  && lexer->lookahead != 0;
        push(s, S_EXPR, peek_body_col(lexer));
        if (s->n) { s->stk[s->n - 1].thn = 1; if (inl) s->stk[s->n - 1].inl = 1; }
        lexer->result_symbol = THEN_OPEN;   return true;
    }
    if (valid[LAZY_OPEN])   {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
        if (lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
            lexer->lookahead != '/'  && lexer->lookahead != 0) return false;   // inline body → plain branch
        push(s, S_EXPR, peek_body_col(lexer));
        lexer->result_symbol = LAZY_OPEN;   return true;
    }
    if (valid[TRY_OPEN])    { push(s, S_TRY,    peek_body_col(lexer)); lexer->result_symbol = TRY_OPEN;    return true; }
    if (valid[ELSE_OPEN]) {
        // Final-else body. An INLINE `else if` (same line) flattens to an elif clause —
        // DON'T open a nested else-body; return false so the grammar's flat elif matches
        // (its elif/else stay at the chain level instead of nesting an if whose layout
        // would over-close at a later `elif`). But a NEWLINE-led else-body whose first
        // statement happens to be `if` is a REAL body — it may have more statements after
        // (`else⏎ if c then x⏎ match …`) — so suppress ONLY for the same-line form.
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
        bool nl_before = (lexer->lookahead == '\n' || lexer->lookahead == '\r' || lexer->lookahead == '/');
        uint32_t col = peek_body_col(lexer);  // positions lexer at the body's first char
        if (!nl_before && lexer->lookahead == 'i') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == 'f') {
                lexer->advance(lexer, true);
                int32_t a = lexer->lookahead;
                bool word = (a >= 'a' && a <= 'z') || (a >= 'A' && a <= 'Z') ||
                            (a >= '0' && a <= '9') || a == '_' || a == '\'';
                if (!word) return false;  // inline `else if …` → flat elif clause
            }
        }
        push(s, S_EXPR, col);
        if (!nl_before && s->n) s->stk[s->n - 1].inl = 1;
        lexer->result_symbol = ELSE_OPEN; return true;
    }
    if (valid[MATCH_OPEN])  { push(s, S_MATCH,  peek_body_col(lexer)); lexer->result_symbol = MATCH_OPEN;  return true; }

    // Lexical trailing-dot float (`1.`, `20.`). Placed AFTER the peek_body_col
    // opens above (LAYOUT/FOR/EXPR/TRY/ELSE/MATCH) — running it before them would
    // destructively advance over the digits of an inline body like `let a = 1` and
    // corrupt the body column. But it MUST come BEFORE the newline-gated opens
    // (BLOCK/TYPE/BRACKET) and RECORD_OPEN: those `return false` for an inline body,
    // which would otherwise short-circuit this probe and make a first array/list
    // element `[|1.|]` mis-lex as `1` + `.|`. A digit can never start one of those
    // (they fire on a newline / a field-shape peek), so checking float first is safe.
    if (valid[FLOAT_TRAILING_DOT]) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
        if (lexer->lookahead >= '0' && lexer->lookahead <= '9') {
            if (scan_trailing_dot_float(lexer)) return true;
            return false;
        }
    }

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
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            // Block form: body on the next line(s). Decline when the next real
            // char CLOSES the bracket (`Html.div [`⏎`⏎`]` — empty across blank
            // lines): there is no element to anchor a context.
            uint32_t col; int32_t bfirst = 0;
            if (next_line_indent(lexer, &col, &bfirst)) {
                if (bfirst == ']' || bfirst == '}' || bfirst == '|') return false;
                push(s, S_BRACKET, col); lexer->result_symbol = BRACKET_OPEN; return true;
            }
            return false;
        }
        // Capture the inline first element's column / lead char BEFORE the
        // element-DSL probe, which advances the lexer destructively on a miss.
        uint32_t inline_col = lexer->get_column(lexer);
        int32_t inline_first = lexer->lookahead;
        // Same-line content after a CE/bracket `{`: an element-DSL builder here
        // (`div() { span() {…} }`) needs its marker — the mid-line block below is
        // unreachable once we return. (Spaces/tabs already skipped above.)
        if (try_element_dsl(lexer, valid)) return true;
        // Inline-FIRST content (`[ yield a`⏎`  yield! b ]`, `seq { x`⏎`  y }`).
        // Open the context at the first element's column so newline-aligned
        // continuation elements still get a `_bracket_semi` separator (otherwise
        // they'd merge into the first element as an application). Skip when the
        // next char closes the bracket immediately (`[]`/`[| |]`/`{}` empty, or a
        // leading `|`/`}`/`]`), which has no element to anchor a context.
        if (inline_first == ']' || inline_first == '}' || inline_first == '|' || inline_first == 0) return false;
        // A leading BLOCK COMMENT is not an element: skip it (depth-aware) and
        // re-decide — `[(* no attributes *)]` (FCS) is an EMPTY list, comment
        // then newline defers to the block form, real content anchors at the
        // comment's column (comment-led element convention).
        if (inline_first == '(') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == '*') {
                lexer->advance(lexer, true);
                int cdepth = 1;
                while (cdepth > 0) {
                    if (lexer->lookahead == 0) return false;
                    if (lexer->lookahead == '(') { lexer->advance(lexer, true); if (lexer->lookahead == '*') { cdepth++; lexer->advance(lexer, true); } }
                    else if (lexer->lookahead == '*') { lexer->advance(lexer, true); if (lexer->lookahead == ')') { cdepth--; lexer->advance(lexer, true); } }
                    else lexer->advance(lexer, true);
                }
                while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
                if (lexer->lookahead == ']' || lexer->lookahead == '}' || lexer->lookahead == '|' || lexer->lookahead == 0) return false;
                if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
                    uint32_t col;
                    if (next_line_indent(lexer, &col, NULL)) { push(s, S_BRACKET, col); lexer->result_symbol = BRACKET_OPEN; return true; }
                    return false;
                }
            }
        }
        push(s, S_BRACKET, inline_col);
        lexer->result_symbol = BRACKET_OPEN; return true;
    }

    // PAREN_FIELD_OPEN: the body of a named-field pattern `Foo(ident = …)` — a
    // dedicated open (valid ONLY in named_field_pattern) so newline-aligned fields
    // get an S_BRACKET separator. Peek `ident(.seg)* =` (the `=` distinguishes a
    // named field from a tuple-arg `Foo(a, b)`); capture the field column. Like
    // RECORD_OPEN but `=`-only and never reused outside the pattern.
    if (valid[PAREN_FIELD_OPEN]) {
        uint32_t col = lexer->get_column(lexer);
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') { lexer->advance(lexer, true); col++; }
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            if (!next_line_indent(lexer, &col, NULL)) return false;
        }
        bool ok = false;
        if (is_name_start(lexer->lookahead)) {
            peek_name_segment(lexer);
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
            while (lexer->lookahead == '.') {              // qualified field name
                lexer->advance(lexer, true);
                if (!is_name_start(lexer->lookahead)) break;
                peek_name_segment(lexer);
                while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
            }
            if (lexer->lookahead == '=') {                 // `=` (not `==`/`=>`) → named field
                lexer->advance(lexer, true);
                if (lexer->lookahead != '=' && lexer->lookahead != '>') ok = true;
            }
        }
        if (ok) { push(s, S_BRACKET, col); lexer->result_symbol = PAREN_FIELD_OPEN; return true; }
        return false;
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
        // A field may lead with `///` doc lines (`{ /// docs⏎  Field: T … }`):
        // skip them (to the NEXT line's content) so the field-shape check
        // still fires; `col` stays at the doc's column — where fields align.
        while (lexer->lookahead == '/' ) {
            // only a /// doc line: peek two more slashes
            lexer->advance(lexer, true);
            if (lexer->lookahead != '/') return false;   // a field can't start with `/`
            lexer->advance(lexer, true);
            if (lexer->lookahead != '/') return false;   // `//` plain comment: bail (rare inside `{`)
            while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0) lexer->advance(lexer, true);
            uint32_t c2; if (!next_line_indent(lexer, &c2, NULL)) return false;
            col = c2;   // the FIELD's column, not the doc's (`{ /// doc⏎    Field: T`)
            nl = true;
        }
        int32_t c = lexer->lookahead;
        bool ok = false;
        char w0[8] = {0};
        // A field may lead with `[<…>]` attribute(s) — common in offside record
        // TYPE bodies (`{ [<JsonProperty("@id")>]⏎ Id : string … }`). Skip them
        // so the `ident =`/`:` field-shape check below still fires; `col` stays at
        // the attribute's `[`, which is where every field of the body aligns.
        if (c == '[') {
            if (!skip_bracket_attrs(lexer)) return false;
            c = lexer->lookahead;
        }
        if (is_name_start(c)) {
            peek_name_capture(lexer, w0, sizeof(w0));
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
            // A leading field modifier (`mutable foo: …`): a second identifier
            // word sits before the `:`. Skip it so the `=`/`:` check sees the
            // field, not the modifier. Copy-update (`x with …`) / object-expr
            // (`new T …`) have a second word too but no trailing `=`/`:`, so they
            // still fall through.
            if (is_name_start(lexer->lookahead)) {
                peek_name_segment(lexer);
                while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
            }
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
        // / inline copy-update match. An object expression on its own line
        // (`{⏎ new IFoo with …}`) must ALSO fall through — its `new` is a literal
        // token with no layout open, so suppress LAYOUT_OPEN when the first word
        // is `new`.
        if (nl && valid[LAYOUT_OPEN] && strcmp(w0, "new") != 0) { push(s, S_LAYOUT, col); lexer->result_symbol = LAYOUT_OPEN; return true; }
        return false;
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
                else if (c1 != '>' && c1 != '|' && c1 != '?' && c1 != '@' && c1 != '!' &&
                         c1 != '%' && c1 != '&' && c1 != '*' && c1 != '+' && c1 != '-' &&
                         c1 != '.' && c1 != '/' && c1 != '<' && c1 != '=' && c1 != '^' &&
                         c1 != '~' && c1 != '$' &&   // `|?>`/`||>`-style custom ops are INFIX, not an arm `|` (fix-3's boundary rule, mid-line flavor)
                         top && layoutish(top->sort) && valid[LAYOUT_END] && has_match_ctx(s)) {
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
            // Word-led mid-line dispatch. A leading word can be a continuation
            // keyword that closes an inline body, OR an Oxpecker element-DSL builder
            // applied to `( … ) {`. Both start at `c`, so read the leading word
            // ONCE and dispatch — reading it twice would advance past it and
            // corrupt the second probe.
            //   - S_EXPR closes before an inline `else`/`elif`/`in`.
            //   - S_TRY closes before an inline `with`/`finally` (dedicated sort, so
            //     this never fires for a `match … with` inside an S_EXPR body).
            //   - ELEMENT_DSL_OPEN fires when `<word>(.seg)* ( … ) {` follows.
            // Continuation closes are tried FIRST: at a `with`/`in` position
            // ELEMENT_DSL_OPEN can be co-valid (the body could continue as an
            // application) and the body-close must win.
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                char w[10]; size_t n = 0; int32_t look = lexer->lookahead;
                while (n < 9 && ((look >= 'a' && look <= 'z') || (look >= 'A' && look <= 'Z') ||
                                 (look >= '0' && look <= '9') || look == '_' || look == '\'')) {
                    w[n++] = (char)look; lexer->advance(lexer, true); look = lexer->lookahead;
                }
                w[n] = '\0';
                if (top && valid[LAYOUT_END]) {
                    // `else`/`elif` close only an INLINE body: a same-line `else`
                    // after an INDENTED then-body belongs to an INNER if on this
                    // line (`if a then⏎    if p then x else y` — dangling else;
                    // the greedy close handed it to the OUTER if and stranded the
                    // outer `else` line, TaggedCollections/FCS style). `in`/`end`
                    // keep the unconditional close.
                    if (top->sort == S_EXPR && ((!strcmp(w, "else") || !strcmp(w, "elif")) ? (top->inl != 0 && top->thn != 0)
                                                : (!strcmp(w, "in") || !strcmp(w, "end")))) {
                        s->n--; lexer->result_symbol = LAYOUT_END; return true;
                    }
                    if (top->sort == S_TRY && (!strcmp(w, "with") || !strcmp(w, "finally"))) {
                        s->n--; lexer->result_symbol = LAYOUT_END; return true;
                    }
                    // KNOWN GAP: a mid-line `with` after a NEXT-LINE record type
                    // body (`type M =⏎  { fields } with⏎  member …`, FSharpPlus
                    // NonEmptyMap). Closing S_TYPEBODY here was tried 2026-06-10
                    // and mis-fired on 23 bench files (interface_impl /
                    // member-accessor `with` forms share the state) — reverted.
                    // The same-line `= { … } with` form parses.
                    // `finally` is owned EXCLUSIVELY by try/finally (unlike
                    // `with`), so when an S_TRY is open SOMEWHERE below, every
                    // inner inline body must close first — one per invocation —
                    // until the S_TRY branch above fires:
                    //   `seq { try for e in c () do yield e finally comp () }`
                    // closes the for-body here, then the try-body above.
                    if (!strcmp(w, "finally") && layoutish(top->sort)) {
                        for (size_t i = 0; i + 1 < s->n; i++) {
                            if (s->stk[i].sort == S_TRY) {
                                s->n--; lexer->result_symbol = LAYOUT_END; return true;
                            }
                        }
                    }
                }
                if (valid[ELEMENT_DSL_OPEN] && element_dsl_parens_brace(lexer)) {
                    lexer->result_symbol = ELEMENT_DSL_OPEN; return true;
                }
            }
            // CE_BRACE_OPEN (zero-width): the `{` of a computation_expression body.
            // Emitted ONLY when the brace content is a CE body — NOT a record field
            // (`ident =`/`ident :`), NOT an object expression (`new …`), NOT a
            // copy-update (`base with …`). Lets `head { new … }` / `head { f = … }`
            // divert to application(head, object_expression/record) while
            // `head { return … }` / `async { … }` stay a computation_expression.
            // Zero-width (mark_end still at baseline): the advances below only PEEK;
            // on a match tree-sitter then lexes the literal `{`, on no-match we fall
            // through to `return false` and the literal `{` is lexed for the
            // record/object/application path.
            if (c == '{' && valid[CE_BRACE_OPEN]) {
                lexer->advance(lexer, true);            // past `{`
                // `{|` is an anonymous-record opener, never a CE body `{`.
                if (lexer->lookahead != '|') {
                    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
                           lexer->lookahead == '\n' || lexer->lookahead == '\r') lexer->advance(lexer, true);
                    if (ce_brace_content_is_ce_body(lexer)) { lexer->result_symbol = CE_BRACE_OPEN; return true; }
                }
            }
            // A TRAILING `;` at end-of-line in a layout body that is about to
            // close by dedent/EOF: consume it INTO the LAYOUT_END (`let f () =⏎
            // g ()⏎ a;` — NuGetV3 style). Once the body is a sequence the
            // grammar has no slot for the `;` (a grammar-level optional never
            // fires — the `;` shift commits to the separator reading), so the
            // terminator must disappear here. Only when the line truly ends
            // after the `;` (an inline `a; b` keeps its literal separator) and
            // only on a real dedent — an equal-column next line is a SIBLING
            // statement (`module M =⏎ f x;⏎ g y;`), handled by the `";"` _token.
            if (c == ';' && valid[LAYOUT_END] && top && layoutish(top->sort)) {
                lexer->advance(lexer, false);           // consume `;`
                if (lexer->lookahead != ';') {          // leave `;;` to the extras
                    lexer->mark_end(lexer);             // token = just the `;`
                    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
                    if (lexer->lookahead == '\n' || lexer->lookahead == '\r' || lexer->lookahead == 0) {
                        uint32_t ncol; int32_t nfirst = 0;
                        if (!next_line_indent(lexer, &ncol, &nfirst) || ncol < top->col) {
                            s->n--; lexer->result_symbol = LAYOUT_END; return true;
                        }
                    }
                }
                return false;                           // not trailing: literal `;`
            }
            // Same disease, BRACKET flavor: a trailing `;` right before the
            // closing delimiter of a CE / list / array body (`seq { yield x; }`,
            // `yield 1;`⏎`}`) — the grammar's own trailing-`;` optional never
            // fires (the `;` shift commits to extending the LAST STATEMENT's
            // expression into a sequence), so consume the `;` INTO the
            // BRACKET_CLOSE. Same-line (`; }`) and next-line-closer forms.
            if (c == ';' && valid[BRACKET_CLOSE] && top && top->sort == S_BRACKET) {
                lexer->advance(lexer, false);           // consume `;`
                if (lexer->lookahead != ';') {          // leave `;;` to the extras
                    lexer->mark_end(lexer);             // token = just the `;`
                    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
                    int32_t a = lexer->lookahead;
                    if (is_close_bracket(a) || a == '}' || a == ']') {
                        s->n--; lexer->result_symbol = BRACKET_CLOSE; return true;
                    }
                    if (a == '\n' || a == '\r' || a == 0) {
                        uint32_t ncol; int32_t nfirst = 0;
                        if (next_line_indent(lexer, &ncol, &nfirst) &&
                            (is_close_bracket(nfirst) || nfirst == '}' || nfirst == ']')) {
                            s->n--; lexer->result_symbol = BRACKET_CLOSE; return true;
                        }
                    }
                }
                return false;                           // not trailing: literal `;`
            }
            // DOC-RESUME dispatch: a previous zero-width token (a close, or an
            // earlier gate) was anchored AT this `///` line — the scan resumes
            // mid-line ON the docs. Skip the doc block (+ blank lines), compute
            // the post-doc line's first/col, and run the same dispatch the
            // boundary path uses (CASE/AND gates, typebody-close-at-docs). On
            // no-match return false: the parser lexes the doc itself next.
            if (c == '/' && valid[CASE_DOCS_OPEN] + valid[AND_DOCS_OPEN] + valid[LAYOUT_END] + valid[LAYOUT_SEMI] > 0) {
                lexer->advance(lexer, true);
                if (lexer->lookahead == '/') {
                    lexer->advance(lexer, true);
                    if (lexer->lookahead == '/') {
                        g_skipped_doc_lines = true;
                        // consume the rest of this doc line, then any further
                        // doc-only / blank lines
                        for (;;) {
                            while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0)
                                lexer->advance(lexer, true);
                            if (lexer->lookahead == 0) return false;
                            if (lexer->lookahead == '\r') lexer->advance(lexer, true);
                            if (lexer->lookahead == '\n') lexer->advance(lexer, true);
                            uint32_t ind2 = 0;
                            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') { ind2++; lexer->advance(lexer, true); }
                            if (lexer->lookahead == '\n' || lexer->lookahead == '\r') continue;
                            if (lexer->lookahead == '/') {
                                lexer->advance(lexer, true);
                                if (lexer->lookahead == '/') { lexer->advance(lexer, true); continue; }
                                return false;
                            }
                            // post-doc real line
                            if (try_and_docs(s, lexer, valid, lexer->lookahead, ind2, top)) return true;
                            return false;
                        }
                    }
                }
                return false;
            }
            // CTOR_TUPLE_GATE (zero-width): at a let-binding NAME position,
            // `ident(.ident)* ( … ) ,` means `let Ctor(a, b), rest = …` — a
            // tuple DECONSTRUCTION (fn defs never have `,` after params). At
            // this consumption-safe tail the word-branch above may have eaten
            // the first ≤9 identifier chars; resume from wherever we are.
            if (valid[CTOR_TUPLE_GATE] &&
                ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
                while ((lexer->lookahead >= 'a' && lexer->lookahead <= 'z') ||
                       (lexer->lookahead >= 'A' && lexer->lookahead <= 'Z') ||
                       (lexer->lookahead >= '0' && lexer->lookahead <= '9') ||
                       lexer->lookahead == '_' || lexer->lookahead == '\'' ||
                       lexer->lookahead == '.') lexer->advance(lexer, true);
                while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
                if (lexer->lookahead == '(') {
                    lexer->advance(lexer, true);
                    int pdepth = 1, guard = 0;
                    bool ok = true;
                    while (pdepth > 0 && ok) {
                        if (++guard > 20000 || lexer->lookahead == 0 ||
                            lexer->lookahead == '\n' || lexer->lookahead == '\r') { ok = false; break; }
                        if (lexer->lookahead == '(') pdepth++;
                        else if (lexer->lookahead == ')') pdepth--;
                        else if (lexer->lookahead == '"') {
                            lexer->advance(lexer, true);
                            while (lexer->lookahead != '"' && lexer->lookahead != 0 && lexer->lookahead != '\n') {
                                if (lexer->lookahead == '\\') lexer->advance(lexer, true);
                                lexer->advance(lexer, true);
                            }
                            if (lexer->lookahead != '"') { ok = false; break; }
                        }
                        lexer->advance(lexer, true);
                    }
                    if (ok) {
                        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true);
                        if (lexer->lookahead == ',') { lexer->result_symbol = CTOR_TUPLE_GATE; return true; }
                    }
                }
                return false;   // consumption-safe: this is a return-false tail
            }
            // Same-line block comment after code (`1 (* a (* b *) *)`): the
            // external must lex it (nesting); nothing else fires for `(`.
            if (c == '(') {
                lexer->advance(lexer, false);
                if (lexer->lookahead == '*') { lexer->advance(lexer, false); return finish_block_comment(lexer, valid); }
                return false;
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
    g_region_stop = true;
    g_doc_gate_possible = valid[CASE_DOCS_OPEN] || valid[AND_DOCS_OPEN];
    g_top_col_for_docs = top ? top->col : 0;
    bool nli = next_line_indent(lexer, &col, &first);
    g_region_stop = false;
    if (!nli) {                                             // EOF after trailing blanks
        // Dangling `///` docs at EOF inside this body: let them lex as a
        // standalone statement before the body closes (see the dedent twin).
        if (g_skipped_doc_lines && top && layoutish(top->sort) && g_doc_indent >= top->col) return false;
        if (valid[BRACKET_CLOSE] && top && top->sort == S_BRACKET) { s->n--; lexer->result_symbol = BRACKET_CLOSE; return true; }
        if (valid[MATCH_END]    && top && top->sort == S_MATCH)    { s->n--; lexer->result_symbol = MATCH_END;    return true; }
        if (valid[LAYOUT_END]   && top && layoutish(top->sort))   { s->n--; lexer->result_symbol = LAYOUT_END;   return true; }
        return false;
    }
    if (first == 2) {
        // Line-start comment-ONLY line — next_line_indent consumed the whole
        // comment with advance(false) and mark_end'ed at its `*)`.
        if (!valid[BLOCK_COMMENT] && !valid[BLOCK_DOC_COMMENT]) return false;
        lexer->result_symbol = (g_comment_doc && valid[BLOCK_DOC_COMMENT]) ? BLOCK_DOC_COMMENT
                             : (valid[BLOCK_COMMENT] ? BLOCK_COMMENT : BLOCK_DOC_COMMENT);
        return true;
    }
    if (!top) {
        // Top-level (no layout context on the stack): a new statement on this line that
        // is an element-DSL builder (`pipeline "x" {` / `div() {` as a NON-first
        // top-level item) still needs its `_element_dsl_open` marker. There's no context
        // to emit a separator, so probe here at the line's first token; otherwise
        // `pipeline "x"` reduces to a plain application and the `{ … }` body errors.
        // `///`-docs probe FIRST: try_element_dsl consumes lookahead reading
        // words on a miss, which would corrupt the and/case word check.
        if (try_and_docs(s, lexer, valid, first, col, NULL)) return true;
        if (try_element_dsl(lexer, valid)) return true;
        // Top-level builder with its `{` on the next line (`seq`⏎`{ … }`).
        if (try_ce_brace(lexer, valid, first)) return true;
        return false;
    }

    // A leading `|` is a match arm UNLESS it is `|]` (array close) or `|}` (anon
    // record close). We can't cheaply peek the 2nd char here (next_line_indent
    // already advanced), so treat `|` as an arm marker; the bracket cases are
    // handled by BRACKET_CLOSE above/below via valid-gating.
    bool bar_arm = (first == '|');
    // `|>` `||` `|?>` … — a `|`-led OPERATOR is never an arm marker; peek the
    // char after the `|` once, here, so BOTH the infix check below and the
    // S_MATCH close (`|>` at exactly the arm column pipes the WHOLE match) see
    // the same classification. `|]`/`|}` stay arm-ish (bracket closers handle).
    int32_t bar_c1 = 0;
    if (bar_arm) {
        lexer->advance(lexer, true);
        bar_c1 = lexer->lookahead;
        if (bar_c1 == '>' || bar_c1 == '|' || bar_c1 == '?' || bar_c1 == '@' ||
            bar_c1 == '!' || bar_c1 == '%' || bar_c1 == '&' || bar_c1 == '*' ||
            bar_c1 == '+' || bar_c1 == '-' || bar_c1 == '.' || bar_c1 == '/' ||
            bar_c1 == '<' || bar_c1 == '=' || bar_c1 == '^' || bar_c1 == '~' ||
            bar_c1 == '$') bar_arm = false;
    }

    // `///` docs followed by `and` — gate the and-clause doc slot. FIRST: by
    // the time the per-sort logic runs, semi/decl-starter branches treat `and`
    // specially and the grammar may reduce the preceding decl, dropping the
    // docs to the standalone net. Strictly gated (valid + docs-were-skipped +
    // exact word `and`), so it cannot preempt closes that matter: when an
    // enclosing body still has to close first, AND_DOCS_OPEN isn't valid yet.
    if (try_and_docs(s, lexer, valid, first, col, top)) return true;

    // A leading infix operator continues the previous expression (F#'s
    // leading-operator rule) — UNLESS it DEDENTS below an EXPRESSION body
    // (S_EXPR: then/elif/else/lambda/let-in value) or below a match ARM column
    // (S_MATCH), in which case that body/arm-list must close first and
    // re-invocation continues the OUTER chain. This pipes the whole if in
    // `if c then a else b⏎ |> f`, and the whole match in `|> match … with⏎
    // | arm -> …⏎ |> next` (Chocolatey pipeline style) — without the S_MATCH
    // case the dedented `|>` extended the LAST ARM's body, and continuation
    // ARGUMENT lines after it then mis-lexed as new arm patterns. A decl body
    // (S_LAYOUT) keeps the previous behaviour.
    // S_EXPR/S_MATCH: any dedent closes first. S_LAYOUT (arm/decl bodies):
    // FSC grants infix tokens limited offside GRACE (token length + 1), so a
    // mildly-dedented op still continues the body — but one dedented WELL below
    // (`|>` at the pipeline col under a match arm body, Chocolatey style) is
    // offside and must close the body/arm-list first.
    bool infix_continues = !(top->sort == S_EXPR && col < top->col) &&
                           // ≤ for S_MATCH: an op AT the arm column can't be an
                           // arm — the arm-list must END so the op continues the
                           // whole match (`| false -> b⏎|> g` at the arm col).
                           !(top->sort == S_MATCH && col <= top->col) &&
                           !(top->sort == S_LAYOUT && col + 4 < top->col);

    // `|>`/`<|`/`>>` pipe chains, `=`/`<`/`>`/`*`/… arithmetic, `::` cons.
    // `|` alone is a match arm (not infix); only `|>`/`||` are. `&`/`:` count
    // only doubled. Other unary-capable leads (`!` `~`) are excluded.
    if (infix_continues) {
        int32_t c0 = first;
        if (c0 == '|' || c0 == '<' || c0 == '>' || c0 == '=' ||
            c0 == '*' || c0 == '/' || c0 == '%' || c0 == '^' || c0 == '&' || c0 == ':') {
            int32_t c1;
            if (c0 == '|') c1 = bar_c1;                  // already peeked above
            else { lexer->advance(lexer, true); c1 = lexer->lookahead; }
            bool infix = false;
            // `|` + any operator char = a custom `|`-led infix operator
            // continuation (`|>`, `||`, `|?>`, `||>`, `|@`, …). A match-arm
            // `|` is followed by whitespace or a pattern char instead.
            if (c0 == '|')      infix = (c1 == '>' || c1 == '|' || c1 == '?' ||
                                         c1 == '@' || c1 == '!' || c1 == '%' ||
                                         c1 == '&' || c1 == '*' || c1 == '+' ||
                                         c1 == '-' || c1 == '.' || c1 == '/' ||
                                         c1 == '<' || c1 == '=' || c1 == '^' ||
                                         c1 == '~' || c1 == '$');
            else if (c0 == '&') infix = (c1 == '&');                        // &&
            else if (c0 == ':') infix = (c1 == ':' || c1 == '>' || c1 == '?'); // :: :> :?
            else if (c0 == '/') infix = (c1 != '/');                       // `//` = COMMENT, not an operator
            else                infix = true;                              // = < > * % ^
            if (infix) return false;
        }

        // Leading `+`/`-`/`@` — also continuation, but only in a layout body (a
        // bracket / match arm-list keeps newline-as-element/arm separator). They
        // are unary/prefix-capable. Excluded forms: `->` (lambda/match arrow),
        // `@"…"` (verbatim string), `@>` / `@@>` (code-quotation close).
        // `@@` followed by anything but `>` is the custom path-concat operator
        // (FAKE's `dir @@ file` written leading) — a continuation.
        if (layoutish(top->sort) && (first == '+' || first == '-' || first == '@')) {
            lexer->advance(lexer, true);
            int32_t c1 = lexer->lookahead;
            if (first == '+') return false;
            if (first == '-' && c1 != '>') return false;
            if (first == '@') {
                if (c1 != '"' && c1 != '>' && c1 != '@') return false;
                // `@>` / `@@>` at the BODY column closes a multi-line quotation
                // (`<@`⏎`    body`⏎`@>`): no separator, no close — the token
                // belongs to the still-open quotation expression. A DEDENTED
                // closer falls through so the layout close fires first.
                if (c1 == '>' && col == top->col) return false;
                if (c1 == '@') {
                    lexer->advance(lexer, true);
                    if (lexer->lookahead != '>') return false;   // `@@…` operator, not `@@>`
                    if (col == top->col) return false;           // `@@>` at body col — see above
                }
            }
        }

        // A leading `.` is always a continuation: a fluent member chain on its
        // own line (`builder⏎ .Method()`), a `.`-led custom operator (`.>>.`,
        // FParsec style), or a `..` range — no F# statement can START with `.`.
        if (layoutish(top->sort) && first == '.') return false;
    }

    switch (top->sort) {
        case S_BRACKET:
            if (valid[BRACKET_CLOSE] && (is_close_bracket(first) || first == '|')) { s->n--; lexer->result_symbol = BRACKET_CLOSE; return true; }
            // Same blocker as LAYOUT_SEMI: a CE statement separator must not fire
            // before `else`/`elif`/… — otherwise `if c then return a`⏎`else …`
            // inside a CE detaches the else (banked-fix #3, in the new model).
            if (valid[BRACKET_SEMI] && col == top->col && !semi_blocked(lexer, first)) { lexer->result_symbol = BRACKET_SEMI; return true; }
            // First element of a `{`-block body that is itself an element DSL
            // (`div() {⏎ span() {…}`): the mid-line block is unreachable from the
            // line-boundary path, so probe here (after the separator above, so a
            // SUBSEQUENT element gets its separator first then this on re-invoke).
            if (try_element_dsl(lexer, valid)) return true;
            return false;
        case S_MATCH:
            // Close the arm-list when a line dedents below the arm column, or sits
            // at the arm column but does NOT start a new `|` arm. EXCEPTION: a
            // `|` exactly TWO columns left of the arm column is a continuation
            // arm whose PATTERN aligns with the (inline) first arm's pattern —
            // Hopac house style:
            //   … >>= function Cons (_, i) -> push xM i x
            //                | Nil -> imp ()
            if (valid[MATCH_END] && (col < top->col || (col == top->col && !bar_arm)) &&
                !(bar_arm && col + 2 == top->col)) { s->n--; lexer->result_symbol = MATCH_END; return true; }
            return false;
        case S_LAYOUT:
        case S_TYPEBODY:
        case S_EXPR:
        case S_DECL:
        case S_TRY:
            // DANGLING DOC: skipped `///` lines sit AT/INSIDE this body, but
            // the line after them dedents — the docs belong to THIS body (a
            // floating doc statement), not to the dedented declaration. Hold
            // the close; the doc lexes as a standalone statement (the wrapper
            // fork dies at the close that follows), then the dedent re-fires.
            if (valid[LAYOUT_END] && col < top->col &&
                g_skipped_doc_lines && g_doc_indent >= top->col) return false;
            if (valid[LAYOUT_END] && col < top->col) { s->n--; lexer->result_symbol = LAYOUT_END; return true; }
            // A leading `|` arm marker AT the body column: FSC permits
            // continuation arms MORE indented than their match (`| _ -> ()` at
            // col 8, match arms at col 4 — FCS PostInferenceChecks style). The
            // body must close so the over-indented arm reaches the enclosing
            // match. Gated on such a match existing further left, and NOT
            // S_TYPEBODY (DU cases legitimately lead with `|` at the body col).
            if (valid[LAYOUT_END] && bar_arm && col == top->col && top->sort != S_TYPEBODY) {
                for (int i = (int)s->n - 2; i >= 0; i--) {
                    if (s->stk[i].sort == S_MATCH && s->stk[i].col < col) {
                        s->n--; lexer->result_symbol = LAYOUT_END; return true;
                    }
                    if (s->stk[i].col < col && s->stk[i].sort != S_MATCH) break;
                }
            }
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
            // A module-only keyword (`open`/`module`/`namespace`/`exception`)
            // aligned AT the type-body column closes the type body. A NON-indented
            // DU (`type T =⏎| A⏎| B⏎open …`) puts the body at the module column, so
            // a dedent never fires; without this, the union field type
            // over-consumes the following `open Foo` as a postfix type.
            if (top->sort == S_TYPEBODY && valid[LAYOUT_END] && col == top->col &&
                first >= 'a' && first <= 'z') {
                char w[12]; size_t wn = 0; int32_t lk = lexer->lookahead;
                while (wn < 11 && lk >= 'a' && lk <= 'z') { w[wn++] = (char)lk; lexer->advance(lexer, true); lk = lexer->lookahead; }
                w[wn] = '\0';
                bool boundary = !((lk >= 'a' && lk <= 'z') || (lk >= 'A' && lk <= 'Z') ||
                                  (lk >= '0' && lk <= '9') || lk == '_' || lk == '\'');
                if (boundary && (!strcmp(w, "open") || !strcmp(w, "module") ||
                                 !strcmp(w, "namespace") || !strcmp(w, "exception"))) {
                    s->n--; lexer->result_symbol = LAYOUT_END; return true;
                }
            }
            // S_DECL: never separate before a declaration keyword — the line is a
            // new `_token`, not a `sequence_expression` continuation of the prior
            // bare-expression statement.
            if (top->sort == S_DECL && decl_starter(lexer, first)) return false;
            if (valid[LAYOUT_SEMI] && col == top->col && !semi_blocked(lexer, first) &&
                !(g_skipped_doc_lines && word_is_decl_kw(g_post_doc_word))) { lexer->result_symbol = LAYOUT_SEMI; return true; }
            // Element DSL as the first statement of an indented let/expr body
            // (`let page =⏎ div() {…}`): probe after the separator above.
            if (try_element_dsl(lexer, valid)) return true;
            // CE body `{` on its OWN line below the builder (`seq`⏎`    {`⏎
            // `        yield …` — FAKE/WiX style): the mid-line CE_BRACE_OPEN
            // dispatch is unreachable from here, so classify the brace content
            // now. Records / object expressions keep the literal `{` (no token).
            if (try_ce_brace(lexer, valid, first)) return true;
            return false;
    }
    return false;
}
