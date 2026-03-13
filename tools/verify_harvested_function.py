#!/usr/bin/env python3
"""Verify harvested function bytes against emitted C++ control flow.

This tool reads a recompile result JSON, parses the generated module source,
extracts the verbatim harvested executable byte blob embedded in the source,
and reports for selected functions:

- original instruction bytes
- decoded MIPS disassembly
- emitted C++ block labels / control flow
- immediate caller sites
- branch/delay-slot coverage
- load/store width + signedness mapping
- function-boundary / inline-data sanity checks
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


@dataclass
class FunctionMeta:
    name: str
    entry: int
    end: int
    has_prologue: bool
    has_epilogue: bool
    direct_calls: List[int]
    indirect_call_count: int


@dataclass
class DecodedInstruction:
    address: int
    word: int
    bytes_le: bytes
    text: str
    mnemonic: str
    valid: bool
    branch_target: Optional[int] = None
    is_control: bool = False
    is_call: bool = False
    is_return: bool = False
    load_store_helper: Optional[str] = None


@dataclass
class CommentEvent:
    block: str
    address: int
    text: str
    lines: List[str] = field(default_factory=list)


@dataclass
class FunctionSourceInfo:
    name: str
    block_order: List[str]
    block_successors: Dict[str, List[str]]
    comment_events: List[CommentEvent]
    comment_by_address: Dict[int, List[CommentEvent]]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--recompile-result",
        required=True,
        help="Path to recompile.result.json produced by psxrecomp --json",
    )
    parser.add_argument(
        "--address",
        action="append",
        help="Function entry address to inspect (hex or decimal). Defaults to allocator targets.",
    )
    parser.add_argument(
        "--caller-window",
        type=int,
        default=2,
        help="Instructions of context to show before/after each caller site (default: 2)",
    )
    return parser.parse_args()


def parse_int(value: str) -> int:
    value = value.strip()
    return int(value, 0)


def hex8(value: int) -> str:
    return f"0x{value:08X}"


def read_json(path: Path) -> dict:
    return json.loads(path.read_text())


def load_function_metadata(manifest: dict) -> List[FunctionMeta]:
    out: List[FunctionMeta] = []
    for entry in manifest["functions"]:
        out.append(
            FunctionMeta(
                name=entry["name"],
                entry=int(entry["entryAddress"], 16),
                end=int(entry["endAddress"], 16),
                has_prologue=bool(entry["hasPrologue"]),
                has_epilogue=bool(entry["hasEpilogue"]),
                direct_calls=[int(value, 16) for value in entry["directCalls"]],
                indirect_call_count=int(entry["indirectCallCount"]),
            )
        )
    out.sort(key=lambda item: item.entry)
    return out


def parse_ram_blob(source_text: str) -> Tuple[int, bytes]:
    load_match = re.search(r"static constexpr Address kRamInitLoadAddress = 0x([0-9a-fA-F]+);", source_text)
    data_match = re.search(r"static const u8 kRamInitData\[] = \{(.*?)\n\s*\};", source_text, re.DOTALL)
    if not load_match or not data_match:
        raise RuntimeError("Failed to locate embedded RAM init blob in generated source.")
    load_address = int(load_match.group(1), 16)
    byte_values = [int(value) for value in re.findall(r"\b\d+\b", data_match.group(1))]
    return load_address, bytes(byte_values)


REG_NAMES = [
    "zero",
    "at",
    "v0",
    "v1",
    "a0",
    "a1",
    "a2",
    "a3",
    "t0",
    "t1",
    "t2",
    "t3",
    "t4",
    "t5",
    "t6",
    "t7",
    "s0",
    "s1",
    "s2",
    "s3",
    "s4",
    "s5",
    "s6",
    "s7",
    "t8",
    "t9",
    "k0",
    "k1",
    "gp",
    "sp",
    "fp",
    "ra",
]

SPECIAL = {
    0x00: "sll",
    0x02: "srl",
    0x03: "sra",
    0x04: "sllv",
    0x06: "srlv",
    0x07: "srav",
    0x08: "jr",
    0x09: "jalr",
    0x10: "mfhi",
    0x11: "mthi",
    0x12: "mflo",
    0x13: "mtlo",
    0x18: "mult",
    0x19: "multu",
    0x1A: "div",
    0x1B: "divu",
    0x20: "add",
    0x21: "addu",
    0x22: "sub",
    0x23: "subu",
    0x24: "and",
    0x25: "or",
    0x26: "xor",
    0x27: "nor",
    0x2A: "slt",
    0x2B: "sltu",
}

REGIMM = {0x00: "bltz", 0x01: "bgez", 0x10: "bltzal", 0x11: "bgezal"}

LOAD_STORE_HELPERS = {
    "lb": "readMemory8s",
    "lbu": "readMemory8",
    "lh": "readMemory16s",
    "lhu": "readMemory16",
    "lw": "readMemory32",
    "sb": "writeMemory8",
    "sh": "writeMemory16",
    "sw": "writeMemory32",
}


def decode_instruction(address: int, word: int) -> DecodedInstruction:
    op = (word >> 26) & 0x3F
    rs = (word >> 21) & 0x1F
    rt = (word >> 16) & 0x1F
    rd = (word >> 11) & 0x1F
    sa = (word >> 6) & 0x1F
    funct = word & 0x3F
    imm = word & 0xFFFF
    simm = imm if imm < 0x8000 else imm - 0x10000
    target = ((address + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
    bytes_le = word.to_bytes(4, byteorder="little", signed=False)

    def fmt_reg(index: int) -> str:
        return f"${REG_NAMES[index]}"

    text = f".word 0x{word:08X}"
    mnemonic = "unknown"
    valid = True
    branch_target: Optional[int] = None
    is_control = False
    is_call = False
    is_return = False
    helper: Optional[str] = None

    if word == 0:
        mnemonic = "nop"
        text = "nop"
    elif op == 0x00:
        mnemonic = SPECIAL.get(funct, "unknown")
        if mnemonic == "unknown":
            valid = False
        elif funct in (0x00, 0x02, 0x03):
            text = f"{mnemonic} {fmt_reg(rd)}, {fmt_reg(rt)}, {sa}"
        elif funct in (0x04, 0x06, 0x07):
            text = f"{mnemonic} {fmt_reg(rd)}, {fmt_reg(rt)}, {fmt_reg(rs)}"
        elif funct == 0x08:
            is_control = True
            is_return = rs == 31
            text = f"jr {fmt_reg(rs)}"
        elif funct == 0x09:
            is_control = True
            is_call = True
            text = f"jalr {fmt_reg(rd)}, {fmt_reg(rs)}"
        elif funct in (0x10, 0x12):
            text = f"{mnemonic} {fmt_reg(rd)}"
        elif funct in (0x11, 0x13):
            text = f"{mnemonic} {fmt_reg(rs)}"
        elif funct in (0x18, 0x19, 0x1A, 0x1B):
            text = f"{mnemonic} {fmt_reg(rs)}, {fmt_reg(rt)}"
        else:
            text = f"{mnemonic} {fmt_reg(rd)}, {fmt_reg(rs)}, {fmt_reg(rt)}"
    elif op == 0x01:
        mnemonic = REGIMM.get(rt, "unknown")
        branch_target = address + 4 + (simm << 2)
        is_control = True
        if mnemonic == "unknown":
            valid = False
        text = f"{mnemonic} {fmt_reg(rs)}, {hex8(branch_target)}"
    elif op == 0x02:
        mnemonic = "j"
        branch_target = target
        is_control = True
        text = f"j {hex8(target)}"
    elif op == 0x03:
        mnemonic = "jal"
        branch_target = target
        is_control = True
        is_call = True
        text = f"jal {hex8(target)}"
    elif op in (0x04, 0x05):
        mnemonic = "beq" if op == 0x04 else "bne"
        branch_target = address + 4 + (simm << 2)
        is_control = True
        if op == 0x04 and rs == 0 and rt == 0:
            text = f"b {hex8(branch_target)}"
        elif op == 0x04 and rt == 0:
            text = f"beqz {fmt_reg(rs)}, {hex8(branch_target)}"
        elif op == 0x05 and rt == 0:
            text = f"bnez {fmt_reg(rs)}, {hex8(branch_target)}"
        else:
            text = f"{mnemonic} {fmt_reg(rs)}, {fmt_reg(rt)}, {hex8(branch_target)}"
    elif op in (0x06, 0x07):
        mnemonic = "blez" if op == 0x06 else "bgtz"
        branch_target = address + 4 + (simm << 2)
        is_control = True
        text = f"{mnemonic} {fmt_reg(rs)}, {hex8(branch_target)}"
    elif op == 0x08:
        mnemonic = "addi"
        text = f"addi {fmt_reg(rt)}, {fmt_reg(rs)}, {simm}"
    elif op == 0x09:
        mnemonic = "addiu"
        text = f"addiu {fmt_reg(rt)}, {fmt_reg(rs)}, {simm}"
    elif op == 0x0A:
        mnemonic = "slti"
        text = f"slti {fmt_reg(rt)}, {fmt_reg(rs)}, {simm}"
    elif op == 0x0B:
        mnemonic = "sltiu"
        text = f"sltiu {fmt_reg(rt)}, {fmt_reg(rs)}, {simm}"
    elif op == 0x0C:
        mnemonic = "andi"
        text = f"andi {fmt_reg(rt)}, {fmt_reg(rs)}, 0x{imm:04X}"
    elif op == 0x0D:
        mnemonic = "ori"
        text = f"ori {fmt_reg(rt)}, {fmt_reg(rs)}, 0x{imm:04X}"
    elif op == 0x0E:
        mnemonic = "xori"
        text = f"xori {fmt_reg(rt)}, {fmt_reg(rs)}, 0x{imm:04X}"
    elif op == 0x0F:
        mnemonic = "lui"
        text = f"lui {fmt_reg(rt)}, 0x{imm:04X}"
    elif op in (0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26):
        mnemonic = {
            0x20: "lb",
            0x21: "lh",
            0x22: "lwl",
            0x23: "lw",
            0x24: "lbu",
            0x25: "lhu",
            0x26: "lwr",
        }[op]
        helper = LOAD_STORE_HELPERS.get(mnemonic)
        text = f"{mnemonic} {fmt_reg(rt)}, {simm}({fmt_reg(rs)})"
    elif op in (0x28, 0x29, 0x2A, 0x2B, 0x2E):
        mnemonic = {
            0x28: "sb",
            0x29: "sh",
            0x2A: "swl",
            0x2B: "sw",
            0x2E: "swr",
        }[op]
        helper = LOAD_STORE_HELPERS.get(mnemonic)
        text = f"{mnemonic} {fmt_reg(rt)}, {simm}({fmt_reg(rs)})"
    elif op in (0x10, 0x11, 0x12, 0x13):
        mnemonic = f"cop{op - 0x10}"
        text = f"{mnemonic} 0x{word:08X}"
    else:
        valid = False

    return DecodedInstruction(
        address=address,
        word=word,
        bytes_le=bytes_le,
        text=text,
        mnemonic=mnemonic,
        valid=valid,
        branch_target=branch_target,
        is_control=is_control,
        is_call=is_call,
        is_return=is_return,
        load_store_helper=helper,
    )


def decode_range(load_address: int, blob: bytes, start: int, end_inclusive: int) -> List[DecodedInstruction]:
    if start < load_address or end_inclusive < start:
        raise RuntimeError(f"Requested range {hex8(start)}..{hex8(end_inclusive)} is outside blob.")
    begin = start - load_address
    end = end_inclusive - load_address
    if end + 4 > len(blob):
        raise RuntimeError(f"Requested range {hex8(start)}..{hex8(end_inclusive)} exceeds embedded blob.")
    out: List[DecodedInstruction] = []
    for offset in range(begin, end + 1, 4):
        address = load_address + offset
        word = int.from_bytes(blob[offset : offset + 4], byteorder="little", signed=False)
        out.append(decode_instruction(address, word))
    return out


def extract_function_source(source_text: str, func_name: str) -> str:
    signature = f"bool {func_name}(RecompilerContext& context, Address startAddress)"
    start = source_text.find(signature)
    if start < 0:
        raise RuntimeError(f"Failed to locate generated definition for {func_name}.")
    brace = source_text.find("{", start)
    if brace < 0:
        raise RuntimeError(f"Malformed generated definition for {func_name}.")
    depth = 0
    for index in range(brace, len(source_text)):
        ch = source_text[index]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return source_text[start : index + 1]
    raise RuntimeError(f"Unterminated generated definition for {func_name}.")


def parse_function_source_info(func_name: str, func_text: str) -> FunctionSourceInfo:
    block_order: List[str] = []
    block_successors: Dict[str, List[str]] = defaultdict(list)
    comment_events: List[CommentEvent] = []
    comment_by_address: Dict[int, List[CommentEvent]] = defaultdict(list)
    current_block: Optional[str] = None
    current_event: Optional[CommentEvent] = None

    def flush_event() -> None:
        nonlocal current_event
        if current_event is None:
            return
        comment_events.append(current_event)
        comment_by_address[current_event.address].append(current_event)
        current_event = None

    for raw_line in func_text.splitlines():
        line = raw_line.rstrip()
        case_match = re.search(r"case BlockId::(block_0x[0-9a-f]+):", line)
        if case_match:
            flush_event()
            current_block = case_match.group(1)
            if current_block not in block_order:
                block_order.append(current_block)
            continue
        comment_match = re.search(r"// 0x([0-9A-F]{8}):\s*(.*)$", line)
        if comment_match:
            flush_event()
            if current_block is None:
                continue
            current_event = CommentEvent(
                block=current_block,
                address=int(comment_match.group(1), 16),
                text=comment_match.group(2).strip(),
            )
            continue
        if current_event is not None:
            stripped = line.strip()
            if stripped:
                current_event.lines.append(stripped)
        if current_block is not None:
            for successor in re.findall(r"block = BlockId::(block_0x[0-9a-f]+);", line):
                if successor not in block_successors[current_block]:
                    block_successors[current_block].append(successor)
    flush_event()
    return FunctionSourceInfo(
        name=func_name,
        block_order=block_order,
        block_successors=dict(block_successors),
        comment_events=comment_events,
        comment_by_address=dict(comment_by_address),
    )


def find_call_sites(functions: Sequence[FunctionMeta], load_address: int, blob: bytes, targets: Iterable[int]) -> Dict[int, List[Tuple[FunctionMeta, DecodedInstruction]]]:
    target_set = set(targets)
    out: Dict[int, List[Tuple[FunctionMeta, DecodedInstruction]]] = defaultdict(list)
    for function in functions:
        for instruction in decode_range(load_address, blob, function.entry, function.end):
            if instruction.is_call and instruction.branch_target in target_set:
                out[instruction.branch_target].append((function, instruction))
    for entries in out.values():
        entries.sort(key=lambda item: item[1].address)
    return dict(out)


def helper_match_status(instruction: DecodedInstruction, source_info: FunctionSourceInfo) -> Tuple[str, str]:
    expected = instruction.load_store_helper
    if expected is None:
        return ("skip", "")
    chunks = source_info.comment_by_address.get(instruction.address, [])
    haystack = "\n".join(line for chunk in chunks for line in chunk.lines)
    if expected in haystack:
        return ("ok", expected)
    return ("mismatch", expected)


def verify_delay_slots(instructions: Sequence[DecodedInstruction], source_info: FunctionSourceInfo, function_end: int) -> List[Tuple[DecodedInstruction, bool, str]]:
    out = []
    addresses_in_comments = set(source_info.comment_by_address.keys())
    for instruction in instructions:
        if not instruction.is_control:
            continue
        delay_address = instruction.address + 4
        if delay_address > function_end:
            out.append((instruction, False, "no in-range delay slot"))
            continue
        same_block = False
        for event in source_info.comment_by_address.get(instruction.address, []):
            if any(candidate.block == event.block for candidate in source_info.comment_by_address.get(delay_address, [])):
                same_block = True
                break
        if delay_address in addresses_in_comments and same_block:
            out.append((instruction, True, "delay slot emitted in same block"))
        elif delay_address in addresses_in_comments:
            out.append((instruction, True, "delay slot emitted in different block"))
        else:
            out.append((instruction, False, "delay slot comment missing"))
    return out


def previous_function(functions: Sequence[FunctionMeta], entry: int) -> Optional[FunctionMeta]:
    prev = None
    for function in functions:
        if function.entry >= entry:
            break
        prev = function
    return prev


def contiguous_address_check(instructions: Sequence[DecodedInstruction], source_info: FunctionSourceInfo) -> Tuple[bool, List[int]]:
    missing = [instruction.address for instruction in instructions if instruction.address not in source_info.comment_by_address]
    return (not missing, missing)


def summarize_block_ranges(source_info: FunctionSourceInfo) -> List[Tuple[str, int, int]]:
    grouped: Dict[str, List[int]] = defaultdict(list)
    for event in source_info.comment_events:
        grouped[event.block].append(event.address)
    out = []
    for block in source_info.block_order:
        addresses = grouped.get(block, [])
        if not addresses:
            continue
        out.append((block, min(addresses), max(addresses)))
    return out


def print_instruction_window(load_address: int, blob: bytes, center: int, before_after: int) -> None:
    start = center - before_after * 4
    end = center + before_after * 4
    start = max(start, load_address)
    instructions = decode_range(load_address, blob, start, end)
    for instruction in instructions:
        marker = "<CALLSITE>" if instruction.address == center else ""
        raw = " ".join(f"{byte:02X}" for byte in instruction.bytes_le)
        print(f"    {hex8(instruction.address)}: {raw}  {instruction.text} {marker}".rstrip())


def print_function_report(
    function: FunctionMeta,
    instructions: Sequence[DecodedInstruction],
    source_info: FunctionSourceInfo,
    functions: Sequence[FunctionMeta],
    caller_sites: Sequence[Tuple[FunctionMeta, DecodedInstruction]],
) -> bool:
    print("=" * 88)
    print(f"Function {function.name}  [{hex8(function.entry)} - {hex8(function.end)}]")
    print(
        f"Prologue={function.has_prologue}  Epilogue={function.has_epilogue}  "
        f"Instructions={len(instructions)}"
    )
    prev = previous_function(functions, function.entry)
    if prev is None:
        print("Entry boundary: first harvested function in manifest")
    else:
        print(
            f"Entry boundary: previous function {prev.name} ends at {hex8(prev.end)}; "
            f"gap={function.entry - prev.end - 4} bytes"
        )

    print("Immediate callers:")
    if not caller_sites:
        print("  (none found via direct JAL scan)")
    else:
        for caller_function, call_instruction in caller_sites:
            print(
                f"  {hex8(call_instruction.address)} in {caller_function.name} -> {hex8(function.entry)}"
            )

    contiguous_ok, missing_comments = contiguous_address_check(instructions, source_info)
    valid_count = sum(1 for instruction in instructions if instruction.valid)
    print(
        f"Inline-data sanity: decoded-valid={valid_count}/{len(instructions)}, "
        f"comment-covered={len(instructions) - len(missing_comments)}/{len(instructions)}"
    )
    if not contiguous_ok:
        print("  Missing emitted comments for:")
        for address in missing_comments[:8]:
            print(f"    {hex8(address)}")
        if len(missing_comments) > 8:
            print(f"    ... {len(missing_comments) - 8} more")

    helper_mismatches: List[Tuple[DecodedInstruction, str]] = []
    helper_checked = 0
    for instruction in instructions:
        status, expected = helper_match_status(instruction, source_info)
        if status == "skip":
            continue
        helper_checked += 1
        if status != "ok":
            helper_mismatches.append((instruction, expected))
    print(
        f"Load/store helper check: {helper_checked - len(helper_mismatches)}/{helper_checked} matched"
    )
    for instruction, expected in helper_mismatches:
        print(f"  MISMATCH {hex8(instruction.address)} expected emitted helper {expected}")

    delay_results = verify_delay_slots(instructions, source_info, function.end)
    delay_failures = [entry for entry in delay_results if not entry[1]]
    print(
        f"Branch/delay-slot check: {len(delay_results) - len(delay_failures)}/{len(delay_results)} covered"
    )
    for instruction, _, reason in delay_failures:
        print(f"  MISMATCH {hex8(instruction.address)} {instruction.text}: {reason}")

    print("Emitted CFG blocks:")
    for block, start, end in summarize_block_ranges(source_info):
        successors = source_info.block_successors.get(block, [])
        successor_text = ", ".join(successors) if successors else "(return/fallthrough)"
        print(f"  {block}: {hex8(start)} .. {hex8(end)} -> {successor_text}")

    print("Original instruction bytes + disassembly:")
    for instruction in instructions:
        raw = " ".join(f"{byte:02X}" for byte in instruction.bytes_le)
        annotations: List[str] = []
        if instruction.branch_target is not None:
            annotations.append(f"target={hex8(instruction.branch_target)}")
        if instruction.load_store_helper:
            annotations.append(f"emits={instruction.load_store_helper}")
        note = f"  [{' ; '.join(annotations)}]" if annotations else ""
        print(f"  {hex8(instruction.address)}: {raw}  {instruction.text}{note}")

    verdict_ok = (
        contiguous_ok
        and valid_count == len(instructions)
        and not helper_mismatches
        and not delay_failures
    )
    print(
        f"Verdict for {function.name}: "
        f"{'OK - harvested bytes and emitted CFG agree' if verdict_ok else 'MISMATCH - investigate report above'}"
    )
    return verdict_ok


def print_caller_report(
    target: FunctionMeta,
    caller_sites: Sequence[Tuple[FunctionMeta, DecodedInstruction]],
    load_address: int,
    blob: bytes,
    source_text: str,
    caller_window: int,
) -> None:
    if not caller_sites:
        return
    print("Caller-site windows:")
    shown = set()
    for caller_function, call_instruction in caller_sites:
        key = (caller_function.entry, call_instruction.address)
        if key in shown:
            continue
        shown.add(key)
        print(
            f"  {caller_function.name} @ {hex8(call_instruction.address)} calling {target.name}"
        )
        print_instruction_window(load_address, blob, call_instruction.address, caller_window)
        caller_source = parse_function_source_info(
            caller_function.name,
            extract_function_source(source_text, caller_function.name),
        )
        blocks = {
            event.block
            for event in caller_source.comment_by_address.get(call_instruction.address, [])
        }
        if blocks:
            for block in sorted(blocks):
                successors = caller_source.block_successors.get(block, [])
                successor_text = ", ".join(successors) if successors else "(return/fallthrough)"
                print(f"    emitted block: {block} -> {successor_text}")
        else:
            print("    emitted block: (comment for callsite not found)")


def main() -> int:
    args = parse_args()
    recompile_result_path = Path(args.recompile_result)
    recompile_result = read_json(recompile_result_path)
    source_path = Path(recompile_result["artifacts"]["source"])
    manifest_path = Path(recompile_result["artifacts"]["manifest"])

    source_text = source_path.read_text()
    manifest = read_json(manifest_path)
    functions = load_function_metadata(manifest)
    function_by_entry = {function.entry: function for function in functions}
    load_address, blob = parse_ram_blob(source_text)

    target_addresses = (
        [parse_int(value) for value in args.address]
        if args.address
        else [0x80011A58, 0x80011C8C, 0x80011CA0]
    )

    unknown = [address for address in target_addresses if address not in function_by_entry]
    if unknown:
        print("Unknown function entries:", ", ".join(hex8(address) for address in unknown), file=sys.stderr)
        return 1

    caller_map = find_call_sites(functions, load_address, blob, target_addresses)
    overall_ok = True

    print(f"Recompile result: {recompile_result_path}")
    print(f"Generated source:  {source_path}")
    print(f"Manifest:          {manifest_path}")
    print(f"Embedded blob:     load={hex8(load_address)} size={len(blob)} bytes")

    for target_address in target_addresses:
        function = function_by_entry[target_address]
        instructions = decode_range(load_address, blob, function.entry, function.end)
        function_source = parse_function_source_info(
            function.name,
            extract_function_source(source_text, function.name),
        )
        caller_sites = caller_map.get(target_address, [])
        if not print_function_report(function, instructions, function_source, functions, caller_sites):
            overall_ok = False
        print_caller_report(function, caller_sites, load_address, blob, source_text, args.caller_window)

    print("=" * 88)
    if overall_ok:
        print("Overall verdict: allocator harvest looks semantically consistent; focus shifts back to runtime corruption.")
        return 0
    print("Overall verdict: at least one emitted-vs-origin mismatch was found; inspect the report above.")
    return 2


if __name__ == "__main__":
    sys.exit(main())
