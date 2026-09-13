#include "tree_sitter/parser.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// ============================================================================
// Uniform-layout external scanner for F#.
//
// Model (after tree-sitter-haskell):
//   * OPENS are GRAMMAR-driven: the grammar emits a zero-width open token right
//     after the layout keyword (`=` / `then` / `->` / `do` / `with` / `[` / `{`).
//     The scanner reacts by pushing a context at the body's first-token column.
//     It does NOT guess which construct opens.
//   * CLOSES / SEPARATORS are SCANNER-driven by column comparison, but GATED on
//     `valid(...)` - true when the grammar expects that token OR on parse-error
//     recovery (all-symbols-valid). The scanner decides only WHETHER to close at
//     this indent, never WHICH construct.
//   * One context stack of sorts (see Sort below). Multi-level dedent = one
//     close per scan call; tree-sitter re-invokes at the same
//     (mark_end-restored) position.
//
// Layout of this file:
//   * Sym / Sort / Ctx / Scanner: the tokens, the context sorts and the state.
//     `Scanner.scan` is zeroed at the start of every scan; only the stack and
//     the two claim columns outlive a scan.
//   * Lexer idioms and peek helpers (`next_line_indent`, `peek_body_col`, the
//     `try_*` probes). A probe that advances the lexer is "destructive": its
//     caller must decide right after it, never fall through to another probe.
//   * Scan phases (`scan_*`, `mid_*`, `boundary_*`) in the order they run, and
//     `scanner_scan` which strings them together. See "Scan phases".
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
    RECORD_OPEN,        // `{` record body - peeks `ident =`/`ident :`; suppressed for new/copy-update
    BLOCK_OPEN,         // newline-gated layout open for MODULE bodies (S_LAYOUT, closes via LAYOUT_END)
    TYPE_OPEN,          // newline-gated layout open for TYPE bodies (S_TYPEBODY - also closes before `with`)
    EXPR_OPEN,          // expression body (then/elif body, lambda, let-in value) - S_EXPR
    ELSE_OPEN,          // final-else body - S_EXPR, but SUPPRESSED when next token is `if`
                        //   (`else if` flattens to an elif clause, no nested else-body)
    FLOAT_TRAILING_DOT, // lexical: `1.` trailing-dot float (unrelated to layout)
    INTERP_STRING_TEXT,   // text chunk in $"..."   (external so // isn't a comment)
    INTERP_VERBATIM_TEXT, // text chunk in $@"..." / @$"..."
    INTERP_TRIPLE_TEXT,   // text chunk in $"""..."""
    FOR_OPEN,             // `for ... do` body open; suppressed for query-CE operators
    CTOR_ATTR,            // zero-width: attribute on a primary ctor - only when `[<...>]+ (` follows
    TRY_OPEN,             // try/finally body open (S_TRY) - closes before `with`/`finally`
    LABEL_ATTR,           // zero-width: attribute on a labelled param - only when `[<...>]+ ident:` follows
    ELEMENT_DSL_OPEN,     // zero-width: Oxpecker element-DSL builder - only when `ident ( ... ) {` follows
    AND_DOCS_OPEN,        // zero-width: `///` doc lines followed by the word `and` - docs attach to the and-clause
    CASE_DOCS_OPEN,       // zero-width: `///` doc lines followed by `|` - docs attach to the union/enum case
    PAREN_FIELD_OPEN,     // named-field-pattern body open `Foo(ident = ...)` - S_BRACKET context for newline fields
    CE_BRACE_OPEN,        // the `{` of a computation_expression body - consumed+emitted ONLY when brace content is a CE body (not record/object/copy-update)
    BLOCK_COMMENT,        // `(* ... *)` NESTED (regex can't nest)
    BLOCK_DOC_COMMENT,    // `(** ... *)` doc form
    THEN_OPEN,            // then/elif body open - S_EXPR with thn=1 (mid-line `else` may close it)
    LAZY_OPEN,            // lazy block-body open - S_EXPR, declines INLINE bodies
    CTOR_TUPLE_GATE,      // zero-width: `let Ctor(a, b), rest` - only when `ident ( ... ) ,` follows
    PREPROC_BREAK,        // zero-width: a `#if`-family directive line splits a signature - ends the member before the next branch's declaration line
    DECL_SEMI,            // a statement-terminating `;` directly before a DECLARATION line - consumed as trivia (extras) so the sequence can end
    MEMBERS_OPEN,         // zero-width: members indented below a SAME-LINE type body (`type DU = | A`\n`    member ...`) - pushes S_TYPEBODY
    LABEL_GATE,           // zero-width: `ident :` ahead (not `::` `:>` `:?` `:=`) - a labelled type element (`x: int -> ...`)
    PAREN_BLOCK_OPEN,     // zero-width: `(` followed by a newline - pushes S_EXPR at the body column, closed by `)`
    INFIX_BLOCK_OPEN,     // zero-width: `&&`/`||` then a newline and a deeper line - pushes S_EXPR at that column
    FIELD_BLOCK_OPEN,     // zero-width: record field `=` then a newline - pushes S_EXPR at the value column
} Sym;

// Sorts (all dedent-close via LAYOUT_END except as noted):
//   S_LAYOUT   generic decl body (let/member/module body)
//   S_TYPEBODY type body - ALSO closes before a `with` augmentation at body col
//   S_EXPR     expression body (then/elif/else/lambda/let-in value) - ALSO closes
//              before an inline `else`/`elif`/`in`. Crucially a DECL body
//              (S_LAYOUT) does NOT, so `module M =\n let f = if a then 1 else 0\n
//              let g` closes only the then-body at `else`, not the module body.
//   S_MATCH    arm-list (closes on dedent below arm col; no semicolons)
//   S_BRACKET  [ / [| / { ... explicit-close
//   S_DECL     module/source declaration body - like S_LAYOUT, but a
//              `_layout_semi` is NEVER emitted before a declaration keyword
//              (`let`/`type`/`module`/...). A module body is `repeat(_token)`, so a
//              bare-expression `_token` must not extend into the next declaration
//              as a `sequence_expression` (`ignore x\n let y = ...` is two decls,
//              whereas a function body - S_LAYOUT - DOES sequence `let` as let-in).
//   S_TRY      try / finally body - like S_EXPR, but ALSO closes before an inline
//              `with`/`finally` (a dedicated sort so the close is try-specific and
//              doesn't fire for a `match ... with` inside an enclosing expr body).
typedef enum { S_LAYOUT, S_MATCH, S_BRACKET, S_TYPEBODY, S_EXPR, S_DECL, S_TRY } Sort;

// True for the dedent-closing layout sorts (decl body, type body, expr body, module body, try body).
static inline bool layoutish(uint8_t sort) { return sort == S_LAYOUT || sort == S_TYPEBODY || sort == S_EXPR || sort == S_DECL || sort == S_TRY; }

typedef struct { uint16_t col; uint8_t sort; uint8_t inl:1, thn:1, par:1, inf:1; } Ctx;  // inl: body opened INLINE; thn: then/elif body (closeable at mid-line else); par: `(` block body (only `)` closes it); inf: `&&`/`||` right-operand block (closes before `->`/then/do/with)

#define MAXD 512

// State that lives for the whole parse and is serialized with the parse tree.
typedef struct {
    Ctx stk[MAXD];
    uint16_t n;
    // Column of an `else`/`elif` that already closed a then-body, so the same token
    // does not close the enclosing then-body too. -1 when none.
    int32_t else_claim_col;
    // Column of an `in` that just closed an arm-list and may still close the let
    // value around it. -1 when none.
    int32_t in_claim_col;
    // State of the current scan only, zeroed at the start of every scan.
    struct {
        // Inputs of the main next_line_indent call: stop at a line-start block comment,
        // and whether a doc-attachment gate is valid at the layout top column.
        bool region_stop;
        bool doc_gate_possible;
        uint32_t top_col_for_docs;
        // Outputs of next_line_indent: what it skipped on its way to the next real line,
        // the indent of the first skipped `///` line, and whether a stopped block
        // comment is the `(**` doc form.
        bool skipped_doc_lines;
        bool skipped_line_comments;
        bool skipped_directive;
        bool skipped_alt_directive;
        uint32_t doc_indent;
        bool comment_doc;
        // The word after skipped doc lines (try_and_docs) and the last word read by the
        // mid-line dispatch, for the checks that run after them in the same scan.
        char post_doc_word[10];
        char midline_word[10];
    } scan;
} Scanner;

