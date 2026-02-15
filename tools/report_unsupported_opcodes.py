#!/usr/bin/env python3
"""Aggregate recompiler warning logs into unsupported-opcode tracking reports."""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

LEGACY_UNSUPPORTED_RE = re.compile(r"Unsupported opcode @\s*(0x[0-9a-fA-F]+)")
UNSUPPORTED_ADDR_RE = re.compile(r"Unsupported opcode:.*@\s*(0x[0-9a-fA-F]+)")
UNSUPPORTED_WORD_RE = re.compile(r"word=0x([0-9a-fA-F]{8})")
UNSUPPORTED_OP_RE = re.compile(r"op=0x([0-9a-fA-F]{1,2})")
UNSUPPORTED_FUNCT_RE = re.compile(r"funct=0x([0-9a-fA-F]{1,2})")

R_TYPE_FUNCTS = {
    0x08: "JR",
    0x09: "JALR",
    0x0C: "SYSCALL",
    0x0D: "BREAK",
}
J_TYPE_OPS = {0x02: "J", 0x03: "JAL"}

OWNER_DEFAULT = "unassigned"
MILESTONE_DEFAULT = "M4-opcode-closure"


def load_result(result_json_path: Path) -> dict[str, Any] | None:
    try:
        data = json.loads(result_json_path.read_text(encoding="utf-8"))
    except Exception:
        return None
    if not isinstance(data, dict):
        return None
    return data


def extract_warnings(result: dict[str, Any]) -> list[str]:
    warnings = result.get("warnings", [])
    if not isinstance(warnings, list):
        return []
    return [warning for warning in warnings if isinstance(warning, str)]


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


def decode_mnemonic(op: int | None, funct: int | None) -> str:
    if op is None:
        return "UNKNOWN"
    if op == 0x00:
        return R_TYPE_FUNCTS.get(funct, "SPECIAL")
    if op in J_TYPE_OPS:
        return J_TYPE_OPS[op]
    return f"OP_{op:02X}"


def opcode_family(mnemonic: str) -> str:
    if mnemonic in {"JR", "JALR", "J", "JAL", "BLTZAL", "BGEZAL"}:
        return "control-flow"
    if mnemonic == "BREAK":
        return "trap"
    if mnemonic.startswith("OP_"):
        return "unknown-primary"
    return mnemonic.lower()


def addressing_mode_for_mnemonic(mnemonic: str) -> str:
    if mnemonic in {"JR", "JALR"}:
        return "register"
    if mnemonic in {"J", "JAL"}:
        return "absolute"
    return "unknown"


def gap_kind_for_mnemonic(mnemonic: str) -> str:
    if mnemonic in {"JR", "JALR", "J", "JAL", "BLTZAL", "BGEZAL"}:
        return "control-flow-gap"
    if mnemonic == "UNKNOWN" or mnemonic.startswith("OP_"):
        return "decode-gap"
    return "lowering-gap"


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
            "mnemonic": "UNKNOWN",
            "opcodeFamily": "unknown-primary",
            "addressingMode": "unknown",
            "gapKind": "decode-gap",
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
    mnemonic = decode_mnemonic(op, funct)

    return {
        "address": address,
        "word": f"0x{word:08x}" if word is not None else None,
        "op": f"0x{op:02x}" if op is not None else None,
        "funct": f"0x{funct:02x}" if funct is not None else None,
        "mnemonic": mnemonic,
        "opcodeFamily": opcode_family(mnemonic),
        "addressingMode": addressing_mode_for_mnemonic(mnemonic),
        "gapKind": gap_kind_for_mnemonic(mnemonic),
        "warning": warning,
        "classification": classification,
        "classificationReason": reason,
    }


