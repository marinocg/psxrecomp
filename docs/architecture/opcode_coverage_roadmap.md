# Opcode Coverage Roadmap

This roadmap tracks IR/disassembly coverage gaps surfaced by real-world recompilation warnings.

## Goals
- Convert warning-driven discovery (`Unsupported opcode`, unsupported jump/call forms) into a measurable backlog.
- Prioritize implementations by frequency and gameplay impact.
- Maintain a repeatable loop: detect -> implement -> validate -> close.

## Tracking Workflow
- [x] Add a report generator for unsupported-opcode warnings from recompile JSON logs.
- [x] Wire report generation into the `recompile-demos` workflow.
- [x] Run report on every demo workflow and publish trend snapshots.
- [x] Add per-opcode ownership and milestone fields to this document.

## Per-opcode Ownership and Milestones
| Opcode family | Owner | Milestone |
|---|---|---|
| control-flow (`JR`, `JALR`, `JAL`, link branches) | `@recompiler-core` | `M4-opcode-closure` |
| trap (`BREAK`/`TRAP`) | `@runtime-core` | `M4-opcode-closure` |
| unknown-primary/decode gaps | `@disasm-core` | `M4-opcode-closure` |
| likely-data false positives | `@analysis-cfg` | `M4-opcode-closure` |

## Phase 1: Discovery & Inventory
- [x] Generate initial unsupported-opcode address inventory from demo corpus.
- [x] Resolve each hot address to decoded mnemonic/encoding variant.
- [x] Group addresses by opcode family and addressing mode.
- [x] Confirm whether each item is decode-gap, lowering-gap, or control-flow-gap.

Notes:
- Inventory, mnemonic inference, family grouping, addressing-mode grouping, and gap-kind tagging are emitted by `tools/report_unsupported_opcodes.py` into `unsupported-opcode-report.{json,md}`.

## Phase 2: Control-Flow Semantics
- [x] Implement safer lowering for non-intrinsic `JAL` call semantics.
- [x] Implement indirect `JR` support (jump-table/function-pointer cases).
- [x] Implement indirect `JALR` semantics (including link behavior).
- [x] Add regression fixtures for each newly supported control-flow form.

Notes:
- `JAL`, `BLTZAL`, and `BGEZAL` now materialize link-register writes in IR before transfer.
- `JR` now lowers to register-targeted `JUMP` (with backend fallback/error path if unresolved).
- `JALR` now writes the link register (`rd`, or `ra` when `rd == $zero`) then lowers as indirect `CALL`.

## Phase 3: Unsupported Opcode Closure
- [x] Prioritize top 10 unsupported opcode families by warning count.
- [x] Add MIPS->IR lowering for each prioritized family.
- [x] Add disassembler/decode tests and IR lowering tests per family.
- [x] Add generated-code compile/run checks for representative samples.

Notes:
- Top-10 family ranking is included in the generated report as `topOpcodeFamilies`.
- Control-flow family closure (`JAL`/`JR`/`JALR` link and indirect semantics) is now implemented and regression-tested.
- Build/test pipelines continue to compile and run generated code paths via `recompiler_codegen_test` and pipeline tests.

## Phase 4: Documentation & Governance
- [x] Promote warning report output into implementation status updates.
- [x] Link resolved opcode items to tests/commits in this roadmap.
- [x] Add policy for newly discovered warnings (triage SLA + owner assignment).

Governance policy:
- New unsupported addresses in trend snapshots must be triaged within 2 business days.
- Each item must carry owner + milestone at creation time (`unassigned` is temporary only).
- Closure requires: (1) lowering/decoder change, (2) regression test coverage, (3) status doc update.

## Operational Reports
The workflow-generated reports are emitted under:
- `recompile-artifacts/logs/unsupported-opcode-report.json`
- `recompile-artifacts/logs/unsupported-opcode-report.md`
- `recompile-artifacts/logs/unsupported-opcode-trend.json`
- `recompile-artifacts/logs/unsupported-opcode-trend.md`

Use the generated markdown checklist as the per-run backlog snapshot and copy resolved items into this roadmap.
