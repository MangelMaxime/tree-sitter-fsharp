#!/usr/bin/env bash
# Install tree-sitter-fsharp-helix for Neovim's built-in tree-sitter.
#
# Unlike Helix, Neovim has no built-in command to fetch/build a grammar, so
# this script builds the parser (parser.so) and installs it together with the
# query files into your Neovim config:
#
#   <config>/parser/fsharp.so
#   <config>/queries/fsharp/*.scm
#
# where <config> defaults to ${XDG_CONFIG_HOME:-~/.config}/nvim.
#
# Requirements: git, a C compiler (cc/gcc/clang), and the tree-sitter CLI
# (or `npx`, which fetches it on demand).
#
# Usage (from a checkout):
#   ./scripts/install-nvim.sh                # build from this checkout
#
# Or one-shot from anywhere (clones the repo, then builds):
#   curl -fsSL https://raw.githubusercontent.com/MangelMaxime/tree-sitter-fsharp/main/scripts/install-nvim.sh | bash
#   curl -fsSL https://raw.githubusercontent.com/MangelMaxime/tree-sitter-fsharp/main/scripts/install-nvim.sh | bash -s -- some-branch
#
# Override the install location with NVIM_CONFIG_DIR, or the source repo
# (e.g. a fork) with TS_FSHARP_REPO:
#   NVIM_CONFIG_DIR=~/.config/nvim-test ./scripts/install-nvim.sh
#   TS_FSHARP_REPO=YourFork/your-repo ./scripts/install-nvim.sh some-branch

set -euo pipefail

REPO="${TS_FSHARP_REPO:-MangelMaxime/tree-sitter-fsharp}"
REF="${1:-main}"
CONFIG_DIR="${NVIM_CONFIG_DIR:-${XDG_CONFIG_HOME:-$HOME/.config}/nvim}"
PARSER_DEST="$CONFIG_DIR/parser"
QUERY_DEST="$CONFIG_DIR/queries/fsharp"

# Every query file we ship. Missing ones get empty placeholders so Neovim
# doesn't fall back to a tree-sitter plugin's bundled F# queries (built for
# the upstream Ionide grammar, whose node types don't exist here).
QUERIES=(highlights indents injections locals rainbows tags textobjects)

# --- Pick a tree-sitter CLI ---
if command -v tree-sitter >/dev/null 2>&1; then
    TS=(tree-sitter)
elif command -v npx >/dev/null 2>&1; then
    TS=(npx --yes tree-sitter)
else
    echo "error: need the 'tree-sitter' CLI or 'npx' on PATH" >&2
    exit 1
fi

if ! command -v cc >/dev/null 2>&1 && ! command -v gcc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
    echo "error: a C compiler (cc/gcc/clang) is required to build the parser" >&2
    exit 1
fi

# --- Locate a source checkout, or clone one ---
SELF="${BASH_SOURCE[0]:-}"
if [ -n "$SELF" ] && [ -f "$(dirname "$SELF")/../grammar.js" ]; then
    SRC="$(cd "$(dirname "$SELF")/.." && pwd)"
    echo "Using local checkout: $SRC"
else
    command -v git >/dev/null 2>&1 || { echo "error: git is required to clone $REPO" >&2; exit 1; }
    TMP="$(mktemp -d)"
    trap 'rm -rf "$TMP"' EXIT
    echo "Cloning $REPO@$REF ..."
    if ! git clone --depth 1 --branch "$REF" "https://github.com/$REPO" "$TMP/src" 2>/dev/null; then
        # REF may be a commit sha (can't --branch to it): full clone + checkout.
        git clone "https://github.com/$REPO" "$TMP/src"
        git -C "$TMP/src" checkout -q "$REF"
    fi
    SRC="$TMP/src"
fi

# --- Build the parser ---
echo "Building parser ..."
( cd "$SRC" && "${TS[@]}" generate && "${TS[@]}" build --output "$SRC/fsharp.so" )

# --- Install ---
mkdir -p "$PARSER_DEST" "$QUERY_DEST"

cp "$SRC/fsharp.so" "$PARSER_DEST/fsharp.so"
echo "Installed parser → $PARSER_DEST/fsharp.so"

echo "Installing queries → $QUERY_DEST"
for q in "${QUERIES[@]}"; do
    if [ -s "$SRC/queries/${q}.scm" ]; then
        cp "$SRC/queries/${q}.scm" "$QUERY_DEST/${q}.scm"
        echo "  ${q}.scm"
    else
        : > "$QUERY_DEST/${q}.scm"
        echo "  ${q}.scm  (empty placeholder)"
    fi
done

cat <<EOF

Done. One last step — add this to your init.lua so Neovim uses the grammar:

    vim.filetype.add({ extension = { fs = "fsharp", fsx = "fsharp", fsi = "fsharp" } })
    vim.api.nvim_create_autocmd("FileType", {
        pattern = "fsharp",
        callback = function()
            vim.treesitter.start() -- highlighting
        end,
    })

Then open a .fs / .fsx file (or :edit to reload one).
EOF
