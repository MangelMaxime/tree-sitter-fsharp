module EasyBuild.Commands.Docs

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

/// Builds the documentation site in docs/ with Nacara.
type DocsCommand() =
    inherit Command<DocsSettings>()
    interface ICommandLimiter<DocsSettings>

    override _.Execute(_, settings, _) =
        let verb =
            if settings.Watch then "watch"
            elif settings.Check then "check"
            else "build"

        runCode "dotnet" (Path.Combine(root, "docs")) [ "run"; "--"; verb ]
