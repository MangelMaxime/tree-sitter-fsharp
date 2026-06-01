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
//   LET_BODY_OPEN/CLOSE      — wrap same-line let_binding bodies. Like
//                              INLINE_OPEN/CLOSE but pushes the ENCLOSING
//                              indent column (`stack_top(&s->indents)`),
//                              which approximates the LET keyword's column.
//                              Stops `let x = expr1\nexpr2` at top level from
//                              absorbing the next-line expression as a chained
//                              application. Tracked on its own stack so it
//                              doesn't interfere with INLINE_OPEN's offside
//                              tracking for let_decl_indented.
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
    LET_BODY_OPEN,
    LET_BODY_CLOSE,
    RECORD_BODY_OPEN,
    RECORD_BODY_CLOSE,
    RECORD_FIELD_SEMI,
    MATCH_BODY_OPEN,
    MATCH_BODY_CLOSE,
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
    IndentStack indents;        // pushed by BODY_INDENT / INDENT, popped by their DEDENT
    IndentStack inline_cols;    // pushed by INLINE_OPEN, popped by INLINE_CLOSE
    IndentStack let_body_cols;  // pushed by LET_BODY_OPEN, popped by LET_BODY_CLOSE
    IndentStack record_cols;    // pushed by RECORD_BODY_OPEN, popped by RECORD_BODY_CLOSE
    IndentStack match_body_cols;// pushed by MATCH_BODY_OPEN, popped by MATCH_BODY_CLOSE
} Scanner;

void *tree_sitter_fsharp_external_scanner_create(void) {
    return calloc(1, sizeof(Scanner));
}

void tree_sitter_fsharp_external_scanner_destroy(void *payload) {
    Scanner *s = payload;
    free(s->indents.data);
    free(s->inline_cols.data);
    free(s->let_body_cols.data);
    free(s->record_cols.data);
    free(s->match_body_cols.data);
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
    n = serialize_stack(&s->let_body_cols, buf, n);
    n = serialize_stack(&s->record_cols, buf, n);
    n = serialize_stack(&s->match_body_cols, buf, n);
    return n;
}

