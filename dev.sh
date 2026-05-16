#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HELIX_GRAMMARS="$HOME/.config/helix/runtime/grammars"
HELIX_QUERIES="$HOME/.config/helix/runtime/queries/fsharp"

# --- Compile ---
echo "Generating parser..."
tree-sitter generate "$REPO_DIR/grammar.js"

echo "Building parser.so..."
tree-sitter build --output "$REPO_DIR/parser.so"

# --- Grammar symlink ---
echo "Linking grammar..."
mkdir -p "$HELIX_GRAMMARS"
ln -sf "$REPO_DIR/parser.so" "$HELIX_GRAMMARS/fsharp.so"

# --- Query symlinks ---
if [ -d "$REPO_DIR/queries" ] && compgen -G "$REPO_DIR/queries/*.scm" > /dev/null 2>&1; then
    echo "Linking queries..."
    mkdir -p "$HELIX_QUERIES"
    for scm in "$REPO_DIR/queries/"*.scm; do
        ln -sf "$scm" "$HELIX_QUERIES/$(basename "$scm")"
    done
fi

echo "Done."
