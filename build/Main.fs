module EasyBuild.Main

open Spectre.Console.Cli
open EasyBuild.Commands.Bench
open EasyBuild.Commands.Score
open EasyBuild.Commands.Expansion
open EasyBuild.Commands.DeriveQueries
open EasyBuild.Commands.CheckQueries
open EasyBuild.Commands.Highlight
open EasyBuild.Commands.Grammar
open EasyBuild.Commands.Dev
open EasyBuild.Commands.Docs

[<EntryPoint>]
let main args =
    let app = CommandApp()

    app.Configure(fun config ->
        config.Settings.ApplicationName <- "./build.sh"

        config
            .AddCommand<GenerateCommand>("generate")
            .WithDescription("Regenerate src/ and signature/src/ from grammar.js (skipped when unchanged)")
            .WithExample("generate")
            .WithExample("generate --force")
        |> ignore

        config
            .AddCommand<BuildCommand>("build")
            .WithDescription("Compile parser.so and signature/parser.so (skipped when unchanged)")
            .WithExample("build")
        |> ignore

        config
            .AddCommand<TestCommand>("test")
            .WithDescription("Corpus and highlight tests of both grammars")
            .WithExample("test")
            .WithExample("test --include \"after a line comment\"")
            .WithExample("test --signature")
        |> ignore

        config
            .AddCommand<TestAllCommand>("test-all")
            .WithDescription(
                "Every local gate: corpus, highlight, expansion, query validity, derived queries, snapshot"
            )
            .WithExample("test-all")
        |> ignore

        config.AddBranch(
            "dev",
            fun (dev: IConfigurator<CommandSettings>) ->
                dev.SetDescription "Deploy the grammar and queries to an editor"

                dev
                    .AddCommand<DevHelixCommand>("helix")
                    .WithDescription("Build and copy the parsers and queries into the Helix runtime")
                    .WithExample("dev helix")
                |> ignore

                dev
                    .AddCommand<DevZedCommand>("zed")
                    .WithDescription("Refresh the Zed dev extension in zed/ (then Rebuild it in Zed)")
                    .WithExample("dev zed")
                |> ignore

                dev
                    .AddCommand<DevNvimCommand>("nvim")
                    .WithDescription("Build the parser and open a file in the repo-local Neovim config")
                    .WithExample("dev nvim")
                    .WithExample("dev nvim path/to/file.fsx")
                |> ignore
        )
        |> ignore

        config
            .AddCommand<DocsCommand>("docs")
            .WithDescription("Build the documentation site in docs/ (--watch to serve it, --check for CI)")
            .WithExample("docs")
            .WithExample("docs --watch")
        |> ignore

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
                "Regenerate the Neovim, Zed and signature highlight queries from the Helix one"
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
