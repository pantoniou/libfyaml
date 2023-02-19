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
    FyDocumentState,
    load,
    loads,
    dump,
    dumps,
    load_all,
    loads_all,
    dump_all,
    dumps_all,
    from_python,
    path_list_to_unix_path,
    unix_path_to_path_list,
    _parse,
    _scan,
    _emit,
)

import json as _json


# JSON Serialization Support
def json_dumps(obj, **kwargs):
    """
    Serialize obj to a JSON formatted string, with FyGeneric support.

    Automatically converts FyGeneric objects to Python equivalents before
    serialization, allowing seamless JSON encoding of YAML-parsed data.

    Args:
        obj: Object to serialize (can contain FyGeneric objects)
        **kwargs: Additional arguments passed to json.dumps()

    Returns:
        JSON formatted string

    Example:
        >>> import libfyaml
        >>> data = libfyaml.loads("name: Alice\\nage: 30")
        >>> libfyaml.json_dumps(data)
        '{"name": "Alice", "age": 30}'

        >>> # With formatting
        >>> libfyaml.json_dumps(data, indent=2)
        '{\\n  "name": "Alice",\\n  "age": 30\\n}'
    """
    # Convert FyGeneric to Python if needed
    if isinstance(obj, FyGeneric):
        obj = obj.to_python()
    return _json.dumps(obj, **kwargs)


def json_dump(obj, fp, **kwargs):
    """
    Serialize obj to JSON and write to file-like object, with FyGeneric support.

    Automatically converts FyGeneric objects to Python equivalents before
    serialization, allowing seamless JSON encoding of YAML-parsed data.

    Args:
        obj: Object to serialize (can contain FyGeneric objects)
        fp: File-like object to write to
        **kwargs: Additional arguments passed to json.dump()

    Example:
        >>> import libfyaml
        >>> data = libfyaml.loads("name: Alice\\nage: 30")
        >>> with open('output.json', 'w') as f:
        ...     libfyaml.json_dump(data, f, indent=2)
    """
    # Convert FyGeneric to Python if needed
    if isinstance(obj, FyGeneric):
        obj = obj.to_python()
    return _json.dump(obj, fp, **kwargs)


__version__ = "0.9.0"

__all__ = [
    "FyGeneric",
    "FyDocumentState",
    "load",
    "loads",
    "dump",
    "dumps",
    "load_all",
    "loads_all",
    "dump_all",
    "dumps_all",
    "from_python",
    "path_list_to_unix_path",
    "unix_path_to_path_list",
    "json_dumps",
    "json_dump",
]
