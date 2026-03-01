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
    resources_manifest.json
  fs/
    ... exported loose resources (.TIM/.STR/.XA) when found
```

Notes:

- `resources/index/*` is always generated when ISO parsing succeeds.
- `resources/fs/` may be empty (for example when the disc only contains packed archives).

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
    "resourcesManifestPath": "index/resources_manifest.json"
  },
  "exports": {
    "filesystem": {
      "enabled": true,
      "root": "fs",
      "result": {
        "filesExported": 0,
        "bytesExported": 0,
        "skippedDueToLimits": 0
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