def build_report(log_dir: Path, include_failed: bool = False) -> dict[str, Any]:
    result_files = sorted(log_dir.glob("*.result.json"))
    warning_counter: Counter[str] = Counter()
    warnings_by_demo: dict[str, list[str]] = {}
    unsupported_hits: dict[str, list[dict[str, Any]]] = defaultdict(list)
    parsed_result_files: list[str] = []
    skipped_failed_results: list[str] = []

    for result_file in result_files:
        result = load_result(result_file)
        if result is None:
            continue

        if not include_failed and result.get("success") is False:
            skipped_failed_results.append(result_file.name)
            continue

        demo = result_file.name.replace(".result.json", "")
        parsed_result_files.append(result_file.name)
        warnings = extract_warnings(result)
        warnings_by_demo[demo] = warnings
        for warning in warnings:
            warning_counter[warning] += 1
            parsed = parse_unsupported_warning(warning)
            if parsed is not None:
                unsupported_hits[parsed["address"]].append({"demo": demo, **parsed})

    unsupported_addresses = sorted(unsupported_hits.keys(), key=detect_python_like_sort_key)
    unsupported_items: list[dict[str, Any]] = []
    classification_counter: Counter[str] = Counter()
    family_counter: Counter[str] = Counter()

    for address in unsupported_addresses:
        entries = unsupported_hits[address]
        demos = sorted({entry["demo"] for entry in entries})
        words = sorted({entry["word"] for entry in entries if entry["word"] is not None})
        mnemonics = sorted({entry["mnemonic"] for entry in entries})
        families = sorted({entry["opcodeFamily"] for entry in entries})
        modes = sorted({entry["addressingMode"] for entry in entries})
        gaps = sorted({entry["gapKind"] for entry in entries})
        classes = Counter(entry["classification"] for entry in entries)
        top_class = classes.most_common(1)[0][0]
        classification_counter[top_class] += len(entries)
        reasons = sorted({entry["classificationReason"] for entry in entries})
        family_counter[families[0] if families else "unknown"] += len(entries)

        unsupported_items.append(
            {
                "address": address,
                "count": len(entries),
                "demos": demos,
                "words": words,
                "mnemonics": mnemonics,
                "opcodeFamilies": families,
                "addressingModes": modes,
                "gapKinds": gaps,
                "classification": top_class,
                "classificationReason": reasons[0] if reasons else "",
                "owner": OWNER_DEFAULT,
                "milestone": MILESTONE_DEFAULT,
            }
        )

    return {
        "generatedAtUtc": datetime.now(timezone.utc).isoformat(),
        "logDir": str(log_dir),
        "resultFiles": [f.name for f in result_files],
        "parsedResultFiles": parsed_result_files,
        "skippedFailedResultFiles": skipped_failed_results,
        "warningCounts": dict(warning_counter.most_common()),
        "warningsByDemo": warnings_by_demo,
        "unsupportedSummary": dict(classification_counter.most_common()),
        "topOpcodeFamilies": [
            {"opcodeFamily": family, "count": count}
            for family, count in family_counter.most_common(10)
        ],
        "unsupportedOpcodes": unsupported_items,
    }


def build_trend(previous: dict[str, Any] | None, current: dict[str, Any]) -> dict[str, Any]:
    previous_items = previous.get("unsupportedOpcodes", []) if previous else []
    prev_by_address = {item.get("address"): item for item in previous_items if "address" in item}
    cur_by_address = {item.get("address"): item for item in current.get("unsupportedOpcodes", [])}

    new_addresses = sorted(set(cur_by_address) - set(prev_by_address), key=detect_python_like_sort_key)
    resolved_addresses = sorted(
        set(prev_by_address) - set(cur_by_address), key=detect_python_like_sort_key
    )

    changed_counts = []
    for address, item in cur_by_address.items():
        prev_item = prev_by_address.get(address)
        if prev_item is None:
            continue
        prev_count = int(prev_item.get("count", 0))
        count = int(item.get("count", 0))
        if prev_count != count:
            changed_counts.append({"address": address, "previous": prev_count, "current": count})

    return {
        "generatedAtUtc": current.get("generatedAtUtc"),
        "previousGeneratedAtUtc": previous.get("generatedAtUtc") if previous else None,
        "currentGeneratedAtUtc": current.get("generatedAtUtc"),
        "newAddresses": new_addresses,
        "resolvedAddresses": resolved_addresses,
        "changedCounts": sorted(changed_counts, key=lambda item: detect_python_like_sort_key(item["address"])),
    }


