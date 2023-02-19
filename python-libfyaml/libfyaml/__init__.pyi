"""Type stubs for libfyaml Python bindings.

This module provides Python bindings to libfyaml using its generic type system.
Like NumPy, parsed data stays in native C format for efficiency, with lazy
conversion to Python objects only when needed.
"""

from typing import (
    Any,
    BinaryIO,
    Dict,
    IO,
    Iterable,
    Iterator,
    List,
    Literal,
    Optional,
    TextIO,
    Tuple,
    Union,
    overload,
)

__version__: str

# Type aliases
_PathLike = Union[str, int]
_YAMLValue = Union[None, bool, int, float, str, List[Any], Dict[str, Any]]
_Mode = Literal["yaml", "yaml1.1", "json"]

class FyGeneric:
    """A lazy proxy object representing parsed YAML/JSON data.

    FyGeneric objects behave like Python dicts, lists, or scalars depending
    on their underlying type. Operations are performed lazily - data stays
    in efficient C format until accessed.

    Supports:
    - Dict operations: `data['key']`, `data.get('key')`, `data.items()`
    - List operations: `data[0]`, `len(data)`, iteration
    - String methods: `data['name'].upper()`
    - Arithmetic: `data['count'] + 1`
    - Comparisons: `data['age'] > 18`
    """

    # Type checking methods
    def is_null(self) -> bool: ...
    def is_bool(self) -> bool: ...
    def is_int(self) -> bool: ...
    def is_float(self) -> bool: ...
    def is_string(self) -> bool: ...
    def is_sequence(self) -> bool: ...
    def is_mapping(self) -> bool: ...
    def is_alias(self) -> bool: ...

    # Conversion methods
    def to_python(self) -> _YAMLValue:
        """Convert this FyGeneric to native Python types recursively."""
        ...

    def copy(self) -> _YAMLValue:
        """Alias for to_python() - convert to native Python types."""
        ...

    def clone(self) -> "FyGeneric":
        """Create a deep copy of this FyGeneric object."""
        ...

    # Tag support
    def get_tag(self) -> Optional[str]:
        """Get the YAML tag for this node, or None if untagged."""
        ...

    # Path access
    def get_path(self) -> List[Union[str, int]]:
        """Get the path from root to this node as a list of keys/indices."""
        ...

    def get_unix_path(self) -> str:
        """Get the path from root to this node as a Unix-style path string."""
        ...

    def get_at_path(self, path: List[Union[str, int]]) -> "FyGeneric":
        """Access a nested value using a path list."""
        ...

    def get_at_unix_path(self, path: str) -> "FyGeneric":
        """Access a nested value using a Unix-style path string."""
        ...

    def set_at_path(self, path: List[Union[str, int]], value: Any) -> None:
        """Set a nested value using a path list (requires mutable=True)."""
        ...

    def set_at_unix_path(self, path: str, value: Any) -> None:
        """Set a nested value using a Unix-style path string (requires mutable=True)."""
        ...

    # Memory management
    def trim(self) -> None:
        """Trim the memory arena to release unused memory."""
        ...

    # Dict-like interface
    def __getitem__(self, key: Union[str, int]) -> "FyGeneric": ...
    def __contains__(self, key: Union[str, int]) -> bool: ...
    def __len__(self) -> int: ...
    def __iter__(self) -> Iterator["FyGeneric"]: ...

    @overload
    def get(self, key: str) -> Optional["FyGeneric"]: ...
    @overload
    def get(self, key: str, default: Any) -> Union["FyGeneric", Any]: ...

    def keys(self) -> Iterator["FyGeneric"]:
        """Return an iterator over mapping keys."""
        ...

    def values(self) -> Iterator["FyGeneric"]:
        """Return an iterator over mapping values."""
        ...

    def items(self) -> Iterator[Tuple["FyGeneric", "FyGeneric"]]:
        """Return an iterator over (key, value) pairs."""
        ...

    # Comparison operators
    def __eq__(self, other: Any) -> bool: ...
    def __ne__(self, other: Any) -> bool: ...
    def __lt__(self, other: Any) -> bool: ...
    def __le__(self, other: Any) -> bool: ...
    def __gt__(self, other: Any) -> bool: ...
    def __ge__(self, other: Any) -> bool: ...

    # Arithmetic operators (for numeric types)
    def __add__(self, other: Any) -> Any: ...
    def __radd__(self, other: Any) -> Any: ...
    def __sub__(self, other: Any) -> Any: ...
    def __rsub__(self, other: Any) -> Any: ...
    def __mul__(self, other: Any) -> Any: ...
    def __rmul__(self, other: Any) -> Any: ...
    def __truediv__(self, other: Any) -> float: ...
    def __rtruediv__(self, other: Any) -> float: ...
    def __floordiv__(self, other: Any) -> int: ...
    def __rfloordiv__(self, other: Any) -> int: ...
    def __mod__(self, other: Any) -> Any: ...
    def __rmod__(self, other: Any) -> Any: ...
    def __neg__(self) -> Any: ...
    def __pos__(self) -> Any: ...
    def __abs__(self) -> Any: ...

    # Bitwise operators (for integer types)
    def __and__(self, other: Any) -> int: ...
    def __rand__(self, other: Any) -> int: ...
    def __or__(self, other: Any) -> int: ...
    def __ror__(self, other: Any) -> int: ...
    def __xor__(self, other: Any) -> int: ...
    def __rxor__(self, other: Any) -> int: ...
    def __invert__(self) -> int: ...
    def __lshift__(self, other: Any) -> int: ...
    def __rlshift__(self, other: Any) -> int: ...
    def __rshift__(self, other: Any) -> int: ...
    def __rrshift__(self, other: Any) -> int: ...

    # Type coercion
    def __bool__(self) -> bool: ...
    def __int__(self) -> int: ...
    def __float__(self) -> float: ...
    def __str__(self) -> str: ...
    def __repr__(self) -> str: ...
    def __hash__(self) -> int: ...
    def __format__(self, format_spec: str) -> str: ...

    # String methods (delegated when underlying type is string)
    def upper(self) -> str: ...
    def lower(self) -> str: ...
    def strip(self, chars: Optional[str] = None) -> str: ...
    def lstrip(self, chars: Optional[str] = None) -> str: ...
    def rstrip(self, chars: Optional[str] = None) -> str: ...
    def split(self, sep: Optional[str] = None, maxsplit: int = -1) -> List[str]: ...
    def rsplit(self, sep: Optional[str] = None, maxsplit: int = -1) -> List[str]: ...
    def join(self, iterable: Iterable[str]) -> str: ...
    def replace(self, old: str, new: str, count: int = -1) -> str: ...
    def startswith(self, prefix: Union[str, Tuple[str, ...]], start: int = 0, end: Optional[int] = None) -> bool: ...
    def endswith(self, suffix: Union[str, Tuple[str, ...]], start: int = 0, end: Optional[int] = None) -> bool: ...
    def find(self, sub: str, start: int = 0, end: Optional[int] = None) -> int: ...
    def rfind(self, sub: str, start: int = 0, end: Optional[int] = None) -> int: ...
    def index(self, sub: str, start: int = 0, end: Optional[int] = None) -> int: ...
    def rindex(self, sub: str, start: int = 0, end: Optional[int] = None) -> int: ...
    def count(self, sub: str, start: int = 0, end: Optional[int] = None) -> int: ...
    def encode(self, encoding: str = "utf-8", errors: str = "strict") -> bytes: ...
    def isalpha(self) -> bool: ...
    def isalnum(self) -> bool: ...
    def isdigit(self) -> bool: ...
    def isnumeric(self) -> bool: ...
    def isspace(self) -> bool: ...
    def islower(self) -> bool: ...
    def isupper(self) -> bool: ...
    def capitalize(self) -> str: ...
    def title(self) -> str: ...
    def swapcase(self) -> str: ...
    def center(self, width: int, fillchar: str = " ") -> str: ...
    def ljust(self, width: int, fillchar: str = " ") -> str: ...
    def rjust(self, width: int, fillchar: str = " ") -> str: ...
    def zfill(self, width: int) -> str: ...