// --- Lexer idioms -----------------------------------------------------------
static inline bool at_line_end(int32_t c) { return c == '\n' || c == '\r' || c == 0; }
static inline bool is_lower(int32_t c) { return c >= 'a' && c <= 'z'; }
static inline bool is_alpha(int32_t c) { return is_lower(c) || (c >= 'A' && c <= 'Z'); }
static inline bool is_digit(int32_t c) { return c >= '0' && c <= '9'; }
static inline bool is_ident_char(int32_t c) { return is_alpha(c) || is_digit(c) || c == '_' || c == '\''; }
static inline bool is_name_start(int32_t c) { return is_alpha(c) || c == '_' || c == '`'; }
// Spaces and tabs.
static inline void skip_hspace(TSLexer *lexer) { while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true); }
// Spaces, tabs and newlines.
static inline void skip_space(TSLexer *lexer) { while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\n' || lexer->lookahead == '\r') lexer->advance(lexer, true); }
// The rest of the current line, stopping at the newline.
static inline void skip_line(TSLexer *lexer) { while (!at_line_end(lexer->lookahead)) lexer->advance(lexer, true); }

static bool skip_bracket_attrs(TSLexer *lexer);
// `let x = v in ...` on ONE line: true when a bare `in` (outside brackets and
// strings) follows on the rest of the line. Consumes lookahead.
static bool line_has_in_keyword(TSLexer *lexer) {
    int depth = 0; int32_t prev = ' ';
    while (!at_line_end(lexer->lookahead)) {
        int32_t c = lexer->lookahead;
        if (c == '"') {
            lexer->advance(lexer, true);
            while (lexer->lookahead != '"' && lexer->lookahead != '\n' && lexer->lookahead != 0) {
                if (lexer->lookahead == '\\') lexer->advance(lexer, true);
                lexer->advance(lexer, true);
            }
            if (lexer->lookahead == '"') lexer->advance(lexer, true);
            prev = '"'; continue;
        }
        if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') depth--;
        else if (c == 'i' && depth == 0 && (prev == ' ' || prev == '\t')) {
            lexer->advance(lexer, true);
            if (lexer->lookahead == 'n') {
                lexer->advance(lexer, true);
                int32_t a = lexer->lookahead;
                if (a == ' ' || a == '\t' || at_line_end(a)) return true;
            }
            prev = 'i'; continue;
        }
        else if (c == 'f' && depth == 0 && (prev == ' ' || prev == '\t')) {   // a `for x in` header owns its `in`
            lexer->advance(lexer, true);
            if (lexer->lookahead == 'o') {
                lexer->advance(lexer, true);
                if (lexer->lookahead == 'r') {
                    lexer->advance(lexer, true);
                    int32_t a = lexer->lookahead;
                    if (a == ' ' || a == '\t' || a == '(') return false;
                }
            }
            prev = 'f'; continue;
        }
        prev = c; lexer->advance(lexer, true);
    }
    return false;
}

// `[?]ident ws* :` ahead, with the `:` not starting `::` `:>` `:?` `:=`: the
// start of a labelled type element. Consumes lookahead; callers only run it
// where nothing else needs the word afterwards.
static bool try_label_gate(TSLexer *lexer) {
    // The boundary path's infix probe may already have consumed a leading `?`.
    skip_hspace(lexer);
    if (lexer->lookahead == '?') {
        lexer->advance(lexer, true);
        skip_hspace(lexer);
    }
    int32_t a = lexer->lookahead;
    if (a == '`') {
        lexer->advance(lexer, true);
        if (lexer->lookahead != '`') return false;
        lexer->advance(lexer, true);
        while (1) {
            if (lexer->lookahead == 0 || lexer->lookahead == '\n') return false;
            if (lexer->lookahead == '`') {
                lexer->advance(lexer, true);
                if (lexer->lookahead == '`') { lexer->advance(lexer, true); break; }
                continue;
            }
            lexer->advance(lexer, true);
        }
    } else {
        if (!(is_alpha(a) || a == '_')) return false;
        while (1) {
            int32_t ch = lexer->lookahead;
            if (is_ident_char(ch)) lexer->advance(lexer, true);
            else break;
        }
    }
    skip_hspace(lexer);
    if (lexer->lookahead != ':') return false;
    lexer->advance(lexer, true);
    int32_t n = lexer->lookahead;
    if (n == ':' || n == '>' || n == '?' || n == '=') return false;
    lexer->result_symbol = LABEL_GATE; return true;
}

// `[<...>]+ ident:` / `[<...>]+ ?ident:` ahead: an attribute on a LABELLED
// (member-signature / delegate) parameter. Consumes lookahead; the caller
// must not probe further on a miss.
static bool try_label_attr(TSLexer *lexer) {
    if (!skip_bracket_attrs(lexer)) return false;
    if (lexer->lookahead == '?') lexer->advance(lexer, true);
    int32_t a = lexer->lookahead;
    if (!(is_alpha(a) || a == '_')) return false;
    while (1) {
        int32_t ch = lexer->lookahead;
        if (is_ident_char(ch)) lexer->advance(lexer, true);
        else break;
    }
    skip_hspace(lexer);
    if (lexer->lookahead == ':') { lexer->result_symbol = LABEL_ATTR; return true; }
    return false;
}

// Is there a match/try/function arm-list (S_MATCH) anywhere on the stack? Used to
// tell a real match-arm `|` (close the inline arm body first) from a UNION case
// separator `type X = A | B` (no arm-list - must NOT close the enclosing body).
static bool has_match_ctx(Scanner *s) {
    for (int i = (int)s->n - 1; i >= 0; i--) if (s->stk[i].sort == S_MATCH) return true;
    return false;
}

static void *scanner_create(void) {
    Scanner *s = calloc(1, sizeof(Scanner));
    if (s) { s->else_claim_col = -1; s->in_claim_col = -1; }
    return s;
}
static void scanner_destroy(void *p) { free(p); }

static unsigned scanner_serialize(void *p, char *buf) {
    Scanner *s = p;
    unsigned size = sizeof(uint16_t) + (unsigned)s->n * sizeof(Ctx);
    if (size > TREE_SITTER_SERIALIZATION_BUFFER_SIZE) return 0;
    memcpy(buf, &s->n, sizeof(uint16_t));
    memcpy(buf + sizeof(uint16_t), s->stk, (size_t)s->n * sizeof(Ctx));
    return size;
}
static void scanner_deserialize(void *p, const char *buf, unsigned len) {
    Scanner *s = p; s->n = 0;
    if (len == 0) return;
    memcpy(&s->n, buf, sizeof(uint16_t));
    memcpy(s->stk, buf + sizeof(uint16_t), (size_t)s->n * sizeof(Ctx));
}

static void push(Scanner *s, uint8_t sort, uint32_t col) {
    if (s->n < MAXD) { s->stk[s->n].sort = sort; s->stk[s->n].col = (uint16_t)col; s->stk[s->n].inl = 0; s->stk[s->n].thn = 0; s->stk[s->n].par = 0; s->stk[s->n].inf = 0; s->n++; }
}

// Skip the body of a block comment whose `(*` is already consumed, through
// its matching `*)`. Nested comments and string literals inside the comment
// are honoured as FSC does (`(* the "*)" token *)` does not end at the quoted
// `*)`; `@"..."` has no escapes; `'"'` is a char). false on EOF.
static bool skip_comment_body(TSLexer *lexer, bool skip) {
    int depth = 1; int32_t prev = 0;
    while (depth > 0) {
        int32_t c = lexer->lookahead;
        if (c == 0) return false;
        if (c == '(') {
            lexer->advance(lexer, skip);
            if (lexer->lookahead == '*') { depth++; lexer->advance(lexer, skip); }
        } else if (c == '*') {
            lexer->advance(lexer, skip);
            if (lexer->lookahead == ')') { depth--; lexer->advance(lexer, skip); }
        } else if (c == '"') {
            bool verbatim = (prev == '@');
            lexer->advance(lexer, skip);
            while (lexer->lookahead != '"' && lexer->lookahead != 0) {
                if (!verbatim && lexer->lookahead == '\\') lexer->advance(lexer, skip);
                lexer->advance(lexer, skip);
            }
            if (lexer->lookahead == 0) return false;
            lexer->advance(lexer, skip);
        } else if (c == '\'') {
            lexer->advance(lexer, skip);
            if (lexer->lookahead == '"') {
                lexer->advance(lexer, skip);
                if (lexer->lookahead == '\'') lexer->advance(lexer, skip);
            }
        } else lexer->advance(lexer, skip);
        prev = c;
    }
    return true;
}

// Consume a block comment from just AFTER its `(*` (already advanced with
// advance(false), so the token starts at the `(`) through the MATCHING `*)`
// (nesting-aware) and emit BLOCK_COMMENT / BLOCK_DOC_COMMENT. External because
// a token regex cannot nest. Returns false on EOF (unterminated) or when
// neither symbol is valid - the reset internal lexer takes over.
static bool finish_block_comment(TSLexer *lexer, const bool *valid) {
    if (!valid[BLOCK_COMMENT] && !valid[BLOCK_DOC_COMMENT]) return false;
    if (lexer->lookahead == ')') return false;   // `(*)` = the multiply operator value, not a comment
    bool doc = false;
    if (lexer->lookahead == '*') {                 // `(**` - doc form...
        lexer->advance(lexer, false);
        if (lexer->lookahead == ')') {             // ...unless `(**)`: EMPTY normal comment
            lexer->advance(lexer, false);
            lexer->mark_end(lexer);
            lexer->result_symbol = valid[BLOCK_COMMENT] ? BLOCK_COMMENT : BLOCK_DOC_COMMENT;
            return true;
        }
        doc = true;
    }
    if (!skip_comment_body(lexer, false)) return false;   // unterminated
    lexer->mark_end(lexer);
    lexer->result_symbol = (doc && valid[BLOCK_DOC_COMMENT]) ? BLOCK_DOC_COMMENT
                         : (valid[BLOCK_COMMENT] ? BLOCK_COMMENT : BLOCK_DOC_COMMENT);
    return true;
}

static bool word_is_decl_kw(const char *w) {
    return !strcmp(w, "let") || !strcmp(w, "type") || !strcmp(w, "open") ||
           !strcmp(w, "module") || !strcmp(w, "exception") || !strcmp(w, "member") ||
           !strcmp(w, "static") || !strcmp(w, "override") || !strcmp(w, "abstract") ||
           !strcmp(w, "val") || !strcmp(w, "interface") || !strcmp(w, "new");
}

// `first` of a line that holds only a block comment (see next_line_indent).
#define FIRST_COMMENT_LINE 2

// Report a line's geometry: its indent and first significant char (`first` is optional).
static bool line_geometry(uint32_t *col, int32_t *first, uint32_t indent, int32_t ch) {
    if (first) *first = ch;
    *col = indent;
    return true;
}

// What a `#`-led line is to the offside rule.
typedef enum {
    DIRECTIVE_SKIPPED,    // `#if`-family, `#nowarn`/`#warnon`, `#line`, `# 14 "f.fs"`: trivia, consumed to the newline
    DIRECTIVE_STATEMENT,  // `#load`/`#r`/...: a statement of its own, the lexer sits after the `#`
} DirectiveKind;

// At a line-start `#`. `#if`-family, `#nowarn`/`#warnon` and `#line` lines are
// skipped like comment lines so they never dedent-close an open body (e.g.
// `#nowarn` between union cases, Argu style); the grammar consumes the directive
// tokens where it allows `preproc_directive`. NOT `#load`/`#r`: those are
// top-level statements that RELY on the dedent-close firing at their line. BOTH
// `#if` and `#else` branches parse as real code (Fable-style dual-path projects
// carry full-sized #else branches). Known cost: keyword splices
// (`#if A`\n`let`\n`#else`\n`use`\n`#endif`, FParsec) don't parse.
static DirectiveKind skip_directive_line(Scanner *s, TSLexer *lexer) {
    lexer->advance(lexer, true);
    skip_hspace(lexer);
    char w[8]; size_t wi = 0;
    while (wi < 7 && is_lower(lexer->lookahead)) { w[wi++] = (char)lexer->lookahead; lexer->advance(lexer, true); }
    w[wi] = '\0';
    if (strcmp(w, "if") == 0 || strcmp(w, "endif") == 0 ||
        strcmp(w, "elif") == 0 || strcmp(w, "else") == 0 ||
        strcmp(w, "nowarn") == 0 || strcmp(w, "warnon") == 0 ||
        strcmp(w, "line") == 0) {
        if (w[0] == 'i' || w[0] == 'e') s->scan.skipped_directive = true;
        if (strcmp(w, "else") == 0 || strcmp(w, "elif") == 0) s->scan.skipped_alt_directive = true;
        skip_line(lexer);
        return DIRECTIVE_SKIPPED;
    }
    // `# 14 "pars.fs"` - fsyacc/fslex line directive.
    if (wi == 0) {
        skip_hspace(lexer);
        if (is_digit(lexer->lookahead)) { skip_line(lexer); return DIRECTIVE_SKIPPED; }
    }
    return DIRECTIVE_STATEMENT;
}

// Compute the indent + first significant char of the NEXT non-blank, non-comment
// line. Returns false at EOF. Skips `//` line comments, `(* *)` nested block
// comments and `#if/#elif/#else/#endif` lines, which are extras transparent to
// the offside rule.
static bool next_line_indent(Scanner *s, TSLexer *lexer, uint32_t *col, int32_t *first) {
    s->scan.skipped_doc_lines = false;
    s->scan.skipped_line_comments = false;
    s->scan.skipped_directive = false;
    s->scan.skipped_alt_directive = false;
    bool marked_line_start = false;
    skip_line(lexer);
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
            // extents). The scan RESUMES from here afterwards - the mid-line
            // doc-resume dispatch in the scan body handles that position.
            if (s->scan.region_stop && s->scan.doc_gate_possible && indent >= s->scan.top_col_for_docs && !marked_line_start) { lexer->mark_end(lexer); marked_line_start = true; }
            lexer->advance(lexer, true);
            if (lexer->lookahead == '/') {
                lexer->advance(lexer, true);
                if (lexer->lookahead == '/') {
                    if (!s->scan.skipped_doc_lines) s->scan.doc_indent = indent;        // first doc line
                    s->scan.skipped_doc_lines = true;                             // a `///` doc line
                }
                s->scan.skipped_line_comments = true;
                skip_line(lexer);
                if (lexer->lookahead == 0) return false;
                continue;
            }
            return line_geometry(col, first, indent, '/');
        }
        if (lexer->lookahead == '(') {
            lexer->advance(lexer, !s->scan.region_stop);
            if (lexer->lookahead != '*') { return line_geometry(col, first, indent, '('); }
            if (s->scan.region_stop) {
                // Line-start block comment, MAIN boundary call. Consume it with
                // advance(false) - the token (if emitted) starts at the `(`; for
                // any OTHER outcome those advances are harmless (zero-width
                // layout tokens never re-mark past the baseline).
                lexer->advance(lexer, false);          // the `*`
                if (lexer->lookahead == ')') {         // `(*)` multiply-op value line
                    return line_geometry(col, first, indent, '(');
                }
                s->scan.comment_doc = (lexer->lookahead == '*');
                if (!skip_comment_body(lexer, false)) return false;
                skip_hspace(lexer);
                // Further block comments on the same line (`(* a *) (* b *)`).
                while (lexer->lookahead == '(') {
                    lexer->advance(lexer, false);
                    if (lexer->lookahead != '*') { return line_geometry(col, first, indent, '('); }
                    lexer->advance(lexer, false);
                    if (!skip_comment_body(lexer, false)) return false;
                    skip_hspace(lexer);
                }
                if (at_line_end(lexer->lookahead)) {
                    lexer->mark_end(lexer);            // full comment span (+trailing ws)
                    // mark_end ONLY here: in the comment-LED branch below the
                    // baseline must stay at the scan start, or the next
                    // zero-width close/semi would SWALLOW the comment text.
                    // Comment-ONLY line: emit it as ONE token BEFORE any close
                    // (extras are transparent; closes fire on re-scan with
                    // post-comment geometry). Handles NESTING - the reason the
                    // internal regex fallback can't do this one.
                    return line_geometry(col, first, indent, FIRST_COMMENT_LINE);
                }
                // Comment-LED line (`(* 4 *) 7`, aligned arrays): geometry first
                // - col is the COMMENT's start indent, first the real char; the
                // comment itself lexes via the internal-regex fallback later.
                // (KNOWN GAP: a NESTED comment here truncates in the fallback.)
                return line_geometry(col, first, indent, lexer->lookahead);
            }
            lexer->advance(lexer, true);
            if (lexer->lookahead == ')') { return line_geometry(col, first, indent, '('); }  // `(*)`
            if (!skip_comment_body(lexer, true)) return false;
            // CONTENT may follow the comment on the same line - a comment-LED
            // element (`(* 4 *) 7`, PriorityQueue-style aligned arrays). The
            // line then counts: its column is the COMMENT's start indent (where
            // the element visually begins) and `first` is the first real char.
            skip_hspace(lexer);
            if (!at_line_end(lexer->lookahead)) {
                return line_geometry(col, first, indent, lexer->lookahead);
            }
            if (lexer->lookahead == 0) return false;
            continue;
        }
        if (lexer->lookahead == 0) return false;
        if (lexer->lookahead == '#') {
            switch (skip_directive_line(s, lexer)) {
                case DIRECTIVE_SKIPPED: if (lexer->lookahead == 0) return false; continue;
                case DIRECTIVE_STATEMENT: return line_geometry(col, first, indent, '#');
            }
        }
        return line_geometry(col, first, indent, lexer->lookahead);
    }
}

// Peek the column where a body's first token sits: skip horizontal whitespace;
// if at a newline the body is on the next line (use next_line_indent), else it
// is inline on the current line (use get_column). This collapses F#'s old
// inline-vs-own-line distinction into one decision.
static uint32_t peek_body_col(Scanner *s, TSLexer *lexer) {
    uint32_t col = lexer->get_column(lexer);
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') { lexer->advance(lexer, true); col++; }
    // A trailing comment after the opener keyword (`match x with // ...`,
    // `let x = (* ... *)`) means the body/arms start on a later line - defer to
    // next_line_indent (which skips comment-only lines) instead of taking the
    // comment's column as the body column.
    if (lexer->lookahead == '/') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '/') { uint32_t nl; return next_line_indent(s, lexer, &nl, NULL) ? nl : 0; }
        return col;  // a lone `/` is inline (operator), body sits at its column
    }
    if (lexer->lookahead == '(') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '*') {
            // Block comment. CONTENT may follow it on the SAME line
            // (`| A -> (* tailcall *) f res`, FCS DiagnosticsLogger style):
            // skip the comment (depth-aware) and check - inline content keeps
            // the comment's start column as the body column (mirrors
            // next_line_indent's comment-led-element rule); otherwise the body
            // is on a later line.
            lexer->advance(lexer, true);
            if (lexer->lookahead == ')') return col;  // `(*)` = the multiply operator value, not a comment - inline body at the `(`
            if (!skip_comment_body(lexer, true)) return 0;
            skip_hspace(lexer);
            if (!at_line_end(lexer->lookahead))
                return col;                       // inline body after the comment
            uint32_t nl; return next_line_indent(s, lexer, &nl, NULL) ? nl : 0;
        }
        return col;  // `(` inline (parenthesised pattern / expression)
    }
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        uint32_t nl;
        if (next_line_indent(s, lexer, &nl, NULL)) return nl;
        return 0;
    }
    return col;
}

// Match a trailing-dot float literal (`1.`, `20.`) at the current position.
// Lexical, independent of layout.
static bool scan_trailing_dot_float(TSLexer *lexer) {
    skip_hspace(lexer);
    if (lexer->lookahead < '0' || lexer->lookahead > '9') return false;
    lexer->advance(lexer, false);
    while (is_digit(lexer->lookahead) || lexer->lookahead == '_') lexer->advance(lexer, false);
    if (lexer->lookahead != '.') return false;
    lexer->advance(lexer, false);
    int32_t after = lexer->lookahead;
    if (after == '.' || is_digit(after) || after == 'e' || after == 'E') return false;
    lexer->mark_end(lexer);
    lexer->result_symbol = FLOAT_TRAILING_DOT;
    return true;
}

static bool is_close_bracket(int32_t c) { return c == ']' || c == '}'; }

static bool is_opchar(int32_t c) {
    return c == '!' || c == '%' || c == '&' || c == '*' || c == '+' || c == '-' || c == '.' ||
           c == '/' || c == '<' || c == '=' || c == '>' || c == '?' || c == '@' || c == '^' ||
           c == '|' || c == '~' || c == '$' || c == ':';
}

// A `|` followed by one of these starts an infix operator (`|>`, `||`, `|?>`, `||>`, `|@`);
// a match-arm `|` is followed by whitespace or a pattern char instead.
static bool is_bar_op_tail(int32_t c) { return is_opchar(c) && c != ':'; }

// Consume one identifier segment at the lookahead - a plain ident
// (`Foo`/`foo'`/`x9`) or a ``quoted name``. Caller ensures the first char is an
// identifier start. Used by the RECORD_OPEN field peek so a qualified field name
// (`FunctionDef.Name = ...`) is recognised as a field, not a copy-update base.
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
        if (is_ident_char(ch)) lexer->advance(lexer, true);
        else break;
    }
}

// Like peek_name_segment but copies the (plain-identifier) segment into buf,
// NUL-terminated and truncated to cap. A backtick segment yields "`" (which
// never matches a plain keyword). Lets the RECORD_OPEN peek tell an object
// expression (`{ new ... }`) from a copy-update base on its own line.
static void peek_name_capture(TSLexer *lexer, char *buf, int cap) {
    int n = 0;
    if (lexer->lookahead == '`') { if (cap > 1) { buf[0] = '`'; buf[1] = 0; } else if (cap > 0) buf[0] = 0; peek_name_segment(lexer); return; }
    while (1) {
        int32_t ch = lexer->lookahead;
        if (is_ident_char(ch)) {
            if (n < cap - 1) buf[n++] = (char)ch;
            lexer->advance(lexer, true);
        } else break;
    }
    if (cap > 0) buf[n < cap ? n : cap - 1] = 0;
}

