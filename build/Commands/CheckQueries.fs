module EasyBuild.Commands.CheckQueries

open System.IO
open System.Text.RegularExpressions
open Spectre.Console
open Spectre.Console.Cli
open EasyBuild
open EasyBuild.Workspace

type CheckQueriesSettings() =
    inherit CommandSettings()

/// Compiles every .scm against the current grammar.
type CheckQueriesCommand() =
    inherit Command<CheckQueriesSettings>()
    interface ICommandLimiter<CheckQueriesSettings>

    override _.Execute(_, _, _) =
        use language = Parser.load Parser.defaultPath

        let files =
            [
                Workspace.queries.``.``
                Workspace.queries.zed.``.``
                Workspace.queries.nvim.``.``
            ]
            |> List.collect (fun dir -> Directory.GetFiles(dir, "*.scm") |> List.ofArray)
            |> List.sort

        let failures =
            files
            |> List.choose (fun file ->
                let source = File.ReadAllText file

                try
                    use _ = language.CreateQuery source
                    None
                with ex ->
                    let line =
                        match Regex.Match(ex.Message, @"index (\d+)") with
                        | m when m.Success ->
                            let index = int m.Groups[1].Value
                            source.Substring(0, min index source.Length).Split('\n').Length
                        | _ -> 0

                    Some(Path.GetRelativePath(Workspace.``.``, file), line, ex.Message)
            )

        for file, line, message in failures do
            AnsiConsole.MarkupLineInterpolated $"[red]INVALID[/] {file}:{line}: {message}"

        if failures.IsEmpty then
            printfn $"all %d{files.Length} queries compile against the grammar"
            0
        else
            1
