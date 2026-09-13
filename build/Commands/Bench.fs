module EasyBuild.Commands.Bench

open System
open System.ComponentModel
open System.Diagnostics
open System.IO
open Spectre.Console
open Spectre.Console.Cli
open TreeSitter
open EasyBuild
open EasyBuild.Workspace

type Outcome =
    | Errors of int
    | TooSlow

let private slowAfter = TimeSpan.FromSeconds 60.0

let private baselinePath (grammar: Parser.Grammar) =
    match grammar with
    | Parser.Source -> Workspace.test.bench.``baseline.txt``
    | Parser.Signature -> Path.Combine(Workspace.test.bench.``.``, "baseline-signature.txt")

let private show (outcome: Outcome) =
    match outcome with
    | Errors n -> string n
    | TooSlow -> "slow"

let private errorNodes (results: Map<string, Outcome>) =
    results
    |> Map.values
    |> Seq.sumBy (
        function
        | Errors n -> n
        | TooSlow -> 0
    )

let private sweep (language: Language) (files: Corpus.SourceFile[]) =
    let measure (i: int) (file: Corpus.SourceFile) =
        let watch = Stopwatch.StartNew()
        let sites = Parser.errorSites language (Corpus.read file.Path)

        if (i + 1) % 500 = 0 then
            printfn $"  ...%d{i + 1}/%d{files.Length} files"

        if watch.Elapsed > slowAfter then
            file.Relative, TooSlow
        else
            file.Relative, Errors sites

    files
    |> Array.mapi measure
    |> Array.filter (fun (_, outcome) -> outcome <> Errors 0)
    |> Map.ofArray

let private readBaseline (path: string) =
    if not (File.Exists path) then
        None
    else
        File.ReadLines path
        |> Seq.filter (fun line -> line <> "" && not (line.StartsWith '#'))
        |> Seq.map (fun line ->
            match line.Split('\t', 2) with
            | [| "slow"; relative |] -> relative, TooSlow
            | [| count; relative |] -> relative, Errors(int count)
            | _ -> failwith $"Malformed baseline line: %s{line}"
        )
        |> Map.ofSeq
        |> Some

let private writeBaseline (path: string) (results: Map<string, Outcome>) (total: int) =
    let lines =
        [
            "# Parser benchmark baseline - regenerate with: ./build.sh bench --update-baseline"
            $"# %d{total} files swept; %d{results.Count} with errors; %d{errorNodes results} error nodes"
            for KeyValue(relative, outcome) in results do
                $"%s{show outcome}\t%s{relative}"
        ]

    File.WriteAllText(path, String.concat "\n" lines + "\n")

let private summarize (results: Map<string, Outcome>) (files: Corpus.SourceFile[]) =
    let table = Table().Border TableBorder.Minimal
    table.AddColumn "project" |> ignore

    for column in
        [
            "files"
            "failing"
            "clean"
            "nodes"
        ] do
        table.AddColumn(TableColumn(column).RightAligned()) |> ignore

    let row (name: string) (total: int) (failing: int) (nodes: int) =
        let clean = float (total - failing) / float total * 100.0

        table.AddRow(name, string total, string failing, $"%.1f{clean}%%", string nodes)
        |> ignore

    let byRepo = files |> Array.groupBy _.Repo.Name |> Array.sortBy fst

    for name, repoFiles in byRepo do
        let failing =
            results |> Map.filter (fun relative _ -> relative.StartsWith(name + "/"))

        row name repoFiles.Length failing.Count (errorNodes failing)

    row "TOTAL" files.Length results.Count (errorNodes results)
    AnsiConsole.Write table

let private worse (before: Outcome option) (now: Outcome) =
    match before, now with
    | Some TooSlow, _ -> false
    | _, TooSlow -> true
    | Some(Errors a), Errors b -> b > a
    | None, Errors b -> b > 0

let private better (before: Outcome) (now: Outcome) =
    match before, now with
    | TooSlow, Errors _ -> true
    | Errors a, Errors b -> b < a
    | _ -> false

let private compare (baseline: Map<string, Outcome>) (results: Map<string, Outcome>) =
    let repaired =
        baseline
        |> Map.filter (fun relative _ -> not (results.ContainsKey relative))
        |> Map.toList
        |> List.sortByDescending (snd >> show >> String.length)

    let improved =
        results
        |> Map.toList
        |> List.choose (fun (relative, now) ->
            match baseline.TryFind relative with
            | Some before when better before now -> Some(relative, before, now)
            | _ -> None
        )

    let regressions =
        results
        |> Map.toList
        |> List.choose (fun (relative, now) ->
            if worse (baseline.TryFind relative) now then
                Some(relative, baseline.TryFind relative, now)
            else
                None
        )

    if not repaired.IsEmpty then
        printfn $"\nfixed (%d{repaired.Length}):"

        for relative, before in List.truncate 15 repaired do
            printfn $"  -%-5s{show before} %s{relative}"

    if not improved.IsEmpty then
        printfn $"improved (%d{improved.Length}):"

        for relative, before, now in List.truncate 15 improved do
            printfn $"  %s{show before}->%-5s{show now} %s{relative}"

    if not regressions.IsEmpty then
        AnsiConsole.MarkupLine $"\n[red]REGRESSIONS ({regressions.Length}):[/]"

        for relative, before, now in List.truncate 30 regressions do
            let before = before |> Option.map show |> Option.defaultValue "0"
            printfn $"  %s{before}->%-5s{show now} %s{relative}"

        1
    else
        AnsiConsole.MarkupLine "\n[green]no regressions[/]"

        if not repaired.IsEmpty || not improved.IsEmpty then
            printfn "improvements above: run `./build.sh bench --update-baseline` to lock them in"

        0

type BenchSettings() =
    inherit CommandSettings()

    [<CommandOption("--update-baseline")>]
    [<Description("Accept the current sweep as the new baseline (writes test/bench/baseline.txt)")>]
    member val UpdateBaseline = false with get, set

    [<CommandOption("--summary")>]
    [<Description("Print the per-project table")>]
    member val Summary = false with get, set

    [<CommandOption("--signature")>]
    [<Description("Sweep the .fsi files with the signature grammar (baseline-signature.txt)")>]
    member val Signature = false with get, set

/// Sweeps the pinned repositories and fails on any file that parses worse than the baseline.
type BenchCommand() =
    inherit Command<BenchSettings>()
    interface ICommandLimiter<BenchSettings>

    override _.Execute(_, settings, _) =
        let grammar =
            if settings.Signature then
                Parser.Signature
            else
                Parser.Source

        let baseline = baselinePath grammar
        let repos = Corpus.manifest ()
        Corpus.ensureClones repos
        Steps.build false false
        use language = Parser.loadAs grammar grammar.ParserPath
        Parser.selfTest grammar language
        let files = Corpus.filesWith grammar.Extensions repos
        printfn $"sweeping %d{files.Length} files ..."
        let results = sweep language files
        let clean = float (files.Length - results.Count) / float files.Length * 100.0

        printfn
            $"\n%d{files.Length} files; %d{results.Count} failing (%.1f{clean}%% clean); %d{errorNodes results} error nodes"

        if settings.Summary then
            summarize results files

        if settings.UpdateBaseline then
            writeBaseline baseline results files.Length
            printfn $"baseline written: %s{Path.GetRelativePath(Workspace.``.``, baseline)}"
            0
        else
            match readBaseline baseline with
            | None -> failwith "No baseline. Run `./build.sh bench --update-baseline` first."
            | Some baseline -> compare baseline results
