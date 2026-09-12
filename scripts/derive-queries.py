#!/usr/bin/env python3
"""Derive the editor-specific highlight queries from the Helix one.

queries/highlights.scm (Helix capture names) is the source of truth. Every
other editor wants the same patterns under its own capture names, and keeping
two 800-line files in step by hand does not work: a rule added to one file is
forgotten in the other.

  queries/nvim/highlights.scm   GENERATED: Helix patterns with Neovim capture
                                names and predicates, plus the hand-written
                                Neovim-only rules in scripts/nvim-highlights-extra.scm
                                appended (last capture wins, so they refine).
  queries/zed/highlights.scm    HAND-MAINTAINED (its comments differ): checked
                                to contain the same patterns as Helix after the
                                Helix->Zed capture mapping from its header.

    scripts/derive-queries.py           rewrite the generated files
    scripts/derive-queries.py --check   exit 1 if anything is out of date (CI)
"""
from __future__ import annotations

import difflib
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HELIX = REPO / "queries" / "highlights.scm"
NVIM = REPO / "queries" / "nvim" / "highlights.scm"
NVIM_EXTRA = REPO / "scripts" / "nvim-highlights-extra.scm"
ZED = REPO / "queries" / "zed" / "highlights.scm"

# Helix -> Neovim (nvim-treesitter capture names). Longest names first so
# `keyword.control.access` is tried before `keyword.control`.
NVIM_MAP = {
    "keyword.control.access": "keyword.modifier",
    "keyword.control": "keyword",
    "keyword.directive": "keyword.directive",
    "keyword.operator": "keyword.operator",
    "comment.line.documentation": "comment.documentation",
    "comment.block.documentation": "comment.documentation",
    "comment.line": "comment",
    "comment.block": "comment",
    "constant.numeric.integer": "number",
    "constant.numeric.float": "number.float",
    "constant.builtin.boolean": "boolean",
    "constant.character": "character",
    "variable.other.member": "variable.member",
    "type.parameter": "type",
    "namespace": "module",
    "punctuation": "punctuation.delimiter",
    # Rendered with the default text colour on purpose; Neovim ignores captures
    # that start with an underscore.
    "wildcard": "_wildcard",
}

# Helix -> Zed, as documented in the header of queries/zed/highlights.scm.
ZED_MAP = {
    "keyword.control.access": "keyword",
    "keyword.control": "keyword",
    "keyword.operator": "keyword",
    "keyword.directive": "preproc",
    "comment.line.documentation": "comment.doc",
    "comment.block.documentation": "comment.doc",
    "comment.line": "comment",
    "comment.block": "comment",
    "constant.numeric.integer": "number",
    "constant.numeric.float": "number",
    "constant.builtin.boolean": "boolean",
    "constant.character": "constant",
    "variable.other.member": "property",
    "variable.builtin": "variable.special",
    "type.parameter": "type",
    "namespace": "type @namespace",
}

CAPTURE = re.compile(r"@([A-Za-z_][\w.]*)")


def map_captures(text: str, table: dict[str, str]) -> str:
    def sub(m: re.Match) -> str:
        name = m.group(1)
        return "@" + table.get(name, name)

    return CAPTURE.sub(sub, text)


ANY_OF = re.compile(r'\(#match\? (@[\w.]+) "\^\(([\w|]+)\)\$"\)')
MATCH = re.compile(r"\(#match\?")


def nvim_predicates(text: str) -> str:
    """`#match?` takes a Vim regex in Neovim, where `(`, `|` and `+` are
    literal. The Helix file only uses two shapes: anchored alternations, which
    become `#any-of?`, and character classes, which are valid Lua patterns."""

    def any_of(m: re.Match) -> str:
        words = " ".join(f'"{w}"' for w in m.group(2).split("|"))
        return f"(#any-of? {m.group(1)} {words})"

    text = ANY_OF.sub(any_of, text)
    return MATCH.sub("(#lua-match?", text)


def strip_comments(text: str) -> str:
    out = []
    for line in text.splitlines():
        # A `;` inside a string literal ("," ";") is not a comment.
        in_str = False
        for i, ch in enumerate(line):
            if ch == '"' and (i == 0 or line[i - 1] != "\\"):
                in_str = not in_str
            elif ch == ";" and not in_str:
                line = line[:i]
                break
        out.append(line)
    return "\n".join(out)


def patterns(text: str) -> list[str]:
    """Top-level patterns of a query file, whitespace-normalised."""
    text = strip_comments(text)
    result, depth, cur, in_str = [], 0, [], False
    for ch in text:
        if in_str:
            cur.append(ch)
            if ch == '"':
                in_str = False
            continue
        if ch == '"':
            in_str = True
        elif ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        cur.append(ch)
        if depth == 0 and ch in ")]":
            result.append(" ".join("".join(cur).split()))
            cur = []
    tail = " ".join("".join(cur).split())
    if tail:
        # A bare capture after a pattern, e.g. `... ] @keyword`, belongs to it.
        for piece in re.split(r"(?=@)", tail):
            if piece.strip():
                result[-1] += " " + piece.strip()
    # Merge trailing `@capture` tokens that followed a closing bracket.
    merged: list[str] = []
    for p in result:
        if p.startswith("@") and merged:
            merged[-1] += " " + p
        else:
            merged.append(p)
    return merged


def render_nvim(helix: str, extra: str) -> str:
    body = nvim_predicates(map_captures(helix, NVIM_MAP))
    header = (
        "; GENERATED by scripts/derive-queries.py from ../highlights.scm - do not edit.\n"
        "; Neovim capture names (nvim-treesitter conventions) and `#lua-match?`/`#any-of?`\n"
        "; predicates; the Neovim-only refinements at the end come from\n"
        "; scripts/nvim-highlights-extra.scm. Regenerate: scripts/derive-queries.py\n\n"
    )
    return header + body.rstrip("\n") + "\n\n" + extra.rstrip("\n") + "\n"


def check_zed(helix: str, zed: str) -> list[str]:
    want = patterns(map_captures(helix, ZED_MAP))
    have = patterns(zed)
    if want == have:
        return []
    return list(
        difflib.unified_diff(want, have, "helix mapped to zed", "queries/zed/highlights.scm", lineterm="", n=1)
    )


def main() -> int:
    check = "--check" in sys.argv[1:]
    helix = HELIX.read_text(encoding="utf-8")
    extra = NVIM_EXTRA.read_text(encoding="utf-8") if NVIM_EXTRA.exists() else ""
    status = 0

    nvim = render_nvim(helix, extra)
    if check:
        current = NVIM.read_text(encoding="utf-8") if NVIM.exists() else ""
        if current != nvim:
            print(f"{NVIM.relative_to(REPO)} is out of date; run scripts/derive-queries.py")
            status = 1
        else:
            print(f"{NVIM.relative_to(REPO)} is up to date")
    else:
        NVIM.parent.mkdir(parents=True, exist_ok=True)
        NVIM.write_text(nvim, encoding="utf-8", newline="\n")
        print(f"wrote {NVIM.relative_to(REPO)}")

    diff = check_zed(helix, ZED.read_text(encoding="utf-8"))
    if diff:
        print(f"{ZED.relative_to(REPO)} has drifted from queries/highlights.scm (comments ignored):")
        print("\n".join(diff))
        status = 1
    else:
        print(f"{ZED.relative_to(REPO)} matches queries/highlights.scm")
    return status


if __name__ == "__main__":
    sys.exit(main())
