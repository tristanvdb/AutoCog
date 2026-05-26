#!/usr/bin/env python3
"""
Generate a coverage report in Markdown from gcovr and pytest-cov JSON.

Usage: python3 coverage_report.py <gcovr.json> [pytest-cov.json] [-o coverage.md]
"""

import json
import sys
from collections import defaultdict
from datetime import datetime


# Template-heavy files where gcovr inflates line counts due to
# if-constexpr branches and per-tag template instantiations.
# Reported separately so they don't drag down the main C++ coverage.
TEMPLATE_FILES = {
    "libs/autocog/compiler/stl/ast.hxx",
    "libs/autocog/compiler/stl/symbol-scanner.hxx",
    "libs/autocog/compiler/stl/parser.hxx",
    "libs/autocog/compiler/stl/ast/nodes.def",
}


def load_cxx_coverage(path):
    """Parse gcovr JSON into per-file line coverage."""
    with open(path) as f:
        data = json.load(f)

    files = {}
    for entry in data.get("files", []):
        filepath = entry["file"]
        lines = entry["lines"]
        total = len(lines)
        covered = sum(1 for l in lines if l["count"] > 0)
        files[filepath] = (total, covered)

    return files


def load_python_coverage(path):
    """Parse pytest-cov JSON into per-file line coverage."""
    with open(path) as f:
        data = json.load(f)

    files = {}
    for filepath, fdata in data.get("files", {}).items():
        s = fdata["summary"]
        parts = filepath.split("/")
        if "modules" in parts:
            idx = parts.index("modules")
            key = "/".join(parts[idx + 1:])
        else:
            key = filepath
        files[key] = (s["num_statements"], s["covered_lines"])

    return files


def split_templates(cxx_files):
    """Split C++ files into regular and template-heavy categories."""
    regular = {}
    templates = {}
    for path, counts in cxx_files.items():
        if path in TEMPLATE_FILES:
            templates[path] = counts
        else:
            regular[path] = counts
    return regular, templates


def group_by_component(files, depth_map):
    """Group files into components based on path depth rules."""
    components = defaultdict(lambda: [0, 0, []])  # [total, covered, files]

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


def pct(covered, total):
    return f"{covered / total * 100:.0f}%" if total > 0 else "N/A"


def totals(files):
    t = sum(v[0] for v in files.values())
    c = sum(v[1] for v in files.values())
    return t, c


def write_report(out, cxx_files, py_files):
    """Write the full markdown report."""

    cxx_regular, cxx_templates = split_templates(cxx_files)

    reg_total, reg_covered = totals(cxx_regular)
    tpl_total, tpl_covered = totals(cxx_templates)
    py_total, py_covered = totals(py_files) if py_files else (0, 0)
    all_total = reg_total + tpl_total + py_total
    all_covered = reg_covered + tpl_covered + py_covered

    # ========================================================================
    # Header
    # ========================================================================

    out.write("# Coverage Report\n\n")
    out.write(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")

    # ========================================================================
    # Global summary
    # ========================================================================

    out.write("## Summary\n\n")
    out.write("| Category | Lines | Covered | Coverage |\n")
    out.write("|----------|------:|--------:|---------:|\n")
    out.write(f"| C++      | {reg_total} | {reg_covered} | {pct(reg_covered, reg_total)} |\n")
    out.write(f"| C++ (templates) | {tpl_total} | {tpl_covered} | {pct(tpl_covered, tpl_total)} |\n")
    if py_files:
        out.write(f"| Python   | {py_total} | {py_covered} | {pct(py_covered, py_total)} |\n")
    out.write(f"| **Total** | **{all_total}** | **{all_covered}** | **{pct(all_covered, all_total)}** |\n")
    out.write(f"| **Total (excl. templates)** | **{reg_total + py_total}** "
              f"| **{reg_covered + py_covered}** "
              f"| **{pct(reg_covered + py_covered, reg_total + py_total)}** |\n")
    out.write("\n")

    # ========================================================================
    # C++ by component
    # ========================================================================

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
        out.write(f"| `{key}` | {total} | {covered} | {pct(covered, total)} |\n")
    out.write(f"| **Total** | **{reg_total}** | **{reg_covered}** | **{pct(reg_covered, reg_total)}** |\n")
    out.write("\n")

    # ========================================================================
    # C++ per file detail
    # ========================================================================

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
            out.write(f"| `{short}` | {total} | {covered} | {pct(covered, total)} |\n")
        out.write("\n</details>\n\n")

    # ========================================================================
    # C++ Templates (separate section)
    # ========================================================================

    if cxx_templates:
        out.write("## C++ Templates (inflated by instantiation)\n\n")
        out.write("These files have gcovr line counts inflated by template instantiations.\n")
        out.write("Each `if constexpr` branch is counted per instantiation, most are structurally dead.\n\n")
        out.write("| File | Lines (gcovr) | Covered | Coverage |\n")
        out.write("|------|------:|--------:|---------:|\n")
        for key in sorted(cxx_templates):
            total, covered = cxx_templates[key]
            short = key.split("/")[-1]
            out.write(f"| `{short}` | {total} | {covered} | {pct(covered, total)} |\n")
        out.write(f"| **Total** | **{tpl_total}** | **{tpl_covered}** | **{pct(tpl_covered, tpl_total)}** |\n")
        out.write("\n")

    # ========================================================================
    # Python coverage
    # ========================================================================

    if py_files:
        out.write("## Python Coverage\n\n")
        out.write("| Module | Lines | Covered | Coverage |\n")
        out.write("|--------|------:|--------:|---------:|\n")
        for key in sorted(py_files):
            total, covered = py_files[key]
            out.write(f"| `{key}` | {total} | {covered} | {pct(covered, total)} |\n")
        out.write(f"| **Total** | **{py_total}** | **{py_covered}** | **{pct(py_covered, py_total)}** |\n")
        out.write("\n")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <gcovr.json> [pytest-cov.json] [-o output.md]")
        sys.exit(1)

    cxx_path = sys.argv[1]
    py_path = None
    output_path = None

    args = sys.argv[2:]
    i = 0
    while i < len(args):
        if args[i] == "-o" and i + 1 < len(args):
            output_path = args[i + 1]
            i += 2
        else:
            py_path = args[i]
            i += 1

    cxx_files = load_cxx_coverage(cxx_path)
    py_files = load_python_coverage(py_path) if py_path else {}

    if output_path:
        with open(output_path, "w") as f:
            write_report(f, cxx_files, py_files)
        print(f"Coverage report written to {output_path}")
    else:
        write_report(sys.stdout, cxx_files, py_files)


if __name__ == "__main__":
    main()
