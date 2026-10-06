//! This crate provides F# language support for the [tree-sitter] parsing library.
//!
//! It contains two grammars:
//!
//! - [`LANGUAGE_FSHARP`] for implementation files (`.fs`, `.fsx`).
//! - [`LANGUAGE_FSHARP_SIGNATURE`] for signature files (`.fsi`).
//!
//! Add a language to a tree-sitter [`Parser`], and then use the parser to parse some code:
//!
//! ```
//! let code = r#"
//! let add x y = x + y
//! "#;
//! let mut parser = tree_sitter::Parser::new();
//! let language = tree_sitter_fsharp_mangel::LANGUAGE_FSHARP;
//! parser
//!     .set_language(&language.into())
//!     .expect("Error loading F# parser");
//! let tree = parser.parse(code, None).unwrap();
//! assert!(!tree.root_node().has_error());
//! ```
//!
//! [`Parser`]: https://docs.rs/tree-sitter/0.26.9/tree_sitter/struct.Parser.html
//! [tree-sitter]: https://tree-sitter.github.io/

use tree_sitter_language::LanguageFn;

extern "C" {
    fn tree_sitter_fsharp() -> *const ();
    fn tree_sitter_fsharp_signature() -> *const ();
}

/// The tree-sitter [`LanguageFn`] for F# implementation files.
pub const LANGUAGE_FSHARP: LanguageFn = unsafe { LanguageFn::from_raw(tree_sitter_fsharp) };

/// The tree-sitter [`LanguageFn`] for F# signature files.
pub const LANGUAGE_FSHARP_SIGNATURE: LanguageFn =
    unsafe { LanguageFn::from_raw(tree_sitter_fsharp_signature) };

/// The content of the [`node-types.json`] file for F# implementation files.
///
/// [`node-types.json`]: https://tree-sitter.github.io/tree-sitter/using-parsers/6-static-node-types
pub const FSHARP_NODE_TYPES: &str = include_str!("../../src/node-types.json");

/// The content of the [`node-types.json`] file for F# signature files.
///
/// [`node-types.json`]: https://tree-sitter.github.io/tree-sitter/using-parsers/6-static-node-types
pub const FSHARP_SIGNATURE_NODE_TYPES: &str = include_str!("../../signature/src/node-types.json");

/// The syntax highlighting query for F# implementation files.
pub const FSHARP_HIGHLIGHTS_QUERY: &str = include_str!("../../queries/highlights.scm");

/// The language injection query for F# implementation files.
pub const FSHARP_INJECTIONS_QUERY: &str = include_str!("../../queries/injections.scm");

/// The local variable query for F# implementation files.
pub const FSHARP_LOCALS_QUERY: &str = include_str!("../../queries/locals.scm");

/// The symbol tagging query for F# implementation files.
pub const FSHARP_TAGS_QUERY: &str = include_str!("../../queries/tags.scm");

/// The syntax highlighting query for F# signature files.
pub const FSHARP_SIGNATURE_HIGHLIGHTS_QUERY: &str =
    include_str!("../../queries/signature/highlights.scm");

/// The language injection query for F# signature files.
pub const FSHARP_SIGNATURE_INJECTIONS_QUERY: &str =
    include_str!("../../queries/signature/injections.scm");

/// The local variable query for F# signature files.
pub const FSHARP_SIGNATURE_LOCALS_QUERY: &str = include_str!("../../queries/signature/locals.scm");

/// The symbol tagging query for F# signature files.
pub const FSHARP_SIGNATURE_TAGS_QUERY: &str = include_str!("../../queries/signature/tags.scm");

#[cfg(test)]
mod tests {
    #[test]
    fn test_can_load_grammar() {
        let mut parser = tree_sitter::Parser::new();
        parser
            .set_language(&super::LANGUAGE_FSHARP.into())
            .expect("Error loading F# parser");
    }

    #[test]
    fn test_can_load_signature_grammar() {
        let mut parser = tree_sitter::Parser::new();
        parser
            .set_language(&super::LANGUAGE_FSHARP_SIGNATURE.into())
            .expect("Error loading F# signature parser");
    }

    #[test]
    fn test_queries_compile() {
        let queries = [
            (super::LANGUAGE_FSHARP, super::FSHARP_HIGHLIGHTS_QUERY),
            (super::LANGUAGE_FSHARP, super::FSHARP_INJECTIONS_QUERY),
            (super::LANGUAGE_FSHARP, super::FSHARP_LOCALS_QUERY),
            (super::LANGUAGE_FSHARP, super::FSHARP_TAGS_QUERY),
            (super::LANGUAGE_FSHARP_SIGNATURE, super::FSHARP_SIGNATURE_HIGHLIGHTS_QUERY),
            (super::LANGUAGE_FSHARP_SIGNATURE, super::FSHARP_SIGNATURE_INJECTIONS_QUERY),
            (super::LANGUAGE_FSHARP_SIGNATURE, super::FSHARP_SIGNATURE_LOCALS_QUERY),
            (super::LANGUAGE_FSHARP_SIGNATURE, super::FSHARP_SIGNATURE_TAGS_QUERY),
        ];
        for (language, source) in queries {
            tree_sitter::Query::new(&language.into(), source).expect("Error compiling query");
        }
    }
}
