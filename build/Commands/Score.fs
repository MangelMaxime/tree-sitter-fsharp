module EasyBuild.Commands.Score

open System
open System.ComponentModel
open System.IO
open System.Text.Json
open System.Text.Json.Serialization
open System.Text.RegularExpressions
open Spectre.Console
open Spectre.Console.Cli
open TreeSitter
open EasyBuild
open EasyBuild.Workspace

type Coverage =
    {
        Files: int
        Failing: int
        CleanPct: float
    }

type Recall =
    {
        Files: int
        Flagged: int
        RecallPct: float
    }

type FalsePositive =
    {
        Files: int
        Flagged: int
        FpPct: float
    }

type Degeneracy =
    {
        Sites: int
        Files: int
    }

type Highlight =
    {
        Captures: int
        Asserted: int
        CoveragePct: float
        Uncovered: string list
    }

type Scoreboard =
    {
        CoverageBench: Coverage option
        CoverageDotnetValid: Coverage option
        RejectionRecall: Recall option
        RejectionFalsePositive: FalsePositive option
        Degeneracy: Degeneracy option
        Highlight: Highlight option
    }

type Axis =
    | Coverage
    | Rejection
    | Degeneracy
    | Highlight

let private allAxes =
    set
        [
            Coverage
            Rejection
            Degeneracy
            Highlight
        ]

let private scoreDir = Workspace.test.score.``.``
let private baselinePath = Workspace.test.score.``baseline.json``

let private json =
    JsonSerializerOptions(
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
    )

let private percent (part: int) (total: int) =
    Math.Round(100.0 * float part / float (max total 1), 2)

// The conformance suite's syntax diagnostics: unexpected token, offside, numeric literal,
// unmatched '(' / '{', missing qualification. FS0035 and FS0025 are not syntax errors.
let private syntaxDiagnostics =
    set
        [
            "FS0010"
            "FS0058"
            "FS1156"
            "FS0583"
            "FS0584"
            "FS0599"
        ]

let private expects = Regex(@"id=""?(FS\d+)")

let private readList (name: string) =
    let path = Path.Combine(scoreDir, name)

    if not (File.Exists path) then
        []
    else
        File.ReadLines path
        |> Seq.filter (fun line -> line <> "" && not (line.StartsWith '#'))
        |> List.ofSeq

let private validCorpus () =
    let excluded = readList "corpus-exclude.txt"
    let directories = excluded |> List.filter _.EndsWith('/')
    let files = excluded |> List.filter (fun e -> not (e.EndsWith '/')) |> set

    readList "corpus-valid.txt"
    |> List.filter (fun row ->
        not (files.Contains row) && not (directories |> List.exists row.StartsWith)
    )

let private keywords () =
    File.ReadLines Workspace.test.score.``keywords.txt``
    |> Seq.map (fun line -> line.Split('#', 2).[0].Trim())
    |> Seq.filter ((<>) "")
    |> set

/// Classifies dotnet/fsharp's test tree into the frozen corpus lists.
let private regenerateCorpora () =
    let root = Path.Combine(Corpus.directory, "fsharp")

    if not (Directory.Exists root) then
        failwith "dotnet/fsharp is not cloned yet. Run `task bench` once first."

    let valid = ResizeArray()
    let syntax = ResizeArray()
    let typeOnly = ResizeArray()

    for path in Directory.EnumerateFiles(root, "*.fs", SearchOption.AllDirectories) |> Seq.sort do
        let relative = Path.GetRelativePath(Corpus.directory, path).Replace('\\', '/')
        let name = Path.GetFileName path

        if relative.Split('/') |> Array.contains ".git" then
            ()
        elif not (name.StartsWith "E_" || name.StartsWith "neg") then
            valid.Add relative
        else
            let ids =
                expects.Matches(File.ReadAllText path)
                |> Seq.map (fun m -> m.Groups[1].Value)
                |> set

            if not (Set.intersect ids syntaxDiagnostics).IsEmpty then
                syntax.Add relative
            elif not ids.IsEmpty then
                typeOnly.Add relative

    let write (name: string) (rows: ResizeArray<string>) (note: string) =
        let lines =
            [
                $"# %s{note}"
                "# frozen from the SHA pinned in scripts/bench-manifest.txt"
                yield! rows
            ]

        File.WriteAllText(Path.Combine(scoreDir, name), String.concat "\n" lines + "\n")
        printfn $"  %s{name}: %d{rows.Count}"

    write "corpus-valid.txt" valid "valid F# from dotnet/fsharp (coverage axis)"
    write "corpus-syntax-error.txt" syntax "expect a SYNTAX diagnostic (rejection recall)"

    write
        "corpus-type-error.txt"
        typeOnly
        "expect only TYPE/name diagnostics - a correct parser ACCEPTS these"

