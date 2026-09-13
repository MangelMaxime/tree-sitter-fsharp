#include "tree_sitter/parser.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// External scanner for F#: layout (offside rule), interpolated-string text,
// nested block comments and a few zero-width lookahead gates.
//
// Layout model:
//   * Opens are grammar-driven: the grammar asks for a zero-width open token
//     after a layout keyword (`=` / `then` / `->` / `do` / `with` / `[` / `{`)
//     and the scanner pushes a context at the column of the body's first token.
//   * Closes and separators are scanner-driven by column comparison, gated on
//     `valid[...]` (true when the grammar expects the token, or on error
//     recovery when every symbol is valid). The scanner decides whether to
//     close at this indent; the grammar decides which construct.
//   * One stack of contexts (Ctx), each with a sort (Sort). A multi-level
//     dedent emits one close per scan call; layout tokens are zero-width, so
//     tree-sitter re-invokes the scanner at the same position.
//   * `Scanner.scan` is zeroed at the start of every scan; only the stack and
//     the two claim columns outlive a scan, and only the stack is serialized.
//   * A peek that advances the lexer is destructive: its caller must decide
//     right after it and never fall through to another peek.
//
// Token enum must match the `externals:` order in grammar.js.
typedef enum {
    ERROR_SENTINEL,     // never valid except on error recovery (all symbols valid)
    LAYOUT_OPEN,        // let/member/decl body; pushes S_LAYOUT
    LAYOUT_SEMI,        // next line at the body column
    LAYOUT_END,         // next line dedents below the body column
    MATCH_OPEN,         // arm-list after `with`/`function`; pushes S_MATCH
    MATCH_END,          // next line below the arm column, or at it and not a `|`
    BRACKET_OPEN,       // `[` / `[|` / `{` body; pushes S_BRACKET
    BRACKET_SEMI,       // next line at the element column
    BRACKET_CLOSE,      // `]` / `|]` / `}` closing a block bracket
    RECORD_OPEN,        // `{` followed by a field shape (`ident =` / `ident :`); pushes S_BRACKET
    BLOCK_OPEN,         // module body on the next line; pushes S_DECL
    TYPE_OPEN,          // type body on the next line; pushes S_TYPEBODY
    EXPR_OPEN,          // expression body (lambda, let-in value); pushes S_EXPR
    ELSE_OPEN,          // else body; declined for a same-line `else if` (elif clause)
    FLOAT_TRAILING_DOT, // `1.` (lexical)
    INTERP_STRING_TEXT,   // text chunk of $"..." (external so a leading `//` is not a comment)
    INTERP_VERBATIM_TEXT, // text chunk of $@"..." / @$"..."
    INTERP_TRIPLE_TEXT,   // text chunk of $"""..."""
    FOR_OPEN,             // `for ... do` body; declined when the body does not indent (query CE)
    CTOR_ATTR,            // zero-width: `[<...>]+ (` follows (attribute on a primary ctor)
    TRY_OPEN,             // try body; pushes S_TRY
    LABEL_ATTR,           // zero-width: `[<...>]+ ident:` follows (attribute on a labelled param)
    ELEMENT_DSL_OPEN,     // zero-width: `ident ( ... ) {` follows (element-DSL builder)
    AND_DOCS_OPEN,        // zero-width: `///` lines followed by `and`
    CASE_DOCS_OPEN,       // zero-width: `///` lines followed by `|`
    PAREN_FIELD_OPEN,     // `Foo(ident = ...)` body; pushes S_BRACKET
    CE_BRACE_OPEN,        // zero-width before the `{` of a CE body (not a record/object/copy-update)
    BLOCK_COMMENT,        // `(* ... *)`, nested
    BLOCK_DOC_COMMENT,    // `(** ... *)`
    THEN_OPEN,            // then/elif body; pushes S_EXPR with thn=1
    LAZY_OPEN,            // lazy body on the next line; pushes S_EXPR
    CTOR_TUPLE_GATE,      // zero-width: `ident ( ... ) ,` follows (`let Ctor(a, b), rest`)
    PREPROC_BREAK,        // zero-width: a `#if`-family line splits one declaration into two spellings
    DECL_SEMI,            // extra: a `;` directly before a declaration line
    MEMBERS_OPEN,         // zero-width: members indented under a same-line type body; pushes S_TYPEBODY
    LABEL_GATE,           // zero-width: `ident :` follows (not `::` `:>` `:?` `:=`)
    PAREN_BLOCK_OPEN,     // zero-width: `(` block; pushes S_EXPR with par=1, closed only by `)`
    INFIX_BLOCK_OPEN,     // zero-width: `&&`/`||` then a deeper `let`/`use` line; pushes S_EXPR with inf=1
    FIELD_BLOCK_OPEN,     // zero-width: record field `=` then a newline; pushes S_EXPR
} Sym;

// Sorts. All but S_MATCH and S_BRACKET close on dedent via LAYOUT_END.
//   S_LAYOUT   let/member body
//   S_TYPEBODY type body; also closes before a `with` augmentation at the body column
//   S_EXPR     expression body; also closes before an inline `else`/`elif`/`in`
//   S_MATCH    arm-list; closes below the arm column, never emits a separator
//   S_BRACKET  `[` / `[|` / `{` body; closed by the delimiter
//   S_DECL     module/source body; LAYOUT_SEMI is never emitted before a declaration keyword
//   S_TRY      try body; also closes before an inline `with`/`finally`
typedef enum { S_LAYOUT, S_MATCH, S_BRACKET, S_TYPEBODY, S_EXPR, S_DECL, S_TRY } Sort;

static inline bool layoutish(uint8_t sort) { return sort == S_LAYOUT || sort == S_TYPEBODY || sort == S_EXPR || sort == S_DECL || sort == S_TRY; }

typedef struct { uint16_t col; uint8_t sort; uint8_t inl:1, thn:1, par:1, inf:1, cases:1; } Ctx;  // inl: body opened on the opener's line; thn: then/elif body; par: `(` block, closed only by `)`; inf: `&&`/`||` right operand; cases: type body whose first line is a `|` case

#define MAXD 512

// Lives for the whole parse. Only `stk`/`n` are serialized; the claim columns are not.
typedef struct {
    Ctx stk[MAXD];
    uint16_t n;
    // Column of an `else`/`elif` that already closed a then-body; -1 when none.
    int32_t else_claim_col;
    // Column of an `in` that closed an arm-list and may still close the enclosing let value; -1 when none.
    int32_t in_claim_col;
    // Zeroed at the start of every scan.
    struct {
        // Inputs of the boundary-phase next_line_indent call: stop at a line-start block
        // comment; whether a doc gate is valid, and the layout top column it needs.
        bool region_stop;
        bool doc_gate_possible;
        uint32_t top_col_for_docs;
        // Outputs of next_line_indent: what it skipped before the next real line, the indent
        // of the first `///` line, and whether a stopped block comment is the `(**` form.
        bool skipped_doc_lines;
        bool skipped_line_comments;
        bool skipped_directive;
        bool skipped_alt_directive;
        uint32_t doc_indent;
        bool comment_doc;
        // Words consumed earlier in the same scan (try_and_docs, mid_word), reused by later checks.
        char post_doc_word[10];
        char midline_word[10];
    } scan;
} Scanner;