def write_markdown(report: dict[str, Any], output_md: Path) -> None:
    lines: list[str] = []
    lines.append("# Unsupported Opcode Tracking Report")
    lines.append("")
    lines.append(f"- Generated at (UTC): `{report['generatedAtUtc']}`")
    lines.append(f"- Log directory: `{report['logDir']}`")
    lines.append(f"- Found result files: {len(report['resultFiles'])}")
    lines.append(f"- Parsed result files: {len(report.get('parsedResultFiles', []))}")
    skipped_failed = report.get("skippedFailedResultFiles", [])
    if skipped_failed:
        lines.append(f"- Skipped failed result files: {len(skipped_failed)}")
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

        lines.append("## Top opcode families")
        lines.append("")
        if report["topOpcodeFamilies"]:
            lines.append("| Family | Hits |")
            lines.append("|---|---:|")
            for item in report["topOpcodeFamilies"]:
                lines.append(f"| `{item['opcodeFamily']}` | {item['count']} |")
        lines.append("")

        lines.append("## Unsupported opcode inventory")
        lines.append("")
        for item in unsupported:
            demos = ", ".join(item["demos"])
            words = ", ".join(item["words"]) if item["words"] else "n/a"
            mnemonics = ", ".join(item["mnemonics"]) if item["mnemonics"] else "UNKNOWN"
            families = ", ".join(item["opcodeFamilies"]) if item["opcodeFamilies"] else "unknown"
            modes = ", ".join(item["addressingModes"]) if item["addressingModes"] else "unknown"
            gaps = ", ".join(item["gapKinds"]) if item["gapKinds"] else "unknown"
            lines.append(
                f"- [ ] `{item['address']}` words `{words}` seen {item['count']} time(s) across: {demos}"
            )
            lines.append(
                f"  - Mnemonic(s): `{mnemonics}` · Family: `{families}` · Addressing: `{modes}` · Gap: `{gaps}`"
            )
            lines.append(
                f"  - Owner: `{item['owner']}` · Milestone: `{item['milestone']}` · Classification: `{item['classification']}`"
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


def write_trend_markdown(trend: dict[str, Any], output_md: Path) -> None:
    lines = ["# Unsupported Opcode Trend Snapshot", ""]
    lines.append(f"- Current report timestamp: `{trend.get('currentGeneratedAtUtc')}`")
    previous_ts = trend.get("previousGeneratedAtUtc")
    lines.append(f"- Previous report timestamp: `{previous_ts if previous_ts else 'none'}`")
    lines.append("")

    lines.append("## New unsupported addresses")
    lines.append("")
    if trend["newAddresses"]:
        for address in trend["newAddresses"]:
            lines.append(f"- `{address}`")
    else:
        lines.append("- none")

    lines.append("")
    lines.append("## Resolved unsupported addresses")
    lines.append("")
    if trend["resolvedAddresses"]:
        for address in trend["resolvedAddresses"]:
            lines.append(f"- `{address}`")
    else:
        lines.append("- none")

    lines.append("")
    lines.append("## Count deltas for existing addresses")
    lines.append("")
    if trend["changedCounts"]:
        for item in trend["changedCounts"]:
            lines.append(
                f"- `{item['address']}`: {item['previous']} -> {item['current']}"
            )
    else:
        lines.append("- none")

    output_md.write_text("\n".join(lines) + "\n", encoding="utf-8")


def load_json(path: Path) -> dict[str, Any] | None:
    if not path.exists():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return None
    if not isinstance(data, dict):
        return None
    return data


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log-dir", required=True, help="Directory containing *.result.json files")
    parser.add_argument("--output-json", required=True, help="Output JSON report path")
    parser.add_argument("--output-md", required=True, help="Output markdown report path")
    parser.add_argument(
        "--include-failed",
        action="store_true",
        help="Include warnings from result files where success=false",
    )
    parser.add_argument(
        "--baseline-json",
        help="Optional previous JSON report used to generate trend snapshots",
    )
    parser.add_argument("--output-trend-json", help="Optional output trend JSON path")
    parser.add_argument("--output-trend-md", help="Optional output trend markdown path")
    args = parser.parse_args()

    log_dir = Path(args.log_dir)
    output_json = Path(args.output_json)
    output_md = Path(args.output_md)

    report = build_report(log_dir, include_failed=args.include_failed)
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_md.parent.mkdir(parents=True, exist_ok=True)

    output_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    write_markdown(report, output_md)

    baseline = load_json(Path(args.baseline_json)) if args.baseline_json else None
    if args.output_trend_json or args.output_trend_md:
        trend = build_trend(baseline, report)
        if args.output_trend_json:
            trend_path = Path(args.output_trend_json)
            trend_path.parent.mkdir(parents=True, exist_ok=True)
            trend_path.write_text(json.dumps(trend, indent=2) + "\n", encoding="utf-8")
            print(f"Wrote opcode trend JSON: {trend_path}")
        if args.output_trend_md:
            trend_md_path = Path(args.output_trend_md)
            trend_md_path.parent.mkdir(parents=True, exist_ok=True)
            write_trend_markdown(trend, trend_md_path)
            print(f"Wrote opcode trend markdown: {trend_md_path}")

    print(f"Wrote opcode report JSON: {output_json}")
    print(f"Wrote opcode report markdown: {output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