let private readable (path: string) =
    try
        Some(Corpus.read path)
    with :? IOException ->
        None

/// (files, failing) over the paths, relative to the corpus directory.
let private sweep (language: Language) (paths: string list) =
    let sources =
        paths |> List.choose (fun rel -> readable (Path.Combine(Corpus.directory, rel)))

    let failing = sources |> List.filter (Parser.isClean language >> not) |> List.length
    sources.Length, failing

let private coverageAxis language (files: Corpus.SourceFile[]) =
    let total, failing = sweep language [ for file in files -> file.Relative ]

    {
        Files = total
        Failing = failing
        CleanPct = percent (total - failing) total
    }

let private degeneracyAxis language (files: Corpus.SourceFile[]) =
    let words = keywords ()

    let hits =
        files
        |> Array.choose (fun file -> readable file.Path)
        |> Array.choose (Parser.identifiersAmong language words)
        |> Array.filter (fun hits -> not hits.IsEmpty)

    {
        Sites = hits |> Array.sumBy List.length
        Files = hits.Length
    }

let private highlightAxis () =
    let query =
        File.ReadLines Workspace.queries.``highlights.scm``
        |> Seq.map (fun line -> line.Split(';', 2).[0])
        |> String.concat "\n"

    // Captures starting with `_` only feed predicates; they never colour anything.
    let captures =
        Regex.Matches(query, @"@([\w.]+)")
        |> Seq.map (fun m -> m.Groups[1].Value.TrimEnd '.')
        |> Seq.filter (fun c -> not (c.StartsWith '_'))
        |> set

    let asserted =
        Directory.GetFiles Workspace.test.highlight.``.``
        |> Seq.collect (fun file -> Regex.Matches(File.ReadAllText file, @"(?:\^|<-)\s*([\w.]+)"))
        |> Seq.map (fun m -> m.Groups[1].Value)
        |> set

    let covered = Set.intersect captures asserted

    {
        Captures = captures.Count
        Asserted = covered.Count
        CoveragePct = percent covered.Count captures.Count
        Uncovered = Set.difference captures asserted |> List.ofSeq |> List.truncate 20
    }

let private score (language: Language) (axes: Set<Axis>) =
    let files = lazy (Corpus.files (Corpus.manifest ()))

    let rejection (name: string) =
        match readList name with
        | [] -> None
        | paths -> Some(sweep language paths)

    {
        CoverageBench =
            if axes.Contains Coverage then
                Some(coverageAxis language files.Value)
            else
                None
        CoverageDotnetValid =
            if axes.Contains Coverage then
                match validCorpus () with
                | [] -> None
                | paths ->
                    let total, failing = sweep language paths

                    Some
                        {
                            Files = total
                            Failing = failing
                            CleanPct = percent (total - failing) total
                        }
            else
                None
        RejectionRecall =
            if axes.Contains Rejection then
                rejection "corpus-syntax-error.txt"
                |> Option.map (fun (total, flagged) ->
                    {
                        Files = total
                        Flagged = flagged
                        RecallPct = percent flagged total
                    }
                )
            else
                None
        RejectionFalsePositive =
            if axes.Contains Rejection then
                rejection "corpus-type-error.txt"
                |> Option.map (fun (total, flagged) ->
                    {
                        Files = total
                        Flagged = flagged
                        FpPct = percent flagged total
                    }
                )
            else
                None
        Degeneracy =
            if axes.Contains Degeneracy then
                Some(degeneracyAxis language files.Value)
            else
                None
        Highlight =
            if axes.Contains Highlight then
                Some(highlightAxis ())
            else
                None
    }

type private Better =
    | Higher
    | Lower

let private rows: (string * (Scoreboard -> float option) * Better * float) list =
    [
        "coverage: bench corpus clean %",
        (fun s -> s.CoverageBench |> Option.map _.CleanPct),
        Higher,
        0.0
        "coverage: dotnet/fsharp valid clean %",
        (fun s -> s.CoverageDotnetValid |> Option.map _.CleanPct),
        Higher,
        0.05
        "rejection: syntax-error recall %",
        (fun s -> s.RejectionRecall |> Option.map _.RecallPct),
        Higher,
        0.0
        "rejection: false-positive %",
        (fun s -> s.RejectionFalsePositive |> Option.map _.FpPct),
        Lower,
        0.3
        "degeneracy: keyword-as-identifier sites",
        (fun s -> s.Degeneracy |> Option.map (fun d -> float d.Sites)),
        Lower,
        0.0
        "highlight: capture coverage % (ours only)",
        (fun s -> s.Highlight |> Option.map _.CoveragePct),
        Higher,
        0.0
    ]

