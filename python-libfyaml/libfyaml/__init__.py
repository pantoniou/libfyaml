"""
libfyaml - Fast YAML/JSON parser with zero-casting interface

This module provides Python bindings to libfyaml using its generic type system.
Like NumPy, parsed data stays in native C format for efficiency, with lazy
conversion to Python objects only when needed.

Key features:
- ZERO manual type conversions needed
- Arithmetic, comparisons, and type-specific methods work automatically
- Lazy conversion for excellent performance
- Dict-like interface with keys(), values(), items()
- File I/O helpers: load() and dump()

Example:
    >>> import libfyaml
    >>> doc = libfyaml.loads("name: Alice\\nage: 30")
    >>> doc["name"].upper()  # String methods work directly!
    'ALICE'
    >>> doc["age"] + 1  # Arithmetic works directly!
    31
    >>> doc.to_python()  # Full conversion: {'name': 'Alice', 'age': 30}
"""

from libfyaml._libfyaml import (
    FyGeneric,
    load,
    loads,
    dump,
    dumps,
    from_python,
)

__version__ = "0.2.0"

__all__ = [
    "FyGeneric",
    "load",
    "loads",
    "dump",
    "dumps",
    "from_python",
]
