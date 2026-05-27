#!/usr/bin/env node
// Verify that every keyword which begins a declaration in grammar.js OR
// continues a structural construct also appears in scanner.c's `blockers[]`
// array. The blockers[] list governs which keywords stop a `virtual_semi`
// from firing — if it drifts behind the grammar (new decl keyword added but
// not blocked), virtual_semi inappropriately fires and breaks parsing in
// hard-to-diagnose ways.
//
// This script is intentionally simple: it hard-codes the list of expected
// blockers (grouped by purpose, with the source rule for each) and asserts
// that every expected entry is present in scanner.c. The hard-coded list is
// the documented invariant; the scanner is the implementation. Drift in
// either direction is detected:
//   - Missing in scanner: ERROR (the actual drift risk).
//   - Extra in scanner: WARN (probably a stale entry).
//
// Usage:  node scripts/check-blockers.js
// Exit:   0 if everything matches; non-zero on missing/error.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.resolve(__dirname, "..");

// [keyword, grammar rule (for the error message)]. Order mirrors scanner.c.
const EXPECTED = [
    // ── Continuation keywords (must NOT start a new sibling expression) ──
    ["else",      "if_expression"],
    ["elif",      "if_expression"],
    ["then",      "if_expression"],
    ["with",      "type_extension, try_expression, object_expression, etc."],
    ["do",        "for_expression / while_expression / do_stmt"],
    ["in",        "for_expression / let_expression"],
    ["and",       "let_and_binding / type_and_decl / SRTP constraints"],
    ["finally",   "try_expression"],
    ["of",        "union_case / exception_decl"],

    // ── Closes a class/struct/interface/begin block ──
    ["end",       "class_type_defn / struct_type_defn / interface_type_defn / begin_end_expression"],

    // ── Class-body sibling declaration starters ──
    ["member",    "member_defn (via _instance_member_prefix)"],
    ["abstract",  "abstract_member_defn"],
    ["override",  "member_defn (via _instance_member_prefix)"],
    ["default",   "member_defn (via _instance_member_prefix)"],
    ["inherit",   "inherit_decl"],
    ["interface", "interface_impl / interface_type_defn"],
    ["val",       "val_field"],
    ["new",       "secondary_constructor / new_expression"],
    ["static",    "static prefix on let/do/member"],

    // ── Module-body / top-level declaration starters ──
    ["let",       "let_binding"],
    ["type",      "type_decl / type_extension"],
    ["module",    "module_decl"],
    ["namespace", "namespace_decl"],
    ["exception", "exception_decl"],
    ["open",      "import_decl"],
];

const scannerPath = path.join(REPO, "src", "scanner.c");
const scannerSrc = fs.readFileSync(scannerPath, "utf8");

// Locate the `blockers[]` array literal and pull its quoted strings.
const arrayMatch = scannerSrc.match(
    /static\s+const\s+char\s+\*blockers\[\]\s*=\s*\{([\s\S]*?)NULL\s*,?\s*\}/,
);
if (!arrayMatch) {
    console.error("ERROR: could not locate `blockers[]` array in src/scanner.c");
    process.exit(2);
}
const actualBlockers = [...arrayMatch[1].matchAll(/"([^"]+)"/g)].map((m) => m[1]);
const actualSet = new Set(actualBlockers);

// Check every expected keyword is in the scanner's list.
const missing = EXPECTED.filter(([kw]) => !actualSet.has(kw));
if (missing.length > 0) {
    console.error(
        `\nERROR: ${missing.length} expected blocker(s) missing from src/scanner.c's blockers[]:`,
    );
    for (const [kw, where] of missing) {
        console.error(`  - "${kw}"  (used by ${where})`);
    }
    console.error(
        "\nFix: add the missing entries to `blockers[]` in src/scanner.c.\n",
    );
    process.exit(1);
}

// Warn (but don't fail) if scanner has extras we don't know about.
const expectedSet = new Set(EXPECTED.map(([kw]) => kw));
const extras = actualBlockers.filter((kw) => !expectedSet.has(kw));
if (extras.length > 0) {
    console.warn(
        `\nWARN: scanner.c has ${extras.length} blocker(s) not in this script's expected list:`,
    );
    for (const kw of extras) {
        console.warn(`  - "${kw}"`);
    }
    console.warn(
        "\nEither add them to EXPECTED (with the rule that introduced them) " +
        "or remove them from scanner.c if they're stale.\n",
    );
}

console.log(
    `OK: all ${EXPECTED.length} expected blockers present in src/scanner.c.`,
);