// --- Lexer idioms ---
static inline bool at_line_end(int32_t c) { return c == '\n' || c == '\r' || c == 0; }
static inline bool is_lower(int32_t c) { return c >= 'a' && c <= 'z'; }
static inline bool is_alpha(int32_t c) { return is_lower(c) || (c >= 'A' && c <= 'Z'); }
static inline bool is_digit(int32_t c) { return c >= '0' && c <= '9'; }
static inline bool is_ident_char(int32_t c) { return is_alpha(c) || is_digit(c) || c == '_' || c == '\''; }
static inline bool is_name_start(int32_t c) { return is_alpha(c) || c == '_' || c == '`'; }
static inline void skip_hspace(TSLexer *lexer) { while (lexer->lookahead == ' ' || lexer->lookahead == '\t') lexer->advance(lexer, true); }
static inline void skip_space(TSLexer *lexer) { while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\n' || lexer->lookahead == '\r') lexer->advance(lexer, true); }
static inline void skip_line(TSLexer *lexer) { while (!at_line_end(lexer->lookahead)) lexer->advance(lexer, true); }

static bool skip_bracket_attrs(TSLexer *lexer);
// A bare `in` (outside brackets and strings) on the rest of the line. Consumes lookahead.
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

// `[?]ident :` ahead, the `:` not starting `::` `:>` `:?` `:=`. Consumes lookahead.
static bool try_label_gate(TSLexer *lexer) {
    // The boundary infix probe may already have consumed a leading `?`.
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

// `[<...>]+ [?]ident:` ahead. Consumes lookahead, also on a miss.
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
    if (s->n < MAXD) { s->stk[s->n].sort = sort; s->stk[s->n].col = (uint16_t)col; s->stk[s->n].inl = 0; s->stk[s->n].thn = 0; s->stk[s->n].par = 0; s->stk[s->n].inf = 0; s->stk[s->n].cases = 0; s->n++; }
}

// Body of a block comment after its `(*`, through the matching `*)`. As in FSC, nested
// comments and string literals count: `(* "*)" *)` does not end at the quoted `*)`. false on EOF.
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

// Called just after `(*`, consumed with advance(false) so the token starts at the `(`.
// External because a token regex cannot nest. false on EOF or when neither symbol is valid.
static bool finish_block_comment(TSLexer *lexer, const bool *valid) {
    if (!valid[BLOCK_COMMENT] && !valid[BLOCK_DOC_COMMENT]) return false;
    if (lexer->lookahead == ')') return false;   // `(*)` = the multiply operator value, not a comment
    bool doc = false;
    if (lexer->lookahead == '*') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == ')') {             // `(**)` is an empty normal comment
            lexer->advance(lexer, false);
            lexer->mark_end(lexer);
            lexer->result_symbol = valid[BLOCK_COMMENT] ? BLOCK_COMMENT : BLOCK_DOC_COMMENT;
            return true;
        }
        doc = true;
    }
    if (!skip_comment_body(lexer, false)) return false;
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

// `first` value of next_line_indent for a line that holds only a block comment.
#define FIRST_COMMENT_LINE 2

static bool line_geometry(uint32_t *col, int32_t *first, uint32_t indent, int32_t ch) {
    if (first) *first = ch;
    *col = indent;
    return true;
}

typedef enum {
    DIRECTIVE_SKIPPED,    // `#if`-family, `#nowarn`/`#warnon`, `#line`, `# 14 "f.fs"`: trivia, consumed to the newline
    DIRECTIVE_STATEMENT,  // `#load`/`#r`/...: a statement; the lexer sits after the `#`
} DirectiveKind;

// `#if`-family, `#nowarn`/`#warnon` and `#line` lines are transparent to the offside rule.
// `#load`/`#r` are statements that rely on the dedent-close firing at their line.
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

// Indent and first significant char of the next non-blank, non-comment line; false at
// EOF. `//`, `(* *)` and `#if`-family lines are transparent to the offside rule.
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
            // Boundary call: the zero-width baseline moves to the first comment line, so
            // tokens emitted this scan start at the docs; the next scan resumes there (mid_doc_resume).
            if (s->scan.region_stop && s->scan.doc_gate_possible && indent >= s->scan.top_col_for_docs && !marked_line_start) { lexer->mark_end(lexer); marked_line_start = true; }
            lexer->advance(lexer, true);
            if (lexer->lookahead == '/') {
                lexer->advance(lexer, true);
                if (lexer->lookahead == '/') {
                    if (!s->scan.skipped_doc_lines) s->scan.doc_indent = indent;
                    s->scan.skipped_doc_lines = true;
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
                // Consumed with advance(false) so an emitted comment token starts at the `(`.
                lexer->advance(lexer, false);
                if (lexer->lookahead == ')') {         // `(*)` operator value
                    return line_geometry(col, first, indent, '(');
                }
                s->scan.comment_doc = (lexer->lookahead == '*');
                if (!skip_comment_body(lexer, false)) return false;
                skip_hspace(lexer);
                while (lexer->lookahead == '(') {
                    lexer->advance(lexer, false);
                    if (lexer->lookahead != '*') { return line_geometry(col, first, indent, '('); }
                    lexer->advance(lexer, false);
                    if (!skip_comment_body(lexer, false)) return false;
                    skip_hspace(lexer);
                }
                if (at_line_end(lexer->lookahead)) {
                    // mark_end only for a comment-only line: on a comment-led line the baseline
                    // must stay at the scan start or the next zero-width token would swallow the comment.
                    lexer->mark_end(lexer);
                    return line_geometry(col, first, indent, FIRST_COMMENT_LINE);
                }
                // Comment-led line (`(* 4 *) 7`): the column is the comment's start indent.
                return line_geometry(col, first, indent, lexer->lookahead);
            }
            lexer->advance(lexer, true);
            if (lexer->lookahead == ')') { return line_geometry(col, first, indent, '('); }
            if (!skip_comment_body(lexer, true)) return false;
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

// Column of a body's first token: on the opener's line, or the next real line's indent.
static uint32_t peek_body_col(Scanner *s, TSLexer *lexer) {
    uint32_t col = lexer->get_column(lexer);
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') { lexer->advance(lexer, true); col++; }
    // A trailing comment after the opener means the body starts on a later line.
    if (lexer->lookahead == '/') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '/') { uint32_t nl; return next_line_indent(s, lexer, &nl, NULL) ? nl : 0; }
        return col;
    }
    if (lexer->lookahead == '(') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '*') {
            // Content after a same-line block comment keeps the comment's start column.
            lexer->advance(lexer, true);
            if (lexer->lookahead == ')') return col;  // `(*)` operator value
            if (!skip_comment_body(lexer, true)) return 0;
            skip_hspace(lexer);
            if (!at_line_end(lexer->lookahead))
                return col;
            uint32_t nl; return next_line_indent(s, lexer, &nl, NULL) ? nl : 0;
        }
        return col;
    }
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        uint32_t nl;
        if (next_line_indent(s, lexer, &nl, NULL)) return nl;
        return 0;
    }
    return col;
}

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

// `|` + operator char is an infix operator (`|>`, `||`, `|?>`); a match-arm `|` is not.
static bool is_bar_op_tail(int32_t c) { return is_opchar(c) && c != ':'; }

