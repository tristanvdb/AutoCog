#!/usr/bin/env python3
"""
Generate the coverage report (Markdown + JSON) from gcovr and pytest-cov JSON,
and check measured coverage against thresholds.

Usage:
    python3 coverage_report.py <gcovr.json> [pytest-cov.json] \
        [--install-prefix=PATH] [--outdir=tests] [--ctest=N] [--pytest=N]

Always writes <outdir>/coverage.md and <outdir>/coverage.json.

Exit code: nonzero if any gating category is below its threshold (so the
coverage CI job fails, which in turn blocks a release -- release reuses this
artifact and never recomputes it). Both report files are written before a
nonzero exit, so the artifact and badges still reflect the failing run.

This module is the single source of truth for:
  - the template-file classification (TEMPLATE_FILES),
  - the coverage thresholds (THRESHOLDS, FILE_THRESHOLDS),
  - the pass/fail computation consumed by CI badges and the release gate.
"""

import json
import os
import sys
from collections import defaultdict
from datetime import datetime


# Template-heavy files where gcovr inflates line counts due to if-constexpr
# branches and per-tag template instantiations. Reported (and thresholded)
# separately so they don't distort the main C++ figure.
TEMPLATE_FILES = {
    "libs/autocog/compiler/stl/ast.hxx",
    "libs/autocog/compiler/stl/symbol-scanner.hxx",
    "libs/autocog/compiler/stl/parser.hxx",
    "libs/autocog/compiler/stl/ast/nodes.def",
}

# Category thresholds (percent). Single source of truth, shared by the badges
# and (transitively, via the CI exit code) the release gate. Every category
# here gates: if measured < threshold the run fails. `total` is informational
# only and intentionally has no threshold.
THRESHOLDS = {
    "cxx": 83.0,
    "cxx_templates": 36.0,
    "python": 87.0,
}

# Optional per-file thresholds for fine-grained control, keyed by the same
# path form used in the report (e.g. "libs/autocog/compiler/stl/evaluate.cxx").
# Empty for now; populate to gate individual files. A file with no entry has
# threshold None and always passes.
FILE_THRESHOLDS = {}


def load_cxx_coverage(path):
    """Parse gcovr JSON into {filepath: (lines, covered)}."""
    with open(path) as f:
        data = json.load(f)
    files = {}
    for entry in data.get("files", []):
        lines = entry["lines"]
        files[entry["file"]] = (len(lines), sum(1 for l in lines if l["count"] > 0))
    return files


def load_python_coverage(path, install_prefix=None):
    """Parse pytest-cov JSON into {module: (statements, covered)}.

    install_prefix: stripped from file paths (both sides resolved to absolute),
    so this works whether pytest-cov recorded absolute or run-relative paths.
    Falls back to the legacy "modules" heuristic when no prefix is given.
    """
    with open(path) as f:
        data = json.load(f)
    abs_prefix = os.path.abspath(install_prefix) if install_prefix else None
    files = {}
    for filepath, fdata in data.get("files", {}).items():
        s = fdata["summary"]
        abs_path = os.path.abspath(filepath)
        if abs_prefix and abs_path.startswith(abs_prefix):
            key = abs_path[len(abs_prefix):].lstrip("/")
        else:
            parts = filepath.split("/")
            if "modules" in parts:
                key = "/".join(parts[parts.index("modules") + 1:])
            else:
                key = filepath
        files[key] = (s["num_statements"], s["covered_lines"])
    return files


def split_templates(cxx_files):
    """Split C++ files into (regular, template-heavy)."""
    regular, templates = {}, {}
    for path, counts in cxx_files.items():
        (templates if path in TEMPLATE_FILES else regular)[path] = counts
    return regular, templates


def group_by_component(files, depth_map):
    """Group files into components based on path depth rules."""
    components = defaultdict(lambda: [0, 0, []])
    for filepath, (total, covered) in files.items():
        parts = filepath.split("/")
        key = filepath
        for prefix, depth in depth_map.items():
            if filepath.startswith(prefix):
                key = "/".join(parts[:depth])
                break
        components[key][0] += total
        components[key][1] += covered
        components[key][2].append((filepath, total, covered))
    return components


def _pct(covered, total):
    return round(100 * covered / total, 1) if total > 0 else 0.0