// Read the identifier-shaped word at the lexer position into w (cap bytes,
// NUL-terminated), consuming it.
static void read_word(TSLexer *lexer, char *w, size_t cap) {
    size_t n = 0; int32_t look = lexer->lookahead;
    while (n + 1 < cap && (is_ident_char(look))) {
        w[n++] = (char)look; lexer->advance(lexer, true); look = lexer->lookahead;
    }
    w[n] = '\0';
}

// A line whose first significant char/word does NOT start a new statement, so a
// LAYOUT_SEMI before it would be wrong (it continues the current construct):
//   * closing delimiters `)` `]` `}` and `|` (match arm / `|>` pipe);
//   * a leading `,` - a tuple / argument-list separator (`f(`\n` a`\n` , b)`), never a
//     statement start;
//   * continuation keywords of an enclosing if/try/let (`else`/`elif`/`then`/
//     `with`/`finally`/`in`/`and`).
// `first` is the leading char (from next_line_indent, where lookahead==first).
static bool semi_blocked_word(const char *w) {
    return !strcmp(w, "else") || !strcmp(w, "elif") || !strcmp(w, "then") ||
           !strcmp(w, "with") || !strcmp(w, "finally") || !strcmp(w, "in") || !strcmp(w, "and") ||
           !strcmp(w, "when") ||   // static-optimization equations / arm-guard continuations
           !strcmp(w, "done");
}

static bool semi_blocked(TSLexer *lexer, int32_t first) {
    if (first == ')' || first == ']' || first == '}' || first == '|' || first == ',') return true;
    if (is_lower(first)) {
        char w[12]; read_word(lexer, w, sizeof w);
        return semi_blocked_word(w);
    }
    return false;
}

// Consume a maximal TEXT run of an interpolated string, stopping (without
// consuming) at the next structural token: `{` interpolation, closing quote, or
// `%` printf/percent. Doubled braces `{{`/`}}` and (per kind) escapes/quotes are
// part of the text. `mark_end` is advanced only over confirmed text, so an
// over-peeked terminator is excluded from the token. Returns true iff >=1 char of
// text was consumed; on false the caller returns false and tree-sitter lexes the
// structural token itself (resuming from the pre-scan position).
//
// Done in the external scanner - which runs BEFORE extra-skipping - so a leading
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
        if (c == '\\' && kind == TX_STRING) {           // escape: \\ , \n , \uXXXX ... (lenient)
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
// starts a fresh declaration `_token` - never a continuation of the previous
// bare-expression statement. Blocking LAYOUT_SEMI before them stops the previous
// `_token` from absorbing the declaration into a `sequence_expression` (which then
// fails when, e.g., the `let` has no continuation). `first` is the leading char.
static bool decl_starter_word(const char *w) {
    return !strcmp(w, "let") || !strcmp(w, "use") || !strcmp(w, "do") ||
           !strcmp(w, "type") || !strcmp(w, "module") || !strcmp(w, "open") ||
           !strcmp(w, "exception") || !strcmp(w, "namespace") || !strcmp(w, "inline") ||
           !strcmp(w, "member") || !strcmp(w, "static") || !strcmp(w, "val") ||
           !strcmp(w, "abstract") || !strcmp(w, "inherit") || !strcmp(w, "override") ||
           !strcmp(w, "default") || !strcmp(w, "interface");
}

static bool decl_starter(TSLexer *lexer, int32_t first) {
    // `[<Attr>]` on its own line - an attribute row always decorates the NEXT
    // declaration, never continues the previous statement. (A bare `[` is a list
    // literal, which IS a statement.)
    if (first == '[') { lexer->advance(lexer, true); return lexer->lookahead == '<'; }
    // `#load` / `#r` / `#nowarn` / ... - a directive is its own `_token`.
    // (`#if`/`#elif`/`#else`/`#endif` lines are skipped by next_line_indent.)
    if (first == '#') return true;
    if (first < 'a' || first > 'z') return false;
    char w[12]; read_word(lexer, w, sizeof w);
    return decl_starter_word(w);
}

// Body column for a layout open, plus the split-branch verdict: the body we are
// about to open sits behind a `#else`/`#elif` and starts a DECLARATION, so this
// `=` closed the `#if` branch of a declaration written twice (`#if X`\n`let f x =`
// \n`#else`\n`let f x =`\n`#endif`\n`    body`) and the body belongs to the LAST
// branch. The caller declines the open, leaving the body `optional(...)` empty.
static bool split_branch_body(Scanner *s, TSLexer *lexer, uint32_t *body_col) {
    s->scan.skipped_alt_directive = false;              // peek_body_col skips the reset for an INLINE body
    *body_col = peek_body_col(s, lexer);
    return s->scan.skipped_alt_directive && decl_starter(lexer, lexer->lookahead);
}

// Consume one or more consecutive `[<...>]` attributes, leaving the lexer at the
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
        skip_space(lexer);
        if (lexer->lookahead == '[') {               // another `[<...>]`?
            lexer->advance(lexer, true);
            if (lexer->lookahead == '<') { lexer->advance(lexer, true); continue; }
            return false;
        }
        break;
    }
    return true;
}

