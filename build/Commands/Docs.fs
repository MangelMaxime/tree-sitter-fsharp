module EasyBuild.Commands.Docs

open System
open System.ComponentModel
open System.IO
open Spectre.Console.Cli
open EasyBuild.Tools

type DocsSettings() =
    inherit CommandSettings()

    [<CommandOption("--watch")>]
    [<Description("Serve the site on http://localhost:8080 and rebuild on change")>]
    member val Watch = false with get, set

    [<CommandOption("--check")>]
    [<Description("Render every page and resolve every link without writing the output")>]
    member val Check = false with get, set

    [<CommandOption("--deploy")>]
    [<Description("Build, then publish the output to the gh-pages branch")>]
    member val Deploy = false with get, set

    [<CommandOption("--dry-run")>]
    [<Description("With --deploy: print what the publish would change, and push nothing")>]
    member val DryRun = false with get, set

/// Builds the documentation site in docs/ with Nacara.
type DocsCommand() =
    inherit Command<DocsSettings>()
    interface ICommandLimiter<DocsSettings>

    override _.Execute(_, settings, _) =
        let docs = Path.Combine(root, "docs")

        let verb =
            if settings.Watch then "watch"
            elif settings.Check then "check"
            else "build"

        // Nacara downloads the grammar at this commit from GitHub, so it must be pushed; otherwise
        // the F# code blocks use the grammar bundled with the plugin.
        let head = gitRead root [ "rev-parse"; "HEAD" ]

        if gitRead root [ "branch"; "-r"; "--contains"; head ] <> "" then
            Environment.SetEnvironmentVariable("TREE_SITTER_FSHARP_COMMIT", head)
        else
            printfn $"%s{head.Substring(0, 7)} is not pushed: code blocks use the bundled F# grammar"
        match runCode "dotnet" docs [ "run"; "--"; verb ] with
        | 0 when settings.Deploy ->
            runCode "dotnet" docs [ "run"; "--no-build"; "--"; "gh-pages"; if settings.DryRun then "--dry-run" ]
        | code -> code
