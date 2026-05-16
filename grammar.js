/**
 * @file F# tree sitter definition focused on Helix
 * @author Mangel Maxime
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

export default grammar({
  name: "fsharp",

  word: $ => $.identifier,

  extras: $ => [/\s+/],

  rules: {
    source_file: $ => repeat($._token),

    import_decl: ($) => seq("open", optional("type"), $.long_identifier),

    _token: $ => choice(
      // $.keyword,
      $.import_decl,
      $.identifier,
      $.line_comment,
      /[0-9][0-9_]*/,
      /[^\s\w]+/,
    ),

    // keyword: _ => choice(...KEYWORDS),

    identifier: _ => /[a-zA-Z_][a-zA-Z0-9_']*/,

    long_identifier: $ =>
      prec.right(
        seq($.identifier, repeat(seq(".", $.identifier))),
      ),

    line_comment: _ => token(seq('//', /.*/)),
  }
});
