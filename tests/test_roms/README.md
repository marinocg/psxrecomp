# Test ROMs

This directory is for test ROMs used in integration testing.

## Structure

```
test_roms/
├── homebrew/          # Open-source homebrew ROMs (OK to commit)
├── commercial/        # Commercial ROMs (DO NOT commit - in .gitignore)
└── README.md          # This file
```

## Homebrew Test ROMs

The following homebrew ROMs are useful for testing:

### Basic Functionality
- **PSX Hello World**: Simple "Hello World" program
- **PSX Memory Test**: Tests RAM access patterns
- **PSX GPU Test**: Tests basic GPU rendering

### Finding Homebrew ROMs

You can find PSX homebrew at:
- [PSX-Place](https://www.psx-place.com/resources/categories/homebrew.14/)
- [Homebrew Hub](https://psxdev.net/)
- Various PSX development forums

## Commercial ROMs

⚠️ **Important**: Do NOT commit commercial ROMs to the repository.

For testing with commercial games:
1. Place them in `test_roms/commercial/` (this is gitignored)
2. Use them only for local testing
3. Document test results without sharing the ROMs

## Adding Test ROMs

When adding homebrew ROMs:

1. Verify the license allows redistribution
2. Place in `homebrew/` directory
3. Add entry to this README with:
   - ROM name
   - Description
   - Author/Source
   - License
   - Expected test results

Example:
```markdown
### My Test ROM
- **File**: `homebrew/mytest.bin`
- **Author**: Developer Name
- **License**: MIT
- **Description**: Tests basic CPU operations
- **Expected**: Should display "TEST OK" on screen
```

## Test Data Format

For automated testing, include:
- `.bin` or `.iso` file (game image)
- `.cue` file if multi-track
- `.md` file with test expectations

Example test file:
```markdown
# test_rom_name.md

## Expected Behavior
- Should display "Hello World" at coordinates (100, 100)
- Frame count should reach 60 within 1 second
- No GPU errors

## Known Issues
- None

## Test Commands
```bash
./psxrecomp homebrew/test_rom.bin -o output_test
cd output_test && cmake . && make
./recompiled_game
```
```

## Creating Your Own Test ROMs

If you want to create test ROMs for specific features:

1. Use [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK) for homebrew development
2. Keep tests simple and focused
3. Document expected behavior
4. Include source code if possible

## Running Tests

```bash
# Run all integration tests
cd build
ctest -L integration

# Run specific ROM test
./tests/integration/rom_test homebrew/test.bin
```

## Current Test Coverage

### Implemented Tests
- None yet (project in early development)

### Planned Tests
- [ ] Basic CPU instruction execution
- [ ] Memory access patterns
- [ ] GPU primitive rendering
- [ ] SPU audio playback
- [ ] CD-ROM data reading
- [ ] Controller input
- [ ] Memory card I/O

## Contributing Test Cases

If you have or create useful test ROMs:

1. Ensure they're freely licensed
2. Document thoroughly
3. Submit via pull request
4. Include expected output/screenshots

---

**Remember**: Never commit commercial ROMs or copyrighted content!
