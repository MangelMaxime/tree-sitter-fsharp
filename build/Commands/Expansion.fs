module EasyBuild.Commands.Expansion

open System
open System.ComponentModel
open System.IO
open Spectre.Console.Cli
open TreeSitter
open EasyBuild
open EasyBuild.Workspace

type Fixture =
    {
        Name: string
        Source: string
        Steps: string list
    }

let private header = String('=', 80)
let private divider = String('-', 80)
let private stepMarker = ">> step"
let private cursor = "‸"

let parseFixtures (text: string) =
    let lines = text.Split '\n'
    let fixtures = ResizeArray()
    let mutable i = 0

    while i < lines.Length do
        if lines[i] = header then
            let name = lines[i + 1]
            let sourceStart = i + 3
            let dividerAt = Array.IndexOf(lines, divider, sourceStart)
            let source = String.Join("\n", lines[sourceStart .. dividerAt - 1])
            let steps = ResizeArray()
            let current = ResizeArray()
            let mutable inStep = false
            i <- dividerAt + 1

            while i < lines.Length && lines[i] <> header do
                if lines[i] = stepMarker then
                    if inStep then
                        steps.Add(String.Join("\n", current).TrimEnd '\n')
                        current.Clear()

                    inStep <- true
                elif inStep then
                    current.Add lines[i]

                i <- i + 1

            if inStep then
                steps.Add(String.Join("\n", current).TrimEnd '\n')

            fixtures.Add
                {
                    Name = name
                    Source = source
                    Steps = List.ofSeq steps
                }
        else
            i <- i + 1

    List.ofSeq fixtures

let private cursorPosition (source: string) =
    let lines = source.Split '\n'

    match lines |> Array.tryFindIndex _.Contains(cursor) with
    | Some row -> row, lines[row].IndexOf cursor
    | None -> failwith $"fixture has no %s{cursor} cursor marker"

/// The texts Helix selects on successive expand-selection presses: the parent chain from the
/// node under the cursor, skipping parents with the same range.
let private chain (language: Language) (source: string) (row: int, column: int) =
    use parser = new Parser(language)
    use tree = parser.Parse source

    let start =
        tree.RootNode.GetNamedDescendantForPosition(Point(row, column), Point(row, column + 1))

    let sameRange (a: Node) (b: Node) =
        a.StartPosition = b.StartPosition && a.EndPosition = b.EndPosition

    let rec up (node: Node) (acc: Node list) =
        let acc =
            match acc with
            | last :: _ when sameRange last node -> acc
            | _ -> node :: acc

        match node.Parent with
        | null -> List.rev acc
        | parent -> up parent acc

    up start [] |> List.map (fun node -> node.Text.TrimEnd '\n')

let private run (language: Language) (fixture: Fixture) =
    let position = cursorPosition fixture.Source
    let source = fixture.Source.Replace(cursor, "", StringComparison.Ordinal) + "\n"
    let chain = chain language source position

    fixture.Steps
    |> List.indexed
    |> List.choose (fun (i, want) ->
        let got = chain |> List.tryItem i |> Option.defaultValue "<chain exhausted>"

        if got <> want then
            Some(i + 1, want, got)
        else
            None
    )

type ExpansionSettings() =
    inherit CommandSettings()

    [<CommandOption("-i|--filter <SUBSTRING>")>]
    [<Description("Only run the fixtures whose name contains this")>]
    member val Filter = "" with get, set

/// Simulates Helix expand-selection against test/expansion/*.txt.
type ExpansionCommand() =
    inherit Command<ExpansionSettings>()
    interface ICommandLimiter<ExpansionSettings>

    override _.Execute(_, settings, _) =
        use language = Parser.load Parser.defaultPath

        let fixtures =
            Directory.GetFiles(Workspace.test.expansion.``.``, "*.txt")
            |> Array.sort
            |> Seq.collect (File.ReadAllText >> parseFixtures)
            |> Seq.filter (fun fixture -> fixture.Name.Contains settings.Filter)
            |> List.ofSeq

        let indent (text: string) =
            text.Replace("\n", "\n                   ")

        let passed =
            fixtures
            |> List.filter (fun fixture ->
                match run language fixture with
                | [] ->
                    printfn $"  ok   %s{fixture.Name}"
                    true
                | failures ->
                    printfn $"  FAIL %s{fixture.Name}"

                    for step, want, got in failures do
                        printfn $"       step %d{step}:"
                        printfn $"         expected: %s{indent want}"
                        printfn $"         got:      %s{indent got}"

                    false
            )
            |> List.length

        printfn $"\n%d{passed}/%d{fixtures.Length} expansion tests passed"

        if passed = fixtures.Length then
            0
        else
            1
