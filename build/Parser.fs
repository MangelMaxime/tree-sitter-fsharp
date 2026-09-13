/// The only place a parser is loaded. Every source gets a fresh parser, as an editor opening the
/// file does.
module EasyBuild.Parser

open System
open System.Collections.Generic
open System.IO
open TreeSitter
open EasyBuild.Workspace

let defaultPath = Path.Combine(Workspace.``.``, "parser.so")

let load (path: string) =
    if not (File.Exists path) then
        failwith $"No parser at %s{path}. Run `task build` first."

    new Language(Path.GetFullPath path, "tree_sitter_fsharp")

/// Fails when parser.so is older than the sources it is compiled from.
let checkFreshness () =
    let modified (path: string) = File.GetLastWriteTimeUtc path

    if modified Workspace.``grammar.js`` > modified Workspace.src.``parser.c`` then
        failwith "grammar.js is newer than src/parser.c. Run `task generate` first."

    if not (File.Exists defaultPath) then
        failwith "No parser.so. Run `task build` first."

    if
        modified defaultPath < modified Workspace.src.``parser.c``
        || modified defaultPath < modified Workspace.src.``scanner.c``
    then
        failwith "parser.so is older than src/. Run `task build` first."

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

let selfTest (language: Language) =
    let rootType (source: string) =
        use parser = new Parser(language)
        use tree = parser.Parse source
        tree.RootNode.Type

    let failures =
        [
            if rootType "let x = 1\n" <> "source_file" then
                "a valid snippet did not yield a source_file root: is this the F# parser?"
            if errorSites language "let x = 1\n" <> 0 then
                "a valid snippet reported error sites"
            if errorSites language "let = = ((( garbage\n" = 0 then
                "a garbage snippet reported no error site: stale parser?"
        ]

    if not failures.IsEmpty then
        failwith ("Parser self-test failed:\n  " + String.concat "\n  " failures)
