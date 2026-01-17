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

# Version info for compatibility
__version__ = '6.0.1'  # Mimic PyYAML version for compatibility checks
__with_libyaml__ = True  # We're using libfyaml as backend

# Import submodules for 'from yaml.xxx import ...' compatibility
from libfyaml.pyyaml_compat import nodes
from libfyaml.pyyaml_compat import constructor
from libfyaml.pyyaml_compat import representer
from libfyaml.pyyaml_compat import parser
from libfyaml.pyyaml_compat import cyaml
from libfyaml.pyyaml_compat import resolver
from libfyaml.pyyaml_compat import scanner
from libfyaml.pyyaml_compat import composer
from libfyaml.pyyaml_compat import error

# Import error classes for direct access (yaml.YAMLError, etc.)
from libfyaml.pyyaml_compat.error import YAMLError, MarkedYAMLError, Mark
from libfyaml.pyyaml_compat.scanner import ScannerError
from libfyaml.pyyaml_compat.parser import ParserError
from libfyaml.pyyaml_compat.composer import ComposerError


# Loader classes with add_constructor support for custom tags
class BaseLoader:
    """Base loader class for PyYAML API compatibility.

    This class provides PyYAML-compatible constructor methods that work
    with FyGeneric nodes wrapped in _NodeWrapper objects.
    """
    yaml_constructors = {}
    yaml_multi_constructors = {}

    def __init__(self, stream=None):
        """Initialize loader with optional stream."""
        self._stream = stream

    @classmethod
    def add_constructor(cls, tag, constructor_func):
        """Add a constructor for a specific YAML tag."""
        # Create a copy of the dict for this class if it's inherited
        if 'yaml_constructors' not in cls.__dict__:
            cls.yaml_constructors = cls.yaml_constructors.copy()
        cls.yaml_constructors[tag] = constructor_func

    @classmethod
    def add_multi_constructor(cls, tag_prefix, multi_constructor):
        """Add a multi-constructor for a tag prefix."""
        if 'yaml_multi_constructors' not in cls.__dict__:
            cls.yaml_multi_constructors = cls.yaml_multi_constructors.copy()
        cls.yaml_multi_constructors[tag_prefix] = multi_constructor

    def construct_scalar(self, node):
        """Construct a scalar value from a node.

        Args:
            node: A _NodeWrapper or FyGeneric node

        Returns:
            The scalar value (str, int, float, bool, or None)
        """
        if node is None:
            return ''
        # Handle _NodeWrapper
        if hasattr(node, '_generic'):
            return node._generic.to_python()
        # Handle FyGeneric directly
        if hasattr(node, 'to_python'):
            return node.to_python()
        # Handle raw value
        if hasattr(node, 'value'):
            return node.value
        return str(node)

    def construct_mapping(self, node, deep=False):
        """Construct a mapping (dict) from a node.

        Args:
            node: A _NodeWrapper or FyGeneric mapping node
            deep: If True, recursively construct nested objects

        Returns:
            A dictionary
        """
        if node is None:
            return {}
        # Get the generic from wrapper
        generic = node._generic if hasattr(node, '_generic') else node
        if hasattr(generic, 'value'):
            generic = generic.value

        if hasattr(generic, 'is_mapping') and generic.is_mapping():
            if deep:
                return {self.construct_object(k, deep=True): self.construct_object(v, deep=True)
                        for k, v in generic.items()}
            else:
                return {k.to_python() if hasattr(k, 'to_python') else k:
                        v.to_python() if hasattr(v, 'to_python') else v
                        for k, v in generic.items()}
        elif isinstance(generic, dict):
            return generic
        return {}

    def construct_sequence(self, node, deep=False):
        """Construct a sequence (list) from a node.

        Args:
            node: A _NodeWrapper or FyGeneric sequence node
            deep: If True, recursively construct nested objects

        Returns:
            A list
        """
        if node is None:
            return []
        # Get the generic from wrapper
        generic = node._generic if hasattr(node, '_generic') else node
        if hasattr(generic, 'value'):
            generic = generic.value

        if hasattr(generic, 'is_sequence') and generic.is_sequence():
            if deep:
                return [self.construct_object(item, deep=True) for item in generic]
            else:
                return [item.to_python() if hasattr(item, 'to_python') else item
                        for item in generic]
        elif isinstance(generic, list):
            return generic
        return []

    def construct_object(self, node, deep=False):
        """Construct a Python object from a node.

        This is the main entry point for constructing objects. It checks
        for registered constructors and dispatches appropriately.

        Args:
            node: A _NodeWrapper or FyGeneric node
            deep: If True, recursively construct nested objects

        Returns:
            The constructed Python object
        """
        if node is None:
            return None

        # Get the generic
        generic = node._generic if hasattr(node, '_generic') else node

        # Get tag
        tag = None
        if hasattr(node, 'tag'):
            tag = node.tag
        elif hasattr(generic, 'get_tag'):
            tag = generic.get_tag()

        # Check for exact tag match in constructors
        if tag and tag in self.yaml_constructors:
            constructor = self.yaml_constructors[tag]
            wrapped = _NodeWrapper(generic) if not hasattr(node, '_generic') else node
            return constructor(self, wrapped)

        # Check for multi-constructors (tag prefix matches)
        if tag and self.yaml_multi_constructors:
            for prefix, multi_constructor in self.yaml_multi_constructors.items():
                if prefix and tag.startswith(prefix):
                    wrapped = _NodeWrapper(generic) if not hasattr(node, '_generic') else node
                    return multi_constructor(self, tag[len(prefix):], wrapped)

        # Check for None tag constructor (catch-all)
        if None in self.yaml_constructors:
            constructor = self.yaml_constructors[None]
            wrapped = _NodeWrapper(generic) if not hasattr(node, '_generic') else node
            return constructor(self, wrapped)

        # Default: convert based on type
        if hasattr(generic, 'is_mapping') and generic.is_mapping():
            return self.construct_mapping(node, deep=deep)
        elif hasattr(generic, 'is_sequence') and generic.is_sequence():
            return self.construct_sequence(node, deep=deep)
        elif hasattr(generic, 'to_python'):
            return generic.to_python()
        elif hasattr(node, 'value'):
            return node.value
        return node

    def construct_pairs(self, node, deep=False):
        """Construct a list of (key, value) pairs from a mapping node."""
        if node is None:
            return []
        generic = node._generic if hasattr(node, '_generic') else node
        if hasattr(generic, 'value'):
            generic = generic.value

        if hasattr(generic, 'is_mapping') and generic.is_mapping():
            if deep:
                return [(self.construct_object(k, deep=True), self.construct_object(v, deep=True))
                        for k, v in generic.items()]
            else:
                return [(k.to_python() if hasattr(k, 'to_python') else k,
                         v.to_python() if hasattr(v, 'to_python') else v)
                        for k, v in generic.items()]
        return []