// Skip a `"`-initiated string at the opening quote: triple `"""..."""` or regular
// `"..."` (with `\"` escape). Lexer ends just past the closing quote(s).
static void edsl_skip_dquote(TSLexer *lexer) {
    lexer->advance(lexer, true);                          // opening "
    if (lexer->lookahead == '"') {
        lexer->advance(lexer, true);
        if (lexer->lookahead != '"') return;             // empty "" string
        lexer->advance(lexer, true);                     // triple """ ... """
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

// Skip a verbatim `@"..."` body at the opening quote; `""` is an escaped quote.
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

// Consume a balanced `( ... )` group with the lexer positioned at the opening `(`.
// Args MAY span lines (`div(class'="a"\n , id="b")`), so every string/comment form
// is skipped to keep the paren depth exact - a `)`, `{` or `"` inside a string
// must not miscount (this is what made an earlier newline-allowing version
// mis-fire on an emoticon `:^)` inside a triple string in a multi-line `(fun ... )`
// arg). Returns false on EOF / runaway.
static bool edsl_skip_balanced_parens(TSLexer *lexer) {
    int depth = 0, guard = 0;
    for (;;) {
        if (++guard > 8192) return false;                // runaway guard
        int32_t c = lexer->lookahead;
        if (c == 0) return false;
        if (c == '"') { edsl_skip_dquote(lexer); continue; }
        if (c == '@') {                                  // @"verbatim" / @$"..."
            lexer->advance(lexer, true);
            if (lexer->lookahead == '$') lexer->advance(lexer, true);
            if (lexer->lookahead == '"') edsl_skip_verbatim(lexer);
            continue;
        }
        if (c == '$') {                                  // $"interp" / $@"..." / $"""..."""
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
            if (lexer->lookahead == '*') {               // (* block comment *) - not a paren
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

// Skip the rest of a brace-balanced `{| ... |}` anonymous-record argument - the caller
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
        if (is_ident_char(ch)) { lexer->advance(lexer, true); continue; }
        break;
    }
    return true;
}

static bool ce_brace_content_is_ce_body(TSLexer *lexer);

// Tail of element_dsl_ahead: the caller has consumed the first name segment;
// check the optional `.seg` qualification, then `( ... )( .m( ... ) )* {`.
static bool element_dsl_parens_brace(TSLexer *lexer) {
    while (lexer->lookahead == '.') {                     // qualified `A.B.div`
        lexer->advance(lexer, true);
        if (!edsl_skip_name(lexer)) return false;
    }
    skip_hspace(lexer);
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
        // Oxpecker.Solid component: `Component {| props |} { children }` - the
        // builder argument is an anonymous record. Require `{|` (not a plain `{`,
        // which would be the body).
        lexer->advance(lexer, true);
        if (lexer->lookahead != '|') return false;
        if (!edsl_skip_braces_after_open(lexer)) return false;
    } else {
        return false;
    }
    // After each arg: the body `{` must be SAME-line (only spaces/tabs between, so
    // `foo(a, b)`\n`{ record }` isn't read as one DSL); otherwise a fluent
    // `.method( ... )` chain link may continue (those CAN sit on their own lines).
    for (;;) {
        skip_hspace(lexer);
        if (lexer->lookahead == '{') {
            // Only an element-DSL if the brace holds a CE body - NOT a record /
            // object-expr / copy-update. Otherwise `f "msg" { x with ... }` /
            // `f "s" { name = 1 }` must stay application(f, "msg", record/copy-update).
            lexer->advance(lexer, true);                  // past `{`
            if (lexer->lookahead == '|') return false;    // `{|` anon record
            skip_space(lexer);
            return ce_brace_content_is_ce_body(lexer);
        }
        skip_space(lexer);
        if (lexer->lookahead != '.') return false;        // not a chain link -> not a DSL
        lexer->advance(lexer, true);
        if (!edsl_skip_name(lexer)) return false;
        skip_hspace(lexer);
        if (lexer->lookahead != '(') return false;        // method must be CALLED
        if (!edsl_skip_balanced_parens(lexer)) return false;
    }
}

// Lookahead for the Oxpecker element-DSL head: the CE builder is an APPLICATION,
// optionally extended by a fluent method chain -
//   `div() {`, `div(attrs) {`, `div(attrs).hxTarget("#x").hxSwap("y") {`
// (chain links may sit on their own lines). The lexer is positioned at the
// builder's first char. Advances DESTRUCTIVELY; the caller emits a ZERO-WIDTH
// token so the over-advance is discarded and the real tokens are re-lexed. This
// lookahead is what lets the parser tell `div() { ... }` (element DSL) from
// `a()`\n`b()` (two applications): the LR table can't peek past the args/chain
// for the `{`. Reads the first name segment, then defers to
// element_dsl_parens_brace.
static bool element_dsl_ahead(TSLexer *lexer) {
    if (!is_name_start(lexer->lookahead)) return false;
    for (;;) {
        int32_t ch = lexer->lookahead;
        if (is_ident_char(ch)) { lexer->advance(lexer, true); continue; }
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
// (false). Used to decide whether to emit CE_BRACE_OPEN so `head { ... }` forks to a CE
// vs application(head, record/object). Destructive peek - only ever called right before a
// `return false` (on no-match) or a zero-width `return true`, so the advances are
// discarded by the caller's mark_end baseline.
//
//   CE body   -> true:  `return ...`, `let x = ...`, `yield ...`, `if ...`, `for ...`, `x`,
//                       `f x`, `x :: xs`, `(...)`, `[ ... ]`, `1`, ...  (and empty `{}`)
//   record    -> false: `ident = ...`  /  `ident : ty`  (NOT `::`)
//   object    -> false: `new ...`
//   copy-update -> false: `base with ...`
static bool ce_brace_content_is_ce_body(TSLexer *lexer) {
    // Look past leading trivia first: with `{ // note`\n`A = 1 }` the callers stop
    // at the `/`, which is not a name start and would classify as a CE body.
    // A `/`- or `(`-led NON-comment stays CE (same verdict as the checks below).
    for (;;) {
        skip_space(lexer);
        if (lexer->lookahead == '/') {
            lexer->advance(lexer, true);
            if (lexer->lookahead != '/') return true;      // `/` operator expression
            skip_line(lexer);
            continue;
        }
        if (lexer->lookahead == '(') {
            lexer->advance(lexer, true);
            if (lexer->lookahead != '*') {
                // `{ (expr) with F = ... }` - copy-update over a parenthesised base.
                // Skip the balanced group; a following `with` decides copy-update,
                // anything else falls through to the CE reading below.
                int pdepth = 1, pguard = 0;
                while (pdepth > 0) {
                    if (++pguard > 4096) return true;
                    int32_t e = lexer->lookahead;
                    if (e == 0) return true;
                    if (e == '"') { edsl_skip_dquote(lexer); continue; }
                    if (e == '(') pdepth++;
                    else if (e == ')') pdepth--;
                    lexer->advance(lexer, true);
                }
                skip_hspace(lexer);
                if (is_name_start(lexer->lookahead)) {
                    char wp[8] = {0};
                    peek_name_capture(lexer, wp, sizeof(wp));
                    if (!strcmp(wp, "with")) return false;
                }
                return true;
            }
            lexer->advance(lexer, true);
            if (lexer->lookahead == ')') return true;      // `(*)` multiply operator
            if (!skip_comment_body(lexer, true)) return true;
            continue;
        }
        break;
    }
    int32_t c = lexer->lookahead;
    if (c == '}') return true;                 // empty CE body `{ }`
    if (c == '!') {                            // `{ !cell with ... }`: deref'd copy-update base
        lexer->advance(lexer, true);
        c = lexer->lookahead;
    }
    if (!is_name_start(c)) return true;        // literal / paren / bracket / operator -> CE expr
    char w0[12] = {0};
    peek_name_capture(lexer, w0, sizeof(w0));
    if (!strcmp(w0, "new")) return false;      // object expression `{ new T ... }`
    if (!strcmp(w0, "inherit")) return false;  // object construction `{ inherit T(...) ... }`
    // CE statement keywords (reserved -> can never be a record field name). `let!`,
    // `use!`, `do!`, `match!`, `yield!`, `return!` share the base word read here.
    static const char *kw[] = {"let","use","do","return","yield","if","for","while",
                               "match","try","fun","function","lazy","assert", NULL};
    for (int i = 0; kw[i]; i++) if (!strcmp(w0, kw[i])) return true;
    // Otherwise scan the leading expression: `=`/`:` (record field) or a later `with`
    // (copy-update) => NOT a CE; anything else => CE bare-expression body.
    for (int guard = 0; guard < 64; guard++) {
        skip_hspace(lexer);
        int32_t d = lexer->lookahead;
        if (d == '=') return false;                 // record field `name = ...`
        if (d == ':') {                             // `:` field type, but `::` is cons (CE)
            lexer->advance(lexer, true);
            return lexer->lookahead == ':';         // `::` -> CE ; `:` -> record field
        }
        if (d == '.') { lexer->advance(lexer, true); continue; }   // qualified name / member access
        // A bracketed/parenthesised APPLICATION ARGUMENT before a possible
        // `with` (`{ createEx [] doQuery with X = h }` - copy-update whose base
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
        // A STRING application argument before a possible `with`
        // (`{ Include "" with Includes = [] }`): skip it and keep scanning.
        if (d == '"') { edsl_skip_dquote(lexer); continue; }
        if (d == '@') {
            lexer->advance(lexer, true);
            if (lexer->lookahead != '"') return true;
            edsl_skip_verbatim(lexer);
            continue;
        }
        if (is_name_start(d)) {
            char w[8] = {0};
            peek_name_capture(lexer, w, sizeof(w));
            if (!strcmp(w, "with")) return false;   // copy-update `base with ...`
            continue;                               // application arg / next path segment
        }
        if (d == '\n' || d == '\r') {             // `{ base`\n`  with ...`: copy-update on the next line
            skip_space(lexer);
            if (is_name_start(lexer->lookahead)) {
                char w[8] = {0};
                peek_name_capture(lexer, w, sizeof(w));
                if (!strcmp(w, "with")) return false;
            }
            return true;
        }
        return true;                                // `}`/`;`/`(`/`[`/literal/op -> CE
    }
    return true;
}

// Dispatch for `///` doc lines at a layout boundary (s->scan.skipped_doc_lines):
// the post-doc word decides where the docs belong (a `|` case, an `and`
// clause, a member, or the next declaration). The lexer sits AT the first char
// after next_line_indent. Reads the word destructively - every taken branch
// RETURNS a zero-width token (mark_end stays at the baseline), and the lone
// fall-through only loses position for checks that re-derive it themselves.
// Call LAST.
static bool try_and_docs(Scanner *s, TSLexer *lexer, const bool *valid,
                         int32_t first, uint32_t col, Ctx *top) {
    s->scan.post_doc_word[0] = '\0';
    if (!s->scan.skipped_doc_lines) return false;
    // Docs followed by a `|` case - union/enum case attachment.
    if (valid[CASE_DOCS_OPEN] && first == '|') {
        lexer->result_symbol = CASE_DOCS_OPEN; return true;
    }

    // Word-led dispatch (`and` / decl keywords / member words).
    if (is_lower(first)) {
        char w[10]; size_t n = 0; int32_t lk = lexer->lookahead;
        while (n < 9 && (is_ident_char(lk))) {
            w[n++] = (char)lk; lexer->advance(lexer, true); lk = lexer->lookahead;
        }
        w[n] = '\0';
        { size_t i = 0; for (; w[i] && i < 9; i++) s->scan.post_doc_word[i] = w[i]; s->scan.post_doc_word[i] = '\0'; }
        // docs + `and` - the and-clause attachment marker.
        if (valid[AND_DOCS_OPEN] && !strcmp(w, "and")) {
            lexer->result_symbol = AND_DOCS_OPEN; return true;
        }
        // Inside a TYPE body at the body column, docs + a DECL keyword mean
        // the body ends here and the docs decorate the NEXT declaration -
        // close the body BEFORE the docs (`type V =\n| A\n\n/// d\n[<A>]\ntype W`).
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
    // attribute alone is ambiguous - peek PAST the attr group to the word.
    if (first == '[' && top && top->sort == S_TYPEBODY && col <= top->col &&
        (valid[LAYOUT_END] || valid[LAYOUT_SEMI])) {
        if (!skip_bracket_attrs(lexer)) return false;
        skip_space(lexer);
        char w[10]; size_t n = 0; int32_t lk = lexer->lookahead;
        while (n < 9 && is_lower(lk)) { w[n++] = (char)lk; lexer->advance(lexer, true); lk = lexer->lookahead; }
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
// classification - the line-boundary twin of the mid-line CE_BRACE_OPEN
// dispatch, for a builder whose `{` sits on the NEXT line (`seq`\n`    {`).
// Zero-width (mark_end stays at the baseline): advances only peek. Call it
// LAST before `return false` - it consumes lookahead even on a miss.
static bool try_ce_brace(TSLexer *lexer, const bool *valid, int32_t first) {
    if (first != '{' || !valid[CE_BRACE_OPEN]) return false;
    lexer->advance(lexer, true);                  // past `{`
    if (lexer->lookahead == '|') return false;    // `{|` anonymous record
    skip_space(lexer);
    if (ce_brace_content_is_ce_body(lexer)) { lexer->result_symbol = CE_BRACE_OPEN; return true; }
    return false;
}

// ---- Scan phases -------------------------------------------------------------
// One scan is a fixed sequence of phases, each looking at the same position:
//   1. interpolated-string text (lexical; scanner_scan)
//   2. scan_ctor_attr        zero-width gate before a primary constructor
//   3. scan_body_opens       grammar-driven opens that peek the body column
//   4. scan_trailing_float   lexical `1.`, between the two kinds of opens
//   5. scan_newline_opens    opens that fire only when the body is on the next line
//   6. scan_paren_field_open, scan_record_open
//   7. scan_mid_line         same-line content: closers, keywords, `;`, docs, gates
//   8. scan_line_boundary    at a newline: EOF, next-line geometry, continuation,
//                            then the per-sort close/separator decision
// A phase returns PASS to hand the position to the next one; EMITTED and DECLINED
// end the scan. Phases peek by advancing the lexer, so once a phase has read past
// the position it must decide (DECLINED) rather than PASS: the next phase would
// see a lexer parked past the line's first word.
typedef enum { PASS, EMITTED, DECLINED } Step;

static inline Step emit(TSLexer *lexer, Sym sym) { lexer->result_symbol = sym; return EMITTED; }
static inline Step close_top(Scanner *s, TSLexer *lexer, Sym sym) { s->n--; return emit(lexer, sym); }

static Step scan_ctor_attr(TSLexer *lexer, const bool *valid) {
// CTOR_ATTR (zero-width): valid only in the primary-constructor position, after
// a type name. Look ahead past one or more `[<...>]` attributes; emit ONLY when a
// `(` (the constructor params) follows. This distinguishes a ctor attribute
// (`type T [<ParamObject>] (...)`) from a standalone attribute on the NEXT
// declaration (`[<Measure>] type cm`\n`[<Measure>] type kg`, where `[<Measure>]`
// is followed by `type`). Zero-width, so the attributes/`(` are re-lexed after.
if (valid[CTOR_ATTR]) {
    // Any mix of `[<attr>]` rows, `///` doc lines and `//` comments may
    // precede a primary ctor's `(` (`type StringSyntaxAttribute\n ///<param
    // ...>\n (syntax: string, ...) =` - Feliz StringSyntax; `[<ParamObject>] //
    // ...\n (params) =` - Feliz POJO). Require at least ONE such row so a
    // PLAIN ctor `type T (x) =` keeps its ordinary ungated path.
    bool seen_row = false;
    for (;;) {
        skip_space(lexer);
        if (lexer->lookahead == '/') {
            lexer->advance(lexer, true);
            if (lexer->lookahead != '/') return DECLINED;
            skip_line(lexer);
            seen_row = true;
            continue;
        }
        if (lexer->lookahead == '[') {        // an attribute row
            if (!skip_bracket_attrs(lexer)) return DECLINED;
            seen_row = true;
            continue;
        }
        break;
    }
    if (!seen_row) return DECLINED;
    // Optional access modifier between the attrs and the `(`:
    // `type T [<ParamObject; Emit("$0")>]\n private (...)`.
    if (lexer->lookahead == 'p' || lexer->lookahead == 'i') {
        char aw[10]; size_t an = 0;
        while (an < 9 && is_lower(lexer->lookahead)) {
            aw[an++] = (char)lexer->lookahead; lexer->advance(lexer, true);
        }
        aw[an] = '\0';
        if (strcmp(aw, "private") && strcmp(aw, "internal") && strcmp(aw, "public")) return DECLINED;
        skip_space(lexer);
    }
    if (lexer->lookahead == '(') { return emit(lexer, CTOR_ATTR); }
    return DECLINED;
}
    return PASS;
}

static Step scan_body_opens(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top) {
// ---- Grammar-driven OPENS (zero-width; push a context) --------------------
// Checked BEFORE the float probe: these only peek (and restore position via
// mark_end on return), whereas `scan_trailing_dot_float` advances over digits
// DESTRUCTIVELY even on failure - running it first would corrupt the body
// column for an inline body like `let a = 1` (peek would see the newline -> 0).
// When RECORD_OPEN is also valid we're right after a `{`; the RECORD_OPEN
// block below owns that decision (field -> record; own-line base -> layout;
// same-line `new`/`x with` -> fall through to object-expr/copy-update). So the
// generic LAYOUT_OPEN must NOT pre-empt it.
if (valid[LAYOUT_OPEN] && !valid[RECORD_OPEN]) {
    uint32_t bc;
    skip_hspace(lexer);
    bool inl = lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
               lexer->lookahead != '/'  && lexer->lookahead != 0;
    // A split declaration's `#if` branch ends at its own `=`: emit the break
    // (the grammar then takes the body-less path) instead of opening a body
    // that would swallow the `#else` branch's declaration.
    if (split_branch_body(s, lexer, &bc)) {
        if (!valid[PREPROC_BREAK]) return DECLINED;
        return emit(lexer, PREPROC_BREAK);
    }
    // Where a CE statement `let` and an expression `let ... in` both apply
    // (CE bodies), an inline value followed by ` in` is the expression form.
    if (inl && valid[EXPR_OPEN] && top && top->sort == S_BRACKET && line_has_in_keyword(lexer)) {
        push(s, S_EXPR, bc); s->stk[s->n - 1].inl = 1;
        return emit(lexer, EXPR_OPEN);
    }
    push(s, S_LAYOUT, bc);
    if (inl && s->n) s->stk[s->n - 1].inl = 1;
    return emit(lexer, LAYOUT_OPEN);
}
// FOR_OPEN: the body of a `for ... do`. Like LAYOUT_OPEN but SUPPRESSED when the
// body would not indent past the enclosing context - that's a query-CE
// `for x in xs do`\n`where ...`/`select ...`, where the operators sit at the CE
// column, not in an indented loop body. Suppressing keeps the for body empty so
// the operators stay `query_operator` CE siblings (a real loop body always
// indents past the `for`, so this never suppresses a genuine body). Dedicated
// (not LAYOUT_OPEN) so only for-do bodies get this rule.
if (valid[FOR_OPEN]) {
    uint32_t bc = peek_body_col(s, lexer);
    if (top && bc <= top->col) {
        // No indented body - query-CE for-clause. Emit the enclosing
        // statement separator so the following `where`/`select`/`join`/... is a
        // SIBLING `query_operator`, not absorbed as an application argument of
        // the empty-body `for`. (peek_body_col advanced, but the separator is
        // zero-width at the mark_end baseline, so over-advance is discarded.)
        if (top->sort == S_BRACKET && valid[BRACKET_SEMI]) { return emit(lexer, BRACKET_SEMI); }
        if (layoutish(top->sort) && valid[LAYOUT_SEMI])    { return emit(lexer, LAYOUT_SEMI); }
        return DECLINED;
    }
    push(s, S_LAYOUT, bc); return emit(lexer, FOR_OPEN);
}
if (valid[EXPR_OPEN])   {
    skip_hspace(lexer);
    bool inl = lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
               lexer->lookahead != '/'  && lexer->lookahead != 0;
    push(s, S_EXPR, peek_body_col(s, lexer));
    if (inl && s->n) s->stk[s->n - 1].inl = 1;
    return emit(lexer, EXPR_OPEN);
}
if (valid[THEN_OPEN])   {
    skip_hspace(lexer);
    bool inl = lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
               lexer->lookahead != '/'  && lexer->lookahead != 0;
    push(s, S_EXPR, peek_body_col(s, lexer));
    if (s->n) { s->stk[s->n - 1].thn = 1; if (inl) s->stk[s->n - 1].inl = 1; }
    s->else_claim_col = -1;
    return emit(lexer, THEN_OPEN);
}
if (valid[LAZY_OPEN])   {
    skip_hspace(lexer);
    if (lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
        lexer->lookahead != '/'  && lexer->lookahead != 0) return DECLINED;   // inline body -> plain branch
    push(s, S_EXPR, peek_body_col(s, lexer));
    return emit(lexer, LAZY_OPEN);
}
if (valid[TRY_OPEN])    { push(s, S_TRY,    peek_body_col(s, lexer)); return emit(lexer, TRY_OPEN); }
if (valid[ELSE_OPEN]) {
    // Final-else body. An INLINE `else if` (same line) flattens to an elif clause -
    // DON'T open a nested else-body; return false so the grammar's flat elif matches
    // (its elif/else stay at the chain level instead of nesting an if whose layout
    // would over-close at a later `elif`). But a NEWLINE-led else-body whose first
    // statement happens to be `if` is a REAL body - it may have more statements after
    // (`else\n if c then x\n match ...`) - so suppress ONLY for the same-line form.
    skip_hspace(lexer);
    bool nl_before = (lexer->lookahead == '\n' || lexer->lookahead == '\r' || lexer->lookahead == '/');
    uint32_t col = peek_body_col(s, lexer);  // positions lexer at the body's first char
    // `else`\n`if ...` at the enclosing body's own column is a flat else-if
    // chain too (LargeConditionals: 200 levels would overflow the stack).
    bool flat_col = nl_before && top && col <= top->col;
    if ((!nl_before || flat_col) && lexer->lookahead == 'i') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == 'f') {
            lexer->advance(lexer, true);
            int32_t a = lexer->lookahead;
            bool word = is_ident_char(a);
            if (!word) return DECLINED;  // inline `else if ...` -> flat elif clause
        }
    }
    push(s, S_EXPR, col);
    if (!nl_before && s->n) s->stk[s->n - 1].inl = 1;
    s->else_claim_col = -1;
    return emit(lexer, ELSE_OPEN);
}
if (valid[MATCH_OPEN])  { push(s, S_MATCH,  peek_body_col(s, lexer)); return emit(lexer, MATCH_OPEN); }
    return PASS;
}

static Step scan_trailing_float(TSLexer *lexer, const bool *valid) {
// Lexical trailing-dot float (`1.`, `20.`). Placed AFTER the peek_body_col
// opens above (LAYOUT/FOR/EXPR/TRY/ELSE/MATCH) - running it before them would
// destructively advance over the digits of an inline body like `let a = 1` and
// corrupt the body column. But it MUST come BEFORE the newline-gated opens
// (BLOCK/TYPE/BRACKET) and RECORD_OPEN: those `return false` for an inline body,
// which would otherwise short-circuit this probe and make a first array/list
// element `[|1.|]` mis-lex as `1` + `.|`. A digit can never start one of those
// (they fire on a newline / a field-shape peek), so checking float first is safe.
if (valid[FLOAT_TRAILING_DOT]) {
    skip_hspace(lexer);
    if (is_digit(lexer->lookahead)) {
        if (scan_trailing_dot_float(lexer)) return EMITTED;
        return DECLINED;
    }
}
    return PASS;
}

static Step scan_newline_opens(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top) {
// BLOCK_OPEN: a type/module body is a layout ONLY when its members are on the
// NEXT line (`type X =\n members`, `module M =\n decls`). For an inline body
// (`type X = {...}` / `type X = int` / `module L = Lib` abbrev) it must NOT fire,
// so the grammar's inline alternative matches. Newline-gated like BRACKET_OPEN,
// but pushes S_LAYOUT (dedent-close via LAYOUT_END).
if (valid[BLOCK_OPEN]) {
    skip_hspace(lexer);
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        uint32_t col;
        if (next_line_indent(s, lexer, &col, NULL)) { push(s, S_DECL, col); return emit(lexer, BLOCK_OPEN); }
    }
    return DECLINED; // inline body - let the grammar's inline alternative match
}
// TYPE_OPEN: like BLOCK_OPEN but the context is S_TYPEBODY so a `with`
// augmentation at the body column closes it (see the S_TYPEBODY boundary case).
if (valid[TYPE_OPEN]) {
    skip_hspace(lexer);
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        uint32_t col;
        if (next_line_indent(s, lexer, &col, NULL)) { push(s, S_TYPEBODY, col); return emit(lexer, TYPE_OPEN); }
    }
    return DECLINED; // inline type body (record/alias/inline DU) - let it match
}
// FIELD_BLOCK_OPEN: a record field value that starts on the next line.
if (valid[FIELD_BLOCK_OPEN]) {
    skip_hspace(lexer);
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        uint32_t col; int32_t bfirst = 0;
        if (next_line_indent(s, lexer, &col, &bfirst) && top && col > top->col && bfirst != '}' && bfirst != '|') {
            push(s, S_EXPR, col); return emit(lexer, FIELD_BLOCK_OPEN);
        }
    }
    return DECLINED;
}
// INFIX_BLOCK_OPEN: the right operand of a line-ending `&&`/`||` starts on
// a deeper line.
if (valid[INFIX_BLOCK_OPEN]) {
    skip_hspace(lexer);
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        uint32_t col; int32_t bfirst = 0;
        // Only a `let`/`use`-led operand needs the block; anything else
        // continues the operator line as before.
        if (next_line_indent(s, lexer, &col, &bfirst) && top && col > top->col && (bfirst == 'l' || bfirst == 'u')) {
            char w[12]; read_word(lexer, w, sizeof w);
            if (!strcmp(w, "let") || !strcmp(w, "use")) {
                push(s, S_EXPR, col); s->stk[s->n - 1].inf = 1; return emit(lexer, INFIX_BLOCK_OPEN);
            }
        }
    }
    return DECLINED;
}
// PAREN_BLOCK_OPEN: `(` with its content on the following line(s). Declined
// for an empty `(`\n`)` (that is the `unit` token).
if (valid[PAREN_BLOCK_OPEN]) {
    skip_hspace(lexer);
    int32_t c0 = lexer->lookahead;
    if (c0 == '\n' || c0 == '\r') {
        uint32_t col; int32_t bfirst = 0;
        if (next_line_indent(s, lexer, &col, &bfirst) && bfirst != ')' && !is_opchar(bfirst)) {
            push(s, S_EXPR, col); s->stk[s->n - 1].par = 1; return emit(lexer, PAREN_BLOCK_OPEN);
        }
        return DECLINED;
    }
    // Inline-first content anchors the body at its own column, so a line
    // aligned with it is a new statement (F# sequences it too). Operator-led
    // content (`(+)`, `(-x)`, `(<@ e @>)`) and comment-led content keep the
    // plain form.
    if (c0 == ')' || c0 == 0 || c0 == '\'' || is_opchar(c0)) return DECLINED;
    uint32_t inline_col = lexer->get_column(lexer);
    if (c0 == '(') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '*' || lexer->lookahead == '^' || lexer->lookahead == '\'') return DECLINED;
    }
    push(s, S_EXPR, inline_col); s->stk[s->n - 1].par = 1; return emit(lexer, PAREN_BLOCK_OPEN);
}
if (valid[BRACKET_OPEN]) {
    skip_hspace(lexer);
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        // Block form: body on the next line(s). Decline when the next real
        // char CLOSES the bracket (`Html.div [`\n`\n`]` - empty across blank
        // lines): there is no element to anchor a context.
        uint32_t col; int32_t bfirst = 0;
        if (next_line_indent(s, lexer, &col, &bfirst)) {
            if (bfirst == ']' || bfirst == '}' || bfirst == '|') return DECLINED;
            push(s, S_BRACKET, col); return emit(lexer, BRACKET_OPEN);
        }
        return DECLINED;
    }
    // Capture the inline first element's column / lead char BEFORE the
    // element-DSL probe, which advances the lexer destructively on a miss.
    uint32_t inline_col = lexer->get_column(lexer);
    int32_t inline_first = lexer->lookahead;
    // A trailing LINE COMMENT is not an element: the rest of the line is
    // comment text, so the real content (if any) starts on a later line.
    // Defer to the block form - next_line_indent skips comment-only lines,
    // so `[| //a`\n`//b`\n`|]` reads as an EMPTY array (anchoring a context on
    // the comment left a stray zero-width ERROR), and elements below get a
    // context at THEIR column, not the comment's.
    if (inline_first == '/') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '/') {
            uint32_t col; int32_t bfirst = 0;
            if (!next_line_indent(s, lexer, &col, &bfirst)) return DECLINED;
            if (bfirst == ']' || bfirst == '}' || bfirst == '|') return DECLINED;
            push(s, S_BRACKET, col); return emit(lexer, BRACKET_OPEN);
        }
    }
    // Same-line content after a CE/bracket `{`: an element-DSL builder here
    // (`div() { span() {...} }`) needs its marker - the mid-line block below is
    // unreachable once we return. (Spaces/tabs already skipped above.)
    if (try_element_dsl(lexer, valid)) return EMITTED;
    // Inline-FIRST content (`[ yield a`\n`  yield! b ]`, `seq { x`\n`  y }`).
    // Open the context at the first element's column so newline-aligned
    // continuation elements still get a `_bracket_semi` separator (otherwise
    // they'd merge into the first element as an application). Skip when the
    // next char closes the bracket immediately (`[]`/`[| |]`/`{}` empty, or a
    // leading `|`/`}`/`]`), which has no element to anchor a context.
    if (inline_first == ']' || inline_first == '}' || inline_first == '|' || inline_first == 0) return DECLINED;
    // A leading BLOCK COMMENT is not an element: skip it (depth-aware) and
    // re-decide - `[(* no attributes *)]` (FCS) is an EMPTY list, comment
    // then newline defers to the block form, real content anchors at the
    // comment's column (comment-led element convention).
    if (inline_first == '(') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '*') {
            lexer->advance(lexer, true);
            if (!skip_comment_body(lexer, true)) return DECLINED;
            skip_hspace(lexer);
            if (lexer->lookahead == ']' || lexer->lookahead == '}' || lexer->lookahead == '|' || lexer->lookahead == 0) return DECLINED;
            if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
                uint32_t col;
                if (next_line_indent(s, lexer, &col, NULL)) { push(s, S_BRACKET, col); return emit(lexer, BRACKET_OPEN); }
                return DECLINED;
            }
        }
    }
    push(s, S_BRACKET, inline_col);
    return emit(lexer, BRACKET_OPEN);
}
    return PASS;
}