def pct_str(covered, total):
    return f"{covered / total * 100:.0f}%" if total > 0 else "N/A"


def totals(files):
    return sum(v[0] for v in files.values()), sum(v[1] for v in files.values())


def _file_node(path, total, covered):
    """Build a {measured, threshold, lines, pass} node for one file."""
    measured = _pct(covered, total)
    threshold = FILE_THRESHOLDS.get(path)
    passed = threshold is None or measured >= threshold
    return {"measured": measured, "threshold": threshold,
            "lines": total, "pass": passed}


def _category_node(name, files):
    """Build a category node (with per-file detail) and its threshold/pass."""
    total, covered = totals(files)
    measured = _pct(covered, total)
    threshold = THRESHOLDS.get(name)
    file_nodes = {p: _file_node(p, t, c) for p, (t, c) in files.items()}
    agg_pass = threshold is None or measured >= threshold
    files_pass = all(n["pass"] for n in file_nodes.values())
    return {
        "measured": measured,
        "threshold": threshold,
        "lines": total,
        "pass": agg_pass and files_pass,
        "files": file_nodes,
    }


def build_summary(cxx_files, py_files, ctest=None, pytest=None):
    """Compute the coverage.json structure. Single source of truth for pass/fail."""
    cxx_regular, cxx_templates = split_templates(cxx_files)

    summary = {
        "cxx": _category_node("cxx", cxx_regular),
        "cxx_templates": _category_node("cxx_templates", cxx_templates),
    }
    if py_files:
        summary["python"] = _category_node("python", py_files)

    reg_t, reg_c = totals(cxx_regular)
    py_t, py_c = totals(py_files) if py_files else (0, 0)
    summary["total"] = _pct(reg_c + py_c, reg_t + py_t)

    if ctest is not None:
        summary["ctest"] = ctest
    if pytest is not None:
        summary["pytest"] = pytest

    summary["pass"] = all(
        v["pass"]
        for k, v in summary.items()
        if isinstance(v, dict) and v.get("threshold") is not None
    )
    return summary


def report_failures(summary, out=sys.stderr):
    """Print failing categories and files. Returns True if anything failed."""
    failed = False
    for name, node in summary.items():
        if not isinstance(node, dict) or node.get("threshold") is None:
            continue
        if not node["pass"]:
            failed = True
            if node["measured"] < node["threshold"]:
                print(f"FAIL {name}: {node['measured']}% < threshold {node['threshold']}%", file=out)
            for fpath, fnode in node.get("files", {}).items():
                if not fnode["pass"]:
                    print(f"  FAIL {fpath}: {fnode['measured']}% < threshold {fnode['threshold']}%", file=out)
    return failed


def write_json(path, summary):
    with open(path, "w") as f:
        json.dump(summary, f, indent=2)


