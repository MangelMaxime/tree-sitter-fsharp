module EasyBuild.Commands.Grammar

open System.ComponentModel
open System.IO
open System.Threading
open Spectre.Console.Cli
open EasyBuild
open EasyBuild.Tools
open EasyBuild.Commands.Expansion
open EasyBuild.Commands.CheckQueries
open EasyBuild.Commands.DeriveQueries
open EasyBuild.Commands.Highlight

type ForceSettings() =
    inherit CommandSettings()

    [<CommandOption("--force")>]
    [<Description("Run even when the inputs did not change")>]
    member val Force = false with get, set

/// Regenerates src/ and signature/src/ from the two grammar.js files.
type GenerateCommand() =
    inherit Command<ForceSettings>()
    interface ICommandLimiter<ForceSettings>

    override _.Execute(_, settings, _) =
        Steps.generate true settings.Force
        0

/// Compiles parser.so and signature/parser.so, regenerating first when needed.
type BuildCommand() =
    inherit Command<ForceSettings>()
    interface ICommandLimiter<ForceSettings>

    override _.Execute(_, settings, _) =
        Steps.build true settings.Force
        0

type TestSettings() =
    inherit CommandSettings()

    [<CommandOption("--signature")>]
    [<Description("Only the signature grammar")>]
    member val Signature = false with get, set

    [<CommandOption("-i|--include <NAME>")>]
    [<Description("Only the corpus tests whose name matches this regex")>]
    member val Include = "" with get, set

    [<CommandOption("-u|--update")>]
    [<Description("Rewrite the expected trees of the corpus tests")>]
    member val Update = false with get, set

let private signatureDirectory = Path.Combine(root, "signature")

/// The corpus and highlight tests of both grammars; returns the first failing exit code.
/// Runs a command in-process the way the CLI would.
let private run (command: ICommand<'settings>) (context: CommandContext) (settings: 'settings) =
    command.ExecuteAsync(context, settings, CancellationToken.None).Result

let private corpusTests (settings: TestSettings) =
    let arguments =
        [
            "test"
            if settings.Include <> "" then
                "--include"
                settings.Include
            if settings.Update then
                "--update"
        ]

    let directories =
        if settings.Signature then
            [ signatureDirectory ]
        else
            [ root; signatureDirectory ]

    directories
    |> List.map (fun directory -> treeSitterCode directory arguments)
    |> List.tryFind ((<>) 0)
    |> Option.defaultValue 0

/// Runs `tree-sitter test` for the source grammar and the signature grammar.
type TestCommand() =
    inherit Command<TestSettings>()
    interface ICommandLimiter<TestSettings>

    override _.Execute(_, settings, _) = corpusTests settings

/// Every local gate: corpus, highlight assertions, expansion fixtures, query validity,
/// derived queries and the highlight snapshot.
type TestAllCommand() =
    inherit Command<EmptySettings>()
    interface ICommandLimiter<EmptySettings>

    override _.Execute(context, _, _) =
        Steps.build false false

        let gates: (string * (unit -> int)) list =
            [
                "corpus", (fun () -> corpusTests (TestSettings()))
                "expansion", (fun () -> run (ExpansionCommand()) context (ExpansionSettings()))
                "check-queries", (fun () -> run (CheckQueriesCommand()) context (CheckQueriesSettings()))
                "derive-queries --check",
                (fun () -> run (DeriveQueriesCommand()) context (DeriveQueriesSettings(Check = true)))
                "highlight-snapshot --check",
                (fun () -> run (HighlightSnapshotCommand()) context (SnapshotSettings(Check = true)))
            ]

        gates
        |> List.tryPick (fun (name, gate) ->
            printfn $"--- %s{name} ---"
            let code = gate ()

            if code <> 0 then
                Some code
            else
                None
        )
        |> Option.defaultValue 0
