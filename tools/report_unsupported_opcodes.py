#!/usr/bin/env python3
"""Aggregate recompiler warning logs into unsupported-opcode tracking reports."""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

UNSUPPORTED_RE = re.compile(r"Unsupported opcode @\s*(0x[0-9a-fA-F]+)")


def load_warnings(result_json_path: Path) -> list[str]:
    try:
        data = json.loads(result_json_path.read_text(encoding="utf-8"))
    except Exception:
        return []
    warnings = data.get("warnings", [])
    if not isinstance(warnings, list):
        return []
    return [w for w in warnings if isinstance(w, str)]


def detect_python_like_sort_key(addr: str) -> int:
    try:
        return int(addr, 16)
    except Exception:
        return 0


def build_report(log_dir: Path) -> dict[str, Any]:
    result_files = sorted(log_dir.glob("*.result.json"))
    warning_counter: Counter[str] = Counter()
    warnings_by_demo: dict[str, list[str]] = {}
    unsupported_hits: dict[str, list[str]] = defaultdict(list)

    for result_file in result_files:
        demo = result_file.name.replace(".result.json", "")
        warnings = load_warnings(result_file)
        warnings_by_demo[demo] = warnings
        for warning in warnings:
            warning_counter[warning] += 1
            unsupported_match = UNSUPPORTED_RE.search(warning)
            if unsupported_match:
                unsupported_hits[unsupported_match.group(1).lower()].append(demo)

    unsupported_addresses = sorted(unsupported_hits.keys(), key=detect_python_like_sort_key)
    return {
        "logDir": str(log_dir),
        "resultFiles": [f.name for f in result_files],
        "warningCounts": dict(warning_counter.most_common()),
        "warningsByDemo": warnings_by_demo,
        "unsupportedOpcodes": [
            {
                "address": address,
                "count": len(unsupported_hits[address]),
                "demos": sorted(set(unsupported_hits[address])),
            }
            for address in unsupported_addresses
        ],
    }


def write_markdown(report: dict[str, Any], output_md: Path) -> None:
    lines: list[str] = []
    lines.append("# Unsupported Opcode Tracking Report")
    lines.append("")
    lines.append(f"- Log directory: `{report['logDir']}`")
    lines.append(f"- Parsed result files: {len(report['resultFiles'])}")
    lines.append("")

    unsupported = report["unsupportedOpcodes"]
    if not unsupported:
        lines.append("No `Unsupported opcode` warnings were found in parsed JSON outputs.")
    else:
        lines.append("## Unsupported opcode backlog (address-based)")
        lines.append("")
        lines.append("Use this checklist to track implementation and validation.")
        lines.append("")
        for item in unsupported:
            demos = ", ".join(item["demos"])
            lines.append(
                f"- [ ] Address `{item['address']}` seen {item['count']} time(s) across: {demos}"
            )

    lines.append("")
    lines.append("## High-frequency warnings")
    lines.append("")
    if not report["warningCounts"]:
        lines.append("No warnings were collected.")
    else:
        lines.append("| Warning | Count |")
        lines.append("|---|---:|")
        for warning, count in report["warningCounts"].items():
            escaped = warning.replace("|", "\\|")
            lines.append(f"| `{escaped}` | {count} |")

    output_md.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log-dir", required=True, help="Directory containing *.result.json files")
    parser.add_argument("--output-json", required=True, help="Output JSON report path")
    parser.add_argument("--output-md", required=True, help="Output markdown report path")
    args = parser.parse_args()

    log_dir = Path(args.log_dir)
    output_json = Path(args.output_json)
    output_md = Path(args.output_md)

    report = build_report(log_dir)
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_md.parent.mkdir(parents=True, exist_ok=True)

    output_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    write_markdown(report, output_md)

    print(f"Wrote opcode report JSON: {output_json}")
    print(f"Wrote opcode report markdown: {output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
