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
//                              indent column (`idx_top(&s->stk, K_INDENT)`),
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
    MATCH_ARM_SEP,
    FOR_BODY_OPEN,
    FOR_BODY_CLOSE,
    FLOAT_TRAILING_DOT,
    BRACKET_OPEN,   // after `[`/`[|` with the body on the next line: captures the
                    //   element column (kind K_BRACKET)
    BRACKET_SEP,    // separator between newline-aligned bracket elements — a
                    //   DEDICATED token (a nested sequence_expression can't steal
                    //   it, unlike VIRTUAL_SEMI), so elements never chain
    BRACKET_CLOSE,  // pops the K_BRACKET column at `]`/`|]`
    CE_BODY_OPEN,   // after `builder {` with the body on the next line: pushes the
                    //   statement column as a normal K_INDENT (so `current`, the
                    //   indent/dedent logic, and every body-close — LET_BODY_CLOSE,
                    //   MATCH_BODY_CLOSE — track the CE statement column correctly)
    CE_SEP,         // separator between newline-aligned CE statements — DEDICATED
                    //   like BRACKET_SEP (checked before VIRTUAL_SEMI), so a
                    //   binding's trailing `$._expression` can't absorb the next
                    //   statement into a sequence_expression
    CE_BODY_CLOSE,  // pops the CE body's K_INDENT at `}`
    TYPE_AUGMENT_DEDENT, // BODY_DEDENT variant emitted when a `with` type
                    //   augmentation follows the (indented) type body. Pops the
                    //   body's K_INDENT exactly like BODY_DEDENT, but is a
                    //   DISTINCT symbol so the grammar can route the parse into
                    //   the augmentation branch — without it, `type_decl`
                    //   reduces early when the `with` sits at an enclosing indent
                    //   column (e.g. the module body column == the type column)
                    //   and the augmentation members detach as an error.
} TokenType;

// One unified offside stack. Each entry is a column plus a KIND tag recording
// which construct opened it. This replaces the five separate column stacks the
// scanner used to keep — a single source of truth for "what column am I at", so
// the per-construct OPEN/CLOSE logic can never disagree about nesting.
//
// Operations are kind-scoped: idx_top / idx_has / idx_pop act on the TOPMOST
// entry of the requested kind. Entries are pushed in nesting order, so the
// topmost entry of a kind is exactly what that construct's old dedicated stack
// would have had on top — making this change behaviour-preserving. idx_pop
// removes that entry wherever it sits (almost always the very top, since inner
// constructs close first); removing a buried entry preserves the relative order
// of the rest, and thus every kind's "topmost", intact.
typedef enum {
    K_INDENT,      // BODY_INDENT / INDENT / FOR_BODY_OPEN bodies (old `indents`)
    K_INLINE,      // same-line let_decl_indented body (old `inline_cols`)
    K_LET_BODY,    // same-line let_binding body (old `let_body_cols`)
    K_RECORD,      // inline record-expression field list (old `record_cols`)
    K_MATCH_BODY,  // same-line match/try/function arm body (old `match_body_cols`)
    K_BRACKET,     // list/array element body column (BRACKET_OPEN). Drives ONLY
                   //   BRACKET_SEP/BRACKET_CLOSE; deliberately NOT consulted by
                   //   `current`/VIRTUAL_SEMI or any other construct, so it can't
                   //   ripple or corrupt unrelated code if left dangling by an error.
    K_CE,          // computation-expression statement column (CE_BODY_OPEN). Like
                   //   K_BRACKET it drives ONLY CE_SEP/CE_BODY_CLOSE and is NOT fed
                   //   into `current`/VIRTUAL_SEMI — so a dangling entry can't ripple,
                   //   and statement separation never competes with the generic
                   //   sequence machinery (the bug that broke the K_INDENT approach).
} IndentKind;

typedef struct {
    uint32_t *cols;
    uint8_t  *kinds;
    uint32_t  size;
    uint32_t  capacity;
} IndentStack;

static void idx_push(IndentStack *s, uint32_t col, uint8_t kind) {
    if (s->size == s->capacity) {
        s->capacity = s->capacity ? s->capacity * 2 : 8;
        s->cols  = realloc(s->cols,  s->capacity * sizeof(uint32_t));
        s->kinds = realloc(s->kinds, s->capacity * sizeof(uint8_t));
    }
    s->cols[s->size] = col;
    s->kinds[s->size] = kind;
    s->size++;
}

// Index of the topmost entry of `kind`, or -1 if there is none.
static int idx_find(const IndentStack *s, uint8_t kind) {
    for (int i = (int)s->size - 1; i >= 0; i--) {
        if (s->kinds[i] == kind) return i;
    }
    return -1;
}

static bool idx_has(const IndentStack *s, uint8_t kind) {
    return idx_find(s, kind) >= 0;
}

static uint32_t idx_top(const IndentStack *s, uint8_t kind) {
    int i = idx_find(s, kind);
    return i >= 0 ? s->cols[i] : 0;
}

static void idx_pop(IndentStack *s, uint8_t kind) {
    int i = idx_find(s, kind);
    if (i < 0) return;
    for (uint32_t j = (uint32_t)i; j + 1 < s->size; j++) {
        s->cols[j]  = s->cols[j + 1];
        s->kinds[j] = s->kinds[j + 1];
    }
    s->size--;
}

// True if some open indented body (K_INDENT) sits at column <= col. Used by the
// leading-infix-operator dedent rule: when an enclosing body is at/left of the
// operator's column, the operator continues THAT body, so the deeper bodies
// above it must dedent/close normally. Only when NO body is at <= col does the
// operator sit to the left of every body and continue the innermost one from
// below — the one case where we suppress the dedent.
static bool has_indent_le(const IndentStack *s, uint32_t col) {
    for (uint32_t i = 0; i < s->size; i++) {
        if (s->kinds[i] == K_INDENT && s->cols[i] <= col) return true;
    }
    return false;
}

// Current offside baseline: innermost open indented body, counting K_INDENT AND
// K_CE. K_CE is included so that inside a CE body `current` is the CE statement
// column — otherwise a multi-line application/let-body in a CE statement treats
// the next statement (deeper than the OUTER indent) as an indented continuation
// and chains it, lexing if/then/return as identifiers. CE_SEP fires before
// VIRTUAL_SEMI, so this does not yield stray virtual-semis.
static uint32_t cur_col(const IndentStack *s) {
    for (int i = (int)s->size - 1; i >= 0; i--) {
        if (s->kinds[i] == K_INDENT || s->kinds[i] == K_CE) return s->cols[i];
    }
    return 0;
}

