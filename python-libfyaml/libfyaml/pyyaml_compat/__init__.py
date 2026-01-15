"""PyYAML-compatible API using libfyaml.

This module provides a drop-in replacement for PyYAML's most common functions,
allowing projects like pre-commit to use libfyaml without code changes.

Usage:
    # Instead of:
    import yaml

    # Use:
    from libfyaml import pyyaml_compat as yaml

    # Then use normally:
    data = yaml.safe_load("foo: bar")
    output = yaml.safe_dump({"foo": "bar"})

Supported API:
    - load(stream, Loader) / safe_load(stream)
    - dump(data, stream, **kwargs) / safe_dump(data, stream, **kwargs)
    - load_all(stream, Loader) / safe_load_all(stream)
    - dump_all(docs, stream, **kwargs) / safe_dump_all(docs, stream, **kwargs)
    - SafeLoader, CSafeLoader (stub classes for API compatibility)
    - SafeDumper, CSafeDumper (stub classes for API compatibility)
"""

import libfyaml as fy

# Import submodules for 'from yaml.xxx import ...' compatibility
from libfyaml.pyyaml_compat import nodes
from libfyaml.pyyaml_compat import constructor


# Loader classes with add_constructor support for custom tags
class BaseLoader:
    """Base loader class for PyYAML API compatibility."""
    yaml_constructors = {}
    yaml_multi_constructors = {}

    @classmethod
    def add_constructor(cls, tag, constructor_func):
        """Add a constructor for a specific YAML tag."""
        cls.yaml_constructors[tag] = constructor_func

    @classmethod
    def add_multi_constructor(cls, tag_prefix, multi_constructor):
        """Add a multi-constructor for a tag prefix."""
        cls.yaml_multi_constructors[tag_prefix] = multi_constructor

    def construct_scalar(self, node):
        """Construct a scalar value from a node."""
        if node is None:
            return ''
        return node.value if hasattr(node, 'value') else str(node)


class SafeLoader(BaseLoader):
    """Safe loader class for PyYAML API compatibility."""
    yaml_constructors = {}
    yaml_multi_constructors = {}


class Loader(SafeLoader):
    """Full loader class (same as SafeLoader in our safe implementation)."""
    yaml_constructors = {}
    yaml_multi_constructors = {}


# C-accelerated versions (same as regular versions in libfyaml)
CSafeLoader = SafeLoader
CLoader = Loader
CBaseLoader = BaseLoader


# Dumper classes
class BaseDumper:
    """Base dumper class for PyYAML API compatibility."""
    pass


class SafeDumper(BaseDumper):
    """Safe dumper class for PyYAML API compatibility."""
    pass


class Dumper(SafeDumper):
    """Full dumper class for PyYAML API compatibility."""
    pass


# C-accelerated dumper versions
CSafeDumper = SafeDumper
CDumper = Dumper


def _normalize_yaml(text):
    """Normalize YAML text to handle edge cases libfyaml doesn't parse directly.

    Some bare values (empty string, null, [], {}) need special handling.
    """
    stripped = text.strip()

    # Empty or whitespace-only -> None
    if not stripped:
        return None, True

    # Bare null values -> None
    if stripped in ('~', 'null', 'Null', 'NULL'):
        return None, True

    # Empty collections need document marker for libfyaml
    if stripped in ('[]', '[ ]'):
        return [], True
    if stripped in ('{}', '{ }'):
        return {}, True

    # Bare quoted empty string
    if stripped in ('""', "''"):
        return '', True

    return text, False


def load(stream, Loader=SafeLoader):
    """Parse YAML stream and return Python object.

    Args:
        stream: String or file-like object containing YAML
        Loader: Loader class (ignored - libfyaml always uses safe parsing)

    Returns:
        Python object (dict, list, str, int, float, bool, or None)

    Example:
        >>> load("foo: bar", Loader=SafeLoader)
        {'foo': 'bar'}
    """
    if hasattr(stream, 'read'):
        stream = stream.read()

    # Handle edge cases that libfyaml doesn't parse directly
    result, handled = _normalize_yaml(stream)
    if handled:
        return result

    return fy.loads(stream).to_python()


def safe_load(stream):
    """Parse YAML stream safely and return Python object.

    This is equivalent to load(stream, Loader=SafeLoader).

    Args:
        stream: String or file-like object containing YAML

    Returns:
        Python object (dict, list, str, int, float, bool, or None)

    Example:
        >>> safe_load("foo: bar")
        {'foo': 'bar'}
    """
    return load(stream, SafeLoader)


def compose(stream, Loader=SafeLoader):
    """Parse YAML stream and return node tree (FyGeneric).

    Unlike load(), this returns the FyGeneric node directly without
    converting to Python objects. This is useful for YAML manipulation.

    Args:
        stream: String or file-like object containing YAML
        Loader: Loader class (ignored)

    Returns:
        FyGeneric node, or None for empty/null documents
    """
    if hasattr(stream, 'read'):
        stream = stream.read()

    # Handle edge cases
    result, handled = _normalize_yaml(stream)
    if handled:
        # For compose, we need to return a node-like object or None
        if result is None:
            return None
        # For empty collections, create FyGeneric from them
        return fy.from_python(result)

    return fy.loads(stream)


