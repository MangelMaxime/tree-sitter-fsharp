module EasyBuild.Main

open Spectre.Console.Cli
open EasyBuild.Commands.Bench
open EasyBuild.Commands.Score
open EasyBuild.Commands.Expansion
open EasyBuild.Commands.DeriveQueries
open EasyBuild.Commands.CheckQueries
open EasyBuild.Commands.Highlight

[<EntryPoint>]
let main args =
    let app = CommandApp()

    app.Configure(fun config ->
        config.Settings.ApplicationName <- "dotnet run --project build --"

        config
            .AddCommand<BenchCommand>("bench")
            .WithDescription(
                "Sweep the pinned real-world repos and fail on any regression against test/bench/baseline.txt"
            )
            .WithExample("bench")
            .WithExample("bench --summary")
            .WithExample("bench --update-baseline")
        |> ignore

        config
            .AddCommand<ScoreCommand>("score")
            .WithDescription("Score the grammar on the four axes described in LIMITATIONS.md")
            .WithExample("score")
            .WithExample("score --compare")
            .WithExample("score --vs /path/to/other-fsharp.so")
        |> ignore

        config
            .AddCommand<ExpansionCommand>("expansion")
            .WithDescription("Run the Helix expand-selection fixtures in test/expansion")
            .WithExample("expansion")
            .WithExample("expansion -i multiDoc")
        |> ignore

        config
            .AddCommand<DeriveQueriesCommand>("derive-queries")
            .WithDescription(
                "Regenerate queries/nvim/highlights.scm from the Helix one and check the Zed one"
            )
            .WithExample("derive-queries")
            .WithExample("derive-queries --check")
        |> ignore

        config
            .AddCommand<CheckQueriesCommand>("check-queries")
            .WithDescription("Compile every .scm in queries/ against the current grammar")
            .WithExample("check-queries")
        |> ignore

        config
            .AddCommand<HighlightSnapshotCommand>("highlight-snapshot")
            .WithDescription("Write the resolved highlight capture of every token of examples/references.fsx")
            .WithExample("highlight-snapshot")
            .WithExample("highlight-snapshot --check")
        |> ignore

        config
            .AddCommand<HighlightCoverageCommand>("highlight-coverage")
            .WithDescription("List the tokens that get no highlight capture over a sample of the bench")
            .WithExample("highlight-coverage")
            .WithExample("highlight-coverage --files 100 --top 40")
        |> ignore
    )

    app.Run args
