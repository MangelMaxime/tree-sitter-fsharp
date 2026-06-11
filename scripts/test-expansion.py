#!/usr/bin/env python3
"""Expansion tests: simulate Helix expand-selection against fixture files.

Helix's `expand_selection` walks the syntax-tree parent chain from the node
under the cursor, skipping parents with the same range. That chain is fully
determined by the parse tree's node EXTENTS — which tree-sitter's corpus
tests cannot assert (they compare structure only). This harness fills the gap.

Fixture format (test/expansion/*.txt), mirroring the corpus style:

    ================================================================================
    test name
    ================================================================================
    <F# source with a single ‸ marking the cursor>
    --------------------------------------------------------------------------------
    >> step
    <exact text Helix would select at step 1>
    >> step
    <exact text at step 2>
    ...

Steps must match the BEGINNING of the simulated chain (a test may stop early;
it doesn't need to walk all the way to source_file). Run from the repo root:

    python3 scripts/test-expansion.py            # all fixtures
    python3 scripts/test-expansion.py -i NAME    # filter by substring
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
FIXTURES = REPO / "test" / "expansion"
HEADER = "=" * 80
DIVIDER = "-" * 80
STEP = ">> step"
MARKER = "‸"

NODE_RE = re.compile(
    r"\(([A-Za-z_]+) \[(\d+), (\d+)\] - \[(\d+), (\d+)\]"
)


def parse_fixtures(path):
    lines = path.read_text().split("\n")
    tests, i = [], 0
    while i < len(lines):
        if lines[i] == HEADER:
            name = lines[i + 1]
            i += 3  # past header, name, header
            src_lines = []
            while lines[i] != DIVIDER:
                src_lines.append(lines[i])
                i += 1
            i += 1  # past divider
            steps, cur = [], None
            while i < len(lines) and lines[i] != HEADER:
                if lines[i] == STEP:
                    if cur is not None:
                        steps.append("\n".join(cur))
                    cur = []
                elif cur is not None:
                    cur.append(lines[i])
                i += 1
            if cur is not None:
                steps.append("\n".join(cur).rstrip("\n"))
            steps = [s.rstrip("\n") for s in steps]
            tests.append((name, "\n".join(src_lines), steps))
        else:
            i += 1
    return tests


def cursor_position(src):
    row = 0
    for line_no, line in enumerate(src.split("\n")):
        col = line.find(MARKER)
        if col != -1:
            return line_no, col
    raise ValueError("fixture has no ‸ cursor marker")


def node_ranges(sexp):
    out = []
    for m in NODE_RE.finditer(sexp):
        out.append(
            (
                (int(m.group(2)), int(m.group(3))),
                (int(m.group(4)), int(m.group(5))),
                m.group(1),
            )
        )
    return out


def contains(start, end, pos):
    return start <= pos < end


def slice_range(src_lines, start, end):
    (sr, sc), (er, ec) = start, end
    if er >= len(src_lines):          # root node ends past the last line
        er, ec = len(src_lines) - 1, len(src_lines[-1])
    if sr == er:
        return src_lines[sr][sc:ec]
    parts = [src_lines[sr][sc:]]
    parts.extend(src_lines[sr + 1 : er])
    parts.append(src_lines[er][:ec])
    return "\n".join(parts)


def expansion_chain(sexp, src, pos):
    """Nodes containing pos, smallest extent first, deduped — Helix's walk."""
    src_lines = src.split("\n")
    containing = [
        (start, end, name)
        for (start, end, name) in node_ranges(sexp)
        if contains(start, end, pos)
    ]
    # parents in a tree nest, so sorting by (size) yields the parent chain;
    # sort key: rows spanned, then cols
    containing.sort(key=lambda n: (n[1][0] - n[0][0], n[1][1] - n[0][1] if n[1][0] == n[0][0] else 10**9))
    chain, seen = [], set()
    for start, end, name in containing:
        key = (start, end)
        if key in seen:
            continue  # Helix skips same-range parents
        seen.add(key)
        chain.append(slice_range(src_lines, start, end).rstrip("\n"))
    return chain


def run_test(name, src, expected):
    row, col = cursor_position(src)
    clean = src.replace(MARKER, "", 1)
    with tempfile.NamedTemporaryFile("w", suffix=".fsx", delete=False) as f:
        f.write(clean + "\n")
        tmp = f.name
    sexp = subprocess.run(
        ["npx", "tree-sitter", "parse", tmp],
        cwd=REPO,
        capture_output=True,
        text=True,
        timeout=30,
    ).stdout
    chain = expansion_chain(sexp, clean, (row, col))
    failures = []
    for i, want in enumerate(expected):
        got = chain[i] if i < len(chain) else "<chain exhausted>"
        if got != want:
            failures.append((i + 1, want, got))
    return failures, chain


def main():
    filt = ""
    if len(sys.argv) >= 3 and sys.argv[1] == "-i":
        filt = sys.argv[2]
    total = passed = 0
    for path in sorted(FIXTURES.glob("*.txt")):
        for name, src, steps in parse_fixtures(path):
            if filt and filt not in name:
                continue
            total += 1
            failures, chain = run_test(name, src, steps)
            if not failures:
                passed += 1
                print(f"  ok   {name}")
            else:
                print(f"  FAIL {name}")
                for step, want, got in failures:
                    print(f"       step {step}:")
                    print("         expected: " + want.replace("\n", "\n                   "))
                    print("         got:      " + got.replace("\n", "\n                   "))
    print(f"\n{passed}/{total} expansion tests passed")
    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
