module Docs.Site

open Feliz.ViewEngine
open Nacara.Core
open Nacara.Plugins
open Nacara.Theme

let theme =
    Theme.defaults
    |> Theme.navbar
        [
            NavbarSection("Users", "users", "users/helix.md")
            NavbarSection("Contributors", "contributors", "contributors/getting-started.md")
        ]
    |> Theme.navbarEnd
        [
            NavbarDynamicWidget Search.trigger
            NavbarIcon("GitHub", "https://github.com/MangelMaxime/tree-sitter-fsharp", Icons.github)
        ]
    |> Theme.editUrl "https://github.com/MangelMaxime/tree-sitter-fsharp/edit/main/docs"
    |> Theme.footer (Html.p [ Html.text "Apache 2.0" ])

// The build script sets the commit so code blocks are coloured by this repository's grammar.
let grammar =
    match System.Environment.GetEnvironmentVariable "TREE_SITTER_FSHARP_COMMIT" with
    | null
    | "" -> []
    | commit ->
        [ TreeSitter.fromGitHub "fsharp" "https://github.com/MangelMaxime/tree-sitter-fsharp" commit ]

let site =
    Site.create "Tree-sitter for F#"
    |> Site.description "An F# grammar for tree-sitter, for Helix, Zed and Neovim"
    |> Site.baseUrl "/tree-sitter-fsharp/"
    |> Site.origin "https://mangelmaxime.github.io"
    |> Site.output "output"
    |> Site.staticFiles "static"
    |> Site.stylesheet "assets/landing.css"
    |> Markdown.register
    |> TextMate.register
    |> TreeSitter.registerWith (fun options -> { options with Grammars = grammar })
    |> Search.register
    |> Sitemap.register
    |> LightningCss.register
    |> Nuglify.minifyHtml
    |> Nuglify.minifyJs
    |> Theme.register theme
    |> Site.collection (Theme.docs theme "content")

[<EntryPoint>]
let main argv = Nacara.run site argv