// One identifier segment, plain or ``quoted``. The caller ensures a name start.
static void peek_name_segment(TSLexer *lexer) {
    if (lexer->lookahead == '`') {
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

// peek_name_segment that copies a plain segment into buf; a ``quoted`` segment yields "`".
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

static void read_word(TSLexer *lexer, char *w, size_t cap) {
    size_t n = 0; int32_t look = lexer->lookahead;
    while (n + 1 < cap && (is_ident_char(look))) {
        w[n++] = (char)look; lexer->advance(lexer, true); look = lexer->lookahead;
    }
    w[n] = '\0';
}

// A line led by one of these continues the current construct: no separator before it.
static bool semi_blocked_word(const char *w) {
    return !strcmp(w, "else") || !strcmp(w, "elif") || !strcmp(w, "then") ||
           !strcmp(w, "with") || !strcmp(w, "finally") || !strcmp(w, "in") || !strcmp(w, "and") ||
           !strcmp(w, "when") ||   // static-optimization `when` equations
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

// Text run of an interpolated string up to (not including) `{`, `}`, `%` or the closing
// quote; `mark_end` moves only over confirmed text. External so a leading `//` is not lexed as a comment.
typedef enum { TX_STRING, TX_VERBATIM, TX_TRIPLE } TextKind;

static bool scan_interp_text(TSLexer *lexer, TextKind kind) {
    bool consumed = false;
    for (;;) {
        int32_t c = lexer->lookahead;
        if (c == 0) break;
        if (c == '%') break;               // `%` starts a format specifier, lexed by the grammar
        if (c == '{' || c == '}') {        // `{{` / `}}` are text
            lexer->advance(lexer, false);
            if (lexer->lookahead == c) { lexer->advance(lexer, false); consumed = true; lexer->mark_end(lexer); continue; }
            break;
        }
        if (c == '"') {
            if (kind == TX_STRING) break;
            if (kind == TX_VERBATIM) {     // `""` is an escaped quote
                lexer->advance(lexer, false);
                if (lexer->lookahead == '"') { lexer->advance(lexer, false); consumed = true; lexer->mark_end(lexer); continue; }
                break;
            }
            // TX_TRIPLE: a lone `"` or `""` is text.
            lexer->advance(lexer, false);
            if (lexer->lookahead == '"') {
                lexer->advance(lexer, false);
                if (lexer->lookahead == '"') break;
                consumed = true; lexer->mark_end(lexer); continue;
            }
            consumed = true; lexer->mark_end(lexer); continue;
        }
        if (c == '\\' && kind == TX_STRING) {
            lexer->advance(lexer, false);
            if (lexer->lookahead != 0) lexer->advance(lexer, false);
            consumed = true; lexer->mark_end(lexer); continue;
        }
        lexer->advance(lexer, false);
        consumed = true; lexer->mark_end(lexer);
    }
    return consumed;
}

// In an S_DECL body a line led by one of these starts a new declaration `_token`;
// LAYOUT_SEMI is never emitted before it.
static bool decl_starter_word(const char *w) {
    return !strcmp(w, "let") || !strcmp(w, "use") || !strcmp(w, "do") ||
           !strcmp(w, "type") || !strcmp(w, "module") || !strcmp(w, "open") ||
           !strcmp(w, "exception") || !strcmp(w, "namespace") || !strcmp(w, "inline") ||
           !strcmp(w, "member") || !strcmp(w, "static") || !strcmp(w, "val") ||
           !strcmp(w, "abstract") || !strcmp(w, "inherit") || !strcmp(w, "override") ||
           !strcmp(w, "default") || !strcmp(w, "interface");
}

static bool decl_starter(TSLexer *lexer, int32_t first) {
    // An attribute row decorates the next declaration; a bare `[` is a list literal.
    if (first == '[') { lexer->advance(lexer, true); return lexer->lookahead == '<'; }
    // `#load` / `#r` / ... is its own `_token`; `#if`-family lines never reach here.
    if (first == '#') return true;
    if (first < 'a' || first > 'z') return false;
    char w[12]; read_word(lexer, w, sizeof w);
    return decl_starter_word(w);
}

// Body column for a layout open, plus: the body sits behind a `#else`/`#elif` and starts a
// declaration, i.e. the declaration is written once per branch and the body belongs to the last.
static bool split_branch_body(Scanner *s, TSLexer *lexer, uint32_t *body_col) {
    s->scan.skipped_alt_directive = false;              // peek_body_col does not reset it for an inline body
    *body_col = peek_body_col(s, lexer);
    return s->scan.skipped_alt_directive && decl_starter(lexer, lexer->lookahead);
}

// Consecutive `[<...>]` groups, leaving the lexer after them; false if not at `[<`.
// Strings inside may contain `>]`.
static bool skip_bracket_attrs(TSLexer *lexer) {
    if (lexer->lookahead != '[') return false;
    lexer->advance(lexer, true);
    if (lexer->lookahead != '<') return false;
    lexer->advance(lexer, true);
    for (;;) {
        for (;;) {
            int32_t c = lexer->lookahead;
            if (c == 0) return false;
            if (c == '"') {
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
        if (lexer->lookahead == '[') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == '<') { lexer->advance(lexer, true); continue; }
            return false;
        }
        break;
    }
    return true;
}

// `"..."` or `"""..."""` at the opening quote, through the closing quote(s).
static void edsl_skip_dquote(TSLexer *lexer) {
    lexer->advance(lexer, true);
    if (lexer->lookahead == '"') {
        lexer->advance(lexer, true);
        if (lexer->lookahead != '"') return;
        lexer->advance(lexer, true);
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

// Verbatim `@"..."` body at the opening quote; `""` is an escaped quote.
static void edsl_skip_verbatim(TSLexer *lexer) {
    lexer->advance(lexer, true);
    while (lexer->lookahead != 0) {
        if (lexer->lookahead == '"') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == '"') { lexer->advance(lexer, true); continue; }
            break;
        }
        lexer->advance(lexer, true);
    }
}

// Balanced `( ... )` group at the opening `(`; may span lines. Strings and comments are
// skipped so their delimiters do not count. false on EOF or runaway.
static bool edsl_skip_balanced_parens(TSLexer *lexer) {
    int depth = 0, guard = 0;
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
        if (c == '/') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == '/') { while (lexer->lookahead != '\n' && lexer->lookahead != 0) lexer->advance(lexer, true); }
            continue;
        }
        if (c == '(') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == '*') {
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

// Rest of a `{| ... |}` group; the caller has already consumed the opening `{`.
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

// Tail of element_dsl_ahead after the first name segment: `(.seg)* arg (.m( ... ))* {`.
static bool element_dsl_parens_brace(TSLexer *lexer) {
    while (lexer->lookahead == '.') {
        lexer->advance(lexer, true);
        if (!edsl_skip_name(lexer)) return false;
    }
    skip_hspace(lexer);
    // The builder argument is a paren group, a string literal, or a `{| ... |}` anonymous record.
    if (lexer->lookahead == '(') {
        if (!edsl_skip_balanced_parens(lexer)) return false;
    } else if (lexer->lookahead == '"') {
        edsl_skip_dquote(lexer);
    } else if (lexer->lookahead == '@') {
        lexer->advance(lexer, true);
        if (lexer->lookahead != '"') return false;
        edsl_skip_verbatim(lexer);
    } else if (lexer->lookahead == '{') {
        lexer->advance(lexer, true);
        if (lexer->lookahead != '|') return false;
        if (!edsl_skip_braces_after_open(lexer)) return false;
    } else {
        return false;
    }
    // The body `{` must be on the same line as the last argument or chain link;
    // chain links may sit on their own lines.
    for (;;) {
        skip_hspace(lexer);
        if (lexer->lookahead == '{') {
            // Only a CE body counts: `f "msg" { x with ... }` stays an application.
            lexer->advance(lexer, true);
            if (lexer->lookahead == '|') return false;
            skip_space(lexer);
            return ce_brace_content_is_ce_body(lexer);
        }
        skip_space(lexer);
        if (lexer->lookahead != '.') return false;
        lexer->advance(lexer, true);
        if (!edsl_skip_name(lexer)) return false;
        skip_hspace(lexer);
        if (lexer->lookahead != '(') return false;
        if (!edsl_skip_balanced_parens(lexer)) return false;
    }
}

// `div() {` / `div(attrs).m("x") {` ahead: an element-DSL builder head, which the LR
// table cannot see past the arguments. Destructive; the caller emits zero-width or returns false.
static bool element_dsl_ahead(TSLexer *lexer) {
    if (!is_name_start(lexer->lookahead)) return false;
    for (;;) {
        int32_t ch = lexer->lookahead;
        if (is_ident_char(ch)) { lexer->advance(lexer, true); continue; }
        break;
    }
    return element_dsl_parens_brace(lexer);
}

// Destructive: call only right before a `return false` or a zero-width emit.
static inline bool try_element_dsl(TSLexer *lexer, const bool *valid) {
    if (valid[ELEMENT_DSL_OPEN] && element_dsl_ahead(lexer)) { lexer->result_symbol = ELEMENT_DSL_OPEN; return true; }
    return false;
}

// Content after a `{` (lexer at the first non-space char): a CE body (true) or a record /
// object expression / copy-update (false). Destructive; call only before a zero-width emit or a miss.
static bool ce_brace_content_is_ce_body(TSLexer *lexer) {
    // Leading comments are trivia: `{ // note`\n`A = 1 }` is a record.
    for (;;) {
        skip_space(lexer);
        if (lexer->lookahead == '/') {
            lexer->advance(lexer, true);
            if (lexer->lookahead != '/') return true;
            skip_line(lexer);
            continue;
        }
        if (lexer->lookahead == '(') {
            lexer->advance(lexer, true);
            if (lexer->lookahead != '*') {
                // `{ (expr) with F = ... }`: copy-update over a parenthesised base.
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
            if (lexer->lookahead == ')') return true;      // `(*)` operator value
            if (!skip_comment_body(lexer, true)) return true;
            continue;
        }
        break;
    }
    int32_t c = lexer->lookahead;
    if (c == '}') return true;
    if (c == '!') {                            // `{ !cell with ... }`: dereferenced copy-update base
        lexer->advance(lexer, true);
        c = lexer->lookahead;
    }
    if (!is_name_start(c)) return true;
    char w0[12] = {0};
    peek_name_capture(lexer, w0, sizeof(w0));
    if (!strcmp(w0, "new")) return false;      // object expression
    if (!strcmp(w0, "inherit")) return false;  // object construction `{ inherit T(...) ... }`
    // Reserved words, so never a record field name; `let!`/`yield!`/... share the base word.
    static const char *kw[] = {"let","use","do","return","yield","if","for","while",
                               "match","try","fun","function","lazy","assert", NULL};
    for (int i = 0; kw[i]; i++) if (!strcmp(w0, kw[i])) return true;
    // `=`/`:` means a field, a later `with` a copy-update, anything else a CE expression.
    for (int guard = 0; guard < 64; guard++) {
        skip_hspace(lexer);
        int32_t d = lexer->lookahead;
        if (d == '=') {                             // `==?`-style operators are CE
            lexer->advance(lexer, true);
            return is_opchar(lexer->lookahead);
        }
        if (d == ':') {
            lexer->advance(lexer, true);
            return lexer->lookahead == ':' || lexer->lookahead == '=';   // `::` cons and `:=` assignment are CE
        }
        if (d == '.') { lexer->advance(lexer, true); continue; }
        // Application argument before a possible `with` (`{ f [] x with A = 1 }`).
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
            if (!strcmp(w, "with")) return false;
            continue;
        }
        if (d == '\n' || d == '\r') {             // `{ base`\n`  with ...`
            skip_space(lexer);
            if (is_name_start(lexer->lookahead)) {
                char w[8] = {0};
                peek_name_capture(lexer, w, sizeof(w));
                if (!strcmp(w, "with")) return false;
            }
            return true;
        }
        return true;
    }
    return true;
}

// After skipped `///` lines: the word after the docs decides whether they attach to a `|`
// case, an `and` clause, or the next declaration. Destructive; call last.
static bool try_and_docs(Scanner *s, TSLexer *lexer, const bool *valid,
                         int32_t first, uint32_t col, Ctx *top) {
    s->scan.post_doc_word[0] = '\0';
    if (!s->scan.skipped_doc_lines) return false;
    if (valid[CASE_DOCS_OPEN] && first == '|') {
        lexer->result_symbol = CASE_DOCS_OPEN; return true;
    }

    if (is_lower(first)) {
        char w[10]; size_t n = 0; int32_t lk = lexer->lookahead;
        while (n < 9 && (is_ident_char(lk))) {
            w[n++] = (char)lk; lexer->advance(lexer, true); lk = lexer->lookahead;
        }
        w[n] = '\0';
        { size_t i = 0; for (; w[i] && i < 9; i++) s->scan.post_doc_word[i] = w[i]; s->scan.post_doc_word[i] = '\0'; }
        if (valid[AND_DOCS_OPEN] && !strcmp(w, "and")) {
            lexer->result_symbol = AND_DOCS_OPEN; return true;
        }
        // In a type body at the body column, docs before a module-level keyword decorate the
        // next declaration: the body closes before the docs. Not `let`: class bodies contain let bindings.
        if (top && top->sort == S_TYPEBODY && valid[LAYOUT_END] && col <= top->col &&
            (!strcmp(w, "type") || !strcmp(w, "open") ||
             !strcmp(w, "module") || !strcmp(w, "namespace") || !strcmp(w, "exception"))) {
            s->n--; lexer->result_symbol = LAYOUT_END; return true;
        }
        return false;
    }
    // Docs before `[<` may decorate a member or the next declaration: peek past the attributes.
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

// A `{` starting the next line under a builder (`seq`\n`    {`). Destructive; call last.
static bool try_ce_brace(TSLexer *lexer, const bool *valid, int32_t first) {
    if (first != '{' || !valid[CE_BRACE_OPEN]) return false;
    lexer->advance(lexer, true);
    if (lexer->lookahead == '|') return false;
    skip_space(lexer);
    if (ce_brace_content_is_ce_body(lexer)) { lexer->result_symbol = CE_BRACE_OPEN; return true; }
    return false;
}

// --- Scan phases ---
// Phases run at the same position, in the order `scanner_scan` calls them. PASS hands the
// position to the next phase; EMITTED and DECLINED end the scan. A phase that has advanced
// past the position must not PASS.
typedef enum { PASS, EMITTED, DECLINED } Step;

static inline Step emit(TSLexer *lexer, Sym sym) { lexer->result_symbol = sym; return EMITTED; }
static inline Step close_top(Scanner *s, TSLexer *lexer, Sym sym) { s->n--; return emit(lexer, sym); }

static Step scan_ctor_attr(TSLexer *lexer, const bool *valid) {
    // Attribute rows, `///` lines and `//` comments may precede a primary ctor's `(`; a
    // standalone attribute row is followed by `type`. A plain `type T (x) =` takes the ungated path.
    if (valid[CTOR_ATTR]) {
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
        if (lexer->lookahead == '[') {
            if (!skip_bracket_attrs(lexer)) return DECLINED;
            seen_row = true;
            continue;
        }
        break;
    }
    if (!seen_row) return DECLINED;
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
    // When RECORD_OPEN is also valid the position is right after a `{`, and
    // scan_record_open owns that decision.
    if (valid[LAYOUT_OPEN] && !valid[RECORD_OPEN]) {
    uint32_t bc;
    skip_hspace(lexer);
    bool inl = lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
               lexer->lookahead != '/'  && lexer->lookahead != 0;
    // A declaration written once per `#if` branch ends at its `=`; the grammar takes the body-less path.
    if (split_branch_body(s, lexer, &bc)) {
        if (!valid[PREPROC_BREAK]) return DECLINED;
        return emit(lexer, PREPROC_BREAK);
    }
    // In a CE body both the statement `let` and `let ... in` apply; an inline value followed by `in` is the latter.
    if (inl && valid[EXPR_OPEN] && top && top->sort == S_BRACKET && line_has_in_keyword(lexer)) {
        push(s, S_EXPR, bc); s->stk[s->n - 1].inl = 1;
        return emit(lexer, EXPR_OPEN);
    }
    push(s, S_LAYOUT, bc);
    if (inl && s->n) s->stk[s->n - 1].inl = 1;
    return emit(lexer, LAYOUT_OPEN);
    }
    // A `for ... do` body always indents past the `for`; a query-CE `for x in xs do`\n`where ...`
    // has no body and its operators are CE siblings at the CE column.
    if (valid[FOR_OPEN]) {
    uint32_t bc = peek_body_col(s, lexer);
    if (top && bc <= top->col) {
        // Emit the enclosing separator so `where`/`select`/... is a sibling `query_operator`.
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
        lexer->lookahead != '/'  && lexer->lookahead != 0) return DECLINED;   // inline body: the grammar's plain alternative
    push(s, S_EXPR, peek_body_col(s, lexer));
    return emit(lexer, LAZY_OPEN);
    }
    if (valid[TRY_OPEN])    { push(s, S_TRY,    peek_body_col(s, lexer)); return emit(lexer, TRY_OPEN); }
    if (valid[ELSE_OPEN]) {
    // A same-line `else if` is a flat elif clause: no body opens. `else`\n`if` on a new line is a
    // real body (more statements may follow), except at the enclosing body's column (flat chain).
    skip_hspace(lexer);
    bool nl_before = (lexer->lookahead == '\n' || lexer->lookahead == '\r' || lexer->lookahead == '/');
    uint32_t col = peek_body_col(s, lexer);
    bool flat_col = nl_before && top && col <= top->col;
    if ((!nl_before || flat_col) && lexer->lookahead == 'i') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == 'f') {
            lexer->advance(lexer, true);
            int32_t a = lexer->lookahead;
            bool word = is_ident_char(a);
            if (!word) return DECLINED;
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
    // Runs after the peeking opens (the probe advances over the digits of an inline body) and
    // before the newline-gated opens, which decline on inline content such as `[|1.|]`.
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
    // A module/type body is a layout only when it starts on the next line; an inline body
    // (`type X = int`, `module L = Lib`) matches the grammar's inline alternative.
    if (valid[BLOCK_OPEN]) {
    skip_hspace(lexer);
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        uint32_t col;
        if (next_line_indent(s, lexer, &col, NULL)) { push(s, S_DECL, col); return emit(lexer, BLOCK_OPEN); }
    }
    return DECLINED;
    }
    if (valid[TYPE_OPEN]) {
    skip_hspace(lexer);
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        uint32_t col; int32_t bfirst = 0;
        if (next_line_indent(s, lexer, &col, &bfirst)) {
            push(s, S_TYPEBODY, col);
            s->stk[s->n - 1].cases = (bfirst == '|');
            return emit(lexer, TYPE_OPEN);
        }
    }
    return DECLINED;
    }
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
    if (valid[INFIX_BLOCK_OPEN]) {
    skip_hspace(lexer);
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        uint32_t col; int32_t bfirst = 0;
        // Only a `let`/`use`-led operand needs the block; anything else continues the operator line.
        if (next_line_indent(s, lexer, &col, &bfirst) && top && col > top->col && (bfirst == 'l' || bfirst == 'u')) {
            char w[12]; read_word(lexer, w, sizeof w);
            if (!strcmp(w, "let") || !strcmp(w, "use")) {
                push(s, S_EXPR, col); s->stk[s->n - 1].inf = 1; return emit(lexer, INFIX_BLOCK_OPEN);
            }
        }
    }
    return DECLINED;
    }
    // Declined for an empty `(`\n`)`, which is the `unit` token.
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
    // Inline content anchors the body at its own column: an aligned line is a new statement.
    // Operator-led (`(+)`, `(-x)`, `(<@ e @>)`) and comment-led content keep the plain form.
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
        // An empty bracket has no element to anchor a context.
        uint32_t col; int32_t bfirst = 0;
        if (next_line_indent(s, lexer, &col, &bfirst)) {
            if (bfirst == ']' || bfirst == '}' || bfirst == '|') return DECLINED;
            push(s, S_BRACKET, col); return emit(lexer, BRACKET_OPEN);
        }
        return DECLINED;
    }
    // Captured before the element-DSL probe, which is destructive on a miss.
    uint32_t inline_col = lexer->get_column(lexer);
    int32_t inline_first = lexer->lookahead;
    // A trailing line comment is not an element: the content starts on a later line.
    if (inline_first == '/') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '/') {
            uint32_t col; int32_t bfirst = 0;
            if (!next_line_indent(s, lexer, &col, &bfirst)) return DECLINED;
            if (bfirst == ']' || bfirst == '}' || bfirst == '|') return DECLINED;
            push(s, S_BRACKET, col); return emit(lexer, BRACKET_OPEN);
        }
    }
    // A same-line element-DSL builder (`div() { span() {...} }`): the mid-line phase is unreachable once this returns.
    if (try_element_dsl(lexer, valid)) return EMITTED;
    // Inline first element (`seq { x`\n`  y }`): the context sits at its column so aligned
    // lines get a separator.
    if (inline_first == ']' || inline_first == '}' || inline_first == '|' || inline_first == 0) return DECLINED;
    // A leading block comment is not an element: `[(* none *)]` is empty; content after it
    // anchors at the comment's column.
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
    // Peek `ident(.seg)* =`; the `=` distinguishes a named field from a tuple argument `Foo(a, b)`.
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
        while (lexer->lookahead == '.') {
            lexer->advance(lexer, true);
            if (!is_name_start(lexer->lookahead)) break;
            peek_name_segment(lexer);
            skip_hspace(lexer);
        }
        if (lexer->lookahead == '=') {
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
    // Field shape: `ident =` (record_field) or `ident :` (record_type_field). `{ new ... }` and
    // `{ base with ... }` fall through to the grammar's other `{` branches.
    if (valid[RECORD_OPEN]) {
    uint32_t col = lexer->get_column(lexer);
    bool nl = false;
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') { lexer->advance(lexer, true); col++; }
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
        nl = true;
        if (!next_line_indent(s, lexer, &col, NULL)) return DECLINED;
    }
    // Leading `///` doc lines: `col` becomes the field's column, not the doc's.
    while (lexer->lookahead == '/' ) {
        lexer->advance(lexer, true);
        if (lexer->lookahead != '/') return DECLINED;
        lexer->advance(lexer, true);
        if (lexer->lookahead != '/') return DECLINED;
        skip_line(lexer);
        uint32_t c2; if (!next_line_indent(s, lexer, &c2, NULL)) return DECLINED;
        col = c2;
        nl = true;
    }
    int32_t c = lexer->lookahead;
    bool ok = false;
    char w0[8] = {0};
    // Leading `[<...>]` attributes: `col` stays at the `[`, where every field aligns.
    if (c == '[') {
        if (!skip_bracket_attrs(lexer)) return DECLINED;
        c = lexer->lookahead;
    }
    if (is_name_start(c)) {
        peek_name_capture(lexer, w0, sizeof(w0));
        // `{ inherit Base(...) ... }` is object construction; the base call has no `=`/`:`.
        if (!strcmp(w0, "inherit")) { push(s, S_BRACKET, col); return emit(lexer, RECORD_OPEN); }
        skip_hspace(lexer);
        // A field modifier (`mutable foo: ...`) puts a second word before the `:`.
        if (is_name_start(lexer->lookahead)) {
            peek_name_segment(lexer);
            skip_hspace(lexer);
        }
        while (lexer->lookahead == '.') {
            lexer->advance(lexer, true);
            if (!is_name_start(lexer->lookahead)) break;
            peek_name_segment(lexer);
            skip_hspace(lexer);
        }
        // `ti (* comment *) : int`.
        while (lexer->lookahead == '(') {
            lexer->advance(lexer, true);
            if (lexer->lookahead != '*') break;
            lexer->advance(lexer, true);
            if (lexer->lookahead == ')') break;      // `(*)` operator value
            if (!skip_comment_body(lexer, true)) return DECLINED;
            skip_hspace(lexer);
        }
        int32_t sep = lexer->lookahead;
        if (sep == '=' || sep == ':') ok = true;
    }
    if (ok) { push(s, S_BRACKET, col); return emit(lexer, RECORD_OPEN); }
    // A copy-update base on its own line (`{`\n`base with`) opens a layout at its column.
    // `new` is a literal token with no layout open, on the same line or its own.
    if (nl && valid[LAYOUT_OPEN] && strcmp(w0, "new") != 0) { push(s, S_LAYOUT, col); return emit(lexer, LAYOUT_OPEN); }
    return DECLINED;
    }
    return PASS;
}