static Step scan_paren_field_open(Scanner *s, TSLexer *lexer, const bool *valid) {
// PAREN_FIELD_OPEN: the body of a named-field pattern `Foo(ident = ...)` - a
// dedicated open (valid ONLY in named_field_pattern) so newline-aligned fields
// get an S_BRACKET separator. Peek `ident(.seg)* =` (the `=` distinguishes a
// named field from a tuple-arg `Foo(a, b)`); capture the field column. Like
// RECORD_OPEN but `=`-only and never reused outside the pattern.
if (valid[PAREN_FIELD_OPEN]) {
    uint32_t col = lexer->get_column(lexer);
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') { lexer->advance(lexer, true); col++; }
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        if (!next_line_indent(s, lexer, &col, NULL)) return DECLINED;
    }
    bool ok = false;
    if (is_name_start(lexer->lookahead)) {
        peek_name_segment(lexer);
        skip_hspace(lexer);
        while (lexer->lookahead == '.') {              // qualified field name
            lexer->advance(lexer, true);
            if (!is_name_start(lexer->lookahead)) break;
            peek_name_segment(lexer);
            skip_hspace(lexer);
        }
        if (lexer->lookahead == '=') {                 // `=` (not `==`/`=>`) -> named field
            lexer->advance(lexer, true);
            if (lexer->lookahead != '=' && lexer->lookahead != '>') ok = true;
        }
    }
    if (ok) { push(s, S_BRACKET, col); return emit(lexer, PAREN_FIELD_OPEN); }
    return DECLINED;
}
    return PASS;
}

static Step scan_record_open(Scanner *s, TSLexer *lexer, const bool *valid) {
// RECORD_OPEN: a `{` record body whose first field starts here (same line as
// `{`, or the next line). Peeks to confirm a field shape (`ident =` for a
// record_field, `ident :` for a record_type_field) and captures the field
// column. SUPPRESSED (return false -> fall through) for `{ new ... }` (object
// expression) and `{ base with ... }` (copy-update), whose first word is NOT
// followed by `=`/`:` - letting the grammar's other `{`-branches match.
if (valid[RECORD_OPEN]) {
    uint32_t col = lexer->get_column(lexer);
    bool nl = false;
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') { lexer->advance(lexer, true); col++; }
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        nl = true;
        if (!next_line_indent(s, lexer, &col, NULL)) return DECLINED;
    }
    // A field may lead with `///` doc lines (`{ /// docs\n  Field: T ... }`):
    // skip them (to the NEXT line's content) so the field-shape check
    // still fires; `col` stays at the doc's column - where fields align.
    while (lexer->lookahead == '/' ) {
        // only a /// doc line: peek two more slashes
        lexer->advance(lexer, true);
        if (lexer->lookahead != '/') return DECLINED;   // a field can't start with `/`
        lexer->advance(lexer, true);
        if (lexer->lookahead != '/') return DECLINED;   // `//` plain comment: bail (rare inside `{`)
        skip_line(lexer);
        uint32_t c2; if (!next_line_indent(s, lexer, &c2, NULL)) return DECLINED;
        col = c2;   // the FIELD's column, not the doc's (`{ /// doc\n    Field: T`)
        nl = true;
    }
    int32_t c = lexer->lookahead;
    bool ok = false;
    char w0[8] = {0};
    // A field may lead with `[<...>]` attribute(s) - common in offside record
    // TYPE bodies (`{ [<JsonProperty("@id")>]\n Id : string ... }`). Skip them
    // so the `ident =`/`:` field-shape check below still fires; `col` stays at
    // the attribute's `[`, which is where every field of the body aligns.
    if (c == '[') {
        if (!skip_bracket_attrs(lexer)) return DECLINED;
        c = lexer->lookahead;
    }
    if (is_name_start(c)) {
        peek_name_capture(lexer, w0, sizeof(w0));
        // `{ inherit Base(...) [; field = ...] }` - object construction. The base
        // call is not an `=`/`:` field, so the check below would miss it.
        if (!strcmp(w0, "inherit")) { push(s, S_BRACKET, col); return emit(lexer, RECORD_OPEN); }
        skip_hspace(lexer);
        // A leading field modifier (`mutable foo: ...`): a second identifier
        // word sits before the `:`. Skip it so the `=`/`:` check sees the
        // field, not the modifier. Copy-update (`x with ...`) / object-expr
        // (`new T ...`) have a second word too but no trailing `=`/`:`, so they
        // still fall through.
        if (is_name_start(lexer->lookahead)) {
            peek_name_segment(lexer);
            skip_hspace(lexer);
        }
        // A qualified field name (`FunctionDef.Name = ...`) - consume `.seg`
        // chains so the `=`/`:` check below still fires. A copy-update base
        // (`Foo.bar with ...`) is followed by `with`, not `=`/`:`, so it still
        // falls through.
        while (lexer->lookahead == '.') {
            lexer->advance(lexer, true);
            if (!is_name_start(lexer->lookahead)) break;
            peek_name_segment(lexer);
            skip_hspace(lexer);
        }
        // `ti (* comment *) : int` - a block comment before the separator.
        while (lexer->lookahead == '(') {
            lexer->advance(lexer, true);
            if (lexer->lookahead != '*') break;      // `(`: an application base, not a field
            lexer->advance(lexer, true);
            if (lexer->lookahead == ')') break;      // `(*)` operator value
            if (!skip_comment_body(lexer, true)) return DECLINED;
            skip_hspace(lexer);
        }
        int32_t sep = lexer->lookahead;
        if (sep == '=' || sep == ':') ok = true;   // record_field / record_type_field
    }
    if (ok) { push(s, S_BRACKET, col); return emit(lexer, RECORD_OPEN); }
    // Not a field. If the base is on its OWN line after `{` (`{\n base with \n
    // field }` copy-update), open a layout at the base's column. Otherwise
    // (same-line `{ new ...}` / `{ x with ...}`) fall through so object-expression
    // / inline copy-update match. An object expression on its own line
    // (`{\n new IFoo with ...}`) must ALSO fall through - its `new` is a literal
    // token with no layout open, so suppress LAYOUT_OPEN when the first word
    // is `new`.
    if (nl && valid[LAYOUT_OPEN] && strcmp(w0, "new") != 0) { push(s, S_LAYOUT, col); return emit(lexer, LAYOUT_OPEN); }
    return DECLINED;
}
    return PASS;
}

static Step mid_closers(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top, int32_t c) {
    // Attribute on a labelled (member-sig) param: `[<ParamArray>] xs: obj[]`.
    // Emit a zero-width LABEL_ATTR only when `[<...>]+` is followed by
    // `ident:` (or `?ident:`). Done HERE (mid-line, gated on `c == '['`) so a
    // non-match falls through to the closer logic / `return false` exactly as
    // before - it never pre-empts the layout opens above.
    if (c == '[' && valid[LABEL_ATTR] && try_label_attr(lexer)) return EMITTED;
    // An infix right-operand block ends before a same-line `->` (guard arrow).
    if (c == '-' && top && top->sort == S_EXPR && top->inf && valid[LAYOUT_END]) {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '>') { return close_top(s, lexer, LAYOUT_END); }
        return DECLINED;
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
        else if (!is_bar_op_tail(c1) &&
                 top && layoutish(top->sort) && !top->par && valid[LAYOUT_END] && has_match_ctx(s)) {
            // A bare same-line `|` is the next match arm; close the inline
            // arm body first (`function | 0 -> "a" | _ -> "b"`). Gated on an
            // S_MATCH being on the stack so a UNION case separator
            // `type X = A | B` (no arm-list) does NOT close the enclosing body.
            return close_top(s, lexer, LAYOUT_END);
        }
    }
    if (closer && top) {
        if (layoutish(top->sort)  && valid[LAYOUT_END])    { return close_top(s, lexer, LAYOUT_END); }
        if (top->sort == S_MATCH   && valid[MATCH_END])     { return close_top(s, lexer, MATCH_END); }
        if (top->sort == S_BRACKET && valid[BRACKET_CLOSE]) { return close_top(s, lexer, BRACKET_CLOSE); }
    }
    return PASS;
}

