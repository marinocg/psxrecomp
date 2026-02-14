# Opcode Coverage Roadmap

This roadmap tracks IR/disassembly coverage gaps surfaced by real-world recompilation warnings.

## Goals
- Convert warning-driven discovery (`Unsupported opcode`, unsupported jump/call forms) into a measurable backlog.
- Prioritize implementations by frequency and gameplay impact.
- Maintain a repeatable loop: detect -> implement -> validate -> close.

## Tracking Workflow
- [x] Add a report generator for unsupported-opcode warnings from recompile JSON logs.
- [x] Wire report generation into the `recompile-demos` workflow.
- [ ] Run report on every demo workflow and publish trend snapshots.
- [ ] Add per-opcode ownership and milestone fields to this document.

## Phase 1: Discovery & Inventory
- [ ] Generate initial unsupported-opcode address inventory from demo corpus.
- [ ] Resolve each hot address to decoded mnemonic/encoding variant.
- [ ] Group addresses by opcode family and addressing mode.
- [ ] Confirm whether each item is decode-gap, lowering-gap, or control-flow-gap.

## Phase 2: Control-Flow Semantics
- [ ] Implement safer lowering for non-intrinsic `JAL` call semantics.
- [ ] Implement indirect `JR` support (jump-table/function-pointer cases).
- [ ] Implement indirect `JALR` semantics (including link behavior).
- [ ] Add regression fixtures for each newly supported control-flow form.

## Phase 3: Unsupported Opcode Closure
- [ ] Prioritize top 10 unsupported opcode families by warning count.
- [ ] Add MIPS->IR lowering for each prioritized family.
- [ ] Add disassembler/decode tests and IR lowering tests per family.
- [ ] Add generated-code compile/run checks for representative samples.

## Phase 4: Documentation & Governance
- [ ] Promote warning report output into implementation status updates.
- [ ] Link resolved opcode items to tests/commits in this roadmap.
- [ ] Add policy for newly discovered warnings (triage SLA + owner assignment).

## Operational Reports
The workflow-generated reports are emitted under:
- `recompile-artifacts/logs/unsupported-opcode-report.json`
- `recompile-artifacts/logs/unsupported-opcode-report.md`

Use the generated markdown checklist as the per-run backlog snapshot and copy resolved items into this roadmap.
