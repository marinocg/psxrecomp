#!/usr/bin/env python3
"""Aggregate recompiler warning logs into unsupported-opcode tracking reports."""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

LEGACY_UNSUPPORTED_RE = re.compile(r"Unsupported opcode @\s*(0x[0-9a-fA-F]+)")
UNSUPPORTED_ADDR_RE = re.compile(r"Unsupported opcode:.*@\s*(0x[0-9a-fA-F]+)")
UNSUPPORTED_WORD_RE = re.compile(r"word=0x([0-9a-fA-F]{8})")
UNSUPPORTED_OP_RE = re.compile(r"op=0x([0-9a-fA-F]{1,2})")
UNSUPPORTED_FUNCT_RE = re.compile(r"funct=0x([0-9a-fA-F]{1,2})")


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


def to_bytes(word: int) -> tuple[int, int, int, int]:
    return (
        word & 0xFF,
        (word >> 8) & 0xFF,
        (word >> 16) & 0xFF,
        (word >> 24) & 0xFF,
    )


def is_ascii_like_word(word: int) -> bool:
    bs = to_bytes(word)
    printable = sum(1 for b in bs if 32 <= b <= 126)
    return printable >= 3


def is_fill_like_word(word: int) -> bool:
    bs = to_bytes(word)
    counts = Counter(bs)
    most_common_count = counts.most_common(1)[0][1]
    fill_bytes = {0x00, 0xDD, 0xCC, 0xFF}
    return most_common_count >= 3 and any(b in fill_bytes for b in counts)


def classify_unsupported(word: int | None, op: int | None, funct: int | None) -> tuple[str, str]:
    if word == 0x0007000D or (op == 0x00 and funct == 0x0D):
        return (
            "actionable-break",
            "Real MIPS BREAK trap instruction. Implement trap lowering/emulation semantics.",
        )
    if word is not None and is_ascii_like_word(word):
        return (
            "likely-data-ascii",
            "Looks like printable ASCII data decoded as code (likely CFG/data classification issue).",
        )
    if word is not None and is_fill_like_word(word):
        return (
            "likely-data-fill",
            "Looks like repeated fill/pattern data decoded as code.",
        )
    return (
        "unknown-needs-triage",
        "Needs manual triage; may be unsupported instruction or control-flow discovery issue.",
    )


def parse_unsupported_warning(warning: str) -> dict[str, Any] | None:
    addr_match = UNSUPPORTED_ADDR_RE.search(warning)
    if not addr_match:
        legacy_match = LEGACY_UNSUPPORTED_RE.search(warning)
        if not legacy_match:
            return None
        return {
            "address": legacy_match.group(1).lower(),
            "word": None,
            "op": None,
            "funct": None,
            "warning": warning,
            "classification": "unknown-needs-triage",
            "classificationReason": "Legacy warning format lacks raw-word detail.",
        }

    address = addr_match.group(1).lower()
    word_match = UNSUPPORTED_WORD_RE.search(warning)
    op_match = UNSUPPORTED_OP_RE.search(warning)
    funct_match = UNSUPPORTED_FUNCT_RE.search(warning)

    word = int(word_match.group(1), 16) if word_match else None
    op = int(op_match.group(1), 16) if op_match else None
    funct = int(funct_match.group(1), 16) if funct_match else None
    classification, reason = classify_unsupported(word, op, funct)

    return {
        "address": address,
        "word": f"0x{word:08x}" if word is not None else None,
        "op": f"0x{op:02x}" if op is not None else None,
        "funct": f"0x{funct:02x}" if funct is not None else None,
        "warning": warning,
        "classification": classification,
        "classificationReason": reason,
    }


def build_report(log_dir: Path) -> dict[str, Any]:
    result_files = sorted(log_dir.glob("*.result.json"))
    warning_counter: Counter[str] = Counter()
    warnings_by_demo: dict[str, list[str]] = {}
    unsupported_hits: dict[str, list[dict[str, Any]]] = defaultdict(list)

    for result_file in result_files:
        demo = result_file.name.replace(".result.json", "")
        warnings = load_warnings(result_file)
        warnings_by_demo[demo] = warnings
        for warning in warnings:
            warning_counter[warning] += 1
            parsed = parse_unsupported_warning(warning)
            if parsed is not None:
                unsupported_hits[parsed["address"]].append({"demo": demo, **parsed})

    unsupported_addresses = sorted(unsupported_hits.keys(), key=detect_python_like_sort_key)
    unsupported_items: list[dict[str, Any]] = []
    classification_counter: Counter[str] = Counter()
    for address in unsupported_addresses:
        entries = unsupported_hits[address]
        demos = sorted({entry["demo"] for entry in entries})
        words = sorted({entry["word"] for entry in entries if entry["word"] is not None})
        classes = Counter(entry["classification"] for entry in entries)
        top_class = classes.most_common(1)[0][0]
        classification_counter[top_class] += len(entries)
        reasons = sorted({entry["classificationReason"] for entry in entries})

        unsupported_items.append(
            {
                "address": address,
                "count": len(entries),
                "demos": demos,
                "words": words,
                "classification": top_class,
                "classificationReason": reasons[0] if reasons else "",
            }
        )

    return {
        "logDir": str(log_dir),
        "resultFiles": [f.name for f in result_files],
        "warningCounts": dict(warning_counter.most_common()),
        "warningsByDemo": warnings_by_demo,
        "unsupportedSummary": dict(classification_counter.most_common()),
        "unsupportedOpcodes": unsupported_items,
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
        lines.append("## Unsupported opcode triage summary")
        lines.append("")
        if report["unsupportedSummary"]:
            lines.append("| Classification | Hits |")
            lines.append("|---|---:|")
            for classification, count in report["unsupportedSummary"].items():
                lines.append(f"| `{classification}` | {count} |")
            lines.append("")

        lines.append("## Actionable instruction gaps")
        lines.append("")
        actionable = [item for item in unsupported if item["classification"].startswith("actionable")]
        if not actionable:
            lines.append("No clearly actionable unsupported opcodes identified.")
        else:
            for item in actionable:
                demos = ", ".join(item["demos"])
                words = ", ".join(item["words"]) if item["words"] else "n/a"
                lines.append(
                    f"- [ ] `{item['address']}` words `{words}` seen {item['count']} time(s) across: {demos}"
                )
                lines.append(f"  - Reason: {item['classificationReason']}")

        lines.append("")
        lines.append("## Likely code-vs-data false positives")
        lines.append("")
        likely_data = [
            item
            for item in unsupported
            if item["classification"] in {"likely-data-ascii", "likely-data-fill"}
        ]
        if not likely_data:
            lines.append("No likely data-decoding false positives detected.")
        else:
            for item in likely_data:
                demos = ", ".join(item["demos"])
                words = ", ".join(item["words"][:3]) if item["words"] else "n/a"
                lines.append(
                    f"- `{item['address']}` ({item['classification']}, words: {words}) seen {item['count']} time(s) across: {demos}"
                )

        lines.append("")
        lines.append("## Needs manual triage")
        lines.append("")
        unknown = [item for item in unsupported if item["classification"] == "unknown-needs-triage"]
        if not unknown:
            lines.append("No unresolved unsupported-opcode entries remain.")
        else:
            for item in unknown:
                demos = ", ".join(item["demos"])
                words = ", ".join(item["words"]) if item["words"] else "n/a"
                lines.append(
                    f"- [ ] `{item['address']}` words `{words}` seen {item['count']} time(s) across: {demos}"
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