static Step mid_closers(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top, int32_t c) {
    if (c == '[' && valid[LABEL_ATTR] && try_label_attr(lexer)) return EMITTED;
    // An infix right-operand block ends before a same-line `->`.
    if (c == '-' && top && top->sort == S_EXPR && top->inf && valid[LAYOUT_END]) {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '>') { return close_top(s, lexer, LAYOUT_END); }
        return DECLINED;
    }
    bool closer = (c == ')' || c == ']' || c == '}');
    if (!closer && c == '@') {            // `@>` / `@@>` quotation close
        lexer->advance(lexer, true);
        if (lexer->lookahead == '>') closer = true;
        else if (lexer->lookahead == '@') { lexer->advance(lexer, true); if (lexer->lookahead == '>') closer = true; }
    }
    if (!closer && c == '|') {            // `|]` / `|}`
        lexer->advance(lexer, true);
        int32_t c1 = lexer->lookahead;
        if (is_close_bracket(c1)) closer = true;
        else if (!is_bar_op_tail(c1) &&
                 top && layoutish(top->sort) && !top->par && valid[LAYOUT_END] && has_match_ctx(s)) {
            // A same-line `|` is the next arm: the inline arm body closes. Gated on an S_MATCH on
            // the stack so a union case separator `A | B` does not close the enclosing body.
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
    // The leading word is read once; every probe consumes it. LABEL_GATE first, then the continuation
    // closes (valid[LAYOUT_END] is false while a body is incomplete), then ELEMENT_DSL_OPEN, co-valid at `with`/`in`.
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
        // get_column is O(line length): compute the column only where a close records it.
        #define MID_COL() ((int32_t)lexer->get_column(lexer) - (int32_t)n)
        memcpy(s->scan.midline_word, w, n + 1);   // mid_ctor_tuple_gate resumes past this word; no strcpy in the Wasm libc subset
        if (top && valid[LAYOUT_END]) {
            // `else`/`elif` close only an inline body: after an indented then-body a same-line
            // `else` belongs to an inner `if` on that line (dangling else).
            bool else_kw = !strcmp(w, "else") || !strcmp(w, "elif");
            // `new(x) as this = { A = x } then this.B <- 1`: the inline ctor body ends before `then`.
            if (!strcmp(w, "then") && top->sort == S_LAYOUT && top->inl && valid[LAYOUT_END]) {
                return close_top(s, lexer, LAYOUT_END);
            }
            if (top->sort == S_EXPR && top->inf && (!strcmp(w, "then") || !strcmp(w, "do") || !strcmp(w, "with"))) {
                return close_top(s, lexer, LAYOUT_END);
            }
            // An `else` also ends an inline lambda body inside a then-branch (`if c then f >>= fun () -> g else h`);
            // the claim column stops the close at the enclosing bodies once the owner has closed.
            bool thn_below = false;
            for (int i = (int)s->n - 2; i >= 0 && !thn_below; i--) thn_below = s->stk[i].thn != 0;
            if (top->sort == S_EXPR && (else_kw ? ((top->thn != 0 || (top->inl && thn_below)) && s->else_claim_col != MID_COL())
                                        : (!strcmp(w, "in") || !strcmp(w, "end")))) {
                if (else_kw && top->thn) s->else_claim_col = MID_COL();
                return close_top(s, lexer, LAYOUT_END);
            }
            // `if c then match v with null -> "a" | x -> x.ToString() else "b"`: the inline arm
            // body, then its arm-list, close before the `else`.
            if (else_kw && thn_below && top->inl && top->sort == S_LAYOUT &&
                s->n >= 2 && s->stk[s->n - 2].sort == S_MATCH && s->else_claim_col != MID_COL()) {
                return close_top(s, lexer, LAYOUT_END);
            }
            // `with` after an inline lambda body (`Seq.tryFind ^ fun x -> p x with`); a `match ... with`
            // or `{ r with` inside the body is incomplete at its `with`, so LAYOUT_END is not valid there.
            if (!strcmp(w, "with") && top->sort == S_EXPR && top->inl) {
                return close_top(s, lexer, LAYOUT_END);
            }
            // `in` after an inline arm body (`match t with | A -> 1 | B -> 2 in f 1`); only with an S_MATCH
            // directly below, since a CE `let! a = e in` value is also an inline S_LAYOUT owning its `in`.
            if (!strcmp(w, "in") && top->sort == S_LAYOUT && top->inl &&
                s->n >= 2 && s->stk[s->n - 2].sort == S_MATCH) {
                return close_top(s, lexer, LAYOUT_END);
            }
            // `let x =`\n`    match ... with`\n`    | _ -> v in`: the `in` ends the arm-list, then the let value.
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
            // `end` ends any inline layout body (`struct val A: int; new(a) = { A = a }; end`).
            if (!strcmp(w, "end") && layoutish(top->sort)) {
                return close_top(s, lexer, LAYOUT_END);
            }
            // A mid-line `and` after an inline body starts the next binding/accessor (`let a = 1 and b = 2`),
            // unless a type variable or `(` follows: a constraint chain (`when ^t: null and ^t: struct`).
            if (!strcmp(w, "and") && layoutish(top->sort) && top->inl) {
                skip_hspace(lexer);
                int32_t a = lexer->lookahead;
                if (a != '\'' && a != '^' && a != '(') {
                    return close_top(s, lexer, LAYOUT_END);
                }
                return DECLINED;
            }
            // With an S_TRY open below, every inner inline body closes before `finally`/`with`, one per
            // scan (`try for e in c do yield e finally ...`); a `match x with` inside is incomplete at its `with`.
            if ((!strcmp(w, "finally") || !strcmp(w, "with")) && layoutish(top->sort)) {
                for (size_t i = 0; i + 1 < s->n; i++) {
                    if (s->stk[i].sort == S_TRY) {
                        return close_top(s, lexer, LAYOUT_END);
                    }
                }
            }
        }
        // The arm-list closes at an `else`/`elif` owned by a then-body below, and at `in`/`end`.
        if (top && top->sort == S_MATCH && valid[MATCH_END] && (!strcmp(w, "else") || !strcmp(w, "elif")) &&
            s->else_claim_col != MID_COL()) {
            bool thn_below = false;
            for (int i = (int)s->n - 2; i >= 0 && !thn_below; i--) thn_below = s->stk[i].thn != 0;
            if (thn_below) return close_top(s, lexer, MATCH_END);
        }
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
    // Emitted only for a CE body, so `head { new ... }` / `head { f = 1 }` parse as
    // application(head, object/record). The grammar lexes the literal `{` afterwards either way.
    if (c == '{' && valid[CE_BRACE_OPEN]) {
        lexer->advance(lexer, true);
        if (lexer->lookahead != '|') {
            skip_space(lexer);
            if (ce_brace_content_is_ce_body(lexer)) { return emit(lexer, CE_BRACE_OPEN); }
        }
    }
    return PASS;
}

static Step mid_semicolon(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top, int32_t c) {
    // A trailing `;` before a dedent/EOF close is consumed into the LAYOUT_END: the grammar has no
    // slot for it (the `;` shift commits to the separator reading). An equal-column next line is a sibling statement.
    if (c == ';' && valid[LAYOUT_END] && top && layoutish(top->sort)) {
        lexer->advance(lexer, false);
        if (lexer->lookahead != ';') {          // `;;` is left to the extras
            lexer->mark_end(lexer);
            skip_hspace(lexer);
            // A same-line closer right after the `;` ends the inline body (`f (fun () -> g (); )`).
            {
                int32_t a = lexer->lookahead;
                bool closer = (a == ')' || a == ']' || a == '}');
                if (!closer && a == '|') {
                    lexer->advance(lexer, true);
                    closer = (lexer->lookahead == ']' || lexer->lookahead == '}');
                }
                if (closer) { return close_top(s, lexer, LAYOUT_END); }
                if (a == '|') return DECLINED;     // lookahead consumed
            }
            if (at_line_end(lexer->lookahead)) {
                uint32_t ncol; int32_t nfirst = 0;
                if (!next_line_indent(s, lexer, &ncol, &nfirst) || ncol < top->col) {
                    return close_top(s, lexer, LAYOUT_END);
                }
            }
        }
        return DECLINED;
    }
    // A `;` before a declaration line terminates the statement; it is an extra because
    // LR(1) cannot peek past the `;` to the next line.
    if (c == ';' && valid[DECL_SEMI] && top && top->sort == S_DECL) {
        lexer->advance(lexer, false);
        if (lexer->lookahead != ';') {          // `;;` is left to fsi_terminator
            lexer->mark_end(lexer);
            skip_hspace(lexer);
            if (lexer->lookahead == 0) { return emit(lexer, DECL_SEMI); }
            if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
                uint32_t ncol; int32_t nfirst = 0;
                if (!next_line_indent(s, lexer, &ncol, &nfirst) || decl_starter(lexer, nfirst)) {
                    return emit(lexer, DECL_SEMI);
                }
            }
        }
        return DECLINED;
    }
    // A trailing `;` before the closing delimiter of a bracket/CE body (`seq { yield x; }`) is
    // consumed into the BRACKET_CLOSE, for the same reason.
    if (c == ';' && valid[BRACKET_CLOSE] && top && top->sort == S_BRACKET) {
        lexer->advance(lexer, false);
        if (lexer->lookahead != ';') {          // `;;` is left to the extras
            lexer->mark_end(lexer);
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
        return DECLINED;
    }
    return PASS;
}

static Step mid_doc_resume(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top, int32_t c) {
    // The previous zero-width token was anchored at this `///` line: skip the doc block and run
    // the boundary docs dispatch. On a miss the grammar lexes the doc itself.
    if (c == '/' && valid[CASE_DOCS_OPEN] + valid[AND_DOCS_OPEN] + valid[LAYOUT_END] + valid[LAYOUT_SEMI] > 0) {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '/') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == '/') {
                s->scan.skipped_doc_lines = true;
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
    // `ident(.ident)* ( ... ) ,` at a let-binding name is a tuple deconstruction (`let Ctor(a, b), rest`);
    // function definitions never have `,` after the params. mid_word may have consumed the first word.
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
        const char *first_word = w0n ? w0 : s->scan.midline_word;
        skip_hspace(lexer);
        // `let AesKey key, AesIV iv = ...`; not after a binding modifier (`let mutable a, b = ...`).
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
        return DECLINED;
    }
    return PASS;
}

