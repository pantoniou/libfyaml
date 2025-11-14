# Generic API Design: Achieving Python Ergonomics in C

This document describes the design evolution of libfyaml's generic type system API, showing how careful API design can achieve Python-level ergonomics while maintaining C's type safety and performance.

## Documentation Structure

This design document is split across several files for easier navigation:

1. **[GENERIC-API-BASICS.md](GENERIC-API-BASICS.md)** - Core API design
   - The problem with verbose generic APIs
   - Solution using C11 `_Generic` dispatch
   - Type checking shortcuts
   - Type casting with `fy_cast()`
   - The empty collection pattern

2. **[GENERIC-API-MEMORY.md](GENERIC-API-MEMORY.md)** - Memory management
   - Two lifetime models (stack vs builder)
   - Builder configuration and policies
   - Deduplication and internalization
   - Stackable allocators
   - Custom allocators

3. **[GENERIC-API-FUNCTIONAL.md](GENERIC-API-FUNCTIONAL.md)** - Functional operations
   - Immutable collection manipulation
   - `fy_assoc()`, `fy_dissoc()`, `fy_conj()`, `fy_assoc_at()`
   - Structural sharing and performance
   - Comparison with imperative approaches
   - Common functional patterns

4. **[GENERIC-API-UTILITIES.md](GENERIC-API-UTILITIES.md)** - Python-like utilities
   - Mapping utilities (`fy_keys()`, `fy_values()`, `fy_items()`, `fy_merge()`)
   - Sequence utilities (`fy_slice()`, `fy_take()`, `fy_drop()`, `fy_reverse()`)
   - Higher-order functions (`fy_filter()`, `fy_map()`, `fy_reduce()`)
   - Collection utilities (`fy_count()`, `fy_flatten()`, `fy_zip()`)

5. **[GENERIC-API-REFERENCE.md](GENERIC-API-REFERENCE.md)** - API reference
   - Iteration API (sequences and mappings)
   - Complete API reference
   - Construction, type checking, value extraction
   - Document integration

6. **[GENERIC-API-EXAMPLES.md](GENERIC-API-EXAMPLES.md)** - Real-world usage
   - Anthropic Messages API example
   - Language comparisons (Python, TypeScript, Rust)
   - Complex processing examples

7. **[GENERIC-API-PATTERNS.md](GENERIC-API-PATTERNS.md)** - Best practices
   - Configuration management
   - Per-request processing
   - Hierarchical scopes
   - Thread-local builders
   - Functional data transformation
   - Anti-patterns to avoid

8. **[GENERIC-API-PERFORMANCE.md](GENERIC-API-PERFORMANCE.md)** - Performance
   - Design principles
   - Performance characteristics
   - Zero-cost abstractions
   - Memory efficiency
   - Benchmark comparisons

## Quick Start

For a high-level overview of the generic type system API, see [GENERIC-API.md](GENERIC-API.md).

For implementation details and design rationale, start with [GENERIC-API-BASICS.md](GENERIC-API-BASICS.md).

## Design Philosophy

The libfyaml generic API achieves **Python-level ergonomics in C** through:

- **C11 `_Generic` dispatch**: Type-safe polymorphism at compile time
- **Smart defaults**: Natural error handling with empty collections
- **Immutability**: Thread-safe by design
- **Flexible memory management**: From stack allocation to custom allocators
- **Zero-cost abstractions**: All dispatch happens at compile time

This design proves that systems languages can match the ergonomics of high-level languages without sacrificing performance or safety.