static Step mid_word(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top, int32_t c) {
    // Word-led mid-line dispatch. A leading word can be a continuation
    // keyword that closes an inline body, a labelled type element
    // (`x: int -> ...`), or an Oxpecker element-DSL builder applied to
    // `( ... ) {`. All start at `c`, so read the leading word ONCE and
    // dispatch - reading it twice would advance past it and corrupt the
    // second probe. Order: LABEL_GATE first (a miss is only possible where
    // no other word-led token applies), then the continuation closes,
    // then ELEMENT_DSL_OPEN (co-valid at a `with`/`in` position, where the
    // body-close must win).
    // A same-line continuation keyword ends an inline layout body so it
    // attaches to the enclosing construct rather than being absorbed:
    //   `let ... = e in body`, `if c then a else b`, `if c then a elif ...`,
    //   `try e with ...` / `... finally ...`.
    // Only the INNERMOST expression body (S_EXPR) closes before an inline
    // `else`/`elif`/`in`, so exactly the then-branch (or let-in value)
    // closes; a DECL body (S_LAYOUT) does not, which keeps `module M =`\n
    // ` let f = if a then 1 else 0`\n` let g` intact. S_TRY closes before an
    // inline `with`/`finally` (a dedicated sort, so this never fires for a
    // `match ... with` inside an S_EXPR body). Everything is gated on
    // valid[LAYOUT_END], true only while a layout body is open and
    // complete: the `in` of `for x in xs` is unaffected, and once the
    // then-branch has closed and the grammar expects the `else`, an
    // ENCLOSING S_EXPR (a `let_decl_indented` value wrapping a
    // parenthesised `(if ... else ...)`) is not closed as well.
    if (valid[LABEL_GATE] && (is_alpha(c) || c == '_' || c == '?' || c == '`')) {
        if (try_label_gate(lexer)) return EMITTED;
        return DECLINED;
    }
    if (is_alpha(c)) {
        char w[10]; size_t n = 0; int32_t look = lexer->lookahead;
        while (n < 9 && (is_ident_char(look))) {
            w[n++] = (char)look; lexer->advance(lexer, true); look = lexer->lookahead;
        }
        w[n] = '\0';
        // get_column walks back to the line start (O(line length)): compute the
        // word's column only where a close records it, never per token.
        #define MID_COL() ((int32_t)lexer->get_column(lexer) - (int32_t)n)
        memcpy(s->scan.midline_word, w, n + 1);   // the CTOR_TUPLE_GATE tail below resumes past it (no strcpy: not in the Wasm libc subset)
        if (top && valid[LAYOUT_END]) {
            // `else`/`elif` close only an INLINE body: a same-line `else`
            // after an INDENTED then-body belongs to an INNER if on this
            // line (`if a then\n    if p then x else y` - dangling else;
            // the greedy close handed it to the OUTER if and stranded the
            // outer `else` line, TaggedCollections/FCS style). `in`/`end`
            // keep the unconditional close.
            bool else_kw = !strcmp(w, "else") || !strcmp(w, "elif");
            // `new(x) as this = { A = x } then this.B <- 1`: the inline ctor body
            // ends before its `then`.
            if (!strcmp(w, "then") && top->sort == S_LAYOUT && top->inl && valid[LAYOUT_END]) {
                return close_top(s, lexer, LAYOUT_END);
            }
            if (top->sort == S_EXPR && top->inf && (!strcmp(w, "then") || !strcmp(w, "do") || !strcmp(w, "with"))) {
                return close_top(s, lexer, LAYOUT_END);
            }
            // An `else` also ends an INLINE non-then body (a lambda inside the
            // then-branch: `if c then f >>= fun () -> g else h`) as long as a
            // then-body further down owns it; the claim stops the close at the
            // enclosing bodies once the owner has closed.
            bool thn_below = false;
            for (int i = (int)s->n - 2; i >= 0 && !thn_below; i--) thn_below = s->stk[i].thn != 0;
            if (top->sort == S_EXPR && (else_kw ? ((top->thn != 0 || (top->inl && thn_below)) && s->else_claim_col != MID_COL())
                                        : (!strcmp(w, "in") || !strcmp(w, "end")))) {
                if (else_kw && top->thn) s->else_claim_col = MID_COL();
                return close_top(s, lexer, LAYOUT_END);
            }
            // `in` after an inline match-arm body (`let f t = match t with
            // | A -> 1 | B -> 2 in f 1`): the arm body is S_LAYOUT; it can
            // only end here (a `for x in` is incomplete, so LAYOUT_END is
            // not valid there).
            // Only an ARM body (S_MATCH directly below): a `let! a = e in
            // ...` value inside a CE is also an inline S_LAYOUT, and its
            // `in` belongs to the grammar.
            if (!strcmp(w, "in") && top->sort == S_LAYOUT && top->inl &&
                s->n >= 2 && s->stk[s->n - 2].sort == S_MATCH) {
                return close_top(s, lexer, LAYOUT_END);
            }
            // `let x =`\n`    match ... with`\n`    | _ -> v in`\n`body`: the `in`
            // ends the arm-list, then (same position) the let value it sits in.
            if (!strcmp(w, "in")) {
                if (top->sort == S_MATCH && valid[MATCH_END]) {
                    s->in_claim_col = MID_COL(); return close_top(s, lexer, MATCH_END);
                }
                if (layoutish(top->sort) && !top->inl && valid[LAYOUT_END] && s->in_claim_col == MID_COL()) {
                    return close_top(s, lexer, LAYOUT_END);
                }
            }
            if (top->sort == S_TRY && (!strcmp(w, "with") || !strcmp(w, "finally"))) {
                return close_top(s, lexer, LAYOUT_END);
            }
            // `end` is exclusively a closer: it ends ANY inline layout body
            // (`struct val A: int; new(a) = { A = a }; end`).
            if (!strcmp(w, "end") && layoutish(top->sort)) {
                return close_top(s, lexer, LAYOUT_END);
            }
            // A mid-line `and` after an INLINE body starts the next
            // binding / accessor (`let a = 1 and b = 2`, `with get () = x
            // and set v = ...`). Without this the body absorbed `and ...` as
            // an application and `and` lexed as an identifier.
            // Not when a type variable or `(` follows: that is a constraint
            // chain (`when ^t: null and ^t: struct`), which can sit inside
            // a still-open (or stale, post-recovery) inline context.
            if (!strcmp(w, "and") && layoutish(top->sort) && top->inl) {
                skip_hspace(lexer);
                int32_t a = lexer->lookahead;
                if (a != '\'' && a != '^' && a != '(') {
                    return close_top(s, lexer, LAYOUT_END);
                }
                return DECLINED;
            }
            // KNOWN GAP: a mid-line `with` after a NEXT-LINE record type
            // body (`type M =`\n`  { fields } with`\n`  member ...`, FSharpPlus
            // NonEmptyMap). Closing S_TYPEBODY here mis-fires on 23 bench
            // files (interface_impl / member-accessor `with` forms share
            // the state). The same-line `= { ... } with` form parses.
            // `finally` is owned EXCLUSIVELY by try/finally (unlike
            // `with`), so when an S_TRY is open SOMEWHERE below, every
            // inner inline body must close first - one per invocation -
            // until the S_TRY branch above fires:
            //   `seq { try for e in c () do yield e finally comp () }`
            // closes the for-body here, then the try-body above.
            // `with` likewise when the inline body BELOW is a try (`try if
            // c then 1 else 2 with _ -> 3`): a `match x with` / `{ r with`
            // / `{ new I with` inside the body cannot reach here, because
            // at their `with` the body is incomplete and LAYOUT_END invalid.
            if ((!strcmp(w, "finally") || !strcmp(w, "with")) && layoutish(top->sort)) {
                for (size_t i = 0; i + 1 < s->n; i++) {
                    if (s->stk[i].sort == S_TRY) {
                        return close_top(s, lexer, LAYOUT_END);
                    }
                }
            }
        }
        // The arm list itself closes at a mid-line `in`/`end` once its
        // last arm body has closed (`... | B -> 2 in f 1`).
        if (top && top->sort == S_MATCH && valid[MATCH_END] && (!strcmp(w, "in") || !strcmp(w, "end"))) {
            if (!strcmp(w, "in")) s->in_claim_col = MID_COL();
            return close_top(s, lexer, MATCH_END);
        }
        if (valid[ELEMENT_DSL_OPEN] && element_dsl_parens_brace(lexer)) {
            return emit(lexer, ELEMENT_DSL_OPEN);
        }
    }
    #undef MID_COL
    return PASS;
}

static Step mid_ce_brace(TSLexer *lexer, const bool *valid, int32_t c) {
    // CE_BRACE_OPEN (zero-width): the `{` of a computation_expression body.
    // Emitted ONLY when the brace content is a CE body - NOT a record field
    // (`ident =`/`ident :`), NOT an object expression (`new ...`), NOT a
    // copy-update (`base with ...`). Lets `head { new ... }` / `head { f = ... }`
    // divert to application(head, object_expression/record) while
    // `head { return ... }` / `async { ... }` stay a computation_expression.
    // Zero-width (mark_end still at baseline): the advances below only PEEK;
    // on a match tree-sitter then lexes the literal `{`, on no-match we fall
    // through to `return false` and the literal `{` is lexed for the
    // record/object/application path.
    if (c == '{' && valid[CE_BRACE_OPEN]) {
        lexer->advance(lexer, true);            // past `{`
        // `{|` is an anonymous-record opener, never a CE body `{`.
        if (lexer->lookahead != '|') {
            skip_space(lexer);
            if (ce_brace_content_is_ce_body(lexer)) { return emit(lexer, CE_BRACE_OPEN); }
        }
    }
    return PASS;
}

static Step mid_semicolon(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top, int32_t c) {
    // A TRAILING `;` at end-of-line in a layout body that is about to
    // close by dedent/EOF: consume it INTO the LAYOUT_END (`let f () =\n
    // g ()\n a;` - NuGetV3 style). Once the body is a sequence the
    // grammar has no slot for the `;` (a grammar-level optional never
    // fires - the `;` shift commits to the separator reading), so the
    // terminator must disappear here. Only when the line truly ends
    // after the `;` (an inline `a; b` keeps its literal separator) and
    // only on a real dedent - an equal-column next line is a SIBLING
    // statement (`module M =\n f x;\n g y;`), handled by the `";"` _token.
    if (c == ';' && valid[LAYOUT_END] && top && layoutish(top->sort)) {
        lexer->advance(lexer, false);           // consume `;`
        if (lexer->lookahead != ';') {          // leave `;;` to the extras
            lexer->mark_end(lexer);             // token = just the `;`
            skip_hspace(lexer);
            // Same-line closer right after the `;`: the inline body ends
            // here (`f (fun () -> g (); )`, `[a; if c then x else y; ]`).
            // valid[LAYOUT_END] already excludes `(a; )` inside a body
            // whose own paren is still open.
            {
                int32_t a = lexer->lookahead;
                bool closer = (a == ')' || a == ']' || a == '}');
                if (!closer && a == '|') {
                    lexer->advance(lexer, true);
                    closer = (lexer->lookahead == ']' || lexer->lookahead == '}');
                }
                if (closer) { return close_top(s, lexer, LAYOUT_END); }
                if (a == '|') return DECLINED;     // lookahead consumed; not a closer
            }
            if (at_line_end(lexer->lookahead)) {
                uint32_t ncol; int32_t nfirst = 0;
                if (!next_line_indent(s, lexer, &ncol, &nfirst) || ncol < top->col) {
                    return close_top(s, lexer, LAYOUT_END);
                }
            }
        }
        return DECLINED;                           // not trailing: literal `;`
    }
    // A `;` that TERMINATES a statement rather than separating two of
    // them: the next line starts a DECLARATION, which can never be the
    // `sequence_expression` operand the grammar demands after `;`.
    // Emitted as an EXTRA so it needs no grammar slot - the decision
    // requires peeking past the `;` to the next line, which LR(1) cannot
    // do (it shifts on the `;` alone and only fails a token later).
    // The newline-separated form of this is handled by blocking
    // LAYOUT_SEMI before decl_starter(); a literal `;` never reaches it.
    if (c == ';' && valid[DECL_SEMI] && top && top->sort == S_DECL) {
        lexer->advance(lexer, false);           // consume `;`
        if (lexer->lookahead != ';') {          // leave `;;` to fsi_terminator
            lexer->mark_end(lexer);             // token = just the `;`
            skip_hspace(lexer);
            // A `;` that ends the file is a terminator too.
            if (lexer->lookahead == 0) { return emit(lexer, DECL_SEMI); }
            if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
                uint32_t ncol; int32_t nfirst = 0;
                if (!next_line_indent(s, lexer, &ncol, &nfirst) || decl_starter(lexer, nfirst)) {
                    return emit(lexer, DECL_SEMI);
                }
            }
        }
        return DECLINED;                           // not a terminator: literal `;`
    }
    // Same disease, BRACKET flavor: a trailing `;` right before the
    // closing delimiter of a CE / list / array body (`seq { yield x; }`,
    // `yield 1;`\n`}`) - the grammar's own trailing-`;` optional never
    // fires (the `;` shift commits to extending the LAST STATEMENT's
    // expression into a sequence), so consume the `;` INTO the
    // BRACKET_CLOSE. Same-line (`; }`) and next-line-closer forms.
    if (c == ';' && valid[BRACKET_CLOSE] && top && top->sort == S_BRACKET) {
        lexer->advance(lexer, false);           // consume `;`
        if (lexer->lookahead != ';') {          // leave `;;` to the extras
            lexer->mark_end(lexer);             // token = just the `;`
            skip_hspace(lexer);
            int32_t a = lexer->lookahead;
            if (is_close_bracket(a) || a == '}' || a == ']') {
                return close_top(s, lexer, BRACKET_CLOSE);
            }
            if (at_line_end(a)) {
                uint32_t ncol; int32_t nfirst = 0;
                if (next_line_indent(s, lexer, &ncol, &nfirst) &&
                    (is_close_bracket(nfirst) || nfirst == '}' || nfirst == ']')) {
                    return close_top(s, lexer, BRACKET_CLOSE);
                }
            }
        }
        return DECLINED;                           // not trailing: literal `;`
    }
    return PASS;
}

static Step mid_doc_resume(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top, int32_t c) {
    // DOC-RESUME dispatch: a previous zero-width token (a close, or an
    // earlier gate) was anchored AT this `///` line - the scan resumes
    // mid-line ON the docs. Skip the doc block (+ blank lines), compute
    // the post-doc line's first/col, and run the same dispatch the
    // boundary path uses (CASE/AND gates, typebody-close-at-docs). On
    // no-match return false: the parser lexes the doc itself next.
    if (c == '/' && valid[CASE_DOCS_OPEN] + valid[AND_DOCS_OPEN] + valid[LAYOUT_END] + valid[LAYOUT_SEMI] > 0) {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '/') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == '/') {
                s->scan.skipped_doc_lines = true;
                // consume the rest of this doc line, then any further
                // doc-only / blank lines
                for (;;) {
                    skip_line(lexer);
                    if (lexer->lookahead == 0) return DECLINED;
                    if (lexer->lookahead == '\r') lexer->advance(lexer, true);
                    if (lexer->lookahead == '\n') lexer->advance(lexer, true);
                    uint32_t ind2 = 0;
                    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') { ind2++; lexer->advance(lexer, true); }
                    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') continue;
                    if (lexer->lookahead == '/') {
                        lexer->advance(lexer, true);
                        if (lexer->lookahead == '/') { lexer->advance(lexer, true); continue; }
                        return DECLINED;
                    }
                    // post-doc real line
                    if (try_and_docs(s, lexer, valid, lexer->lookahead, ind2, top)) return EMITTED;
                    return DECLINED;
                }
            }
        }
        return DECLINED;
    }
    return PASS;
}