// THE canonical enclosing-body baseline for inline body opens (LET_BODY_OPEN and
// MATCH_BODY_OPEN): the innermost enclosing container, counting K_INDENT, K_CE
// AND K_BRACKET. Unlike `idx_top(K_INDENT)`, this includes the list/array element
// column (K_BRACKET) and the CE statement column (K_CE) so an inline let/arm body
// inside a list or CE (`[ … match x with | a -> b … ]`) closes when a line
// returns to that container's column — instead of inheriting the OUTER indent
// and running past the `]`/`}` (which broke `match`/`yield`/`let` bodies in list
// comprehensions and CEs).
//
// IMPORTANT: every inline-body-open MUST use this one function (not a hand-rolled
// `idx_top(K_INDENT)`), so adding a new container kind only needs updating here —
// that consistency is what stops the recurring "new kind not accounted for" bug.
// (This is the SEQUENCING baseline's sibling; `cur_col`/`current` are a DIFFERENT
// concept — the VIRTUAL_SEMI/DEDENT line — and deliberately exclude K_BRACKET.)
static uint32_t inline_body_baseline(const IndentStack *s) {
    for (int i = (int)s->size - 1; i >= 0; i--) {
        IndentKind k = s->kinds[i];
        if (k == K_INDENT || k == K_CE || k == K_BRACKET) return s->cols[i];
    }
    return 0;
}

typedef struct {
    IndentStack stk;
} Scanner;

void *tree_sitter_fsharp_external_scanner_create(void) {
    return calloc(1, sizeof(Scanner));
}

void tree_sitter_fsharp_external_scanner_destroy(void *payload) {
    Scanner *s = payload;
    free(s->stk.cols);
    free(s->stk.kinds);
    free(s);
}

#define IDX_KIND_COUNT 7

// Serialize GROUPED BY KIND (one length-prefixed run per kind, in kind order).
// Critically, the bytes then depend ONLY on each kind's column list and NOT on
// the cross-kind push interleaving — so two scanner states the scanner can't
// tell apart (same idx_top/idx_has for every kind) serialize identically, which
// is what lets tree-sitter merge GLR parse stacks. Encoding the interleaving
// would make logically-equal states look different and suppress that merging,
// changing error-recovery behaviour. This format is byte-compatible with the
// pre-unification five-separate-stacks layout.
unsigned tree_sitter_fsharp_external_scanner_serialize(void *payload, char *buf) {
    Scanner *s = payload;
    unsigned n = 0;
    for (uint8_t k = 0; k < IDX_KIND_COUNT; k++) {
        uint32_t cnt = 0;
        for (uint32_t i = 0; i < s->stk.size; i++) {
            if (s->stk.kinds[i] == k) cnt++;
        }
        if (n + 4 > TREE_SITTER_SERIALIZATION_BUFFER_SIZE) break;
        memcpy(buf + n, &cnt, 4); n += 4;
        for (uint32_t i = 0; i < s->stk.size && n + 4 <= TREE_SITTER_SERIALIZATION_BUFFER_SIZE; i++) {
            if (s->stk.kinds[i] != k) continue;
            memcpy(buf + n, &s->stk.cols[i], 4); n += 4;
        }
    }
    return n;
}

