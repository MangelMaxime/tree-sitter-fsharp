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
        Steps.build false false
        use source = Parser.load Parser.defaultPath
        use signature = Parser.loadAs Parser.Signature Parser.Signature.ParserPath

        let signatureDirs =
            [
                Path.Combine(Workspace.queries.``.``, "signature")
                Path.Combine(Workspace.queries.nvim.``.``, "signature")
                Path.Combine(Workspace.queries.zed.``.``, "signature")
            ]

        let files =
            [
                Workspace.queries.``.``
                Workspace.queries.zed.``.``
                Workspace.queries.nvim.``.``
                yield! signatureDirs
            ]
            |> List.collect (fun dir -> Directory.GetFiles(dir, "*.scm") |> List.ofArray)
            |> List.sort

        let failures =
            files
            |> List.choose (fun file ->
                let text = File.ReadAllText file

                let language =
                    if List.contains (Path.GetDirectoryName file) signatureDirs then
                        signature
                    else
                        source

                try
                    use _ = language.CreateQuery text
                    None
                with ex ->
                    let line =
                        match Regex.Match(ex.Message, @"index (\d+)") with
                        | m when m.Success ->
                            let index = int m.Groups[1].Value
                            text.Substring(0, min index text.Length).Split('\n').Length
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