def write_report(out, cxx_files, py_files):
    """Write the full markdown report."""
    cxx_regular, cxx_templates = split_templates(cxx_files)

    reg_total, reg_covered = totals(cxx_regular)
    tpl_total, tpl_covered = totals(cxx_templates)
    py_total, py_covered = totals(py_files) if py_files else (0, 0)
    all_total = reg_total + tpl_total + py_total
    all_covered = reg_covered + tpl_covered + py_covered

    out.write("# Coverage Report\n\n")
    out.write(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")

    out.write("## Summary\n\n")
    out.write("| Category | Lines | Covered | Coverage |\n")
    out.write("|----------|------:|--------:|---------:|\n")
    out.write(f"| C++      | {reg_total} | {reg_covered} | {pct_str(reg_covered, reg_total)} |\n")
    out.write(f"| C++ (templates) | {tpl_total} | {tpl_covered} | {pct_str(tpl_covered, tpl_total)} |\n")
    if py_files:
        out.write(f"| Python   | {py_total} | {py_covered} | {pct_str(py_covered, py_total)} |\n")
    out.write(f"| **Total** | **{all_total}** | **{all_covered}** | **{pct_str(all_covered, all_total)}** |\n")
    out.write(f"| **Total (excl. templates)** | **{reg_total + py_total}** "
              f"| **{reg_covered + py_covered}** "
              f"| **{pct_str(reg_covered + py_covered, reg_total + py_total)}** |\n")
    out.write("\n")

    cxx_components = group_by_component(cxx_regular, {
        "libs/autocog/": 4,
        "bindings/": 2,
        "tools/": 2,
    })

    out.write("## C++ Coverage by Component\n\n")
    out.write("| Component | Lines | Covered | Coverage |\n")
    out.write("|-----------|------:|--------:|---------:|\n")
    for key in sorted(cxx_components):
        total, covered, _ = cxx_components[key]
        out.write(f"| `{key}` | {total} | {covered} | {pct_str(covered, total)} |\n")
    out.write(f"| **Total** | **{reg_total}** | **{reg_covered}** | **{pct_str(reg_covered, reg_total)}** |\n")
    out.write("\n")

    out.write("### C++ Per-File Detail\n\n")
    for comp_key in sorted(cxx_components):
        _, _, comp_files = cxx_components[comp_key]
        if len(comp_files) <= 1:
            continue
        out.write(f"<details><summary><code>{comp_key}</code></summary>\n\n")
        out.write("| File | Lines | Covered | Coverage |\n")
        out.write("|------|------:|--------:|---------:|\n")
        for filepath, total, covered in sorted(comp_files):
            short = filepath.replace(comp_key + "/", "")
            out.write(f"| `{short}` | {total} | {covered} | {pct_str(covered, total)} |\n")
        out.write("\n</details>\n\n")

    if cxx_templates:
        out.write("## C++ Templates (inflated by instantiation)\n\n")
        out.write("These files have gcovr line counts inflated by template instantiations.\n")
        out.write("Each `if constexpr` branch is counted per instantiation, most are structurally dead.\n\n")
        out.write("| File | Lines (gcovr) | Covered | Coverage |\n")
        out.write("|------|------:|--------:|---------:|\n")
        for key in sorted(cxx_templates):
            total, covered = cxx_templates[key]
            short = key.split("/")[-1]
            out.write(f"| `{short}` | {total} | {covered} | {pct_str(covered, total)} |\n")
        out.write(f"| **Total** | **{tpl_total}** | **{tpl_covered}** | **{pct_str(tpl_covered, tpl_total)}** |\n")
        out.write("\n")

    if py_files:
        out.write("## Python Coverage\n\n")
        out.write("| Module | Lines | Covered | Coverage |\n")
        out.write("|--------|------:|--------:|---------:|\n")
        for key in sorted(py_files):
            total, covered = py_files[key]
            out.write(f"| `{key}` | {total} | {covered} | {pct_str(covered, total)} |\n")
        out.write(f"| **Total** | **{py_total}** | **{py_covered}** | **{pct_str(py_covered, py_total)}** |\n")
        out.write("\n")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <gcovr.json> [pytest-cov.json] "
              f"[--install-prefix=PATH] [--outdir=DIR] [--ctest=N] [--pytest=N]")
        sys.exit(2)

    cxx_path = sys.argv[1]
    py_path = None
    install_prefix = None
    outdir = "tests"
    ctest = pytest = None

    for arg in sys.argv[2:]:
        if arg.startswith("--install-prefix="):
            install_prefix = arg.split("=", 1)[1]
        elif arg.startswith("--outdir="):
            outdir = arg.split("=", 1)[1]
        elif arg.startswith("--ctest="):
            ctest = int(arg.split("=", 1)[1])
        elif arg.startswith("--pytest="):
            pytest = int(arg.split("=", 1)[1])
        elif not arg.startswith("--"):
            py_path = arg

    cxx_files = load_cxx_coverage(cxx_path)
    py_files = load_python_coverage(py_path, install_prefix) if py_path else {}

    md_path = os.path.join(outdir, "coverage.md")
    json_path = os.path.join(outdir, "coverage.json")

    with open(md_path, "w") as f:
        write_report(f, cxx_files, py_files)
    summary = build_summary(cxx_files, py_files, ctest=ctest, pytest=pytest)
    write_json(json_path, summary)

    print(f"Coverage report: {md_path}")
    print(f"Coverage JSON:    {json_path}")
    print(f"total (excl templates): {summary['total']}%")
    for name in ("cxx", "cxx_templates", "python"):
        if name in summary:
            n = summary[name]
            status = "ok" if n["pass"] else "FAIL"
            print(f"  {name}: {n['measured']}% (threshold {n['threshold']}%) [{status}]")

    failed = report_failures(summary)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
