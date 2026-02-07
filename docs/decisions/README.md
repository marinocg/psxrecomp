# Decision Log

This directory contains architectural decision records (ADRs) for the psxrecomp project.

## Template

Each decision should follow this format:

```markdown
# [Number]. [Title]

Date: YYYY-MM-DD

## Status

[Proposed | Accepted | Deprecated | Superseded]

## Context

What is the issue that we're seeing that is motivating this decision?

## Decision

What is the change that we're proposing and/or doing?

## Consequences

What becomes easier or more difficult to do because of this change?
```

## Decisions

1. [Use CMake for Build System](#001-use-cmake-for-build-system)
2. [C++17 as Minimum Standard](#002-cpp17-as-minimum-standard)
3. [Static Recompilation Approach](#003-static-recompilation-approach)

---

### 001. Use CMake for Build System

**Date**: 2026-02-07  
**Status**: Accepted

**Context**: Need a cross-platform build system that works on Windows, Linux, and macOS.

**Decision**: Use CMake 3.15+ as the build system for psxrecomp.

**Consequences**: 
- ✅ Cross-platform compatibility
- ✅ IDE integration (VS Code, CLion, Visual Studio)
- ✅ Package manager support (vcpkg, Conan)
- ⚠️ Learning curve for contributors unfamiliar with CMake

---

### 002. C++17 as Minimum Standard

**Date**: 2026-02-07  
**Status**: Accepted

**Context**: Need modern C++ features while maintaining broad compiler support.

**Decision**: Use C++17 as the minimum C++ standard, with optional C++20 features where available.

**Consequences**:
- ✅ Modern features: std::optional, std::variant, structured bindings
- ✅ Broad compiler support (GCC 8+, Clang 7+, MSVC 2019+)
- ✅ Good balance of features and compatibility
- ⚠️ Cannot use C++20 concepts, ranges, coroutines as required features

---

### 003. Static Recompilation Approach

**Date**: 2026-02-07  
**Status**: Accepted

**Context**: Need to decide between emulation, dynamic recompilation, and static recompilation.

**Decision**: Use static recompilation - convert entire PSX executable to C++ ahead of time.

**Consequences**:
- ✅ Best performance potential (compile-time optimizations)
- ✅ Easier to debug (standard C++ tools)
- ✅ No runtime overhead for code translation
- ⚠️ Cannot handle self-modifying code easily
- ⚠️ Requires full program analysis upfront
- ⚠️ May need fallback interpreter for dynamic code

**Alternatives Considered**:
1. **Emulation**: Too slow for modern expectations
2. **Dynamic Recompilation (JIT)**: Complex, security issues, platform restrictions
3. **Static Recompilation**: Chosen for performance and simplicity