let private render (ours: Scoreboard) (theirs: Scoreboard option) =
    let table = Table().Border TableBorder.Minimal
    table.AddColumn "axis" |> ignore
    table.AddColumn(TableColumn("ours").RightAligned()) |> ignore

    if theirs.IsSome then
        table.AddColumn(TableColumn("other").RightAligned()) |> ignore

    table.AddColumn "better" |> ignore

    let cell (board: Scoreboard option) (pick: Scoreboard -> float option) =
        board |> Option.bind pick |> Option.map string |> Option.defaultValue "-"

    for label, pick, better, _ in rows do
        [
            label
            cell (Some ours) pick
            if theirs.IsSome then
                cell theirs pick
            (string better).ToLowerInvariant()
        ]
        |> Array.ofList
        |> table.AddRow
        |> ignore

    AnsiConsole.Write table

let private gate (baseline: Scoreboard) (ours: Scoreboard) =
    let breaches =
        rows
        |> List.choose (fun (label, pick, better, tolerance) ->
            match pick baseline, pick ours with
            | Some before, Some now ->
                let breached =
                    match better with
                    | Higher -> now < before - tolerance
                    | Lower -> now > before + tolerance

                if breached then
                    Some $"%s{label}: %g{before} -> %g{now}"
                else
                    None
            | _ -> None
        )

    if breaches.IsEmpty then
        AnsiConsole.MarkupLine "\n[green]no regressions[/]"
        0
    else
        AnsiConsole.MarkupLine "\n[red]GATE FAILED:[/]"

        for breach in breaches do
            printfn $"  %s{breach}"

        1

type ScoreSettings() =
    inherit CommandSettings()

    [<CommandOption("--parser <PATH>")>]
    [<Description("Path to our parser (default: parser.so)")>]
    member val Parser = "" with get, set

    [<CommandOption("--vs <PATH>")>]
    [<Description("Path to another F# grammar's compiled parser, scored side by side")>]
    member val Versus = "" with get, set

    [<CommandOption("--axes <LIST>")>]
    [<Description("Comma-separated subset of: coverage, rejection, degeneracy, highlight")>]
    member val Axes = "all" with get, set

    [<CommandOption("--json")>]
    [<Description("Machine-readable output")>]
    member val Json = false with get, set

    [<CommandOption("--compare")>]
    [<Description("Gate against test/score/baseline.json")>]
    member val Compare = false with get, set

    [<CommandOption("--update-baseline")>]
    [<Description("Write test/score/baseline.json")>]
    member val UpdateBaseline = false with get, set

    [<CommandOption("--regen-corpora")>]
    [<Description("Reclassify dotnet/fsharp's tests into test/score/corpus-*.txt")>]
    member val RegenerateCorpora = false with get, set

/// Scores the grammar on four axes; LIMITATIONS.md explains each.
type ScoreCommand() =
    inherit Command<ScoreSettings>()
    interface ICommandLimiter<ScoreSettings>

    override _.Execute(_, settings, _) =
        Corpus.ensureClones (Corpus.manifest ())

        if settings.RegenerateCorpora then
            regenerateCorpora ()
            0
        else
            let axes =
                if settings.Axes = "all" then
                    allAxes
                else
                    settings.Axes.Split ','
                    |> Seq.map (fun name ->
                        match name.Trim() with
                        | "coverage" -> Coverage
                        | "rejection" -> Rejection
                        | "degeneracy" -> Degeneracy
                        | "highlight" -> Highlight
                        | other -> failwith $"Unknown axis: %s{other}"
                    )
                    |> set

            if settings.Parser = "" then
                Parser.checkFreshness Parser.Source

            let parserPath =
                if settings.Parser = "" then
                    Parser.defaultPath
                else
                    settings.Parser

            use language = Parser.load parserPath
            Parser.selfTest Parser.Source language
            let ours = score language axes

            // The highlight axis measures this repo's queries and tests, not a parser.
            let theirs =
                if settings.Versus = "" then
                    None
                else
                    use other = Parser.load settings.Versus
                    Some(score other (Set.remove Highlight axes))

            if settings.Json then
                printfn
                    "%s"
                    (JsonSerializer.Serialize(
                        {|
                            ours = ours
                            other = theirs
                        |},
                        json
                    ))
            else
                render ours theirs

            if settings.UpdateBaseline then
                File.WriteAllText(baselinePath, JsonSerializer.Serialize(ours, json) + "\n")

                printfn
                    $"\nbaseline written: %s{Path.GetRelativePath(Workspace.``.``, baselinePath)}"

                0
            elif settings.Compare then
                if not (File.Exists baselinePath) then
                    failwith "No baseline. Run `task score -- --update-baseline` first."

                gate
                    (JsonSerializer.Deserialize<Scoreboard>(File.ReadAllText baselinePath, json))
                    ours
            else
                0