def compose_all(stream, Loader=SafeLoader):
    """Parse all YAML documents and return node trees.

    Args:
        stream: String or file-like object containing YAML
        Loader: Loader class (ignored)

    Yields:
        FyGeneric nodes, one per document
    """
    if hasattr(stream, 'read'):
        stream = stream.read()

    for doc in fy.loads_all(stream):
        yield doc


def load_all(stream, Loader=SafeLoader):
    """Parse all YAML documents in stream.

    Args:
        stream: String or file-like object containing YAML
        Loader: Loader class (ignored - libfyaml always uses safe parsing)

    Yields:
        Python objects, one per YAML document

    Example:
        >>> list(load_all("---\\nfoo: 1\\n---\\nbar: 2\\n"))
        [{'foo': 1}, {'bar': 2}]
    """
    if hasattr(stream, 'read'):
        stream = stream.read()
    for doc in fy.loads_all(stream):
        yield doc.to_python()


def safe_load_all(stream):
    """Parse all YAML documents in stream safely.

    This is equivalent to load_all(stream, Loader=SafeLoader).

    Args:
        stream: String or file-like object containing YAML

    Yields:
        Python objects, one per YAML document
    """
    return load_all(stream, SafeLoader)


def dump(data, stream=None, Dumper=SafeDumper, default_flow_style=False,
         default_style=None, canonical=None, indent=None, width=None,
         allow_unicode=True, line_break=None, encoding=None,
         explicit_start=None, explicit_end=None, version=None, tags=None,
         sort_keys=True):
    """Serialize Python object to YAML string.

    Args:
        data: Python object to serialize
        stream: File-like object to write to (optional)
        Dumper: Dumper class (ignored - libfyaml uses its own emitter)
        default_flow_style: Use flow style for collections (default: False)
        indent: Indentation width (default: 2)
        sort_keys: Sort dictionary keys (default: True)
        Other PyYAML parameters are accepted but may be ignored.

    Returns:
        YAML string if stream is None, otherwise None

    Example:
        >>> dump({"foo": "bar"})
        'foo: bar\\n'
    """
    # Convert Python object to FyGeneric
    doc = fy.from_python(data)

    # Use module-level dumps function
    result = fy.dumps(doc)

    if stream is not None:
        stream.write(result)
        return None
    return result


def safe_dump(data, stream=None, **kwargs):
    """Serialize Python object to YAML string safely.

    This is equivalent to dump(data, stream, Dumper=SafeDumper, **kwargs).

    Args:
        data: Python object to serialize
        stream: File-like object to write to (optional)
        **kwargs: Additional arguments passed to dump()

    Returns:
        YAML string if stream is None, otherwise None
    """
    kwargs['Dumper'] = SafeDumper
    return dump(data, stream, **kwargs)


def dump_all(documents, stream=None, Dumper=SafeDumper, default_flow_style=False,
             default_style=None, canonical=None, indent=None, width=None,
             allow_unicode=True, line_break=None, encoding=None,
             explicit_start=None, explicit_end=None, version=None, tags=None,
             sort_keys=True):
    """Serialize sequence of Python objects to YAML string.

    Args:
        documents: Iterable of Python objects to serialize
        stream: File-like object to write to (optional)
        Other arguments same as dump()

    Returns:
        YAML string if stream is None, otherwise None
    """
    # Convert all documents to FyGeneric
    docs = [fy.from_python(data) for data in documents]

    # Use module-level dumps_all function
    result = fy.dumps_all(docs)

    if stream is not None:
        stream.write(result)
        return None
    return result


def safe_dump_all(documents, stream=None, **kwargs):
    """Serialize sequence of Python objects to YAML string safely.

    This is equivalent to dump_all(documents, stream, Dumper=SafeDumper, **kwargs).
    """
    kwargs['Dumper'] = SafeDumper
    return dump_all(documents, stream, **kwargs)


# Additional compatibility - these are less commonly used but may be needed

class YAMLError(Exception):
    """Base exception for YAML errors."""
    pass


class YAMLLoadWarning(UserWarning):
    """Warning for unsafe YAML loading."""
    pass


# Expose common error types
MarkedYAMLError = YAMLError
ScannerError = YAMLError
ParserError = YAMLError
ReaderError = YAMLError
ComposerError = YAMLError
ConstructorError = YAMLError
EmitterError = YAMLError
RepresenterError = YAMLError

# Re-export node types at module level (yaml.ScalarNode, etc.)
from libfyaml.pyyaml_compat.nodes import (
    Node,
    ScalarNode,
    SequenceNode,
    MappingNode,
    CollectionNode,
)
