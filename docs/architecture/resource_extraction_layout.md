# Resource Extraction Layout 1.0

This document defines the stable resource output layout emitted by the pipeline for ISO-like
inputs (`.iso`, `.bin`, `.cue`), plus the index schema that tooling should consume first.

## Goals

- Always produce a machine-readable resource index, even when no loose assets are exported.
- Keep extraction outputs deterministic and easy to diff.
- Provide a single entrypoint for future tooling and runtime diagnostics.

## Output Layout

Under each generated module output:

```text
resources/
  index/
    disc_tree.json
    disc_meta.json
    recomp_inputs.json
    resources_manifest.json
  fs/
    ... exported ISO-relative files (policy-driven)
```

Notes:

- `resources/index/*` is always generated when ISO parsing succeeds.
- `resources/fs/` is now a real ISO-relative filesystem export.
- By default (`full` mode), all disc files are exported under `resources/fs/<iso path>`.
- `minimal` and `smart` modes still always include boot-critical files (policy-dependent).

## Filesystem Export Policy

Policy is configured with `PipelineOptions::ResourceExportOptions`:

- `minimal`: export always-included set only (`SYSTEM.CNF`, boot executable, and optionally all `.EXE`).
- `smart`: export always-included set plus small files first, honoring byte caps.
- `full`: export all files (default behavior).

For CLI use, environment variables are supported:

- `PSXRECOMP_RES_FS_MODE=minimal|smart|full`
- `PSXRECOMP_RES_MAX_TOTAL_BYTES`
- `PSXRECOMP_RES_MAX_SINGLE_FILE_BYTES`
- `PSXRECOMP_RES_ALWAYS_EXPORT_SYSTEM_CNF`
- `PSXRECOMP_RES_ALWAYS_EXPORT_BOOT_EXE`
- `PSXRECOMP_RES_ALWAYS_EXPORT_ALL_EXE`
- `PSXRECOMP_RES_ALLOW_PREFIXES` (comma-separated)
- `PSXRECOMP_RES_DENY_PREFIXES` (comma-separated)

## Index Files

### `resources/index/disc_tree.json`

Recursive filesystem listing from ISO root.

- Includes full path, type, size (for files), flags, and extent list.
- Multi-extent files are represented with multiple extent entries.

Schema summary:

```json
{
  "schemaVersion": "1.0",
  "root": "",
  "entries": [
    {
      "path": "DATA/FILE.BIN",
      "type": "file",
      "bytes": 4096,
      "flags": "0x00",
      "extents": [
        { "lba": 1234, "bytes": 2048, "continues": true },
        { "lba": 1235, "bytes": 2048, "continues": false }
      ]
    }
  ]
}
```

### `resources/index/disc_meta.json`

High-level disc metadata for diagnostics and tooling.

- Input/volume information
- Joliet usage
- Sector sizes and track metadata
- Boot executable hint
- File/directory counts

### `resources/index/resources_manifest.json`

Primary entrypoint for tooling.

- References all index files
- Summarizes filesystem export results
- Includes runtime-friendly summary fields for generated runners

Schema summary:

```json
{
  "schemaVersion": "1.0",
  "generatedAt": "2026-03-01T12:34:56Z",
  "pipelineVersion": "1.0.0",
  "disc": {
    "inputPath": "GAME.iso",
    "volumeLabel": "GAME_DISC",
    "isJoliet": true,
    "rawSectorSize": 2352,
    "logicalBlockSize": 2048
  },
  "index": {
    "discTreePath": "index/disc_tree.json",
    "discMetaPath": "index/disc_meta.json",
    "recompInputsPath": "index/recomp_inputs.json",
    "resourcesManifestPath": "index/resources_manifest.json"
  },
  "exports": {
    "filesystem": {
      "enabled": true,
      "root": "fs",
      "mode": "smart",
      "caps": {
        "maxTotalBytes": 536870912,
        "maxSingleFileBytes": 134217728
      },
      "alwaysIncluded": ["systemCnf", "bootExecutable", "allExe"],
      "result": {
        "filesExported": 1234,
        "bytesExported": 456789012,
        "skippedDueToLimits": 27
      }
    },
    "embeddedScan": {
      "enabled": false,
      "result": {
        "containersScanned": 0,
        "hitsExtracted": 0
      }
    }
  },
  "runtimeSummary": {
    "filesystemEnabled": true,
    "filesystemMode": "smart",
    "containersScanned": 0,
    "hitsExtracted": 0
  },
  "stats": {
    "discFiles": 100,
    "discDirs": 10,
    "largestFiles": [
      { "path": "DATA/MAIN.DAT", "bytes": 104857600 },
      { "path": "DATA/ARCHIVE.BIN", "bytes": 52428800 },
      { "path": "VOICE/JP.VB", "bytes": 8388608 }
    ]
  },
  "warnings": []
}
```

### `resources/index/recomp_inputs.json`

Recompilation input descriptor for no-ISO reruns.

- Captures the selected boot executable path and exported filesystem path.
- Captures exported `SYSTEM.CNF` path.
- Enumerates all discovered executables (`findExecutable()` + `listExecutables()` set) and
  includes parsed PS-X EXE metadata from `PsxExeLoader`.

Schema summary:

```json
{
  "schemaVersion": "1.0",
  "boot": { "isoPath": "GAMEB.EXE", "exportedPath": "fs/GAMEB.EXE" },
  "systemCnf": { "exportedPath": "fs/SYSTEM.CNF" },
  "executables": [
    {
      "isoPath": "GAMEA.EXE",
      "exportedPath": "fs/GAMEA.EXE",
      "psxExe": {
        "loadAddr": "0x80010000",
        "entry": "0x80010000",
        "size": 16
      }
    }
  ]
}
```

## Pipeline Manifest Integration

Top-level pipeline `manifest.json` now includes:

- `output.resourceRoot`
- `output.resourceManifest`

These point at `resources/` and `resources/index/resources_manifest.json` respectively when
generated.

## Runner Behavior

Generated runners check for `resources/index/resources_manifest.json`:

- Warn when `resources/` is missing.
- Warn when `resources_manifest.json` is missing.
- Print summary fields:
  - `runtimeSummary.filesystemEnabled`
  - `runtimeSummary.containersScanned`
  - `runtimeSummary.hitsExtracted`