class SafeLoader(BaseLoader):
    """Safe loader class for PyYAML API compatibility."""
    yaml_constructors = {}
    yaml_multi_constructors = {}


class FullLoader(SafeLoader):
    """Full loader class for PyYAML API compatibility."""
    yaml_constructors = {}
    yaml_multi_constructors = {}


class UnsafeLoader(SafeLoader):
    """Unsafe loader class for PyYAML API compatibility."""
    yaml_constructors = {}
    yaml_multi_constructors = {}


class Loader(SafeLoader):
    """Default loader class (same as SafeLoader in our safe implementation)."""
    yaml_constructors = {}
    yaml_multi_constructors = {}


# C-accelerated versions (same as regular versions in libfyaml)
CSafeLoader = SafeLoader
CFullLoader = FullLoader
CUnsafeLoader = UnsafeLoader
CLoader = Loader
CBaseLoader = BaseLoader


# Dumper classes with add_representer support
class BaseDumper:
    """Base dumper class for PyYAML API compatibility."""
    yaml_representers = {}
    yaml_multi_representers = {}

    @classmethod
    def add_representer(cls, data_type, representer_func):
        """Add a representer for a specific type."""
        cls.yaml_representers[data_type] = representer_func

    @classmethod
    def add_multi_representer(cls, data_type, representer_func):
        """Add a multi-representer for a type and its subclasses."""
        cls.yaml_multi_representers[data_type] = representer_func

    def represent_mapping(self, tag, mapping):
        """Represent a mapping."""
        return mapping

    def represent_sequence(self, tag, sequence):
        """Represent a sequence."""
        return sequence

    def represent_scalar(self, tag, value):
        """Represent a scalar."""
        return value


class SafeDumper(BaseDumper):
    """Safe dumper class for PyYAML API compatibility."""
    yaml_representers = {}
    yaml_multi_representers = {}


class Dumper(SafeDumper):
    """Full dumper class for PyYAML API compatibility."""
    yaml_representers = {}
    yaml_multi_representers = {}


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


class _NodeWrapper:
    """Wrap FyGeneric to look like PyYAML node for constructors.

    This provides compatibility with PyYAML's Node interface, allowing
    constructors written for PyYAML to work with libfyaml's FyGeneric.
    """

    def __init__(self, generic):
        self._generic = generic
        self.tag = generic.get_tag() if hasattr(generic, 'get_tag') else None
        # For scalars, provide the value directly
        if hasattr(generic, 'is_mapping') and not generic.is_mapping() and not generic.is_sequence():
            self.value = generic.to_python()
        else:
            self.value = generic
        # Provide start_mark/end_mark for compatibility (some constructors check these)
        self.start_mark = None
        self.end_mark = None

    def __repr__(self):
        return f"<_NodeWrapper tag={self.tag!r} value={self.value!r}>"


