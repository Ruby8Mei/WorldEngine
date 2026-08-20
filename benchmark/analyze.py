#!/usr/bin/env python3
"""Analyze inop_benchmark's CSV output.

Correctness/regression reporting only — no timing analysis. The cipher
core is permanently frozen against optimization changes (see DESIGN.md),
so encrypt_time_us/decrypt_time_us/chars_per_sec have nothing actionable
to say; this tool ignores them by design.

Usage:
    python analyze.py                              # summarize benchmark.csv
    python analyze.py --csv path/to/other.csv
    python analyze.py --baseline previous_run.csv   # also flag regressions

Exit code is 1 if the run has any failures (or, with --baseline, any
regressions), 0 otherwise — usable as a pass/fail gate.
"""
import argparse
import csv
import sys
from collections import defaultdict

# Failure details / corpus content can carry real diacritics, and Windows
# consoles don't default to UTF-8 — force it so output never mangles.
sys.stdout.reconfigure(encoding="utf-8")


def load_rows(path):
    with open(path, encoding="utf-8") as f:
        return list(csv.DictReader(f))


def summarize(rows):
    by_lang = defaultdict(lambda: [0, 0])  # [passed, failed]
    by_cat = defaultdict(lambda: [0, 0])
    for r in rows:
        ok = r["success"] == "1"
        by_lang[r["language"]][0 if ok else 1] += 1
        by_cat[r["category"]][0 if ok else 1] += 1
    return by_lang, by_cat


def print_breakdown(title, counts):
    print(f"\n{title}:")
    for key in sorted(counts):
        passed, failed = counts[key]
        flag = "!!" if failed else "  "
        print(f"  {flag} {key:20s} {passed + failed:5d} tests, {failed} failed")


def print_report(rows, csv_path):
    failed = [r for r in rows if r["success"] == "0"]
    print(f"INOP benchmark analysis — {csv_path}")
    print(f"{len(rows)} test case(s), {len(failed)} failure(s)")

    by_lang, by_cat = summarize(rows)
    print_breakdown("By language", by_lang)
    print_breakdown("By category", by_cat)

    if failed:
        print(f"\nFailing rows ({len(failed)}):")
        for r in failed:
            print(f"  #{r['test_id']} {r['language']}/{r['category']} "
                  f"(config {r['config_index']}, msg {r['message_index']}, "
                  f"len {r['input_length']}): {r['failure_detail']}")


# A test case is identified by its position in the matrix (language,
# category, config_index, message_index), not by its exact message text —
# the RNG seed is fixed, so the same position means the same generated
# settings/category run across two invocations, even if corpus content
# changed slightly between them. That's what makes "did this position
# regress" a meaningful question instead of an exact-string match.
def test_key(r):
    return (r["language"], r["category"], r["config_index"], r["message_index"])


def compare(rows, baseline_rows):
    current = {test_key(r): r["success"] for r in rows}
    baseline = {test_key(r): r["success"] for r in baseline_rows}
    shared = current.keys() & baseline.keys()
    regressions = sorted(k for k in shared if baseline[k] == "1" and current[k] == "0")
    fixes = sorted(k for k in shared if baseline[k] == "0" and current[k] == "1")
    new_tests = current.keys() - baseline.keys()
    removed_tests = baseline.keys() - current.keys()
    return regressions, fixes, new_tests, removed_tests


def print_comparison(regressions, fixes, new_tests, removed_tests):
    print("\n--- Comparison against baseline ---")
    if regressions:
        print(f"REGRESSIONS ({len(regressions)}) — passed in baseline, now failing:")
        for lang, cat, ci, mi in regressions:
            print(f"  {lang}/{cat} config={ci} msg={mi}")
    else:
        print("No regressions.")
    if fixes:
        print(f"\nFixed ({len(fixes)}) — failed in baseline, now passing:")
        for lang, cat, ci, mi in fixes:
            print(f"  {lang}/{cat} config={ci} msg={mi}")
    if new_tests:
        print(f"\n{len(new_tests)} test case(s) present now but not in baseline "
              "(e.g. a new language or a longer corpus).")
    if removed_tests:
        print(f"{len(removed_tests)} test case(s) present in baseline but missing now.")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--csv", default="benchmark.csv",
                     help="benchmark CSV to analyze (default: benchmark.csv)")
    ap.add_argument("--baseline", default=None,
                     help="a previous benchmark CSV to diff against for regressions")
    args = ap.parse_args()

    rows = load_rows(args.csv)
    print_report(rows, args.csv)

    exit_code = 1 if any(r["success"] == "0" for r in rows) else 0

    if args.baseline:
        baseline_rows = load_rows(args.baseline)
        regressions, fixes, new_tests, removed_tests = compare(rows, baseline_rows)
        print_comparison(regressions, fixes, new_tests, removed_tests)
        if regressions:
            exit_code = 1

    sys.exit(exit_code)


if __name__ == "__main__":
    main()
