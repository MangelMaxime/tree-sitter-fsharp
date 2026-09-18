; ----------------------------------------------------------------------------
; Neovim-only refinements. `./build.sh derive-queries` appends this file to the
; mapped Helix rules; the last capture of a node wins, so these narrow the
; one-bucket Helix captures into the groups nvim-treesitter themes style.

; Helix keeps every control keyword under @keyword.control.
["if" "then" "else" "elif" "match" "match!" "when"] @keyword.conditional
["for" "while" "to" "downto"] @keyword.repeat
["return" "return!" "yield" "yield!"] @keyword.return
(try_expression ["try" "with" "finally"] @keyword.exception)
(match_expression "with" @keyword.conditional)

["let" "use" "member" "fun" "function"] @keyword.function

["inherit" "interface" "class" "struct" "delegate" "enum"] @keyword.type

"extern" @keyword.modifier

; Computation-expression builders (`async { }`, `task { }`): nvim-treesitter
; files them under @constant.macro, as ionide/tree-sitter-fsharp does. The
; element-DSL form with arguments (`div() { }`) stays a function call.
(computation_expression builder: (long_identifier) @constant.macro)
(computation_expression builder: (long_identifier) @function.call args: _)

; The throw-like helpers read as exception keywords in Neovim themes.
((long_identifier . (identifier) @keyword.exception .)
 (#any-of? @keyword.exception "raise" "reraise" "failwith" "failwithf"))