static Step mid_block_comment(TSLexer *lexer, const bool *valid, int32_t c) {
    // Same-line nested block comment (`1 (* a (* b *) *)`); nothing else fires for `(`.
    if (c == '(') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == '*') { lexer->advance(lexer, false); return finish_block_comment(lexer, valid) ? EMITTED : DECLINED; }
        return DECLINED;
    }
    return PASS;
}

// --- Mid-line ---
// An inline body / arm-list / bracket can close on the same line before `)` `]` `}` `|]` `|}`;
// one close per scan (arm body, then arm-list, then the `)`).
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
    return DECLINED;
}

static Step boundary_infix_continuation(TSLexer *lexer, Ctx *top, uint32_t col, int32_t first, int32_t bar_c1) {
    // A leading infix operator continues the previous line unless it dedents below an S_EXPR body or
    // sits at/below a match arm column. FSC grants an infix token an offside grace of its length + 1.
    bool expr_strict = (top->sort == S_EXPR && col < top->col);
    bool infix_continues = 
                       // `<=`: an operator at the arm column cannot be an arm, so the arm-list ends.
                       !(top->sort == S_MATCH && col <= top->col) &&
                       !(top->sort == S_LAYOUT && col + 4 < top->col);

    // `|` alone is a match arm; `&` and `:` count only doubled; unary-capable `!` `~` are excluded.
    if (infix_continues) {
    int32_t c0 = first;
    if (c0 == '|' || c0 == '<' || c0 == '>' || c0 == '=' ||
        c0 == '*' || c0 == '/' || c0 == '%' || c0 == '^' || c0 == '&' || c0 == ':' || c0 == '?') {
        int32_t c1;
        if (c0 == '|') c1 = bar_c1;
        else { lexer->advance(lexer, true); c1 = lexer->lookahead; }
        int oplen = 1;
        if (is_opchar(c1)) {
            oplen = 2; lexer->advance(lexer, true);
            while (is_opchar(lexer->lookahead)) { oplen++; lexer->advance(lexer, true); }
        }
        bool infix = false;
        if (c0 == '|')      infix = is_bar_op_tail(c1);
        else if (c0 == '&') infix = (c1 == '&');
        // `::` `:>` `:?`, and a `: T` ascription on its own line: no statement starts with `:`.
        else if (c0 == ':') infix = true;
        // `?=>`-style operators; `?ident` is an optional named argument, i.e. a new element.
        else if (c0 == '?') infix = is_opchar(c1);
        else if (c0 == '/') infix = (c1 != '/');                       // `//` is a comment
        else                infix = true;
        if (infix && top->sort == S_EXPR && !top->par && col + oplen + 1 < top->col) infix = false;
        if (infix) return DECLINED;
    }

    // Leading `+`/`-`/`@` continue only in a layout body (brackets and arm-lists keep the newline
    // as a separator). Not `->`, `@"..."`, `@>`/`@@>`; `@@` followed by anything else is an operator.
    if (!expr_strict && layoutish(top->sort) && (first == '+' || first == '-' || first == '@')) {
        lexer->advance(lexer, true);
        int32_t c1 = lexer->lookahead;
        if (first == '+') return DECLINED;
        if (first == '-') {
            if (c1 != '>') return DECLINED;
            // `->=`-style custom operators continue; a bare `->` is an arrow.
            lexer->advance(lexer, true);
            if (is_opchar(lexer->lookahead)) return DECLINED;
        }
        if (first == '@') {
            if (c1 != '"' && c1 != '>' && c1 != '@') return DECLINED;
            // `@>`/`@@>` at the body column belongs to the open quotation (`<@`\n`    body`\n`@>`):
            // no close; a dedented closer falls through so the layout closes first.
            if (c1 == '>' && col == top->col) return DECLINED;
            if (c1 == '@') {
                lexer->advance(lexer, true);
                if (lexer->lookahead != '>') return DECLINED;
                if (col == top->col) return DECLINED;
            }
        }
    }

    // No F# statement starts with `.`: a member chain, a `.>>.` operator or a `..` range.
    if (!expr_strict && layoutish(top->sort) && first == '.') return DECLINED;
    }
    return PASS;
}