void tree_sitter_fsharp_external_scanner_deserialize(void *payload, const char *buf, unsigned length) {
    Scanner *s = payload;
    unsigned n = 0;
    n = deserialize_stack(&s->indents, buf, length, n);
    n = deserialize_stack(&s->inline_cols, buf, length, n);
    n = deserialize_stack(&s->let_body_cols, buf, length, n);
    n = deserialize_stack(&s->record_cols, buf, length, n);
    n = deserialize_stack(&s->match_body_cols, buf, length, n);
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

        // Count leading whitespace chars. Each space or tab counts as 1 — we
        // don't try to model visual tab width. Indent comparisons only need to
        // be consistent WITHIN a file, so as long as the file uses a single
        // indentation style (all spaces or all tabs), the offside rule works
        // correctly regardless of how an editor visualises tabs. F# style is
        // spaces anyway; this just avoids guessing on tab-indented files.
        uint32_t indent = 0;
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            indent++;
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

// Shared check for INLINE_OPEN and LET_BODY_OPEN: skip horizontal whitespace,
// verify same-line content (not newline/EOF), then peek the rest of the line
// for the `in` keyword (the `let x = expr in expr` form).
//
// Returns:
//    1 — same-line body present and no `in`. `*body_col` is set to the body's
//        first-token column. Caller pushes onto its own stack, sets
//        result_symbol, and returns true.
//   -1 — `in` was found on the rest of the line. Caller should return false
//        so let_expression Branch B can match.
//    0 — body is on the next line. Caller falls through to BODY_INDENT etc.
//
// `mark_end` is called at the body's first-token position when 1 is returned.
static int check_inline_body_open(TSLexer *lexer, uint32_t *body_col) {
    // Skip horizontal whitespace to find the start of the body.
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        lexer->advance(lexer, true);
    }
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r' || lexer->lookahead == 0) {
        return 0;
    }
    *body_col = lexer->get_column(lexer);
    lexer->mark_end(lexer);

    // Peek for `in` on the rest of the current line.
    bool prev_word = false;
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
                if (!n_word) return -1;
                prev_word = true;
            } else {
                prev_word = c_word;
            }
        } else {
            lexer->advance(lexer, true);
            prev_word = c_word;
        }
    }
    return 1;
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
    bool want_let_body_open = valid_symbols[LET_BODY_OPEN];
    bool want_let_body_close = valid_symbols[LET_BODY_CLOSE];
    bool want_record_body_open = valid_symbols[RECORD_BODY_OPEN];
    bool want_record_body_close = valid_symbols[RECORD_BODY_CLOSE];
    bool want_record_field_semi = valid_symbols[RECORD_FIELD_SEMI];
    bool want_match_body_open = valid_symbols[MATCH_BODY_OPEN];
    bool want_match_body_close = valid_symbols[MATCH_BODY_CLOSE];

    if (!want_body_indent && !want_body_dedent && !want_indent && !want_dedent
        && !want_inline_open && !want_inline_close && !want_virtual_semi
        && !want_let_body_open && !want_let_body_close
        && !want_record_body_open && !want_record_body_close
        && !want_record_field_semi
        && !want_match_body_open && !want_match_body_close) return false;

    // INLINE_OPEN: emitted right after `=` when a let_decl_indented body starts on
    // the same line. Pushes the body's first-token column onto `inline_cols`
    // so INLINE_CLOSE can compare and fire at the first line whose column is
    // <= that column.
    //
    // Suppressed in two cases:
    //   1. BODY_INDENT is also valid — let_binding owns this position; we want it
    //      to win over let_decl_indented inline (module-level lets).
    //   2. An `in` keyword appears on the rest of the current line — that means
    //      let_expression Branch B (explicit `let ... = expr in expr`) is intended,
    //      and committing to inline here would dead-end the parse.
    if (want_inline_open && !want_body_indent) {
        uint32_t body_col = 0;
        int r = check_inline_body_open(lexer, &body_col);
        if (r == 1) {
            stack_push(&s->inline_cols, body_col);
            lexer->result_symbol = INLINE_OPEN;
            return true;
        }
        if (r == -1) return false; // `in` found — let Branch B match.
        // r == 0: body on next line — fall through; INDENT will handle it.
    }

    // LET_BODY_OPEN: emitted right after `=` when a let_binding body starts
    // on the same line. Same scaffolding as INLINE_OPEN (via the shared
    // helper above) but uses its OWN stack (`let_body_cols`) and pushes the
    // ENCLOSING indent column rather than the body's first-token column.
    // The enclosing indent approximates the LET keyword's column — F#
    // terminates an inline let-binding body when the next line returns to
    // (or below) that column. Using the body's column instead would
    // prematurely close `function`/`match` arms that sit at lower indent
    // than the keyword itself.
    if (want_let_body_open) {
        uint32_t body_col = 0; // unused for LET_BODY_OPEN
        int r = check_inline_body_open(lexer, &body_col);
        if (r == 1) {
            stack_push(&s->let_body_cols, stack_top(&s->indents));
            lexer->result_symbol = LET_BODY_OPEN;
            return true;
        }
        if (r == -1) return false; // `in` found
        // r == 0: body on next line — fall through; BODY_INDENT will handle it.
    }

    // MATCH_BODY_OPEN: emitted right after `->` when a match/try/function arm
    // body starts on the SAME line. Pushes the ENCLOSING indent column (≈ the
    // `match` keyword's column) onto `match_body_cols`; MATCH_BODY_CLOSE fires
    // when a later line returns to or below it, so an inline arm body
    //   | pat -> expr
    //   nextStatement        (at the match column)
    // stops absorbing `nextStatement` into the arm's sequence_expression.
    //
    // No `in` suppression (unlike LET_BODY_OPEN): an arm body may legitimately
    // contain `in` (`| x -> for i in xs do …`, `| x -> let y = z in …`), so
    // we only check for same-line content.
    if (want_match_body_open) {
        // Skip horizontal whitespace to the body's first token.
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            lexer->advance(lexer, true);
        }
        // Same-line body present? (Not newline / EOF.)
        if (lexer->lookahead != '\n' && lexer->lookahead != '\r' && lexer->lookahead != 0) {
            stack_push(&s->match_body_cols, stack_top(&s->indents));
            lexer->mark_end(lexer); // zero-width — emit at the body's column.
            lexer->result_symbol = MATCH_BODY_OPEN;
            return true;
        }
        // Body on next line — fall through; BODY_INDENT handles it.
    }

    // RECORD_BODY_OPEN: emitted right after `{` when the first field starts
    // on the SAME line. Captures the field's column onto `record_cols` so
    // VIRTUAL_SEMI (below) can fire at subsequent line boundaries that sit
    // at that column — supporting the F# convention
    //   { Firstname : string
    //     Surname  : string }
    // where `{` and the first field share a line, subsequent fields align
    // under the first field's column, and `}` may sit on the last field's
    // line. The block form `{\n  X\n  Y\n}` is left alone — _body_indent
    // already handles it.
    //
    // Suppressed when the first same-line word is `new` — `{` then opens an
    // `object_expression` (`{ new IFoo with … }`), not a record. Committing
    // to a record body here would break that parse.
    if (want_record_body_open) {
        // Skip horizontal whitespace.
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            lexer->advance(lexer, true);
        }
        // Fall through (don't return false) when this isn't a same-line
        // first-field — let BODY_INDENT handle the block form.
        bool same_line_field = (lexer->lookahead != '\n' &&
                                lexer->lookahead != '\r' &&
                                lexer->lookahead != 0 &&
                                lexer->lookahead != '}');
        if (same_line_field) {
            uint32_t col = lexer->get_column(lexer);
            // Mark the token end NOW (zero-width at the field column) so
            // the look-ahead advance() calls below only peek and don't
            // extend the emitted token.
            lexer->mark_end(lexer);
            // Peek the rest of the line to confirm this looks like a
            // record field (identifier followed by `=` or `:`), not:
            //   - `{ new IFoo … }`          object expression
            //   - `{ base with field }`     record copy-update
            //   - `{| base with field |}`   anonymous-record copy-update
            // For those forms we want the grammar's other branches to
            // match — pushing onto record_cols would break them.
            //
            // Walk one identifier-shaped word, then skip whitespace, then
            // check the next non-whitespace char. Only emit if it's `=`
            // (record_field) or `:` (record_type_field).
            bool looks_like_field = false;
            int32_t c = lexer->lookahead;
            bool word_start = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                              c == '_' || c == '`';
            if (word_start) {
                // Consume the identifier-shaped word (allow `` `…` `` form).
                if (c == '`') {
                    lexer->advance(lexer, true);
                    if (lexer->lookahead == '`') {
                        lexer->advance(lexer, true);
                        while (lexer->lookahead != '`' &&
                               lexer->lookahead != '\n' &&
                               lexer->lookahead != '\r' &&
                               lexer->lookahead != 0) {
                            lexer->advance(lexer, true);
                        }
                        if (lexer->lookahead == '`') {
                            lexer->advance(lexer, true);
                            if (lexer->lookahead == '`') lexer->advance(lexer, true);
                        }
                    }
                } else {
                    while (1) {
                        int32_t ch = lexer->lookahead;
                        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                            (ch >= '0' && ch <= '9') || ch == '_' || ch == '\'') {
                            lexer->advance(lexer, true);
                        } else break;
                    }
                }
                // Skip horizontal whitespace.
                while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                    lexer->advance(lexer, true);
                }
                // Field separator: `=` (record_field) or `:` (record_type_field).
                // `:` could also be `::` (cons) or `:>` / `:?` (casts) — those
                // don't appear right after a field name, so a bare `:` is fine
                // as a field marker for our purposes.
                int32_t sep = lexer->lookahead;
                if (sep == '=' || sep == ':') looks_like_field = true;
            }
            if (looks_like_field) {
                stack_push(&s->record_cols, col);
                lexer->result_symbol = RECORD_BODY_OPEN;
                return true;
            }
        }
        // Fall through.
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
            // Mid-line `}` (or `|}` for anonymous records) and a record
            // body is open — RECORD_BODY_CLOSE pops record_cols and lets
            // the grammar consume the closing delimiter next. Goes before
            // BODY_DEDENT so a record's inline close beats a pending
            // BODY_DEDENT (the close is the more specific shape).
            if (want_record_body_close && s->record_cols.size > 0) {
                if (lexer->lookahead == '}') {
                    stack_pop(&s->record_cols);
                    lexer->result_symbol = RECORD_BODY_CLOSE;
                    return true;
                }
                if (lexer->lookahead == '|') {
                    lexer->advance(lexer, true);
                    if (lexer->lookahead == '}') {
                        stack_pop(&s->record_cols);
                        lexer->result_symbol = RECORD_BODY_CLOSE;
                        return true;
                    }
                }
            }
            // Mid-line MATCH_BODY_CLOSE before a new `| …` arm on the SAME
            // line — e.g. single-line `function | 0 -> "a" | _ -> "b"`. A
            // bare `|` that ISN'T `|>` (pipe), `||` (or), or `|}` (anon-record
            // close) starts the next arm, so the inline arm body must close
            // first. The `}` case (anon-record) is handled by RECORD above.
            if (want_match_body_close && s->match_body_cols.size > 0 &&
                lexer->lookahead == '|') {
                lexer->advance(lexer, true);
                int32_t after = lexer->lookahead;
                if (after != '>' && after != '|' && after != '}') {
                    stack_pop(&s->match_body_cols);
                    lexer->result_symbol = MATCH_BODY_CLOSE;
                    return true;
                }
            }
            // Mid-line non-whitespace ahead. If it's a recognised closing
            // delimiter and the parser is expecting BODY_DEDENT (i.e. we are
            // inside an indented body that needs to close before the
            // delimiter), pop one indent level and emit BODY_DEDENT.
            if (want_body_dedent && s->indents.size > 0) {
                if (lexer->lookahead == '}' || lexer->lookahead == ')') {
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
        // EOF: close any open block. LET_BODY_CLOSE / INLINE_CLOSE first so
        // inline-bodied lets close before any surrounding BODY_/INDENT blocks.
        if (want_let_body_close && s->let_body_cols.size > 0) {
            stack_pop(&s->let_body_cols);
            lexer->result_symbol = LET_BODY_CLOSE;
            return true;
        }
        if (want_match_body_close && s->match_body_cols.size > 0) {
            stack_pop(&s->match_body_cols);
            lexer->result_symbol = MATCH_BODY_CLOSE;
            return true;
        }
        if (want_inline_close && s->inline_cols.size > 0) {
            stack_pop(&s->inline_cols);
            lexer->result_symbol = INLINE_CLOSE;
            return true;
        }
        if (want_record_body_close && s->record_cols.size > 0) {
            stack_pop(&s->record_cols);
            lexer->result_symbol = RECORD_BODY_CLOSE;
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

    // LET_BODY_CLOSE: any line at column <= the recorded enclosing indent
    // ends the inline let-binding body. Same shape as INLINE_CLOSE but
    // separate stack (and a different pushed value — enclosing indent, not
    // body's first-token column).
    if (want_let_body_close && s->let_body_cols.size > 0) {
        uint32_t body_col = stack_top(&s->let_body_cols);
        if (col <= body_col) {
            stack_pop(&s->let_body_cols);
            lexer->result_symbol = LET_BODY_CLOSE;
            return true;
        }
    }

    // MATCH_BODY_CLOSE (line boundary): close the inline arm body when either
    //   (a) the next line dedents to <= the recorded enclosing indent (≈ the
    //       `match` column) — a trailing statement or lower-indent
    //       continuation ends the whole match, OR
    //   (b) the next line starts a new `| …` arm (not `|>` / `||` / `|}`),
    //       regardless of column — covers `function`/`match` whose arms are
    //       indented DEEPER than the enclosing `let`/`match` keyword, where
    //       the column test alone would never fire.
    // The grammar's `repeat1(match_arm)` consumes the following arm in case
    // (b); in case (a) the match reduces and the next token is a sibling.
    if (want_match_body_close && s->match_body_cols.size > 0) {
        uint32_t body_col = stack_top(&s->match_body_cols);
        bool new_arm = false;
        if (lexer->lookahead == '|') {
            lexer->advance(lexer, true); // peek past `|` (skip=true: no token extend)
            int32_t after = lexer->lookahead;
            if (after != '>' && after != '|' && after != '}') new_arm = true;
        }
        if (col <= body_col || new_arm) {
            stack_pop(&s->match_body_cols);
            lexer->result_symbol = MATCH_BODY_CLOSE;
            return true;
        }
    }

    // RECORD_BODY_CLOSE: next-line `}` or `|}` (possibly at lower indent)
    // closes the inline record body. The mid-line case is handled above.
    if (want_record_body_close && s->record_cols.size > 0) {
        if (lexer->lookahead == '}') {
            stack_pop(&s->record_cols);
            lexer->result_symbol = RECORD_BODY_CLOSE;
            return true;
        }
        if (lexer->lookahead == '|') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == '}') {
                stack_pop(&s->record_cols);
                lexer->result_symbol = RECORD_BODY_CLOSE;
                return true;
            }
        }
    }

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

    // Same-column BODY_DEDENT for type-augmentation: `type T = body \n    with`.
    // The `with` continues the enclosing `type_decl`, so the inner body's
    // BODY_DEDENT must fire before the parser can match the `with`. The
    // standard "col < current" rule wouldn't trigger because the `with` sits
    // at the body column. Only when the parser already wants BODY_DEDENT.
    if (col == current && want_body_dedent && s->indents.size > 0 &&
        lexer->lookahead == 'w') {
        // Peek `with` as a complete keyword.
        lexer->advance(lexer, true);
        if (lexer->lookahead == 'i') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == 't') {
                lexer->advance(lexer, true);
                if (lexer->lookahead == 'h') {
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

    // VIRTUAL_SEMI: lowest priority — fires only when no INLINE_CLOSE / INDENT /
    // DEDENT applied. Emitted when the next line sits at exactly the current
    // body column (the top of the indents stack — which is pushed by
    // BODY_INDENT when we enter any indented body, let or if-then or
    // for/while/lambda etc.). Top-level _token siblings have an empty stack
    // so this naturally skips them.
    //
    // Also fires at the top of `record_cols` when the next line matches the
    // captured first-field column of an inline `{ F1\n   F2 }` record — that
    // column isn't on the indents stack but is the field separator there.
    //
    // We also block emission when the next char can't start a new expression
    // (`|`, `)`, `]`, `}`, or continuation keywords like `else`/`elif`/`with`).
    // Without those guards the parser commits to a sequence path that fails
    // once it reaches the closer.
    // Inside an inline record body (`{ F1\n   F2 }`), emit RECORD_FIELD_SEMI
    // at the field column INSTEAD of VIRTUAL_SEMI. RECORD_FIELD_SEMI is only
    // consumable by the field-list repeat — a `sequence_expression` nested
    // inside a field's value (e.g. a lambda body) can't shift it, so it
    // reduces all the way out and lets the next field start. Without this,
    // the lambda body would absorb subsequent field names via the
    // sequence's VIRTUAL_SEMI consumption.
    bool at_record_field_col = s->record_cols.size > 0 &&
                               col == stack_top(&s->record_cols);
    if (want_record_field_semi && at_record_field_col) {
        // Apply the same blocker checks as VIRTUAL_SEMI (closing delimiters
        // and continuation keywords must NOT trigger a separator).
        int32_t c = lexer->lookahead;
        bool blocked = (c == '|' || c == ')' || c == ']' || c == '}');
        if (!blocked) {
            lexer->result_symbol = RECORD_FIELD_SEMI;
            return true;
        }
    }

    bool semi_col_match = (s->indents.size > 0 && col == current);
    if (want_virtual_semi && semi_col_match) {
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
