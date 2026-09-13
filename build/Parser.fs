/// The only place a parser is loaded. Every source gets a fresh parser, as an editor opening the
/// file does.
module EasyBuild.Parser

open System
open System.Collections.Generic
open System.IO
open TreeSitter
open EasyBuild.Workspace

/// The two grammars of this repository: F# source and F# signature files.
type Grammar =
    | Source
    | Signature

    member this.Name =
        match this with
        | Source -> "fsharp"
        | Signature -> "fsharp_signature"

    member this.Directory =
        match this with
        | Source -> Workspace.``.``
        | Signature -> Workspace.signature.``.``

    member this.Extensions =
        match this with
        | Source ->
            [
                ".fs"
                ".fsx"
            ]
        | Signature -> [ ".fsi" ]

    member this.ParserPath = Path.Combine(this.Directory, "parser.so")

let defaultPath = Source.ParserPath

let loadAs (grammar: Grammar) (path: string) =
    if not (File.Exists path) then
        failwith $"No parser at %s{path}. Run `task build` first."

    new Language(Path.GetFullPath path, $"tree_sitter_%s{grammar.Name}")

let load (path: string) = loadAs Source path

/// Fails when a grammar's parser.so is older than the sources it is compiled from.
let checkFreshness (grammar: Grammar) =
    let modified (path: string) = File.GetLastWriteTimeUtc path
    let grammarJs = Path.Combine(grammar.Directory, "grammar.js")
    let parserC = Path.Combine(grammar.Directory, "src", "parser.c")

    if modified grammarJs > modified parserC then
        failwith $"%s{grammarJs} is newer than its src/parser.c. Run `task generate` first."

    if not (File.Exists grammar.ParserPath) then
        failwith $"No %s{grammar.ParserPath}. Run `task build` first."

    if
        modified grammar.ParserPath < modified parserC
        || modified grammar.ParserPath < modified Workspace.src.``scanner.c``
    then
        failwith $"%s{grammar.ParserPath} is older than its sources. Run `task build` first."

let descendants (root: Node) =
    seq {
        let stack = Stack<Node>()
        stack.Push root

        while stack.Count > 0 do
            let node = stack.Pop()
            yield node

            for child in node.Children do
                stack.Push child
    }

/// ERROR and MISSING nodes in the source. A MISSING node of a hidden rule cannot be
/// visited, so a source whose only errors are hidden still counts one.
let errorSites (language: Language) (source: string) =
    use parser = new Parser(language)
    use tree = parser.Parse source
    let root = tree.RootNode

    if not root.HasError then
        0
    else
        descendants root
        |> Seq.filter (fun node -> node.IsMissing || node.IsError)
        |> Seq.length
        |> max 1

let isClean (language: Language) (source: string) =
    use parser = new Parser(language)
    use tree = parser.Parse source
    not tree.RootNode.HasError

/// Text and line of every `identifier` node holding one of the words, or None when the
/// source has an error site.
let identifiersAmong (language: Language) (words: Set<string>) (source: string) =
    use parser = new Parser(language)
    use tree = parser.Parse source
    let root = tree.RootNode

    if root.HasError then
        None
    else
        descendants root
        |> Seq.filter (fun node -> node.Type = "identifier" && words.Contains node.Text)
        |> Seq.map (fun node -> node.Text, node.StartPosition.Row + 1)
        |> List.ofSeq
        |> Some

let selfTest (grammar: Grammar) (language: Language) =
    let rootType (source: string) =
        use parser = new Parser(language)
        use tree = parser.Parse source
        tree.RootNode.Type

    let valid =
        match grammar with
        | Source -> "let x = 1\n"
        | Signature -> "val x: int\n"

    let failures =
        [
            if rootType valid <> "source_file" then
                "a valid snippet did not yield a source_file root: is this the F# parser?"
            if errorSites language valid <> 0 then
                "a valid snippet reported error sites"
            if errorSites language "let = = ((( garbage\n" = 0 then
                "a garbage snippet reported no error site: stale parser?"
        ]

    if not failures.IsEmpty then
        failwith ("Parser self-test failed:\n  " + String.concat "\n  " failures)