static Step boundary_bracket(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top, uint32_t col, int32_t first, bool bar_arm, int32_t bar_c1) {
    if (valid[BRACKET_CLOSE] && (is_close_bracket(first) || first == '|')) { return close_top(s, lexer, BRACKET_CLOSE); }
    // No separator before `else`/`elif`/... (`if c then return a`\n`else ...` inside a CE).
    if (valid[BRACKET_SEMI] && col == top->col && !semi_blocked(lexer, first)) { return emit(lexer, BRACKET_SEMI); }
    // A deeper line led by a statement keyword is still a new element (`[ yield a`\n`    for x in xs do ...`).
    if (valid[BRACKET_SEMI] && col > top->col && is_lower(first)) {
        char w[12]; read_word(lexer, w, sizeof w);
        if (!strcmp(w, "yield") || !strcmp(w, "for") || !strcmp(w, "let") || !strcmp(w, "use") ||
            !strcmp(w, "match") || !strcmp(w, "while") || !strcmp(w, "return") || !strcmp(w, "try") ||
            !strcmp(w, "if") || !strcmp(w, "do")) { return emit(lexer, BRACKET_SEMI); }
        return DECLINED;
    }
    // An element-DSL first element (`div() {`\n`  span() {...}`): the mid-line probe is unreachable here.
    if (try_element_dsl(lexer, valid)) return EMITTED;
    // Own-line `1.` element: the mid-line float probe is unreachable here.
    if (valid[FLOAT_TRAILING_DOT] && is_digit(first) && scan_trailing_dot_float(lexer)) return EMITTED;
    return DECLINED;

}

