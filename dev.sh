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
rm -rf "$HELIX_GRAMMARS"
mkdir -p "$HELIX_GRAMMARS"
cp "$REPO_DIR/parser.so" "$HELIX_GRAMMARS/fsharp.so"

# --- Query symlinks ---
echo "Linking queries..."
rm -rf "$HELIX_QUERIES"
mkdir -p "$HELIX_QUERIES"

# Copy repo query files
if [ -d "$REPO_DIR/queries" ]; then
    for scm in "$REPO_DIR/queries/"*.scm; do
        [ -e "$scm" ] || continue
        cp "$scm" "$HELIX_QUERIES/$(basename "$scm")"
    done
fi

# Create empty query files for any types not defined in the repo.
# This prevents Helix from falling back to built-in system queries
# which reference node types that don't exist in this minimal grammar.
for query_type in highlights injections locals textobjects indents tags rainbows; do
    if [ ! -f "$HELIX_QUERIES/${query_type}.scm" ]; then
        touch "$HELIX_QUERIES/${query_type}.scm"
    fi
done

echo "Done."
