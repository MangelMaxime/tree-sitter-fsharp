module EasyBuild.Commands.Highlight

open System
open System.Collections.Generic
open System.ComponentModel
open System.IO
open Spectre.Console
open Spectre.Console.Cli
open TreeSitter
open EasyBuild
open EasyBuild.Workspace

type private Capture =
    {
        Start: int
        End: int
        Pattern: int
        Name: string
    }

/// Every capture of the highlight query over the tree, with the pattern that produced it.
let private captures (query: Query) (root: Node) =
    use cursor = query.Execute root

    [|
        for m in cursor.Matches do
            for c in m.Captures do
                if not (c.Name.StartsWith '_') then
                    {
                        Start = c.Node.StartIndex
                        End = c.Node.EndIndex
                        Pattern = m.PatternIndex
                        Name = c.Name
                    }
    |]

/// The leaf tokens of the tree in source order.
let private tokens (root: Node) =
    let result = ResizeArray<Node>()
    let stack = Stack<Node>()
    stack.Push root

    while stack.Count > 0 do
        let node = stack.Pop()

        if node.Children.Count = 0 then
            result.Add node
        else
            for i in node.Children.Count - 1 .. -1 .. 0 do
                stack.Push node.Children[i]

    result

/// The capture an editor resolves for a token: the innermost capture that contains it,
/// and among equal ranges the last pattern in the query.
let private resolve (all: Capture[]) (token: Node) =
    let s, e = token.StartIndex, token.EndIndex

    all
    |> Array.filter (fun c -> c.Start <= s && e <= c.End)
    |> Array.sortBy (fun c -> c.End - c.Start, -c.Pattern)
    |> Array.tryHead
    |> Option.map _.Name

let private loadQuery (language: Language) =
    language.CreateQuery(File.ReadAllText Workspace.queries.``highlights.scm``)

let private snapshotPath =
    Path.Combine(Workspace.test.``.``, "highlight-snapshot.txt")

let private renderSnapshot () =
    use language = Parser.load Parser.defaultPath
    use query = loadQuery language
    use parser = new Parser(language)
    let source = File.ReadAllText Workspace.examples.``references.fsx``
    use tree = parser.Parse source
    let all = captures query tree.RootNode

    let lines =
        [
            "# Resolved highlight capture of every token in examples/references.fsx."
            "# Regenerate with `task highlight:snapshot`; `-` is a token with no capture."
            for token in tokens tree.RootNode do
                let text = token.Text.Replace("\n", "\\n")

                let text =
                    if text.Length > 40 then
                        text.Substring(0, 40) + "..."
                    else
                        text

                let name = resolve all token |> Option.defaultValue "-"
                $"%d{token.StartPosition.Row + 1}:%d{token.StartPosition.Column}\t%s{text}\t%s{name}"
        ]

    String.concat "\n" lines + "\n"

type SnapshotSettings() =
    inherit CommandSettings()

    [<CommandOption("--check")>]
    [<Description("Fail when test/highlight/snapshot.txt differs from the current queries")>]
    member val Check = false with get, set

/// Writes the resolved capture of every token of examples/references.fsx.
type HighlightSnapshotCommand() =
    inherit Command<SnapshotSettings>()
    interface ICommandLimiter<SnapshotSettings>

    override _.Execute(_, settings, _) =
        let relative = Path.GetRelativePath(Workspace.``.``, snapshotPath)
        let current = renderSnapshot ()

        if settings.Check then
            if File.Exists snapshotPath && File.ReadAllText snapshotPath = current then
                printfn $"%s{relative} is up to date"
                0
            else
                AnsiConsole.MarkupLineInterpolated
                    $"[red]{relative} is out of date[/]: run `task highlight:snapshot` and review the diff"

                1
        else
            File.WriteAllText(snapshotPath, current)
            printfn $"wrote %s{relative}"
            0

type CoverageSettings() =
    inherit CommandSettings()

    [<CommandOption("--files <N>")>]
    [<Description("How many bench files to sample (default 400)")>]
    member val Files = 400 with get, set

    [<CommandOption("--top <N>")>]
    [<Description("How many contexts to list (default 25)")>]
    member val Top = 25 with get, set

/// Lists the tokens that get no capture at all, grouped by their syntactic context.
type HighlightCoverageCommand() =
    inherit Command<CoverageSettings>()
    interface ICommandLimiter<CoverageSettings>

    override _.Execute(_, settings, _) =
        use language = Parser.load Parser.defaultPath
        use query = loadQuery language
        let random = Random 7

        let files =
            Corpus.files (Corpus.manifest ())
            |> Array.filter (fun f -> not (f.Relative.Contains "/tests/"))
            |> Array.sortBy (fun _ -> random.Next())
            |> Array.truncate settings.Files

        let identifiers = Dictionary<string, int * string>()
        let anonymous = Dictionary<string, int>()
        let mutable identifierCount = 0
        let mutable anonymousCount = 0

        for file in files do
            use parser = new Parser(language)
            let source = Corpus.read file.Path
            use tree = parser.Parse source

            if not tree.RootNode.HasError then
                let all = captures query tree.RootNode
                let lines = source.Split '\n'

                for token in tokens tree.RootNode do
                    let plain = (resolve all token).IsNone

                    if token.Type = "identifier" then
                        identifierCount <- identifierCount + 1

                        if plain then
                            let parent = token.Parent

                            let grand =
                                if isNull parent || isNull parent.Parent then
                                    ""
                                else
                                    parent.Parent.Type

                            let case =
                                if Char.IsUpper token.Text[0] then
                                    "Upper"
                                else
                                    "lower"

                            let key = $"%s{case} %s{parent.Type} < %s{grand}"

                            let count, example =
                                match identifiers.TryGetValue key with
                                | true, (n, e) -> n, e
                                | _ ->
                                    let line = lines[token.StartPosition.Row].Trim()
                                    0, $"[%s{token.Text}] %s{line.Substring(0, min 70 line.Length)}"

                            identifiers[key] <- (count + 1, example)
                    elif not token.IsNamed then
                        anonymousCount <- anonymousCount + 1

                        if plain then
                            anonymous[token.Type] <-
                                (match anonymous.TryGetValue token.Type with
                                 | true, n -> n + 1
                                 | _ -> 1)

        let plainIdentifiers = identifiers.Values |> Seq.sumBy fst
        let plainAnonymous = anonymous.Values |> Seq.sum
        printfn $"%d{files.Length} clean files sampled"

        printfn
            $"identifiers: %d{identifierCount}, no capture: %d{plainIdentifiers} (%.1f{100.0 * float plainIdentifiers / float (max 1 identifierCount)}%%)"

        printfn $"keywords and punctuation: %d{anonymousCount}, no capture: %d{plainAnonymous}"

        let table = Table().Border TableBorder.Minimal
        table.AddColumn(TableColumn("count").RightAligned()) |> ignore
        table.AddColumn "identifier context (case parent < grandparent)" |> ignore
        table.AddColumn "example" |> ignore

        for KeyValue(key, (count, example)) in
            identifiers
            |> Seq.sortByDescending (fun kv -> fst kv.Value)
            |> Seq.truncate settings.Top do
            table.AddRow(Markup.Escape(string count), Markup.Escape key, Markup.Escape example)
            |> ignore

        AnsiConsole.Write table

        if anonymous.Count > 0 then
            printfn "keywords and punctuation with no capture:"

            for KeyValue(kind, count) in
                anonymous |> Seq.sortByDescending _.Value |> Seq.truncate settings.Top do
                printfn $"  %6d{count}  %s{kind}"

        0