static Step boundary_match(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top, uint32_t col, int32_t first, bool bar_arm, int32_t bar_c1) {
    // `|]` / `|}` on its own line is the bracket closer, never an arm.
    if (valid[MATCH_END] && first == '|' && (bar_c1 == ']' || bar_c1 == '}')) { return close_top(s, lexer, MATCH_END); }
    // A `|` exactly two columns left of the arm column is a continuation arm whose pattern
    // aligns with an inline first arm's pattern (`function Cons (_, i) -> a`\n`         | Nil -> b`).
    if (valid[MATCH_END] && (col < top->col || (col == top->col && !bar_arm)) &&
        !(bar_arm && col + 2 == top->col)) { return close_top(s, lexer, MATCH_END); }
    return DECLINED;

}

static Step boundary_layout(Scanner *s, TSLexer *lexer, const bool *valid, Ctx *top, uint32_t col, int32_t first, bool bar_arm, int32_t bar_c1) {
    // A member keyword (or `[<`) indented past the context after a same-line type body opens
    // an S_TYPEBODY; a miss keeps the ordinary handling (`type A = B`\n`and C = D`).
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
        return DECLINED;   // lookahead consumed
    }
    // `///` lines at/inside this body followed by a dedent are a floating doc statement of this
    // body: hold the close so the doc lexes first; the dedent then re-fires.
    if (valid[LAYOUT_END] && col < top->col &&
        s->scan.skipped_doc_lines && s->scan.doc_indent >= top->col) return DECLINED;
    // A `|` left of a bare first case is still a case (`type E =`\n`      A = 0`\n`    | B = 1`);
    // types never nest inside match arms.
    if (valid[LAYOUT_END] && col < top->col && top->sort == S_TYPEBODY && bar_arm) return DECLINED;
    // A `(` block closes only at a closer; a dedented declaration keyword is recovery for an unclosed `(`.
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
    // A `#if`-family line between this declaration and a new declaration line: two spellings of
    // one declaration sharing the body after `#endif`. Before the peeks below, which consume the first word.
    if (valid[PREPROC_BREAK] && s->scan.skipped_directive && decl_starter(lexer, first)) {
        return emit(lexer, PREPROC_BREAK);
    }
    // FSC permits continuation arms more indented than their match: the body closes so the arm reaches
    // the S_MATCH. `<=`: an arm body opened at the arm column ends at the next arm. Not S_TYPEBODY (DU cases).
    if (valid[LAYOUT_END] && bar_arm && col == top->col && top->sort != S_TYPEBODY) {
        for (int i = (int)s->n - 2; i >= 0; i--) {
            if (s->stk[i].sort == S_MATCH && s->stk[i].col <= col) {
                return close_top(s, lexer, LAYOUT_END);
            }
            if (s->stk[i].col < col && s->stk[i].sort != S_MATCH) break;
        }
    }
    // A leading `)`/`]`/`}` closes the layout body even at the body column (a lambda whose `)` aligns with its body).
    if (valid[LAYOUT_END] && (first == ')' || is_close_bracket(first))) { return close_top(s, lexer, LAYOUT_END); }
    // `with` at the type-body column closes the type body so the augmentation attaches; a module body must not close.
    if (top->sort == S_TYPEBODY && valid[LAYOUT_END] && col == top->col && first == 'w') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == 'i') { lexer->advance(lexer, true);
        if (lexer->lookahead == 't') { lexer->advance(lexer, true);
        if (lexer->lookahead == 'h') { lexer->advance(lexer, true);
            int32_t a = lexer->lookahead;
            bool word = is_ident_char(a);
            if (!word) { return close_top(s, lexer, LAYOUT_END); }
        }}}
    }
    // A non-indented DU (`type T =`\n`| A`\n`open ...`) puts its body at the module column, so a
    // module-level keyword at that column closes the type body.
    if (top->sort == S_TYPEBODY && valid[LAYOUT_END] && col == top->col &&
        (is_lower(first) || first == '[')) {
        bool ok = true;
        if (first == '[') ok = skip_bracket_attrs(lexer);
        if (ok) {
            char w[12]; size_t wn = 0; int32_t lk = lexer->lookahead;
            while (wn < 11 && is_lower(lk)) { w[wn++] = (char)lk; lexer->advance(lexer, true); lk = lexer->lookahead; }
            w[wn] = '\0';
            bool boundary = !(is_ident_char(lk));
            // `and` only for a union whose cases sit at the `and` column; in a class body it continues `let rec`.
            if (boundary && (!strcmp(w, "open") || !strcmp(w, "module") ||
                             !strcmp(w, "namespace") || !strcmp(w, "exception") ||
                             !strcmp(w, "type") || (!strcmp(w, "and") && top->cases))) {
                return close_top(s, lexer, LAYOUT_END);
            }
        }
    }
    // decl_starter and semi_blocked consume the lookahead, so they run only when a separator is possible.
    if (valid[LAYOUT_SEMI] && col == top->col &&
        !(s->scan.skipped_doc_lines && word_is_decl_kw(s->scan.post_doc_word))) {
        // The first word is read once; each helper consumes it.
        if (is_lower(first)) {
            char w[12]; read_word(lexer, w, sizeof w);
            // `with`/`finally` at the try body's column (`try Map.find x g`\n`    with _ -> ...`).
            if (top->sort == S_TRY && valid[LAYOUT_END] &&
                (!strcmp(w, "with") || !strcmp(w, "finally"))) {
                return close_top(s, lexer, LAYOUT_END);
            }
            if (top->sort == S_DECL && decl_starter_word(w)) return DECLINED;
            // A constructor body (`new (...) as this =`) ends at a `then` at its own column.
            if (!strcmp(w, "then") && valid[LAYOUT_END]) { return close_top(s, lexer, LAYOUT_END); }
            // `else` at a then-body's own column ends the body unless the body is itself an `if`, which owns it.
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
        return DECLINED;   // lookahead consumed
    }
    // `with`/`finally` indented past the try body column. Nothing below applies to such a line.
    if (top->sort == S_TRY && valid[LAYOUT_END] && (first == 'w' || first == 'f')) {
        char w[12]; read_word(lexer, w, sizeof w);
        if (!strcmp(w, "with") || !strcmp(w, "finally")) { return close_top(s, lexer, LAYOUT_END); }
        return DECLINED;
    }
    // Own-line attributed labelled param (`[<Out>] data: byte[]`): the mid-line probe never sees a line start.
    if (first == '[' && valid[LABEL_ATTR] && try_label_attr(lexer)) return EMITTED;
    if (valid[LABEL_GATE] && col > top->col &&
        (is_alpha(first) || first == '_' || first == '?' || first == '`')) {
        if (try_label_gate(lexer)) return EMITTED;
        return DECLINED;
    }
    // Element DSL as the first statement of an indented body (`let page =`\n`    div() {...}`).
    if (try_element_dsl(lexer, valid)) return EMITTED;
    // CE `{` on its own line below the builder: the mid-line CE_BRACE_OPEN dispatch is unreachable here.
    if (try_ce_brace(lexer, valid, first)) return EMITTED;
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
    if (!nli) {
        // Dangling `///` docs at EOF: as for the dedent case in boundary_layout.
        if (s->scan.skipped_doc_lines && top && layoutish(top->sort) && s->scan.doc_indent >= top->col) return DECLINED;
        if (valid[BRACKET_CLOSE] && top && top->sort == S_BRACKET) { return close_top(s, lexer, BRACKET_CLOSE); }
        if (valid[MATCH_END]    && top && top->sort == S_MATCH)    { return close_top(s, lexer, MATCH_END); }
        if (valid[LAYOUT_END]   && top && layoutish(top->sort))   { return close_top(s, lexer, LAYOUT_END); }
        return DECLINED;
    }
    if (first == FIRST_COMMENT_LINE) {
        // next_line_indent consumed the comment with advance(false) and marked its end.
        if (!valid[BLOCK_COMMENT] && !valid[BLOCK_DOC_COMMENT]) return DECLINED;
        // Skipped `//` lines before it would be absorbed into the token: let them lex as extras first.
        if (s->scan.skipped_line_comments) return DECLINED;
        lexer->result_symbol = (s->scan.comment_doc && valid[BLOCK_DOC_COMMENT]) ? BLOCK_DOC_COMMENT
                             : (valid[BLOCK_COMMENT] ? BLOCK_COMMENT : BLOCK_DOC_COMMENT);
        return EMITTED;
    }
    // A leading `|` is an arm marker unless an operator char follows. `|]`/`|}` stay arm-ish and
    // are handled by the bracket closers. Peeked once here for both the infix check and the S_MATCH close.
    bool bar_arm = (first == '|');
    int32_t bar_c1 = 0;
    if (bar_arm) {
        lexer->advance(lexer, true);
        bar_c1 = lexer->lookahead;
        if (is_bar_op_tail(bar_c1)) bar_arm = false;
    }

    // Runs first: the separator/decl-starter branches treat `and` specially and would drop the
    // docs. AND_DOCS_OPEN is not valid while an enclosing body still has to close.
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
    lexer->mark_end(lexer);                       // zero-width baseline; only real tokens re-mark
    if (valid[ERROR_SENTINEL]) return false;      // error recovery

    // Inside an interpolated string no layout token applies; false at a structural char so the grammar lexes it.
    if (valid[INTERP_STRING_TEXT])   { bool ok = scan_interp_text(lexer, TX_STRING);   if (ok) lexer->result_symbol = INTERP_STRING_TEXT;   return ok; }
    if (valid[INTERP_VERBATIM_TEXT]) { bool ok = scan_interp_text(lexer, TX_VERBATIM); if (ok) lexer->result_symbol = INTERP_VERBATIM_TEXT; return ok; }
    if (valid[INTERP_TRIPLE_TEXT])   { bool ok = scan_interp_text(lexer, TX_TRIPLE);   if (ok) lexer->result_symbol = INTERP_TRIPLE_TEXT;   return ok; }

    // The source file is an S_DECL body at column 0. It is never popped: no dedent below
    // column 0 exists and `_layout_end` is not valid at file scope.
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