class FyDocumentState:
    """Represents the state of a parsed YAML document.

    This class is used internally to track document state for multi-document
    parsing and memory management.
    """
    ...


# Core parsing functions
@overload
def loads(
    s: str,
    *,
    mode: _Mode = "yaml",
    dedup: bool = True,
    trim: bool = True,
    mutable: bool = False,
    collect_diag: bool = False,
) -> FyGeneric: ...

@overload
def loads(
    s: bytes,
    *,
    mode: _Mode = "yaml",
    dedup: bool = True,
    trim: bool = True,
    mutable: bool = False,
    collect_diag: bool = False,
) -> FyGeneric: ...

def load(
    fp: Union[TextIO, BinaryIO, str],
    *,
    mode: _Mode = "yaml",
    dedup: bool = True,
    trim: bool = True,
    mutable: bool = False,
) -> FyGeneric:
    """Parse YAML/JSON from a file.

    Args:
        fp: File path or file-like object to read from
        mode: Parse mode - 'yaml', 'yaml1.1', or 'json'
        dedup: Enable string deduplication (default: True)
        trim: Auto-trim arena after parsing (default: True)
        mutable: Enable mutation support (default: False)

    Returns:
        FyGeneric object representing the parsed document
    """
    ...

def loads_all(
    s: Union[str, bytes],
    *,
    mode: _Mode = "yaml",
    dedup: bool = True,
    trim: bool = True,
    mutable: bool = False,
) -> Iterator[FyGeneric]:
    """Parse multiple YAML documents from a string.

    Args:
        s: String containing one or more YAML documents
        mode: Parse mode - 'yaml', 'yaml1.1', or 'json'
        dedup: Enable string deduplication (default: True)
        trim: Auto-trim arena after parsing (default: True)
        mutable: Enable mutation support (default: False)

    Yields:
        FyGeneric objects, one per document
    """
    ...