static Step mid_ctor_tuple_gate(Scanner *s, TSLexer *lexer, const bool *valid, int32_t c) {
    // CTOR_TUPLE_GATE (zero-width): at a let-binding NAME position,
    // `ident(.ident)* ( ... ) ,` means `let Ctor(a, b), rest = ...` - a
    // tuple DECONSTRUCTION (fn defs never have `,` after params). At
    // this consumption-safe tail the word-branch above may have eaten
    // the first <=9 identifier chars; resume from wherever we are.
    if (valid[CTOR_TUPLE_GATE] &&
        (is_alpha(c) || c == '_')) {
        char w0[10]; size_t w0n = 0;
        while (is_alpha(lexer->lookahead) ||
               is_digit(lexer->lookahead) ||
               lexer->lookahead == '_' || lexer->lookahead == '\'' ||
               lexer->lookahead == '.') {
            if (w0n < 9) w0[w0n++] = (char)lexer->lookahead;
            lexer->advance(lexer, true);
        }
        w0[w0n] = '\0';
        const char *first_word = w0n ? w0 : s->scan.midline_word;   // word-branch may have eaten it
        skip_hspace(lexer);
        // `let AesKey key, AesIV iv = ...`: a constructor applied to bare
        // argument names, then `,`. Not when the first word was a binding
        // modifier (`let mutable a, b = ...`, `let private a, b = ...`).
        if (!strcmp(first_word, "mutable") || !strcmp(first_word, "inline") || !strcmp(first_word, "rec") ||
            !strcmp(first_word, "private") || !strcmp(first_word, "internal") || !strcmp(first_word, "public")) return DECLINED;
        if (is_alpha(lexer->lookahead) || lexer->lookahead == '_') {
            while (is_alpha(lexer->lookahead) || lexer->lookahead == '_') {
                while (is_alpha(lexer->lookahead) ||
                       is_digit(lexer->lookahead) ||
                       lexer->lookahead == '_' || lexer->lookahead == '\'') lexer->advance(lexer, true);
                skip_hspace(lexer);
            }
            if (lexer->lookahead == ',') { return emit(lexer, CTOR_TUPLE_GATE); }
            return DECLINED;
        }
        if (lexer->lookahead == '(') {
            lexer->advance(lexer, true);
            // `let Ctor(field = pat) ...`: a named-field deconstruction.
            skip_hspace(lexer);
            if (is_alpha(lexer->lookahead) || lexer->lookahead == '_') {
                while (is_alpha(lexer->lookahead) ||
                       is_digit(lexer->lookahead) ||
                       lexer->lookahead == '_' || lexer->lookahead == '\'') lexer->advance(lexer, true);
                skip_hspace(lexer);
                if (lexer->lookahead == '=') {
                    lexer->advance(lexer, true);
                    if (lexer->lookahead != '=') { return emit(lexer, CTOR_TUPLE_GATE); }
                }
            }
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
                skip_hspace(lexer);
                if (lexer->lookahead == ',') { return emit(lexer, CTOR_TUPLE_GATE); }
                // `let Ctor(a, _) as name = ...`
                if (lexer->lookahead == 'a') {
                    lexer->advance(lexer, true);
                    if (lexer->lookahead == 's') {
                        lexer->advance(lexer, true);
                        if (lexer->lookahead == ' ' || lexer->lookahead == '\t') { return emit(lexer, CTOR_TUPLE_GATE); }
                    }
                }
            }
        }
        return DECLINED;   // consumption-safe: this is a return-false tail
    }
    return PASS;
}

static Step mid_block_comment(TSLexer *lexer, const bool *valid, int32_t c) {
    // Same-line block comment after code (`1 (* a (* b *) *)`): the
    // external must lex it (nesting); nothing else fires for `(`.
    if (c == '(') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == '*') { lexer->advance(lexer, false); return finish_block_comment(lexer, valid) ? EMITTED : DECLINED; }
        return DECLINED;
    }
    return PASS;
}

    // ---- Mid-line closes ------------------------------------------------------
    // An inline body / arm-list / block bracket can close on the SAME line before
    // a closing delimiter `)` `]` `}` (and `|]`/`|}`):
    //   (fun x -> body)   (match v with ... | _ -> k)   [ ... ]   { ... }
    // Fire one close per call (gated by valid + the top sort); tree-sitter
    // re-invokes for multi-level (arm body, then arm-list, then `)`).
static Step scan_mid_line(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top) {
    skip_hspace(lexer);
    int32_t c = lexer->lookahead;
    if (at_line_end(c)) return PASS;
    Step r;
    if ((r = mid_closers(s, lexer, valid, top, c)) != PASS) return r;
    if ((r = mid_word(s, lexer, valid, top, c)) != PASS) return r;
    if ((r = mid_ce_brace(lexer, valid, c)) != PASS) return r;
    if ((r = mid_semicolon(s, lexer, valid, top, c)) != PASS) return r;
    if ((r = mid_doc_resume(s, lexer, valid, top, c)) != PASS) return r;
    if ((r = mid_ctor_tuple_gate(s, lexer, valid, c)) != PASS) return r;
    if ((r = mid_block_comment(lexer, valid, c)) != PASS) return r;
    return DECLINED;   // other same-line content: layout doesn't apply
}

static Step boundary_infix_continuation(TSLexer *lexer, Ctx *top, uint32_t col, int32_t first, int32_t bar_c1) {
// A leading infix operator continues the previous expression (F#'s
// leading-operator rule) - UNLESS it dedents below an EXPRESSION body
// (S_EXPR: then/elif/else/lambda/let-in value) or below a match ARM column
// (S_MATCH), in which case that body/arm-list must close first and
// re-invocation continues the OUTER chain. This pipes the whole if in
// `if c then a else b`\n` |> f`, and the whole match in `|> match ... with`\n
// `| arm -> ...`\n`|> next` (Chocolatey pipeline style) - without the S_MATCH
// case the dedented `|>` extended the LAST ARM's body, and continuation
// ARGUMENT lines after it then mis-lexed as new arm patterns.
// FSC grants an infix token an offside GRACE of its length + 1, so a mildly
// dedented operator still continues the body (`let v =    a`\n`           ||| b`)
// while one dedented WELL below (`|>` at the pipeline column under a match
// arm body, `>> g` four columns left of a lambda body) is offside and closes
// first. Measured inside the block below; the `+`/`-`/`@`/`.` leads keep the
// strict rule (expr_strict).
bool expr_strict = (top->sort == S_EXPR && col < top->col);
bool infix_continues = 
                       // <= for S_MATCH: an op AT the arm column can't be an
                       // arm - the arm-list must END so the op continues the
                       // whole match (`| false -> b\n|> g` at the arm col).
                       !(top->sort == S_MATCH && col <= top->col) &&
                       !(top->sort == S_LAYOUT && col + 4 < top->col);

// `|>`/`<|`/`>>` pipe chains, `=`/`<`/`>`/`*`/... arithmetic, `::` cons.
// `|` alone is a match arm (not infix); only `|>`/`||` are. `&`/`:` count
// only doubled. Other unary-capable leads (`!` `~`) are excluded.
if (infix_continues) {
    int32_t c0 = first;
    if (c0 == '|' || c0 == '<' || c0 == '>' || c0 == '=' ||
        c0 == '*' || c0 == '/' || c0 == '%' || c0 == '^' || c0 == '&' || c0 == ':' || c0 == '?') {
        int32_t c1;
        if (c0 == '|') c1 = bar_c1;                  // already peeked above
        else { lexer->advance(lexer, true); c1 = lexer->lookahead; }
        int oplen = 1;
        if (is_opchar(c1)) {
            oplen = 2; lexer->advance(lexer, true);
            while (is_opchar(lexer->lookahead)) { oplen++; lexer->advance(lexer, true); }
        }
        bool infix = false;
        // `|` + any operator char = a custom `|`-led infix operator
        // continuation (`|>`, `||`, `|?>`, `||>`, `|@`, ...). A match-arm
        // `|` is followed by whitespace or a pattern char instead.
        if (c0 == '|')      infix = is_bar_op_tail(c1);
        else if (c0 == '&') infix = (c1 == '&');                        // &&
        // `::` `:>` `:?`, and a bare `: T` ascription on its own line
        // (`{ A = 1 }`\n`: R`): no statement starts with `:`.
        else if (c0 == ':') infix = true;
        // `?=>!`-style operators; `?ident` is an optional named argument
        // (that line is a new statement / element).
        else if (c0 == '?') infix = is_opchar(c1);
        else if (c0 == '/') infix = (c1 != '/');                       // `//` = COMMENT, not an operator
        else                infix = true;                              // = < > * % ^
        if (infix && top->sort == S_EXPR && !top->par && col + oplen + 1 < top->col) infix = false;
        if (infix) return DECLINED;
    }

    // Leading `+`/`-`/`@` - also continuation, but only in a layout body (a
    // bracket / match arm-list keeps newline-as-element/arm separator). They
    // are unary/prefix-capable. Excluded forms: `->` (lambda/match arrow),
    // `@"..."` (verbatim string), `@>` / `@@>` (code-quotation close).
    // `@@` followed by anything but `>` is the custom path-concat operator
    // (FAKE's `dir @@ file` written leading) - a continuation.
    if (!expr_strict && layoutish(top->sort) && (first == '+' || first == '-' || first == '@')) {
        lexer->advance(lexer, true);
        int32_t c1 = lexer->lookahead;
        if (first == '+') return DECLINED;
        if (first == '-') {
            if (c1 != '>') return DECLINED;
            // `->=` / `->!`-style custom operators continue the line; a bare
            // `->` is a lambda / arm arrow.
            lexer->advance(lexer, true);
            if (is_opchar(lexer->lookahead)) return DECLINED;
        }
        if (first == '@') {
            if (c1 != '"' && c1 != '>' && c1 != '@') return DECLINED;
            // `@>` / `@@>` at the BODY column closes a multi-line quotation
            // (`<@`\n`    body`\n`@>`): no separator, no close - the token
            // belongs to the still-open quotation expression. A DEDENTED
            // closer falls through so the layout close fires first.
            if (c1 == '>' && col == top->col) return DECLINED;
            if (c1 == '@') {
                lexer->advance(lexer, true);
                if (lexer->lookahead != '>') return DECLINED;   // `@@...` operator, not `@@>`
                if (col == top->col) return DECLINED;           // `@@>` at body col - see above
            }
        }
    }

    // A leading `.` is always a continuation: a fluent member chain on its
    // own line (`builder\n .Method()`), a `.`-led custom operator (`.>>.`,
    // FParsec style), or a `..` range - no F# statement can START with `.`.
    if (!expr_strict && layoutish(top->sort) && first == '.') return DECLINED;
}
    return PASS;
}

static Step boundary_bracket(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top, uint32_t col, int32_t first, bool bar_arm, int32_t bar_c1) {
    if (valid[BRACKET_CLOSE] && (is_close_bracket(first) || first == '|')) { return close_top(s, lexer, BRACKET_CLOSE); }
    // Same blocker as LAYOUT_SEMI: a CE statement separator must not fire
    // before `else`/`elif`/... - otherwise `if c then return a`\n`else ...`
    // inside a CE detaches the else.
    if (valid[BRACKET_SEMI] && col == top->col && !semi_blocked(lexer, first)) { return emit(lexer, BRACKET_SEMI); }
    // A DEEPER line led by a statement keyword is still a new element
    // (`[ yield a`\n`    for x in xs do ...`): no expression continues with it.
    if (valid[BRACKET_SEMI] && col > top->col && is_lower(first)) {
        char w[12]; read_word(lexer, w, sizeof w);
        if (!strcmp(w, "yield") || !strcmp(w, "for") || !strcmp(w, "let") || !strcmp(w, "use") ||
            !strcmp(w, "match") || !strcmp(w, "while") || !strcmp(w, "return") || !strcmp(w, "try") ||
            !strcmp(w, "if") || !strcmp(w, "do")) { return emit(lexer, BRACKET_SEMI); }
        return DECLINED;
    }
    // First element of a `{`-block body that is itself an element DSL
    // (`div() {\n span() {...}`): the mid-line block is unreachable from the
    // line-boundary path, so probe here (after the separator above, so a
    // SUBSEQUENT element gets its separator first then this on re-invoke).
    if (try_element_dsl(lexer, valid)) return EMITTED;
    // Own-line `1.` element: the mid-line float probe is unreachable
    // from the boundary path, so run it here (digit-led, nothing above
    // consumed the lookahead).
    if (valid[FLOAT_TRAILING_DOT] && is_digit(first) && scan_trailing_dot_float(lexer)) return EMITTED;
    return DECLINED;

}

static Step boundary_match(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top, uint32_t col, int32_t first, bool bar_arm, int32_t bar_c1) {
    // `|]` / `|}` on its own line: the array / anon-record closer, never
    // an arm (`[|`\n`    match v with`\n`    | A -> 1`\n`    |]`).
    if (valid[MATCH_END] && first == '|' && (bar_c1 == ']' || bar_c1 == '}')) { return close_top(s, lexer, MATCH_END); }
    // Close the arm-list when a line dedents below the arm column, or sits
    // at the arm column but does NOT start a new `|` arm. EXCEPTION: a
    // `|` exactly TWO columns left of the arm column is a continuation
    // arm whose PATTERN aligns with the (inline) first arm's pattern -
    // Hopac house style:
    //   ... >>= function Cons (_, i) -> push xM i x
    //                | Nil -> imp ()
    if (valid[MATCH_END] && (col < top->col || (col == top->col && !bar_arm)) &&
        !(bar_arm && col + 2 == top->col)) { return close_top(s, lexer, MATCH_END); }
    return DECLINED;

}

