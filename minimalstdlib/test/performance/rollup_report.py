#!/usr/bin/env python3
"""
rollup_report.py — aggregate individual performance report Markdown files into
a single summary document.

Usage:
    python3 test/performance/rollup_report.py [--reports-dir <dir>]

The script discovers all *.md files under <reports-dir> (default:
test/performance/reports/) whose names do NOT start with
"PERFORMANCE_SUMMARY_", then emits:

    test/performance/reports/PERFORMANCE_SUMMARY_YYYY-MM-DD_HH-MM.md
"""

import argparse
import os
import re
import sys
from datetime import datetime


def collect_reports(reports_dir: str) -> list[str]:
    """Return sorted list of individual report paths (excludes summary files)."""
    if not os.path.isdir(reports_dir):
        print(f"Reports directory not found: {reports_dir}", file=sys.stderr)
        return []

    paths = []
    for name in os.listdir(reports_dir):
        if name.endswith(".md") and not name.startswith("PERFORMANCE_SUMMARY_"):
            paths.append(os.path.join(reports_dir, name))

    paths.sort()
    return paths


def extract_metadata(content: str) -> tuple[str, str, str]:
    """Return (group, test, date) strings parsed from a report header."""
    group = re.search(r"\*\*Group:\*\*\s*(.+?)  ", content)
    test = re.search(r"\*\*Test:\*\*\s*(.+?)  ", content)
    date = re.search(r"\*\*Date:\*\*\s*(.+?)  ", content)
    return (
        group.group(1).strip() if group else "Unknown",
        test.group(1).strip() if test else "Unknown",
        date.group(1).strip() if date else "Unknown",
    )


def build_summary(report_paths: list[str]) -> str:
    now = datetime.now().strftime("%Y-%m-%d %H:%M")
    lines = [
        "# Performance Summary",
        "",
        f"**Generated:** {now}  ",
        f"**Reports included:** {len(report_paths)}  ",
        "",
        "---",
        "",
    ]

    for path in report_paths:
        with open(path, encoding="utf-8") as f:
            content = f.read()

        group, test, date = extract_metadata(content)
        rel_path = os.path.basename(path)

        lines.append(f"## {group} — {test}")
        lines.append("")
        lines.append(f"*Source: `{rel_path}` (recorded {date})*")
        lines.append("")

        # Extract the Markdown table (lines starting with |)
        table_lines = [ln for ln in content.splitlines() if ln.startswith("|")]
        if table_lines:
            lines.extend(table_lines)
        else:
            lines.append("*(no table data found)*")

        lines.append("")
        lines.append("---")
        lines.append("")

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Roll up performance reports into a summary.")
    parser.add_argument(
        "--reports-dir",
        default="test/performance/reports",
        help="Directory containing individual report *.md files (default: test/performance/reports)",
    )
    args = parser.parse_args()

    report_paths = collect_reports(args.reports_dir)
    if not report_paths:
        print("No individual report files found — nothing to roll up.")
        return 0

    summary = build_summary(report_paths)

    ts = datetime.now().strftime("%Y-%m-%d_%H-%M")
    output_path = os.path.join(args.reports_dir, f"PERFORMANCE_SUMMARY_{ts}.md")

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(summary)

    print(f"Summary written to: {output_path}  ({len(report_paths)} reports included)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