def load_all(
    fp: Union[TextIO, BinaryIO, str],
    *,
    mode: _Mode = "yaml",
    dedup: bool = True,
    trim: bool = True,
    mutable: bool = False,
) -> Iterator[FyGeneric]:
    """Parse multiple YAML documents from a file.

    Args:
        fp: File path or file-like object to read from
        mode: Parse mode - 'yaml', 'yaml1.1', or 'json'
        dedup: Enable string deduplication (default: True)
        trim: Auto-trim arena after parsing (default: True)
        mutable: Enable mutation support (default: False)

    Yields:
        FyGeneric objects, one per document
    """
    ...


# Core dumping functions
def dumps(
    obj: Union[FyGeneric, _YAMLValue],
    *,
    mode: _Mode = "yaml",
) -> str:
    """Serialize object to YAML/JSON string.

    Args:
        obj: FyGeneric or Python object to serialize
        mode: Output mode - 'yaml' or 'json'

    Returns:
        YAML/JSON formatted string
    """
    ...

def dump(
    obj: Union[FyGeneric, _YAMLValue],
    fp: Union[TextIO, str],
    *,
    mode: _Mode = "yaml",
) -> None:
    """Serialize object to YAML/JSON and write to file.

    Args:
        obj: FyGeneric or Python object to serialize
        fp: File path or file-like object to write to
        mode: Output mode - 'yaml' or 'json'
    """
    ...

def dumps_all(
    docs: Iterable[Union[FyGeneric, _YAMLValue]],
    *,
    mode: _Mode = "yaml",
) -> str:
    """Serialize multiple documents to YAML string.

    Args:
        docs: Iterable of FyGeneric or Python objects
        mode: Output mode - 'yaml' or 'json'

    Returns:
        YAML formatted string with document separators
    """
    ...

def dump_all(
    docs: Iterable[Union[FyGeneric, _YAMLValue]],
    fp: Union[TextIO, str],
    *,
    mode: _Mode = "yaml",
) -> None:
    """Serialize multiple documents to YAML and write to file.

    Args:
        docs: Iterable of FyGeneric or Python objects
        fp: File path or file-like object to write to
        mode: Output mode - 'yaml' or 'json'
    """
    ...


# Conversion functions
def from_python(
    obj: _YAMLValue,
    *,
    tag: Optional[str] = None,
) -> FyGeneric:
    """Convert a Python object to FyGeneric.

    Args:
        obj: Python object (dict, list, str, int, float, bool, None)
        tag: Optional YAML tag to attach to the value

    Returns:
        FyGeneric object representing the data
    """
    ...


# Path utilities
def path_list_to_unix_path(path: List[Union[str, int]]) -> str:
    """Convert a path list to a Unix-style path string.

    Args:
        path: List of keys (str) and indices (int)

    Returns:
        Unix-style path string (e.g., "/server/config/0/name")

    Example:
        >>> path_list_to_unix_path(['server', 'config', 0, 'name'])
        '/server/config/0/name'
    """
    ...

def unix_path_to_path_list(path: str) -> List[Union[str, int]]:
    """Convert a Unix-style path string to a path list.

    Args:
        path: Unix-style path string

    Returns:
        List of keys (str) and indices (int)

    Example:
        >>> unix_path_to_path_list('/server/config/0/name')
        ['server', 'config', 0, 'name']
    """
    ...


# JSON serialization helpers
def json_dumps(obj: Union[FyGeneric, _YAMLValue], **kwargs: Any) -> str:
    """Serialize to JSON string with FyGeneric support.

    Automatically converts FyGeneric objects to Python equivalents before
    serialization, allowing seamless JSON encoding of YAML-parsed data.

    Args:
        obj: Object to serialize (can contain FyGeneric objects)
        **kwargs: Additional arguments passed to json.dumps()

    Returns:
        JSON formatted string
    """
    ...

def json_dump(obj: Union[FyGeneric, _YAMLValue], fp: IO[str], **kwargs: Any) -> None:
    """Serialize to JSON and write to file with FyGeneric support.

    Args:
        obj: Object to serialize (can contain FyGeneric objects)
        fp: File-like object to write to
        **kwargs: Additional arguments passed to json.dump()
    """
    ...


# Exported names
__all__: List[str]
