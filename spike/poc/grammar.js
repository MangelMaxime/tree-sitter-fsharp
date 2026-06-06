// Minimal PoC for the uniform-layout model. Captures F# layout essence:
//  - blocks open after `=` / `then` / `else` (grammar-driven `_open`)
//  - statements separated by `_semi` at equal indent
//  - blocks close by `_end` at lesser indent (multi-level), gated valid()
module.exports = grammar({
  name: 'layoutpoc',
  externals: $ => [$.error_sentinel, $._open, $._semi, $._end],
  extras: $ => [/[ \t]/, /\r?\n/],
  conflicts: $ => [],
  rules: {
    // Top level is itself a block (the file's layout context).
    source_file: $ => seq($._open, sep1($._semi, $._stmt), $._end),

    _stmt: $ => choice($.let_decl, $._expr),

    let_decl: $ => seq('let', $.id, '=', $._body),

    // The ONE body shape: open, expr, end. No inline/own-line variants.
    _body: $ => seq($._open, sep1($._semi, $._stmt), $._end),

    _expr: $ => choice($.if_expr, $.id, $.num),

    // if/then/else — each branch is a block (Then sort). `else` is an inline ender.
    if_expr: $ => prec.right(seq('if', $._expr, 'then', $._body, 'else', $._body)),


    id: $ => /[a-z_][a-zA-Z0-9_]*/,
    num: $ => /[0-9]+/,
  }
});
function sep1(sep, rule) { return seq(rule, repeat(seq(sep, rule))); }
