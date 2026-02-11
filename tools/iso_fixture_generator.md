# ISO Fixture Generator

`tools/iso_fixture_generator.py` creates deterministic PlayStation ISO fixtures for debugging parser/recompiler failures.

## Generated fixtures

- **`bad_invalid_pvd.iso`**
  - Intentionally malformed image.
  - No valid `CD001` primary volume descriptor.
  - Expected: ISO parse/open should fail with diagnostics.

- **`good_minimal.iso`**
  - Valid ISO-9660 layout.
  - Contains:
    - `SYSTEM.CNF;1` with `BOOT = cdrom:\GAME.EXE;1`
    - `GAME.EXE;1` minimal valid PS-X EXE payload.
  - Expected: parser + pipeline succeed, generated recompiled output can be validated.

- **`good_demo.iso`** (optional)
  - Generated only when `--demo-exe` is provided.
  - Contains:
    - `SYSTEM.CNF;1` with `BOOT = cdrom:\DEMO.EXE;1`
    - `DEMO.EXE;1` copied from your input executable.

- **`good_demo_with_assets.iso`** (optional)
  - Generated only with `--demo-exe --with-assets`.
  - Adds sample placeholder resources used to validate resource discovery/export:
    - `ASSETS_TEXTURE.TIM;1`
    - `ASSETS_INTRO.STR;1`
    - `ASSETS_THEME.XA;1`

## Usage

```bash
python3 tools/iso_fixture_generator.py --output-dir /tmp/psxrecomp_fixtures
```

With custom demo executable and rich assets:

```bash
python3 tools/iso_fixture_generator.py \
  --output-dir /tmp/psxrecomp_fixtures \
  --demo-exe /path/to/your/demo.psx \
  --with-assets
```

## Validate with psxrecomp

```bash
# expected failure with parse diagnostics
./build/psxrecomp --json -o /tmp/out /tmp/psxrecomp_fixtures/bad_invalid_pvd.iso

# expected success
./build/psxrecomp --json -o /tmp/out /tmp/psxrecomp_fixtures/good_minimal.iso
```

For the richer fixture, inspect resources in output JSON/manifest:

```bash
./build/psxrecomp --json -o /tmp/out /tmp/psxrecomp_fixtures/good_demo_with_assets.iso
```
