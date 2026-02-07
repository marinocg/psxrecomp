# Contributing to PSXRecomp

Thank you for your interest in contributing to PSXRecomp! This document provides guidelines for contributing to the project.

## Code of Conduct

- Be respectful and inclusive
- Focus on constructive feedback
- Help others learn and grow
- Respect different perspectives and experience levels

## Getting Started

1. **Fork the repository** on GitHub
2. **Clone your fork** locally:
   ```bash
   git clone https://github.com/YOUR_USERNAME/psxrecomp.git
   cd psxrecomp
   ```
3. **Create a branch** for your changes:
   ```bash
   git checkout -b feature/your-feature-name
   ```
4. **Make your changes** following our coding standards
5. **Test your changes** thoroughly
6. **Commit your changes** with clear commit messages
7. **Push to your fork** and submit a pull request

## Development Setup

### Prerequisites

- C++17 compatible compiler
- CMake 3.15+
- Git

### Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Running Tests

```bash
cd build
ctest --output-on-failure
```

## Coding Standards

### C++ Style Guide

- Use modern C++17 features
- Follow the naming conventions in [agents.md](agents.md)
- Use `#pragma once` for header guards
- Keep functions focused and single-purpose
- Prefer `const` correctness
- Use smart pointers over raw pointers
- Add Doxygen comments for public APIs

### Example

```cpp
#pragma once

#include <cstdint>
#include <vector>

namespace psxrecomp {

/**
 * @brief Parses PSX ISO 9660 filesystem
 */
class IsoParser {
public:
    /**
     * @brief Construct a new Iso Parser object
     * @param filename Path to ISO file
     */
    explicit IsoParser(const std::string& filename);
    
    /**
     * @brief Parse the ISO file
     * @return true if successful, false otherwise
     */
    bool parse();
    
private:
    std::string m_filename;
    std::vector<uint8_t> m_data;
    
    bool readSector(uint32_t sector);
};

} // namespace psxrecomp
```

## Commit Messages

Write clear, descriptive commit messages:

```
Add ISO parser for PSX-EXE extraction

- Implement ISO 9660 directory parsing
- Add support for Mode 2 sectors
- Extract PSX-EXE from System.cnf

Closes #42
```

Format:
- First line: Short summary (50 chars or less)
- Blank line
- Detailed description if needed
- Reference issues with `#issue_number`

## Pull Request Process

1. **Update documentation** if you're changing functionality
2. **Add tests** for new features
3. **Ensure all tests pass** before submitting
4. **Keep PRs focused** - one feature or fix per PR
5. **Provide context** in the PR description:
   - What does this PR do?
   - Why is this change needed?
   - How has it been tested?
   - Any breaking changes?

### PR Checklist

- [ ] Code follows project style guidelines
- [ ] Self-review of code completed
- [ ] Comments added for complex logic
- [ ] Documentation updated
- [ ] Tests added/updated
- [ ] All tests pass
- [ ] No new compiler warnings
- [ ] Commit messages are clear

## Testing Guidelines

### Unit Tests

- Test individual components in isolation
- Cover both success and failure cases
- Test edge cases and boundary conditions
- Use descriptive test names

### Integration Tests

- Test component interactions
- Use realistic test data
- Test with actual PSX binaries (homebrew only in public tests)

### Test Structure

```cpp
#include <gtest/gtest.h>
#include "psxrecomp/iso_parser.h"

namespace psxrecomp {
namespace test {

TEST(IsoParserTest, ParseValidIso) {
    IsoParser parser("test_data/valid.iso");
    EXPECT_TRUE(parser.parse());
}

TEST(IsoParserTest, ParseInvalidIso) {
    IsoParser parser("test_data/invalid.iso");
    EXPECT_FALSE(parser.parse());
}

} // namespace test
} // namespace psxrecomp
```

## Documentation

- Update README.md for user-facing changes
- Update agents.md for architecture/design changes
- Add inline comments for complex algorithms
- Document assumptions and limitations
- Keep docs/ folder updated

## Areas for Contribution

We especially welcome contributions in these areas:

### High Priority
- ISO/BIN file parsing
- MIPS R3000 instruction disassembly
- PSX-EXE format loader
- Basic IR design

### Medium Priority
- Code optimization passes
- Runtime library components
- Test coverage improvements
- Documentation and examples

### Future Work
- GPU rendering backends
- SPU audio synthesis
- Save state functionality
- Debugging tools

## Questions?

- Check existing issues and discussions
- Review [agents.md](agents.md) for technical details
- Ask in GitHub Discussions
- Open an issue with the `question` label

## License

By contributing to PSXRecomp, you agree that your contributions will be licensed under the MIT License.

## Recognition

All contributors will be acknowledged in the README and release notes.

Thank you for contributing to PSXRecomp! 🎮
