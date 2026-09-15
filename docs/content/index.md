---
title: Tree-sitter for F#
description: An F# grammar for tree-sitter, for Helix, Zed and Neovim
layout: splash
---

<div class="landing">
<section class="landing-hero">

<p class="landing-eyebrow">A tree-sitter grammar for F#</p>

<h1 class="landing-hero__title">Highlighting that <em>reads</em> F#</h1>

<p class="landing-hero__lede">For Helix, Zed and Neovim: colours that follow the structure of your code, indentation that follows the offside rule, textobjects and rainbow brackets.</p>

<p class="landing-actions">
<a class="landing-button landing-button--primary" href="/tree-sitter-fsharp/users/helix/">Install for Helix</a>
<a class="landing-button" href="/tree-sitter-fsharp/users/zed/">Zed</a>
<a class="landing-button" href="/tree-sitter-fsharp/users/neovim/">Neovim</a>
</p>


</section>

<div class="landing-intro">

<p class="landing-lede">What you see here is what the editor sees.</p>

</div>

```fsharp
namespace Scriptorium.Quill

open Scriptorium.Ink
open Scriptorium.Parchment.Sinks
open Fable.Core.JsInterop

module internal Advanced =

    let hasFocused (tests: TestCase list) : bool =
        let rec check test =
            match test with
            | TestCase.SyncTest ctx -> ctx.Mark = TestMark.Focused
            | TestCase.AsyncTest ctx -> ctx.Mark = TestMark.Focused
            | TestCase.TestList { Mark = TestMark.Focused } -> true
            | TestCase.TestList ctx -> List.exists check ctx.Tests

        List.exists check tests

    /// Collects every leaf test path in the tree as a string list (root to leaf).
    let collectPaths (tests: TestCase list) : string list list =
        let rec collect (path: string list) test =
            match test with
            | TestCase.SyncTest def -> [ List.rev (def.Name :: path) ]
            | TestCase.AsyncTest def -> [ List.rev (def.Name :: path) ]
            | TestCase.TestList def -> def.Tests |> List.collect (collect (def.Name :: path))

        tests |> List.collect (collect [])

    /// Returns the full paths of any tests that share a path with another test.
    let findDuplicatePaths (tests: TestCase list) : string list =
        collectPaths tests
        |> List.groupBy id
        |> List.choose (fun (path, group) ->
            if group.Length > 1 then
                Some(path |> String.concat " > ")
            else
                None
        )

    let indent (depth: int) = System.String(' ', depth * 2)
```

<section>

<h2 class="landing-section__title">What you get</h2>

<div class="landing-grid landing-grid--pairs">

<article class="landing-card">
<h3>Tested on real code</h3>
<p>Every change is checked against popular open-source F# projects and the F# compiler's own sources. A file that parses worse than before fails the build.</p>
</article>

<article class="landing-card">
<h3>Failures stay local</h3>
<p>A construct the parser does not know loses its colours on that line. The rest of the file keeps them.</p>
</article>

<article class="landing-card">
<h3>Docs belong to their code</h3>
<p><code>///</code> comments attach to the declaration below, so expand-selection grows from value to binding to documented binding to module.</p>
</article>

<article class="landing-card">
<h3>The tests are the specification</h3>
<p>Every colour decision is pinned by an assertion in a test file, and the queries follow the tests. A colour that moves shows up as a diff.</p>
</article>

</div>

</section>

<section>

<h2 class="landing-section__title">Pick your editor</h2>

<div class="landing-grid">

<article class="landing-card landing-card--editor">
<h3>Helix</h3>
<p>Highlights, injections, locals, textobjects, indents, tags and rainbow brackets. Two grammar entries, one script for the queries.</p>
<a class="landing-card__link" href="/tree-sitter-fsharp/users/helix/">Install for Helix</a>
</article>

<article class="landing-card landing-card--editor">
<h3>Zed</h3>
<p>Ships in Zed's F# extension: highlights, injections, textobjects, indents, outline, brackets and overrides, with FsAutoComplete.</p>
<a class="landing-card__link" href="/tree-sitter-fsharp/users/zed/">Install for Zed</a>
</article>

<article class="landing-card landing-card--editor">
<h3>Neovim</h3>
<p>Highlights, injections, indents, folds and textobjects on Neovim's built-in tree-sitter. One script builds and installs everything.</p>
<a class="landing-card__link" href="/tree-sitter-fsharp/users/neovim/">Install for Neovim</a>
</article>

</div>

</section>

<section class="landing-hero landing-hero--closing">

<h2 class="landing-hero__title">Contribute a colour or a rule</h2>

<p class="landing-hero__lede">One script runs every gate: corpus, highlight assertions, expansion fixtures, query checks and the real-world bench.</p>

<p class="landing-actions">
<a class="landing-button landing-button--primary" href="/tree-sitter-fsharp/contributors/getting-started/">Get started</a>
<a class="landing-button" href="/tree-sitter-fsharp/contributors/highlighting/">How highlighting is decided</a>
</p>

</section>
</div>