def _convert_with_loader(node, loader):
    """Recursively convert FyGeneric to Python using loader's construct methods.

    Args:
        node: FyGeneric node to convert
        loader: Loader instance with construct_* methods

    Returns:
        Constructed Python object
    """
    # Use the loader's construct_object which handles tags and recursion
    wrapped = _NodeWrapper(node)
    return loader.construct_object(wrapped, deep=True)


class _ConstructorProxy(BaseLoader):
    """Lightweight proxy that provides construct_* methods for any Loader class.

    This allows us to use the constructor methods without instantiating the
    full Loader class (which may have parser dependencies like CParser).
    """

    def __init__(self, loader_class, stream=None):
        super().__init__(stream)
        # Copy constructors from the target class
        self.yaml_constructors = getattr(loader_class, 'yaml_constructors', {})
        self.yaml_multi_constructors = getattr(loader_class, 'yaml_multi_constructors', {})


def _diag_to_exception(diag_list, stream_name='<string>'):
    """Convert libfyaml diagnostic info to PyYAML-style exception.

    Args:
        diag_list: List of diagnostic dicts from libfyaml
        stream_name: Name of the stream for error messages

    Returns:
        YAMLError subclass matching PyYAML's error format
    """
    if not diag_list:
        return YAMLError("Unknown YAML error")

    # Get the first (or most relevant) error
    err = diag_list[0]
    message = err.get('message', 'Unknown error')

    # Create Mark for problem location
    # libfyaml uses 1-indexed line/column, PyYAML Mark uses 0-indexed
    line = err.get('line', 1) - 1
    column = err.get('column', 1) - 1
    content = err.get('content', '')

    # Create a Mark with snippet support
    problem_mark = Mark(
        name=stream_name,
        index=0,  # We don't have exact index
        line=line,
        column=column,
        buffer=content + '\n' if content else None,
        pointer=column if content else None
    )

    # Determine error type from message
    msg_lower = message.lower()
    if 'alias' in msg_lower:
        return ComposerError(problem=message, problem_mark=problem_mark)
    elif 'scanner' in msg_lower or 'unexpected' in msg_lower:
        return ScannerError(problem=message, problem_mark=problem_mark)
    elif 'parser' in msg_lower or 'expected' in msg_lower:
        return ParserError(problem=message, problem_mark=problem_mark)
    else:
        # Default to MarkedYAMLError for unknown error types
        return MarkedYAMLError(problem=message, problem_mark=problem_mark)


def load(stream, Loader=SafeLoader):
    """Parse YAML stream and return Python object.

    Args:
        stream: String or file-like object containing YAML
        Loader: Loader class (used for custom constructors via add_constructor)

    Returns:
        Python object (dict, list, str, int, float, bool, or None)

    Raises:
        YAMLError: If parsing fails (ComposerError, ScannerError, ParserError, etc.)

    Example:
        >>> load("foo: bar", Loader=SafeLoader)
        {'foo': 'bar'}
    """
    # Get stream name for error messages
    stream_name = getattr(stream, 'name', '<string>')
    if hasattr(stream, 'read'):
        stream = stream.read()

    # Handle edge cases that libfyaml doesn't parse directly
    result, handled = _normalize_yaml(stream)
    if handled:
        return result

    # Use yaml1.1 mode for PyYAML compatibility (merge keys, octal, etc.)
    # Use collect_diag=True to get error details for PyYAML-style exceptions
    doc = fy.loads(stream, mode='yaml1.1', collect_diag=True)

    # Check if we got an error (collect_diag returns diag sequence on error)
    if doc.is_sequence():
        # This is a diagnostic sequence, not a document
        diag_list = doc.to_python()
        if diag_list and isinstance(diag_list, list) and len(diag_list) > 0:
            if isinstance(diag_list[0], dict) and 'message' in diag_list[0]:
                raise _diag_to_exception(diag_list, stream_name)

    # Check for registered constructors on the Loader class
    constructors = getattr(Loader, 'yaml_constructors', {})
    multi_constructors = getattr(Loader, 'yaml_multi_constructors', {})

    if constructors or multi_constructors:
        # Create a lightweight proxy with the constructors
        # This avoids instantiating Loaders that inherit from parsers (like AnsibleLoader)
        loader = _ConstructorProxy(Loader, stream)
        return _convert_with_loader(doc, loader)

    return doc.to_python()


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

    # Use yaml1.1 mode for PyYAML compatibility
    return fy.loads(stream, mode='yaml1.1')


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

    # Use yaml1.1 mode for PyYAML compatibility
    for doc in fy.loads_all(stream, mode='yaml1.1'):
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
    # Use yaml1.1 mode for PyYAML compatibility
    for doc in fy.loads_all(stream, mode='yaml1.1'):
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

class YAMLLoadWarning(UserWarning):
    """Warning for unsafe YAML loading."""
    pass


# Reader error and other less common errors (alias to MarkedYAMLError for now)
ReaderError = MarkedYAMLError
ConstructorError = MarkedYAMLError
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