void tree_sitter_fsharp_external_scanner_deserialize(void *payload, const char *buf, unsigned length) {
    Scanner *s = payload;
    s->stk.size = 0;
    unsigned n = 0;
    for (uint8_t k = 0; k < IDX_KIND_COUNT; k++) {
        if (n + 4 > length) break;
        uint32_t cnt; memcpy(&cnt, buf + n, 4); n += 4;
        for (uint32_t i = 0; i < cnt && n + 4 <= length; i++) {
            uint32_t col; memcpy(&col, buf + n, 4); n += 4;
            idx_push(&s->stk, col, k);
        }
    }
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
// `first` (optional, may be NULL) receives the first SIGNIFICANT char of the
// reported line. This matters because the comment-probe below advances PAST a
// lone `(` or `/` (checking for `(*` / `//`), so afterwards `lexer->lookahead`
// is the SECOND char — e.g. for a line starting `()` it's `)`. Callers that use
// the next line's leading char for decisions (the offside `la0`) must read
// `first`, not `lexer->lookahead`, or `()`-led statements look like a `)` blocker.
static bool next_line_indent(TSLexer *lexer, uint32_t *col, int32_t *first) {
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
            if (first) *first = '/';
            *col = indent;
            return true;
        }
        // Skip block comments (* ... *)  — supports F#'s nested form.
        if (lexer->lookahead == '(') {
            lexer->advance(lexer, true);
            if (lexer->lookahead != '*') {
                // Lone `(` — significant content at `indent`.
                if (first) *first = '(';
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

        // Skip conditional-compilation directive lines (`#if` / `#elif` /
        // `#else` / `#endif`). They are `extras` — transparent to the offside
        // rule — so the CODE lines in each branch must drive indentation and
        // statement separation, not the directive lines. Without this, a body
        // wrapped in `#if … #endif` mis-computes its column (the directive line)
        // and a stray VIRTUAL_SEMI fires before `#endif`, detaching the body.
        // Other `#` directives (`#nowarn` / `#load` / `#r` / `#line` / …) are
        // real declarations — left as significant content at `indent`.
        if (lexer->lookahead == '#') {
            lexer->advance(lexer, true);
            char w[6];
            size_t wi = 0;
            while (wi < 5 && lexer->lookahead >= 'a' && lexer->lookahead <= 'z') {
                w[wi++] = (char)lexer->lookahead;
                lexer->advance(lexer, true);
            }
            w[wi] = '\0';
            if (strcmp(w, "if") == 0 || strcmp(w, "elif") == 0 ||
                strcmp(w, "else") == 0 || strcmp(w, "endif") == 0) {
                while (lexer->lookahead != '\n' && lexer->lookahead != '\r' &&
                       lexer->lookahead != 0) {
                    lexer->advance(lexer, true);
                }
                if (lexer->lookahead == 0) return false;
                continue;
            }
            // A non-conditional `#` directive — significant content at `indent`.
            if (first) *first = '#';
            *col = indent;
            return true;
        }

        if (first) *first = lexer->lookahead;
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

    // Peek for the let's OWN `in` keyword on the rest of the line (the
    // `let x = expr in expr` form). Read whole words so a binder's `in` is NOT
    // mistaken for the let's `in`: each `for`/`let`/`use` introduces its own `in`
    // (`for x in xs`, `let y = e in …`, `use r = e in …`), so we keep a
    // `pending` count and let each binder consume one following `in`. Only an
    // `in` seen with no pending binder is the let's own — return -1 then.
    // (Without this, `let a = for x in xs do …`, `let a = [ for x in xs -> … ]`,
    // or `let a = let y = 1 in y` suppress LET_BODY_OPEN and the bare-`_expression`
    // body wrongly absorbs the next-line sibling `let`.)
    char word[8];
    size_t wlen = 0;
    bool in_word = false;
    int pending = 0;
    for (;;) {
        int32_t c = lexer->lookahead;
        bool c_word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '\'';
        bool boundary = !c_word || c == '\n' || c == '\r' || c == 0;
        if (boundary && in_word) {
            // A word just ended — classify it.
            if (wlen == 2 && word[0] == 'i' && word[1] == 'n') {
                if (pending > 0) pending--;  // this `in` belongs to a for/let/use binder
                else return -1;              // the let's own `in`
            } else if ((wlen == 3 && word[0] == 'f' && word[1] == 'o' && word[2] == 'r') ||
                       (wlen == 3 && word[0] == 'l' && word[1] == 'e' && word[2] == 't') ||
                       (wlen == 3 && word[0] == 'u' && word[1] == 's' && word[2] == 'e')) {
                pending++;
            }
            in_word = false;
        }
        if (c == '\n' || c == '\r' || c == 0) break;
        if (c_word) {
            if (!in_word) { in_word = true; wlen = 0; }
            if (wlen < sizeof(word)) word[wlen] = (char)c;
            wlen++;
        }
        lexer->advance(lexer, true);
    }
    return 1;
}

// Peek the identifier-shaped word at the current lexer position (assumes the
// lexer is already past leading whitespace) WITHOUT extending the token, and
// return true if it is a token that means "this `for … do` is NOT a plain
// imperative loop body we should sequence." Two families:
//
//   1. Query-CE operators (`where`/`select`/… and a chained `for`): only
//      follow `do` inside a `query { … }` CE — suppress so the for-body stays
//      empty and the operators parse as `query_operator` siblings.
//   2. CE result / bang forms (`yield`/`return` and `do!`/`let!`/`use!`/
//      `and!`/`match!`): the `for` is the iteration of a seq/async/task CE,
//      whose body has CE-specific handling — suppress so that existing path
//      stays intact rather than forcing the body through plain `_expression`.
//      `yield`/`return` are CE-only keywords (invalid in a non-CE for body),
//      so suppressing them is harmless outside CEs. The bang forms are
//      detected by a trailing `!`, so plain `let`/`do`/`match` in an ordinary
//      loop body still sequence.
static bool next_word_blocks_for_body(TSLexer *lexer) {
    char buf[24];
    size_t i = 0;
    int32_t look = lexer->lookahead;
    while (i < sizeof(buf) - 1 &&
           ((look >= 'a' && look <= 'z') || (look >= 'A' && look <= 'Z') ||
            (look >= '0' && look <= '9') || look == '_' || look == '\'')) {
        buf[i++] = (char)look;
        lexer->advance(lexer, true); // skip=true: peek only, don't extend token
        look = lexer->lookahead;
    }
    buf[i] = '\0';

    // (1) Query operators — keep in sync with grammar.js query_operator op set
    // + join / groupBy / leftOuterJoin + `for` (chained from-clause).
    static const char *query_ops[] = {
        "select", "where", "sortBy", "sortByDescending",
        "thenBy", "thenByDescending", "take", "skip",
        "takeWhile", "skipWhile", "distinct", "count",
        "head", "last", "exactlyOne",
        "minBy", "maxBy", "sumBy", "averageBy",
        "find", "exists", "all", "contains", "nth",
        "headOrDefault", "lastOrDefault", "exactlyOneOrDefault",
        "join", "groupBy", "groupValBy", "groupJoin",
        "leftOuterJoin", "for",
        NULL,
    };
    for (const char **k = query_ops; *k; k++) {
        if (strcmp(buf, *k) == 0) return true;
    }

    // (2a) CE result keywords (yield / return) — CE-only, suppress always.
    if (strcmp(buf, "yield") == 0 || strcmp(buf, "return") == 0) return true;

    // (2b) CE bang forms (do! / let! / use! / and! / match!) — the `!` after
    // the keyword marks the CE construct; bare let/do/match keep sequencing.
    if (look == '!' &&
        (strcmp(buf, "do") == 0 || strcmp(buf, "let") == 0 ||
         strcmp(buf, "use") == 0 || strcmp(buf, "and") == 0 ||
         strcmp(buf, "match") == 0)) {
        return true;
    }

    return false;
}

// Peek (no token extend) whether the lexer sits on a complete `with` keyword —
// i.e. `with` followed by a non-identifier char. Advances the lexer past the
// peeked chars; the caller must be at a point where a zero-width token is about
// to be emitted (so the consumed characters are discarded on the next lex).
static bool peek_with(TSLexer *lexer) {
    if (lexer->lookahead != 'w') return false;
    lexer->advance(lexer, true);
    if (lexer->lookahead != 'i') return false;
    lexer->advance(lexer, true);
    if (lexer->lookahead != 't') return false;
    lexer->advance(lexer, true);
    if (lexer->lookahead != 'h') return false;
    lexer->advance(lexer, true);
    int32_t after = lexer->lookahead;
    bool word = (after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z') ||
                (after >= '0' && after <= '9') || after == '_' || after == '\'';
    return !word;
}

// Peek (no token extend) whether the next line starts with a computation-
// expression "bang" binding: `let!` / `use!` / `and!` / `do!` / `match!`.
// Such a line ALWAYS begins a new CE statement — it can never continue a
// preceding `let … = value` body — so the scanner must close that body before
// it. Assumes the lexer sits on the line's first non-whitespace char.
static bool next_line_starts_with_ce_bang(TSLexer *lexer) {
    char buf[8];
    size_t i = 0;
    int32_t look = lexer->lookahead;
    while (i < sizeof(buf) - 1 &&
           ((look >= 'a' && look <= 'z'))) {
        buf[i++] = (char)look;
        lexer->advance(lexer, true); // peek only
        look = lexer->lookahead;
    }
    buf[i] = '\0';
    return look == '!' &&
           (strcmp(buf, "let") == 0 || strcmp(buf, "use") == 0 ||
            strcmp(buf, "and") == 0 || strcmp(buf, "do") == 0 ||
            strcmp(buf, "match") == 0);
}

// Match a trailing-dot float literal (`1.`, `20.`) at the current position.
// Skips leading horizontal whitespace (the scanner is often called on the
// space before an application argument / binary operand). The `.` forms a
// float ONLY when it is NOT followed by another `.` (so `1..2` stays a range)
// nor by a digit / `e` / `E` (those are ordinary float regex alternatives in
// the grammar). Returns true and sets the result symbol on a match.
static bool scan_trailing_dot_float(TSLexer *lexer) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
        lexer->advance(lexer, true);
    }
    if (lexer->lookahead < '0' || lexer->lookahead > '9') return false;
    lexer->advance(lexer, false); // integer part: digit, then digits / `_`
    while ((lexer->lookahead >= '0' && lexer->lookahead <= '9') ||
           lexer->lookahead == '_') {
        lexer->advance(lexer, false);
    }
    if (lexer->lookahead != '.') return false;
    lexer->advance(lexer, false);
    int32_t after = lexer->lookahead;
    if (after == '.' || (after >= '0' && after <= '9') ||
        after == 'e' || after == 'E') {
        return false;
    }
    lexer->mark_end(lexer);
    lexer->result_symbol = FLOAT_TRAILING_DOT;
    return true;
}

bool tree_sitter_fsharp_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    Scanner *s = payload;

    // Error-recovery detection: tree-sitter sets EVERY external valid during
    // error recovery. Normal parse states only ever have a handful valid (real
    // files top out well under this; clean files never hit it). When recovery is
    // active we must NOT push new offside columns — otherwise the scanner keeps
    // opening bodies on garbage lookahead and the stack explodes (observed:
    // hundreds of thousands of pushes), turning one local error into a file-wide
    // cascade (the "error-recovery tail"). Suppressing pushes here can only
    // affect files that are ALREADY in error — clean files never enter recovery,
    // so this is regression-safe by construction.
    int nvalid = 0;
    for (int i = 0; i <= TYPE_AUGMENT_DEDENT; i++) if (valid_symbols[i]) nvalid++;
    bool recovery = nvalid > 20;

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
    bool want_match_arm_sep = valid_symbols[MATCH_ARM_SEP];
    bool want_for_body_open = valid_symbols[FOR_BODY_OPEN];
    bool want_for_body_close = valid_symbols[FOR_BODY_CLOSE];
    bool want_bracket_open = valid_symbols[BRACKET_OPEN];
    bool want_bracket_sep = valid_symbols[BRACKET_SEP];
    bool want_bracket_close = valid_symbols[BRACKET_CLOSE];
    bool want_ce_body_open = valid_symbols[CE_BODY_OPEN];
    bool want_ce_sep = valid_symbols[CE_SEP];
    bool want_ce_body_close = valid_symbols[CE_BODY_CLOSE];
    bool want_type_augment_dedent = valid_symbols[TYPE_AUGMENT_DEDENT];

    // In error recovery, allow ONLY the stack-UNWINDING tokens (closes/dedents):
    // suppress every body-OPEN/INDENT (they idx_push and would explode the stack
    // on garbage lookahead) AND every separator (they'd inject spurious structure
    // mid-error). Keeping closes/dedents lets the offside stack unwind so parsing
    // resyncs cleanly. This contains the recovery cascade that otherwise turns one
    // local error into a file-wide failure. Regression-safe: clean files never
    // enter recovery, so this can only ever affect already-erroring input.
    if (recovery) {
        want_body_indent = false;
        want_indent = false;
        want_inline_open = false;
        want_let_body_open = false;
        want_record_body_open = false;
        want_match_body_open = false;
        want_for_body_open = false;
        want_bracket_open = false;
        want_ce_body_open = false;
        want_virtual_semi = false;
        want_record_field_semi = false;
        want_match_arm_sep = false;
        want_bracket_sep = false;
        want_ce_sep = false;
    }

    if (!want_body_indent && !want_body_dedent && !want_indent && !want_dedent
        && !want_inline_open && !want_inline_close && !want_virtual_semi
        && !want_let_body_open && !want_let_body_close
        && !want_record_body_open && !want_record_body_close
        && !want_record_field_semi
        && !want_match_body_open && !want_match_body_close && !want_match_arm_sep
        && !want_for_body_open && !want_for_body_close
        && !want_bracket_open && !want_bracket_sep && !want_bracket_close
        && !want_ce_body_open && !want_ce_sep && !want_ce_body_close
        && !want_type_augment_dedent) {
        // No offside token applies here. The only remaining external is the
        // trailing-dot float (`1.`, `20.`). Handled at this point — AFTER the
        // offside-open tokens are ruled out — so that `let x = 2.` still emits
        // LET_BODY_OPEN first (the scanner is re-invoked at the same position
        // once that zero-width token is consumed, and only then is the float
        // the sole valid symbol).
        if (valid_symbols[FLOAT_TRAILING_DOT]) return scan_trailing_dot_float(lexer);
        return false;
    }

    // BRACKET_OPEN: emitted right after `[` / `[|` when the list/array body is on
    // the next line(s). Records the first element's column as a K_BRACKET entry
    // so BRACKET_SEP can fire between newline-aligned elements. K_BRACKET does
    // NOT feed `current` (the VIRTUAL_SEMI baseline), so it can't ripple into the
    // separation of inner bodies. Same-line content (`[ a; b ]`) means an inline
    // list — fall through to the grammar's inline `;`-separated branch.
    if (want_bracket_open) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            lexer->advance(lexer, true);
        }
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            lexer->mark_end(lexer); // zero-width at the `[` line
            uint32_t bcol = 0;
            // Don't open for an immediately-closing `]` (empty multi-line list)
            // or EOF — let the close/grammar handle it. An empty multi-line
            // array `[|⏎|]` is rare; it falls through harmlessly.
            if (next_line_indent(lexer, &bcol, NULL) && lexer->lookahead != ']') {
                idx_push(&s->stk, bcol, K_BRACKET);
                lexer->result_symbol = BRACKET_OPEN;
                return true;
            }
            return false;
        }
        // Same-line content: inline list/array. Fall through.
    }

    // CE_BODY_OPEN: emitted right after `builder {` when the computation-expression
    // body is on the next line(s). Records the statement column as a K_CE entry so
    // CE_SEP can fire between newline-aligned statements. EXACTLY like BRACKET_OPEN:
    // K_CE does NOT feed `current` (the VIRTUAL_SEMI baseline), so the dedicated
    // CE_SEP is the only thing that separates statements — a binding's trailing
    // `$._expression` can't steal it, and there's no VIRTUAL_SEMI competition (which
    // is what broke the K_INDENT approach). Same-line content (`builder { return x }`)
    // is an inline CE — fall through to the grammar's inline branch.
    if (want_ce_body_open) {
        while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
            lexer->advance(lexer, true);
        }
        if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
            lexer->mark_end(lexer); // zero-width at the `{` line
            uint32_t bcol = 0;
            // Don't open for an immediately-closing `}` (empty multi-line CE) or EOF.
            if (next_line_indent(lexer, &bcol, NULL) && lexer->lookahead != '}') {
                idx_push(&s->stk, bcol, K_CE);
                lexer->result_symbol = CE_BODY_OPEN;
                return true;
            }
            return false;
        }
        // Same-line content: inline CE. Fall through.
    }

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
            idx_push(&s->stk, body_col, K_INLINE);
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
            idx_push(&s->stk, inline_body_baseline(&s->stk), K_LET_BODY);
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
            idx_push(&s->stk, inline_body_baseline(&s->stk), K_MATCH_BODY);
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
                idx_push(&s->stk, col, K_RECORD);
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
            if (want_record_body_close && idx_has(&s->stk, K_RECORD)) {
                if (lexer->lookahead == '}') {
                    idx_pop(&s->stk, K_RECORD);
                    lexer->result_symbol = RECORD_BODY_CLOSE;
                    return true;
                }
                if (lexer->lookahead == '|') {
                    lexer->advance(lexer, true);
                    if (lexer->lookahead == '}') {
                        idx_pop(&s->stk, K_RECORD);
                        lexer->result_symbol = RECORD_BODY_CLOSE;
                        return true;
                    }
                }
            }
            // Mid-line MATCH_BODY_CLOSE before a closing delimiter `)`/`]`/`}`
            // on the last arm's line — e.g. `(fun k -> match k with | A -> a
            // | B -> b)` where the `)` closes the enclosing paren right after the
            // final arm's inline body. The inline arm body (K_MATCH_BODY) is the
            // innermost open construct, so it must close BEFORE the delimiter (and
            // before any enclosing bracket / body dedent). Checked ahead of the
            // bracket/`)` closes below so the arm body unwinds first.
            if (want_match_body_close && idx_has(&s->stk, K_MATCH_BODY) &&
                (lexer->lookahead == ')' || lexer->lookahead == ']' ||
                 lexer->lookahead == '}')) {
                idx_pop(&s->stk, K_MATCH_BODY);
                lexer->result_symbol = MATCH_BODY_CLOSE;
                return true;
            }
            // Mid-line `]`/`|]` closing a multi-line bracket body whose `]` sits
            // on the last element's line (`[`⏎`a`⏎`b ]`).
            if (want_bracket_close && idx_has(&s->stk, K_BRACKET)) {
                if (lexer->lookahead == ']') {
                    idx_pop(&s->stk, K_BRACKET);
                    lexer->result_symbol = BRACKET_CLOSE;
                    return true;
                }
                if (lexer->lookahead == '|') {
                    lexer->advance(lexer, true);
                    if (lexer->lookahead == ']') {
                        idx_pop(&s->stk, K_BRACKET);
                        lexer->result_symbol = BRACKET_CLOSE;
                        return true;
                    }
                }
            }
            // Mid-line MATCH_BODY_CLOSE before a new `| …` arm on the SAME
            // line — e.g. single-line `function | 0 -> "a" | _ -> "b"`. A
            // bare `|` that ISN'T `|>` (pipe), `||` (or), or `|}` (anon-record
            // close) starts the next arm, so the inline arm body must close
            // first. The `}` case (anon-record) is handled by RECORD above.
            if (want_match_body_close && idx_has(&s->stk, K_MATCH_BODY) &&
                lexer->lookahead == '|') {
                lexer->advance(lexer, true);
                int32_t after = lexer->lookahead;
                if (after != '>' && after != '|' && after != '}') {
                    idx_pop(&s->stk, K_MATCH_BODY);
                    lexer->result_symbol = MATCH_BODY_CLOSE;
                    return true;
                }
            }
            // Mid-line CE_BODY_CLOSE before a closing `}` on the last statement's
            // line (`builder {`⏎`  return x }`). Pops the CE body's K_CE column.
            if (want_ce_body_close && idx_has(&s->stk, K_CE) &&
                lexer->lookahead == '}') {
                idx_pop(&s->stk, K_CE);
                lexer->result_symbol = CE_BODY_CLOSE;
                return true;
            }
            // Mid-line FOR_BODY_CLOSE before a closing `}`/`)` — e.g.
            // `seq { for x in xs do yield x }` with the `}` on the same line
            // as the last body statement. Goes before BODY_DEDENT so the for
            // body's own close fires first.
            if (want_for_body_close && idx_has(&s->stk, K_INDENT) &&
                (lexer->lookahead == '}' || lexer->lookahead == ')')) {
                idx_pop(&s->stk, K_INDENT);
                lexer->result_symbol = FOR_BODY_CLOSE;
                return true;
            }
            // Mid-line non-whitespace ahead. If it's a recognised closing
            // delimiter and the parser is expecting BODY_DEDENT (i.e. we are
            // inside an indented body that needs to close before the
            // delimiter), pop one indent level and emit BODY_DEDENT.
            if (want_body_dedent && idx_has(&s->stk, K_INDENT)) {
                if (lexer->lookahead == '}' || lexer->lookahead == ')') {
                    idx_pop(&s->stk, K_INDENT);
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
                                idx_pop(&s->stk, K_INDENT);
                                lexer->result_symbol = BODY_DEDENT;
                                return true;
                            }
                        }
                    }
                }
            }
            // Mid-line trailing-dot float as an application argument / binary
            // operand (`f 2.`, `x + 2.`): here an offside CLOSE token (e.g.
            // LET_BODY_CLOSE) is also valid, so the early-out above was skipped
            // — but mid-line none of those fire, so the float is the real next
            // token. (The whitespace before the literal was already consumed
            // above; the helper re-skips defensively.)
            if (valid_symbols[FLOAT_TRAILING_DOT] && scan_trailing_dot_float(lexer)) {
                return true;
            }
            return false;
        }
    }

    uint32_t col = 0;
    int32_t line_first = 0; // first SIGNIFICANT char of the next line (see next_line_indent)
    if (!next_line_indent(lexer, &col, &line_first)) {
        // EOF: close any open block. LET_BODY_CLOSE / INLINE_CLOSE first so
        // inline-bodied lets close before any surrounding BODY_/INDENT blocks.
        if (want_let_body_close && idx_has(&s->stk, K_LET_BODY)) {
            idx_pop(&s->stk, K_LET_BODY);
            lexer->result_symbol = LET_BODY_CLOSE;
            return true;
        }
        if (want_match_body_close && idx_has(&s->stk, K_MATCH_BODY)) {
            idx_pop(&s->stk, K_MATCH_BODY);
            lexer->result_symbol = MATCH_BODY_CLOSE;
            return true;
        }
        if (want_for_body_close && idx_has(&s->stk, K_INDENT)) {
            idx_pop(&s->stk, K_INDENT);
            lexer->result_symbol = FOR_BODY_CLOSE;
            return true;
        }
        if (want_inline_close && idx_has(&s->stk, K_INLINE)) {
            idx_pop(&s->stk, K_INLINE);
            lexer->result_symbol = INLINE_CLOSE;
            return true;
        }
        if (want_record_body_close && idx_has(&s->stk, K_RECORD)) {
            idx_pop(&s->stk, K_RECORD);
            lexer->result_symbol = RECORD_BODY_CLOSE;
            return true;
        }
        if (want_bracket_close && idx_has(&s->stk, K_BRACKET)) {
            idx_pop(&s->stk, K_BRACKET);
            lexer->result_symbol = BRACKET_CLOSE;
            return true;
        }
        if (want_ce_body_close && idx_has(&s->stk, K_CE)) {
            idx_pop(&s->stk, K_CE);
            lexer->result_symbol = CE_BODY_CLOSE;
            return true;
        }
        if (idx_has(&s->stk, K_INDENT)) {
            if (want_dedent) {
                idx_pop(&s->stk, K_INDENT);
                lexer->result_symbol = DEDENT;
                return true;
            }
            if (want_body_dedent) {
                idx_pop(&s->stk, K_INDENT);
                lexer->result_symbol = BODY_DEDENT;
                return true;
            }
        }
        return false;
    }

    uint32_t current = idx_top(&s->stk, K_INDENT);

    // Peek the next line's leading 1-2 significant chars ONCE. The lexer can't
    // rewind, so every decision below reads these flags rather than re-advancing
    // (a re-peek would consume the char and corrupt the checks that follow). Two
    // classifications:
    //   bar_arm        — a real `| pat` arm separator: a `|` that ISN'T part of
    //                    `|>` (pipe), `||` (or), or `|}` (anon-record close).
    //   infix_continue — the next line begins with an INFIX operator, so it
    //                    continues the previous expression (see the block below).
    // `line_first` (from next_line_indent), NOT `lexer->lookahead`: the comment
    // probe may have advanced past a lone `(` / `/`, so lexer->lookahead can be the
    // SECOND char (e.g. `)` for a line starting `()`), which would wrongly read as a
    // separator-blocker. The follow-up la1 peek below only fires for operator leads
    // (`|`/`&`/`:`), which are never consumed by the probe, so they stay aligned.
    int32_t la0 = line_first;
    bool bar_arm = false, infix_continue = false;
    // A line that BEGINS with an infix operator continues the previous
    // expression (F#'s leading-infix rule), so the scanner must not close a
    // body or start a virtual-semi statement before it. This generalises the
    // old pipe-only handling (`|>` `<|` `>>` `<<`) to every infix lead:
    // comparison/boolean (`<` `<=` `>` `>=` `=` `<>` `&&` `||`), arithmetic
    // (`*` `/` `%` `^`), cons (`::`), and the pipe/compose family.
    //   - `|` is special: a BARE `|` is a match arm (bar_arm); only `|>`/`||`
    //     are infix and `|}` closes an anonymous record.
    //   - `&` and `:` count only DOUBLED (`&&` / `::`) so a leading address-of
    //     `&x` or type annotation `:` is not mistaken for a continuation.
    //   - Unary-capable leads (`-` `+` `!` `~` `@`) are deliberately excluded.
    if (la0 == '|' || la0 == '<' || la0 == '>' || la0 == '=' ||
        la0 == '*' || la0 == '/' || la0 == '%' || la0 == '^' ||
        la0 == '&' || la0 == ':') {
        lexer->advance(lexer, true);
        int32_t la1 = lexer->lookahead;
        if (la0 == '|') {
            if (la1 == '>' || la1 == '|') infix_continue = true;  // |>  ||
            else if (la1 != '}') bar_arm = true;                  // | pat (not |})
        } else if (la0 == '&') {
            if (la1 == '&') infix_continue = true;                // && (not & address-of)
        } else if (la0 == ':') {
            // `::` cons, `:>` upcast, `:?>` downcast — all continue the previous
            // expression on a leading-operator line. A bare `:` (type annotation)
            // does NOT.
            if (la1 == ':' || la1 == '>' || la1 == '?') infix_continue = true;
        } else {
            infix_continue = true;  // = < > * / % ^  (unambiguously infix here)
        }
    }

    // MATCH_ARM_SEP: next line starts a continuation `| …` arm (not `|>` /
    // `||` / `|}`) and the grammar is mid arm-list (so MATCH_ARM_SEP is a
    // valid symbol — it only is right after a COMPLETE arm). Emit it BEFORE
    // any close token: a `|` continues the match/function, so the enclosing
    // let body / indented block must NOT close here. This keeps a
    // non-indented `let f = function` (arms at the enclosing column, e.g.
    // inside a module) open across all its arms. The grammar's validity gate
    // means this never fires mid-arm-body (an own-line arm body's BODY_DEDENT
    // closes first; only then is the arm complete and MATCH_ARM_SEP valid).
    // EXCEPT a `|` that has dedented BELOW the current indented body
    // (`col < current`) while a dedent is also pending: that `|` belongs to an
    // OUTER match, not the innermost one — the inner match's containing arm body
    // must reduce (BODY_DEDENT) first, and the outer match then gets its
    // MATCH_ARM_SEP on a later scan call. A genuine next arm of THIS match sits
    // at the body indent (`col == current`), so it is unaffected. Without this
    // guard a shallower `|` is greedily absorbed as another arm of the innermost
    // match (e.g. a doubly-nested `match`), leaving the inner arm body's indent
    // un-popped and corrupting the offside baseline for a later own-line `else`.
    if (want_match_arm_sep && bar_arm &&
        !(col < current && (want_dedent || want_body_dedent))) {
        lexer->result_symbol = MATCH_ARM_SEP;
        return true;
    }

    // LET_BODY_CLOSE: any line at column <= the recorded enclosing indent
    // ends the inline let-binding body. Same shape as INLINE_CLOSE but
    // separate stack (and a different pushed value — enclosing indent, not
    // body's first-token column).
    //
    // EXCEPT a new `| …` arm (not `|>` / `||` / `|}`): when the inline body
    // is a `match`/`function` whose arms sit at the LET column (non-indented,
    // `let f = function\n| A -> …\n| B -> …`), each arm's `|` is a
    // continuation of the body, not the end of it — so don't close here.
    // The function/match keeps consuming arms; the let body closes later at
    // a non-`|` line (or EOF).
    if (want_let_body_close && idx_has(&s->stk, K_LET_BODY)) {
        uint32_t body_col = idx_top(&s->stk, K_LET_BODY);
        // A leading pipe/composition operator continues the inline body as a
        // binary expression (`let f = function | A -> a |> g`), so don't close.
        if (infix_continue) return false;
        if (col <= body_col && !bar_arm) {
            idx_pop(&s->stk, K_LET_BODY);
            lexer->result_symbol = LET_BODY_CLOSE;
            return true;
        }
        // Inside a computation expression, `body_col` (the enclosing indent)
        // under-shoots the real statement column, so the test above misses and
        // the inline let body would greedily absorb the next CE statement. A line
        // at EXACTLY the CE statement column (K_CE) starts a NEW statement, so
        // close the let body there — generalising the old `let!`/`do!`-only
        // band-aid to every statement start (`match`, `if`, a plain expression,
        // …). `bar_arm` is excluded so an inline `let f = function`'s own arms
        // (at the CE column) keep extending the body.
        if (col > body_col && !bar_arm &&
            idx_has(&s->stk, K_CE) && col == idx_top(&s->stk, K_CE)) {
            idx_pop(&s->stk, K_LET_BODY);
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
    if (want_match_body_close && idx_has(&s->stk, K_MATCH_BODY)) {
        uint32_t body_col = idx_top(&s->stk, K_MATCH_BODY);
        // A leading pipe/composition operator (`|>` `<|` `>>` `<<`) at or below
        // the arm column continues the LAST arm body as a binary expression
        // rather than closing the match — e.g.
        //   match x with
        //   | A -> a
        //   | B -> b
        //   |> g            (at the arm column → pipes the whole result)
        // Closing here would strand the `|>` with no left operand. Letting the
        // arm body absorb it keeps the parse valid (and the highlighting
        // correct); a genuine non-pipe dedent still closes the match below.
        if (infix_continue) return false;
        if (col <= body_col || bar_arm) {
            idx_pop(&s->stk, K_MATCH_BODY);
            lexer->result_symbol = MATCH_BODY_CLOSE;
            return true;
        }
    }

    // CE_BODY_CLOSE: next-line `}` (possibly at lower indent) closes the
    // multi-line CE body. Pops the K_CE column. Mirrors BRACKET_CLOSE below.
    if (want_ce_body_close && idx_has(&s->stk, K_CE) && lexer->lookahead == '}') {
        idx_pop(&s->stk, K_CE);
        lexer->result_symbol = CE_BODY_CLOSE;
        return true;
    }

    // RECORD_BODY_CLOSE: next-line `}` or `|}` (possibly at lower indent)
    // closes the inline record body. The mid-line case is handled above.
    if (want_record_body_close && idx_has(&s->stk, K_RECORD)) {
        if (lexer->lookahead == '}') {
            idx_pop(&s->stk, K_RECORD);
            lexer->result_symbol = RECORD_BODY_CLOSE;
            return true;
        }
        if (lexer->lookahead == '|') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == '}') {
                idx_pop(&s->stk, K_RECORD);
                lexer->result_symbol = RECORD_BODY_CLOSE;
                return true;
            }
        }
    }

    // BRACKET_CLOSE: next-line `]` (list) or `|]` (array), at the dedented close
    // of a multi-line bracket body.
    if (want_bracket_close && idx_has(&s->stk, K_BRACKET)) {
        if (lexer->lookahead == ']') {
            idx_pop(&s->stk, K_BRACKET);
            lexer->result_symbol = BRACKET_CLOSE;
            return true;
        }
        if (lexer->lookahead == '|') {
            lexer->advance(lexer, true);
            if (lexer->lookahead == ']') {
                idx_pop(&s->stk, K_BRACKET);
                lexer->result_symbol = BRACKET_CLOSE;
                return true;
            }
        }
    }

    // INLINE_CLOSE: any line at column <= the recorded body column ends the inline
    // body (sibling let, continuation expression, or end of enclosing block).
    if (want_inline_close && idx_has(&s->stk, K_INLINE)) {
        uint32_t body_col = idx_top(&s->stk, K_INLINE);
        if (col <= body_col) {
            idx_pop(&s->stk, K_INLINE);
            lexer->result_symbol = INLINE_CLOSE;
            return true;
        }
    }

    // Indent — push a new block.
    // BODY_INDENT is preferred over INDENT so that at the body position of a
    // module-level let_binding, the parser commits to let_binding rather than
    // exploring let_decl_indented (which would fail without _indent).
    // Uses cur_col (K_CE-aware) so a multi-line application/body inside a CE
    // statement stops at the next statement column instead of chaining it in.
    if (col > cur_col(&s->stk)) {
        // FOR_BODY_OPEN: a real `for … do` loop body on the next, more-indented
        // line. Pushes the body column onto `indents` (like BODY_INDENT) so
        // `_virtual_semi` sequences multi-statement bodies. Suppressed when the
        // body's first word is a query-CE operator (`where`/`select`/… or a
        // chained `for`) — then this isn't a loop body, it's a query clause, so
        // we fall through and the for_expression's empty-body branch lets the
        // operator parse as a `query_operator` sibling. Checked before
        // BODY_INDENT so the for body commits to the sequencing form.
        if (want_for_body_open && !next_word_blocks_for_body(lexer)) {
            idx_push(&s->stk, col, K_INDENT);
            lexer->result_symbol = FOR_BODY_OPEN;
            return true;
        }
        if (want_body_indent) {
            idx_push(&s->stk, col, K_INDENT);
            lexer->result_symbol = BODY_INDENT;
            return true;
        }
        if (want_indent) {
            idx_push(&s->stk, col, K_INDENT);
            lexer->result_symbol = INDENT;
            return true;
        }
    }

    // Dedent — pop. DEDENT is preferred over BODY_DEDENT so inner let_decl_indented
    // bodies close before the outer let_binding body.
    //
    // BUT: a line that starts with a leading pipe/composition operator
    // (`|>` `<|` `>>` `<<`) continues the previous expression — F# allows it
    // to sit at a lower indent than the expression body. Don't close the body
    // here; fall through so the operator is lexed and the binary_expression
    // extends. The body closes later at a line that genuinely dedents without
    // a leading operator. Only suppress while a virtual-semi / close is what
    // we'd otherwise emit (an expression context), never the for-body close.
    if (col < current && !want_for_body_close &&
        (want_dedent || want_body_dedent) &&
        infix_continue && !has_indent_le(&s->stk, col)) {
        return false;
    }
    if (col < current) {
        // FOR_BODY_CLOSE mirrors FOR_BODY_OPEN: pop the for-body indent when a
        // line dedents below it. Checked first so a `for` body closes before
        // any enclosing BODY_/INDENT block at the same boundary.
        if (want_for_body_close) {
            idx_pop(&s->stk, K_INDENT);
            lexer->result_symbol = FOR_BODY_CLOSE;
            return true;
        }
        if (want_dedent) {
            idx_pop(&s->stk, K_INDENT);
            lexer->result_symbol = DEDENT;
            return true;
        }
        if (want_body_dedent) {
            idx_pop(&s->stk, K_INDENT);
            // A `with` type-augmentation following the (indented) body, aligned
            // BELOW the body column — the common F# style where `with` sits at
            // the `type` keyword's column. Emit the dedicated TYPE_AUGMENT_DEDENT
            // so the grammar routes into the augmentation branch; otherwise the
            // plain BODY_DEDENT lets `type_decl` reduce early and the members
            // detach (e.g. when `with` lands at the module body column).
            if (want_type_augment_dedent && peek_with(lexer))
                lexer->result_symbol = TYPE_AUGMENT_DEDENT;
            else
                lexer->result_symbol = BODY_DEDENT;
            return true;
        }
    }

    // Same-column type-augmentation: `type T = body \n    with` where the `with`
    // sits at EXACTLY the (indented) body column. The `with` continues the
    // enclosing `type_decl`, so the inner body's dedent must fire before the
    // parser can match the `with`. The standard "col < current" rule wouldn't
    // trigger here because the `with` is at the body column. Emit
    // TYPE_AUGMENT_DEDENT so the augmentation members stay attached.
    //
    // Gated on `want_type_augment_dedent` ALONE (not the broader
    // `want_body_dedent`): that token is only valid when the grammar entered the
    // indented `_type_decl_body_aug` branch, i.e. the K_INDENT at `current` is
    // genuinely the type body's. With an INLINE body (`type T = T of int`
    // followed by `with` at the type's column) no type-body indent was pushed —
    // the K_INDENT at `current` belongs to the ENCLOSING module/body, so popping
    // it here would wrongly close that block and detach the `with`. In that case
    // we fall through (VIRTUAL_SEMI is already blocked before `with`) and let the
    // parser shift `with` straight into `type_decl`'s optional augmentation.
    if (col == current && want_type_augment_dedent &&
        idx_has(&s->stk, K_INDENT) && peek_with(lexer)) {
        idx_pop(&s->stk, K_INDENT);
        lexer->result_symbol = TYPE_AUGMENT_DEDENT;
        return true;
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
    bool at_record_field_col = idx_has(&s->stk, K_RECORD) &&
                               col == idx_top(&s->stk, K_RECORD);
    if (want_record_field_semi && at_record_field_col) {
        // Apply the same blocker checks as VIRTUAL_SEMI (closing delimiters
        // and continuation keywords must NOT trigger a separator). `la0` is the
        // original first char (the precompute above already advanced the lexer
        // past it); `infix_continue` covers a leading `<|`/`>>`/`<<` continuation.
        int32_t c = la0;
        bool blocked = (c == '|' || c == ')' || c == ']' || c == '}' || infix_continue);
        if (!blocked) {
            lexer->result_symbol = RECORD_FIELD_SEMI;
            return true;
        }
    }

    // BRACKET_SEP: separator between newline-aligned list/array elements, fired
    // at the captured K_BRACKET column. Like RECORD_FIELD_SEMI it is DEDICATED —
    // a `sequence_expression` nested in an element (e.g. a multi-line lambda
    // body) cannot shift it, so the element reduces out and the next element
    // starts instead of chaining. Reached only after the dedent logic above, so
    // an element's own inner bodies close before the separator fires.
    bool at_bracket_col = idx_has(&s->stk, K_BRACKET) &&
                          col == idx_top(&s->stk, K_BRACKET);
    if (want_bracket_sep && at_bracket_col) {
        int32_t c = la0;
        bool blocked = (c == '|' || c == ')' || c == ']' || c == '}' || infix_continue);
        if (!blocked) {
            lexer->result_symbol = BRACKET_SEP;
            return true;
        }
    }

    // CE_SEP: separator between newline-aligned computation-expression statements,
    // fired at the CE body column (== `current`, since CE_BODY_OPEN pushed it as a
    // K_INDENT). DEDICATED like BRACKET_SEP and checked BEFORE VIRTUAL_SEMI, so a
    // binding statement's trailing `$._expression` (e.g. `let! x = e`, `do! e`)
    // can't absorb the next statement into a sequence_expression — the expression
    // can't shift CE_SEP, so it reduces and the statement ends. Blocked by `|`
    // (a `match` arm inside the CE keeps consuming arms), the closers, and a
    // leading-infix continuation. Reached after the dedent logic, so a statement's
    // own inner bodies (let/match) close before the separator fires.
    bool at_ce_col = idx_has(&s->stk, K_CE) && col == idx_top(&s->stk, K_CE);
    if (want_ce_sep && at_ce_col) {
        int32_t c = la0;
        bool blocked = (c == '|' || c == ')' || c == ']' || c == '}' || infix_continue);
        // Block before a continuation keyword that belongs to the CURRENT
        // statement rather than starting a new one. A CE_SEP here would split
        // the construct and detach the tail:
        //   - `else`/`elif`/`then`: an `if … then return x` whose `else`/`elif`
        //     sits at the CE column — without this the branch detaches (the
        //     `if` reduces with only the then-branch and `else …` parses as a
        //     stray statement, erroring on a `return!`/`do!` bang body).
        //   - `with`/`finally`: continue a `try`/`match`.
        //   - `in`: a `let … in` continuation.
        //   - `and`/`and!`: a `let!`/`and!` applicative group or `let … and …`.
        // None of these words ever START a CE statement, so blocking is safe.
        // (`la0`/the peeked chars are safe to consume — CE_SEP is zero-width and
        // `mark_end` was already set.)
        if (!blocked && c >= 'a' && c <= 'z') {
            int32_t buf[10];
            size_t n = 0;
            int32_t look = lexer->lookahead;
            while (n < 9 && ((look >= 'a' && look <= 'z') || (look >= 'A' && look <= 'Z') ||
                             (look >= '0' && look <= '9') || look == '_' || look == '\'')) {
                buf[n++] = look;
                lexer->advance(lexer, true);
                look = lexer->lookahead;
            }
            static const char *ce_cont[] = {
                "else", "elif", "then", "with", "finally", "in", "and", NULL,
            };
            for (const char **k = ce_cont; *k; k++) {
                size_t kl = strlen(*k);
                if (kl != n) continue;
                bool eq = true;
                for (size_t j = 0; j < kl; j++)
                    if (buf[j] != (int32_t)(*k)[j]) { eq = false; break; }
                if (eq) { blocked = true; break; }
            }
        }
        if (!blocked) {
            lexer->result_symbol = CE_SEP;
            return true;
        }
    }

    bool semi_col_match = (idx_has(&s->stk, K_INDENT) && col == current);
    if (want_virtual_semi && semi_col_match) {
        // `la0` is the original first char (the precompute above already
        // advanced the lexer past it for `|`/`<`/`>` lines); `infix_continue` blocks a
        // leading `<|`/`>>`/`<<` that continues the previous expression.
        int32_t c = la0;
        bool blocked = (c == '|' || c == ')' || c == ']' || c == '}' || infix_continue);
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
            // sibling declaration inside a class/module body (member/type/
            // etc.) must NOT trigger a virtual_semi — otherwise the parser
            // would commit to an expression-sequence continuation it can't
            // recover from.
            //
            // `let` is deliberately NOT a blocker: inside an expression body
            // (`foo ()` then `let x = …`), the `let` starts a sequence
            // continuation (a `let_expression` whose continuation is the rest
            // of the body), so virtual_semi MUST fire before it. In a
            // class/module body the lets are separate declarations that don't
            // go through virtual_semi at all (they're `_token` /
            // `_class_body_member` repeats), so omitting `let` here is safe —
            // confirmed by the corpus.
            static const char *blockers[] = {
                // Continuation keywords for the enclosing construct
                "else", "elif", "with", "then", "do", "in", "and", "finally", "of",
                // Closes a `class`/`struct`/`interface` block
                "end",
                // Class-body sibling declaration starters
                "member", "abstract", "override", "default", "inherit",
                "interface", "val", "new", "static",
                // Module-body / top-level declaration starters
                "type", "module", "namespace", "exception", "open",
                NULL,
            };
            // `do!` is a CE statement (bang form), NOT the `do` that continues a
            // `for … do` / `while … do` loop — so it must NOT block the
            // virtual-semi separating it from the previous statement (e.g. a
            // `use x = e` whose value would otherwise absorb the `do!` line).
            bool do_bang = (i == 2 && buf[0] == 'd' && buf[1] == 'o' && look == '!');
            for (const char **k = blockers; *k; k++) {
                size_t kl = strlen(*k);
                if (kl != i) continue;
                if (do_bang && kl == 2 && (*k)[0] == 'd' && (*k)[1] == 'o') continue;
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