static Step boundary_layout(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top, uint32_t col, int32_t first, bool bar_arm, int32_t bar_c1) {
    // MEMBERS_OPEN: after a SAME-LINE type body, a line indented past the
    // enclosing context that starts with a member keyword (or `[<`)
    // opens an S_TYPEBODY at its column, so the members become children
    // of the type instead of an application chain headed by `member`.
    // Decided HERE (boundary path) so a miss keeps the ordinary
    // close/separator handling below (`type A = B`\n`and C = D`).
    if (valid[MEMBERS_OPEN] && col > top->col) {
        bool ok = false;
        if (first == '[') {
            lexer->advance(lexer, true);
            ok = (lexer->lookahead == '<');
        } else if (is_lower(first)) {
            char w[12]; read_word(lexer, w, sizeof w);
            ok = !strcmp(w, "member") || !strcmp(w, "static") || !strcmp(w, "override") ||
                 !strcmp(w, "default") || !strcmp(w, "abstract") || !strcmp(w, "interface") ||
                 !strcmp(w, "val") || !strcmp(w, "new") || !strcmp(w, "inherit");
        }
        if (ok) { push(s, S_TYPEBODY, col); return emit(lexer, MEMBERS_OPEN); }
        return DECLINED;   // lookahead consumed: a continuation line after an inline type body
    }
    // DANGLING DOC: skipped `///` lines sit AT/INSIDE this body, but
    // the line after them dedents - the docs belong to THIS body (a
    // floating doc statement), not to the dedented declaration. Hold
    // the close; the doc lexes as a standalone statement (the wrapper
    // fork dies at the close that follows), then the dedent re-fires.
    if (valid[LAYOUT_END] && col < top->col &&
        s->scan.skipped_doc_lines && s->scan.doc_indent >= top->col) return DECLINED;
    // A `|` case LEFT of a bare first case is still a case of this type
    // (`type E =`\n`      A = 0`\n`    | B = 1`); types never nest inside
    // match arms, so a dedented `|` under a type body is never an arm.
    if (valid[LAYOUT_END] && col < top->col && top->sort == S_TYPEBODY && bar_arm) return DECLINED;
    // A `(` block closes only at a closer: a dedented line inside it is a
    // continuation (`when (match x with`\n`  | A -> ...`, `(f a`\n`  +> g)`).
    // A dedented declaration keyword is recovery for an unclosed `(`.
    if (top->par && col < top->col && first != ')' && !is_close_bracket(first) &&
        !decl_starter(lexer, first)) return DECLINED;
    if (top->par && first == ',') return DECLINED;
    if (valid[LAYOUT_END] && col < top->col) {
        if (top->sort == S_EXPR && top->thn && first == 'e') {
            char w[12]; read_word(lexer, w, sizeof w);
            if (!strcmp(w, "else") || !strcmp(w, "elif")) s->else_claim_col = (int32_t)col;
        }
        return close_top(s, lexer, LAYOUT_END);
    }
    // A `#if`-family directive line sits between this declaration and a
    // line that starts a NEW one: the two are alternative spellings of the
    // same declaration, sharing the parameters/body that follow `#endif`.
    // End the first one here (zero-width) so the second parses as a
    // declaration instead of being absorbed as parameters. Checked before
    // the peeks below, which consume the line's first word.
    if (valid[PREPROC_BREAK] && s->scan.skipped_directive && decl_starter(lexer, first)) {
        return emit(lexer, PREPROC_BREAK);
    }
    // A leading `|` arm marker AT the body column: FSC permits
    // continuation arms MORE indented than their match (`| _ -> ()` at
    // col 8, match arms at col 4 - FCS PostInferenceChecks style). The
    // body must close so the over-indented arm reaches the enclosing
    // match. Gated on such a match existing further left, and NOT
    // S_TYPEBODY (DU cases legitimately lead with `|` at the body col).
    // `<=`: an arm body opened AT the arm column (`| p ->`\n`| body` with
    // the body undented to the `|`, LexFilter.fs) ends at the next arm.
    if (valid[LAYOUT_END] && bar_arm && col == top->col && top->sort != S_TYPEBODY) {
        for (int i = (int)s->n - 2; i >= 0; i--) {
            if (s->stk[i].sort == S_MATCH && s->stk[i].col <= col) {
                return close_top(s, lexer, LAYOUT_END);
            }
            if (s->stk[i].col < col && s->stk[i].sort != S_MATCH) break;
        }
    }
    // A leading close delimiter `)`/`]`/`}` ends the enclosing construct,
    // so an open layout body must close first - even at == body col, where
    // neither the dedent above nor a separator (semi_blocked on closers)
    // fires. E.g. a lambda body whose `)` is on its own line:
    //   `g (fun c ->\n        x\n        )` (the `)` aligned with `x`).
    if (valid[LAYOUT_END] && (first == ')' || is_close_bracket(first))) { return close_top(s, lexer, LAYOUT_END); }
    // A `with` type-augmentation aligned AT the body column closes the
    // TYPE body (S_TYPEBODY only) so the augmentation attaches:
    //   type D =\n    | A\n    | B\n    with\n        member ...
    // A module body (S_LAYOUT) at == col must NOT close before `with`.
    if (top->sort == S_TYPEBODY && valid[LAYOUT_END] && col == top->col && first == 'w') {
        lexer->advance(lexer, true);            // 'w'
        if (lexer->lookahead == 'i') { lexer->advance(lexer, true);
        if (lexer->lookahead == 't') { lexer->advance(lexer, true);
        if (lexer->lookahead == 'h') { lexer->advance(lexer, true);
            int32_t a = lexer->lookahead;
            bool word = is_ident_char(a);
            if (!word) { return close_top(s, lexer, LAYOUT_END); }
        }}}
    }
    // A module-only keyword (`open`/`module`/`namespace`/`exception`)
    // aligned AT the type-body column closes the type body. A NON-indented
    // DU (`type T =\n| A\n| B\nopen ...`) puts the body at the module column, so
    // a dedent never fires; without this, the union field type
    // over-consumes the following `open Foo` as a postfix type.
    // `type` too: a non-indented DU (`| A` at column 0) followed by the
    // next `type` decl - with or without an attribute row before it.
    if (top->sort == S_TYPEBODY && valid[LAYOUT_END] && col == top->col &&
        (is_lower(first) || first == '[')) {
        bool ok = true;
        if (first == '[') ok = skip_bracket_attrs(lexer);
        if (ok) {
            char w[12]; size_t wn = 0; int32_t lk = lexer->lookahead;
            while (wn < 11 && is_lower(lk)) { w[wn++] = (char)lk; lexer->advance(lexer, true); lk = lexer->lookahead; }
            w[wn] = '\0';
            bool boundary = !(is_ident_char(lk));
            if (boundary && (!strcmp(w, "open") || !strcmp(w, "module") ||
                             !strcmp(w, "namespace") || !strcmp(w, "exception") ||
                             !strcmp(w, "type"))) {
                return close_top(s, lexer, LAYOUT_END);
            }
        }
    }
    // Separator decision. Both word peeks below (decl_starter,
    // semi_blocked) CONSUME lookahead, so they run only when a separator
    // is actually on the table - otherwise the probes further down would
    // see a lexer parked past the line's first word and always miss.
    if (valid[LAYOUT_SEMI] && col == top->col &&
        !(s->scan.skipped_doc_lines && word_is_decl_kw(s->scan.post_doc_word))) {
        // S_DECL: never separate before a declaration keyword - the line is a
        // new `_token`, not a `sequence_expression` continuation of the prior
        // bare-expression statement.
        // Both tests need the line's first word; read it ONCE (each
        // helper consumes it, so chaining them made the second see "").
        if (is_lower(first)) {
            char w[12]; read_word(lexer, w, sizeof w);
            // `with`/`finally` AT the try body's column ends the body
            // (`try Map.find x g`\n`    with _ -> ...`, body col = `Map`).
            if (top->sort == S_TRY && valid[LAYOUT_END] &&
                (!strcmp(w, "with") || !strcmp(w, "finally"))) {
                return close_top(s, lexer, LAYOUT_END);
            }
            if (top->sort == S_DECL && decl_starter_word(w)) return DECLINED;
            // `new (...) as this =`\n`    body`\n`    then`\n`    effects`: the
            // constructor body ends at a `then` at its own column.
            if (!strcmp(w, "then") && valid[LAYOUT_END]) { return close_top(s, lexer, LAYOUT_END); }
            // `else` at a then-body's own column ends the body unless the
            // body is itself an `if` (which then owns the `else`).
            if ((!strcmp(w, "else") || !strcmp(w, "elif")) && top->sort == S_EXPR && top->thn &&
                s->else_claim_col != (int32_t)col && valid[LAYOUT_END]) {
                s->else_claim_col = (int32_t)col;
                return close_top(s, lexer, LAYOUT_END);
            }
            if (!semi_blocked_word(w)) { s->else_claim_col = -1; return emit(lexer, LAYOUT_SEMI); }
            return DECLINED;
        }
        if (top->sort == S_DECL && decl_starter(lexer, first)) return DECLINED;
        if (!semi_blocked(lexer, first)) { s->else_claim_col = -1; return emit(lexer, LAYOUT_SEMI); }
        return DECLINED;   // peeks consumed the lookahead - no further probing
    }
    // ...and RIGHT of it (a continuation-indented `with`). Nothing after
    // this point applies to a `with`/`finally`-led line, so consuming
    // the word on a miss is harmless.
    if (top->sort == S_TRY && valid[LAYOUT_END] && (first == 'w' || first == 'f')) {
        char w[12]; read_word(lexer, w, sizeof w);
        if (!strcmp(w, "with") || !strcmp(w, "finally")) { return close_top(s, lexer, LAYOUT_END); }
        return DECLINED;
    }
    // Attributed labelled param on its own line (`delegate of`\n
    // `[<Out>] data: byte[] * ...`): the mid-line probe never sees a line
    // start, so run it here. `[` cannot start either probe below.
    if (first == '[' && valid[LABEL_ATTR] && try_label_attr(lexer)) return EMITTED;
    if (valid[LABEL_GATE] && col > top->col &&
        (is_alpha(first) || first == '_' || first == '?' || first == '`')) {
        if (try_label_gate(lexer)) return EMITTED;
        return DECLINED;
    }
    // Element DSL as the first statement of an indented let/expr body
    // (`let page =\n div() {...}`): probe after the separator above.
    if (try_element_dsl(lexer, valid)) return EMITTED;
    // CE body `{` on its OWN line below the builder (`seq`\n`    {`\n
    // `        yield ...` - FAKE/WiX style): the mid-line CE_BRACE_OPEN
    // dispatch is unreachable from here, so classify the brace content
    // now. Records / object expressions keep the literal `{` (no token).
    if (try_ce_brace(lexer, valid, first)) return EMITTED;
    // Own-line `10.` continuation argument (`Expect.equal x`\n`    10.`\n
    // `    "msg"`): same as the S_BRACKET tail above.
    if (valid[FLOAT_TRAILING_DOT] && is_digit(first) && scan_trailing_dot_float(lexer)) return EMITTED;
    return DECLINED;

}

static Step scan_line_boundary(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top) {
    if (lexer->lookahead == 0) {
        if (valid[BRACKET_CLOSE] && top && top->sort == S_BRACKET) { return close_top(s, lexer, BRACKET_CLOSE); }
        if (valid[MATCH_END]    && top && top->sort == S_MATCH)    { return close_top(s, lexer, MATCH_END); }
        if (valid[LAYOUT_END]   && top && layoutish(top->sort))   { return close_top(s, lexer, LAYOUT_END); }
        return DECLINED;
    }

    uint32_t col; int32_t first = 0;
    s->scan.region_stop = true;
    s->scan.doc_gate_possible = valid[CASE_DOCS_OPEN] || valid[AND_DOCS_OPEN];
    s->scan.top_col_for_docs = top ? top->col : 0;
    bool nli = next_line_indent(s, lexer, &col, &first);
    s->scan.region_stop = false;
    if (!nli) {                                             // EOF after trailing blanks
        // Dangling `///` docs at EOF inside this body: let them lex as a
        // standalone statement before the body closes (see the dedent twin).
        if (s->scan.skipped_doc_lines && top && layoutish(top->sort) && s->scan.doc_indent >= top->col) return DECLINED;
        if (valid[BRACKET_CLOSE] && top && top->sort == S_BRACKET) { return close_top(s, lexer, BRACKET_CLOSE); }
        if (valid[MATCH_END]    && top && top->sort == S_MATCH)    { return close_top(s, lexer, MATCH_END); }
        if (valid[LAYOUT_END]   && top && layoutish(top->sort))   { return close_top(s, lexer, LAYOUT_END); }
        return DECLINED;
    }
    if (first == FIRST_COMMENT_LINE) {
        // Line-start comment-ONLY line - next_line_indent consumed the whole
        // comment with advance(false) and mark_end'ed at its `*)`.
        if (!valid[BLOCK_COMMENT] && !valid[BLOCK_DOC_COMMENT]) return DECLINED;
        // `// ...` lines on the way here were skipped as PADDING; emitting the
        // block comment now would absorb their text (no comment node -> no
        // highlight). Decline: the internal lexer lexes them as line_comment
        // extras, and a later scan starts right at the `(*`.
        if (s->scan.skipped_line_comments) return DECLINED;
        lexer->result_symbol = (s->scan.comment_doc && valid[BLOCK_DOC_COMMENT]) ? BLOCK_DOC_COMMENT
                             : (valid[BLOCK_COMMENT] ? BLOCK_COMMENT : BLOCK_DOC_COMMENT);
        return EMITTED;
    }
    // A leading `|` is a match arm UNLESS it is `|]` (array close) or `|}` (anon
    // record close). We can't cheaply peek the 2nd char here (next_line_indent
    // already advanced), so treat `|` as an arm marker; the bracket cases are
    // handled by BRACKET_CLOSE above/below via valid-gating.
    bool bar_arm = (first == '|');
    // `|>` `||` `|?>` ... - a `|`-led OPERATOR is never an arm marker; peek the
    // char after the `|` once, here, so BOTH the infix check below and the
    // S_MATCH close (`|>` at exactly the arm column pipes the WHOLE match) see
    // the same classification. `|]`/`|}` stay arm-ish (bracket closers handle).
    int32_t bar_c1 = 0;
    if (bar_arm) {
        lexer->advance(lexer, true);
        bar_c1 = lexer->lookahead;
        if (is_bar_op_tail(bar_c1)) bar_arm = false;
    }

    // `///` docs followed by `and` - gate the and-clause doc slot. FIRST: by
    // the time the per-sort logic runs, semi/decl-starter branches treat `and`
    // specially and the grammar may reduce the preceding decl, dropping the
    // docs to the standalone net. Strictly gated (valid + docs-were-skipped +
    // exact word `and`), so it cannot preempt closes that matter: when an
    // enclosing body still has to close first, AND_DOCS_OPEN isn't valid yet.
    if (try_and_docs(s, lexer, valid, first, col, top)) return EMITTED;
    Step r = boundary_infix_continuation(lexer, top, col, first, bar_c1);
    if (r != PASS) return r;
    switch (top->sort) {
        case S_BRACKET: return boundary_bracket(s, lexer, valid, top, col, first, bar_arm, bar_c1);
        case S_MATCH:   return boundary_match(s, lexer, valid, top, col, first, bar_arm, bar_c1);
        default:        return boundary_layout(s, lexer, valid, top, col, first, bar_arm, bar_c1);
    }
}

static bool scanner_scan(void *p, TSLexer *lexer, const bool *valid) {
    Scanner *s = p;
    memset(&s->scan, 0, sizeof s->scan);
    lexer->mark_end(lexer);                       // zero-width baseline; re-marked only by real (FLOAT) tokens
    if (valid[ERROR_SENTINEL]) return false;      // parse-error recovery: stay out of tree-sitter's way

    // ---- Interpolated-string text (lexical; before any layout logic) ----------
    // When a text symbol is valid we are inside a string: no layout token applies.
    // Consume the text run, or return false at a structural char so tree-sitter
    // lexes the `{`/`"`/`%` itself.
    if (valid[INTERP_STRING_TEXT])   { bool ok = scan_interp_text(lexer, TX_STRING);   if (ok) lexer->result_symbol = INTERP_STRING_TEXT;   return ok; }
    if (valid[INTERP_VERBATIM_TEXT]) { bool ok = scan_interp_text(lexer, TX_VERBATIM); if (ok) lexer->result_symbol = INTERP_VERBATIM_TEXT; return ok; }
    if (valid[INTERP_TRIPLE_TEXT])   { bool ok = scan_interp_text(lexer, TX_TRIPLE);   if (ok) lexer->result_symbol = INTERP_TRIPLE_TEXT;   return ok; }

    // The source file itself is a declaration body at column 0. Without this
    // implicit context, top-level statements had NO layout context, so no
    // `_layout_semi` ever separated them and consecutive bare expressions
    // (`register ("A", a)`\n`register ("B", b)`) glued into one curried
    // application_expression. S_DECL is the right sort: a `let`/`type`/... line
    // still starts a fresh `_token` instead of extending the previous statement.
    // Never popped in practice - a dedent below column 0 is impossible and
    // `_layout_end` is not valid at source-file scope.
    if (s->n == 0) push(s, S_DECL, 0);
    Ctx *top = &s->stk[s->n - 1];
    Step r;
    if ((r = scan_ctor_attr(lexer, valid)) != PASS) return r == EMITTED;
    if ((r = scan_body_opens(s, lexer, valid, top)) != PASS) return r == EMITTED;
    if ((r = scan_trailing_float(lexer, valid)) != PASS) return r == EMITTED;
    if ((r = scan_newline_opens(s, lexer, valid, top)) != PASS) return r == EMITTED;
    if ((r = scan_paren_field_open(s, lexer, valid)) != PASS) return r == EMITTED;
    if ((r = scan_record_open(s, lexer, valid)) != PASS) return r == EMITTED;
    if ((r = scan_mid_line(s, lexer, valid, top)) != PASS) return r == EMITTED;
    return scan_line_boundary(s, lexer, valid, top) == EMITTED;
}

// The signature grammar includes this file and exports the same scanner under
// its own name.
#ifndef TS_SHARED_SCANNER
void *tree_sitter_fsharp_external_scanner_create(void) { return scanner_create(); }
void tree_sitter_fsharp_external_scanner_destroy(void *p) { scanner_destroy(p); }
unsigned tree_sitter_fsharp_external_scanner_serialize(void *p, char *buf) { return scanner_serialize(p, buf); }
void tree_sitter_fsharp_external_scanner_deserialize(void *p, const char *buf, unsigned len) { scanner_deserialize(p, buf, len); }
bool tree_sitter_fsharp_external_scanner_scan(void *p, TSLexer *lexer, const bool *valid) { return scanner_scan(p, lexer, valid); }
#endif
