/// External tools (tree-sitter CLI, git) and the content-hash cache that skips a step whose
/// inputs did not change.
module EasyBuild.Tools

open System
open System.IO
open System.Security.Cryptography
open System.Text
open BlackFox.CommandLine
open SimpleExec
open Spectre.Console.Cli
open EasyBuild.Workspace

/// Spectre cannot instantiate the abstract CommandSettings, so option-less commands use this.
type EmptySettings() =
    inherit CommandSettings()

let root = Path.GetFullPath Workspace.``.``

let private cli =
    Path.Combine(root, "node_modules", "tree-sitter-cli", "cli.js")

/// The tree-sitter CLI package.json: a CLI upgrade invalidates the generate and build caches.
let cliVersion =
    Path.Combine(root, "node_modules", "tree-sitter-cli", "package.json")

let private commandLine (arguments: string list) =
    arguments
    |> List.fold (fun line argument -> CmdLine.append argument line) CmdLine.empty
    |> CmdLine.toString

/// Runs a program and returns its exit code instead of throwing.
let runCode (name: string) (workingDirectory: string) (arguments: string list) =
    let mutable code = 0

    Command.Run(
        name,
        commandLine arguments,
        workingDirectory = workingDirectory,
        noEcho = true,
        handleExitCode =
            (fun exitCode ->
                code <- exitCode
                true)
    )

    code

let run (name: string) (workingDirectory: string) (arguments: string list) =
    Command.Run(name, commandLine arguments, workingDirectory = workingDirectory, noEcho = true)

/// Standard output of a program, trimmed.
let read (name: string) (workingDirectory: string) (arguments: string list) =
    let struct (output, _) =
        Command.ReadAsync(name, commandLine arguments, workingDirectory = workingDirectory).Result

    output.Trim()

/// Runs the tree-sitter CLI pinned by package-lock.json and returns its exit code.
let treeSitterCode (workingDirectory: string) (arguments: string list) =
    if not (File.Exists cli) then
        failwith "node_modules/tree-sitter-cli is missing: run `npm ci` first"

    let command = String.concat " " arguments
    printfn $"tree-sitter %s{command}"
    runCode "node" workingDirectory (cli :: arguments)

let treeSitter (workingDirectory: string) (arguments: string list) =
    let code = treeSitterCode workingDirectory arguments

    if code <> 0 then
        failwith $"tree-sitter exited with code %d{code}"

let git (workingDirectory: string) (arguments: string list) = run "git" workingDirectory arguments

let gitCode (workingDirectory: string) (arguments: string list) =
    runCode "git" workingDirectory arguments

let gitRead (workingDirectory: string) (arguments: string list) =
    read "git" workingDirectory arguments

let rec copyDirectory (source: string) (destination: string) =
    Directory.CreateDirectory destination |> ignore

    for file in Directory.GetFiles source do
        File.Copy(file, Path.Combine(destination, Path.GetFileName file), true)

    for directory in Directory.GetDirectories source do
        copyDirectory directory (Path.Combine(destination, Path.GetFileName directory))

let private cacheDirectory = Path.Combine(root, ".build-cache")

let private inputHash (inputs: string list) =
    use sha = SHA256.Create()
    let builder = StringBuilder()

    for input in List.sort inputs do
        builder.Append(Path.GetRelativePath(root, input)).Append ' ' |> ignore
        builder.AppendLine(Convert.ToHexString(sha.ComputeHash(File.ReadAllBytes input))) |> ignore

    builder.ToString()

/// Runs `step` unless every output exists and the inputs' contents match the last run.
let incremental
    (name: string)
    (announce: bool)
    (force: bool)
    (inputs: string list)
    (outputs: string list)
    (step: unit -> unit)
    =
    let cache = Path.Combine(cacheDirectory, name)
    let hash = inputHash inputs

    let upToDate =
        not force
        && outputs |> List.forall File.Exists
        && File.Exists cache
        && File.ReadAllText cache = hash

    if upToDate then
        if announce then
            printfn $"%s{name}: up to date"
    else
        step ()
        Directory.CreateDirectory cacheDirectory |> ignore
        File.WriteAllText(cache, hash)
