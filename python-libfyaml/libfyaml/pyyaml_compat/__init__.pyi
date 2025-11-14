"""Type stubs for libfyaml PyYAML compatibility layer.

This module provides a drop-in replacement for PyYAML's most common functions,
allowing projects to use libfyaml without code changes.

Usage:
    # Instead of:
    import yaml

    # Use:
    from libfyaml import pyyaml_compat as yaml

    # Then use normally:
    data = yaml.safe_load("foo: bar")
    output = yaml.safe_dump({"foo": "bar"})
"""

from typing import (
    Any,
    BinaryIO,
    Callable,
    Dict,
    IO,
    Iterable,
    Iterator,
    List,
    Optional,
    TextIO,
    Tuple,
    Type,
    TypeVar,
    Union,
)
from datetime import date, datetime

# Version info
__version__: str
__with_libyaml__: bool

# Type variables
_T = TypeVar("_T")


# Error classes
class YAMLError(Exception):
    """Base exception for YAML errors."""
    ...


class Mark:
    """Position in a YAML stream."""
    name: Optional[str]
    index: int
    line: int
    column: int
    buffer: Optional[str]
    pointer: Optional[int]

    def __init__(
        self,
        name: Optional[str] = None,
        index: int = 0,
        line: int = 0,
        column: int = 0,
        buffer: Optional[str] = None,
        pointer: Optional[int] = None,
    ) -> None: ...
    def get_snippet(self, indent: int = 4, max_length: int = 75) -> Optional[str]: ...


class MarkedYAMLError(YAMLError):
    """YAML error with position information."""
    context: Optional[str]
    context_mark: Optional[Mark]
    problem: Optional[str]
    problem_mark: Optional[Mark]
    note: Optional[str]

    def __init__(
        self,
        context: Optional[str] = None,
        context_mark: Optional[Mark] = None,
        problem: Optional[str] = None,
        problem_mark: Optional[Mark] = None,
        note: Optional[str] = None,
    ) -> None: ...


class ScannerError(MarkedYAMLError):
    """Error during YAML scanning (tokenization)."""
    ...


class ParserError(MarkedYAMLError):
    """Error during YAML parsing."""
    ...


class ComposerError(MarkedYAMLError):
    """Error during YAML composition."""
    ...


class ReaderError(MarkedYAMLError):
    """Error during YAML reading."""
    ...


class ConstructorError(MarkedYAMLError):
    """Error during YAML construction."""
    ...


class EmitterError(YAMLError):
    """Error during YAML emission."""
    ...


class RepresenterError(YAMLError):
    """Error during YAML representation."""
    ...


class YAMLLoadWarning(UserWarning):
    """Warning for unsafe YAML loading."""
    ...


# Node classes
class Node:
    """Base class for YAML nodes."""
    tag: Optional[str]
    value: Any
    start_mark: Optional[Mark]
    end_mark: Optional[Mark]

    def __init__(
        self,
        tag: Optional[str] = None,
        value: Any = None,
        start_mark: Optional[Mark] = None,
        end_mark: Optional[Mark] = None,
    ) -> None: ...


class ScalarNode(Node):
    """Node representing a YAML scalar."""
    id: str
    style: Optional[str]

    def __init__(
        self,
        tag: Optional[str] = None,
        value: Any = None,
        start_mark: Optional[Mark] = None,
        end_mark: Optional[Mark] = None,
        style: Optional[str] = None,
    ) -> None: ...


class CollectionNode(Node):
    """Base class for collection nodes."""
    flow_style: Optional[bool]

    def __init__(
        self,
        tag: Optional[str] = None,
        value: Any = None,
        start_mark: Optional[Mark] = None,
        end_mark: Optional[Mark] = None,
        flow_style: Optional[bool] = None,
    ) -> None: ...


class SequenceNode(CollectionNode):
    """Node representing a YAML sequence."""
    id: str
    value: List[Node]


class MappingNode(CollectionNode):
    """Node representing a YAML mapping."""
    id: str
    value: List[Tuple[Node, Node]]


# Loader classes
class BaseLoader:
    """Base loader class for PyYAML API compatibility."""
    yaml_constructors: Dict[Optional[str], Callable[..., Any]]
    yaml_multi_constructors: Dict[Optional[str], Callable[..., Any]]

    def __init__(self, stream: Optional[Union[str, bytes, IO[Any]]] = None) -> None: ...

    @classmethod
    def add_constructor(
        cls,
        tag: Optional[str],
        constructor: Callable[["BaseLoader", Node], Any],
    ) -> None: ...

    @classmethod
    def add_multi_constructor(
        cls,
        tag_prefix: Optional[str],
        multi_constructor: Callable[["BaseLoader", str, Node], Any],
    ) -> None: ...

    def construct_scalar(self, node: Node) -> Any: ...
    def construct_mapping(self, node: Node, deep: bool = False) -> Dict[Any, Any]: ...
    def construct_sequence(self, node: Node, deep: bool = False) -> List[Any]: ...
    def construct_object(self, node: Node, deep: bool = False) -> Any: ...
    def construct_pairs(self, node: Node, deep: bool = False) -> List[Tuple[Any, Any]]: ...
    def construct_yaml_object(self, node: Node, cls: Type[_T]) -> _T: ...


class SafeLoader(BaseLoader):
    """Safe loader with standard tag constructors."""
    ...


class FullLoader(SafeLoader):
    """Full loader class for PyYAML API compatibility."""
    ...


class UnsafeLoader(SafeLoader):
    """Unsafe loader class for PyYAML API compatibility."""
    ...


class Loader(SafeLoader):
    """Default loader class."""
    ...


# C-accelerated loader aliases
CSafeLoader = SafeLoader
CFullLoader = FullLoader
CUnsafeLoader = UnsafeLoader
CLoader = Loader
CBaseLoader = BaseLoader


# Dumper classes
class BaseDumper:
    """Base dumper class for PyYAML API compatibility."""
    yaml_representers: Dict[Type[Any], Callable[..., Any]]
    yaml_multi_representers: Dict[Type[Any], Callable[..., Any]]

    def __init__(
        self,
        stream: Optional[IO[Any]] = None,
        default_style: Optional[str] = None,
        default_flow_style: bool = False,
        canonical: Optional[bool] = None,
        indent: Optional[int] = None,
        width: Optional[int] = None,
        allow_unicode: Optional[bool] = None,
        line_break: Optional[str] = None,
        encoding: Optional[str] = None,
        explicit_start: Optional[bool] = None,
        explicit_end: Optional[bool] = None,
        version: Optional[Tuple[int, int]] = None,
        tags: Optional[Dict[str, str]] = None,
        sort_keys: bool = True,
    ) -> None: ...

    @classmethod
    def add_representer(
        cls,
        data_type: Type[Any],
        representer: Callable[["BaseDumper", Any], Any],
    ) -> None: ...

    @classmethod
    def add_multi_representer(
        cls,
        data_type: Type[Any],
        representer: Callable[["BaseDumper", Any], Any],
    ) -> None: ...

    def represent_data(self, data: Any) -> Any: ...
    def represent_mapping(self, tag: str, mapping: Iterable[Tuple[Any, Any]]) -> MappingNode: ...
    def represent_sequence(self, tag: str, sequence: Iterable[Any]) -> SequenceNode: ...
    def represent_scalar(self, tag: str, value: Any, style: Optional[str] = None) -> ScalarNode: ...
    def represent_yaml_object(
        self,
        tag: str,
        data: Any,
        cls: Type[Any],
        flow_style: Optional[bool] = None,
    ) -> MappingNode: ...


class SafeDumper(BaseDumper):
    """Safe dumper with standard type representers."""
    ...


class Dumper(SafeDumper):
    """Full dumper class for PyYAML API compatibility."""
    ...


# C-accelerated dumper aliases
CSafeDumper = SafeDumper
CDumper = Dumper


# YAMLObject metaclass and base class
class YAMLObjectMetaclass(type):
    """Metaclass for YAMLObject that auto-registers constructors/representers."""
    ...


class YAMLObject(metaclass=YAMLObjectMetaclass):
    """Base class for YAML-serializable objects."""
    yaml_tag: Optional[str]
    yaml_loader: Optional[Union[Type[BaseLoader], List[Type[BaseLoader]]]]
    yaml_dumper: Optional[Union[Type[BaseDumper], List[Type[BaseDumper]]]]

    @classmethod
    def from_yaml(cls: Type[_T], loader: BaseLoader, node: Node) -> _T: ...

    @classmethod
    def to_yaml(cls, dumper: BaseDumper, data: Any) -> Node: ...


# Module-level functions for adding constructors/representers
def add_constructor(
    tag: Optional[str],
    constructor: Callable[[BaseLoader, Node], Any],
    Loader: Optional[Type[BaseLoader]] = None,
) -> None:
    """Add a constructor for the given tag."""
    ...


def add_multi_constructor(
    tag_prefix: Optional[str],
    multi_constructor: Callable[[BaseLoader, str, Node], Any],
    Loader: Optional[Type[BaseLoader]] = None,
) -> None:
    """Add a multi-constructor for the given tag prefix."""
    ...


def add_representer(
    data_type: Type[Any],
    representer: Callable[[BaseDumper, Any], Any],
    Dumper: Optional[Type[BaseDumper]] = None,
) -> None:
    """Add a representer for the given data type."""
    ...


def add_multi_representer(
    data_type: Type[Any],
    multi_representer: Callable[[BaseDumper, Any], Any],
    Dumper: Optional[Type[BaseDumper]] = None,
) -> None:
    """Add a multi-representer for the given data type."""
    ...


def add_implicit_resolver(
    tag: str,
    regexp: Any,
    first: Optional[List[str]] = None,
    Loader: Optional[Type[BaseLoader]] = None,
    Dumper: Optional[Type[BaseDumper]] = None,
) -> None:
    """Add an implicit resolver for the given tag. (Stub - not implemented)"""
    ...


def add_path_resolver(
    tag: str,
    path: List[Any],
    kind: Optional[Type[Any]] = None,
    Loader: Optional[Type[BaseLoader]] = None,
    Dumper: Optional[Type[BaseDumper]] = None,
) -> None:
    """Add a path resolver for the given tag. (Stub - not implemented)"""
    ...


# Loading functions
def load(
    stream: Union[str, bytes, IO[Any]],
    Loader: Optional[Type[BaseLoader]] = None,
) -> Any:
    """Parse YAML stream and return Python object.

    Args:
        stream: String or file-like object containing YAML
        Loader: Loader class (required - use SafeLoader, FullLoader, or UnsafeLoader)

    Returns:
        Python object (dict, list, str, int, float, bool, or None)

    Raises:
        TypeError: If Loader is not specified
        YAMLError: If parsing fails
    """
    ...


def safe_load(stream: Union[str, bytes, IO[Any]]) -> Any:
    """Parse YAML stream safely and return Python object.

    Equivalent to load(stream, Loader=SafeLoader).
    """
    ...


def full_load(stream: Union[str, bytes, IO[Any]]) -> Any:
    """Parse YAML stream with FullLoader and return Python object.

    Equivalent to load(stream, Loader=FullLoader).
    """
    ...


def unsafe_load(stream: Union[str, bytes, IO[Any]]) -> Any:
    """Parse YAML stream unsafely and return Python object.

    Equivalent to load(stream, Loader=UnsafeLoader).
    """
    ...


def load_all(
    stream: Union[str, bytes, IO[Any]],
    Loader: Optional[Type[BaseLoader]] = None,
) -> Iterator[Any]:
    """Parse all YAML documents in stream.

    Args:
        stream: String or file-like object containing YAML
        Loader: Loader class (required - use SafeLoader, FullLoader, or UnsafeLoader)

    Yields:
        Python objects, one per YAML document

    Raises:
        TypeError: If Loader is not specified
    """
    ...


def safe_load_all(stream: Union[str, bytes, IO[Any]]) -> Iterator[Any]:
    """Parse all YAML documents in stream safely.

    Equivalent to load_all(stream, Loader=SafeLoader).
    """
    ...


def full_load_all(stream: Union[str, bytes, IO[Any]]) -> Iterator[Any]:
    """Parse all YAML documents with FullLoader.

    Equivalent to load_all(stream, Loader=FullLoader).
    """
    ...


def unsafe_load_all(stream: Union[str, bytes, IO[Any]]) -> Iterator[Any]:
    """Parse all YAML documents unsafely.

    Equivalent to load_all(stream, Loader=UnsafeLoader).
    """
    ...


# Compose functions (return nodes instead of Python objects)
def compose(
    stream: Union[str, bytes, IO[Any]],
    Loader: Type[BaseLoader] = ...,
) -> Optional[Node]:
    """Parse YAML stream and return node tree.

    Unlike load(), this returns the node directly without
    converting to Python objects.
    """
    ...


def compose_all(
    stream: Union[str, bytes, IO[Any]],
    Loader: Type[BaseLoader] = ...,
) -> Iterator[Node]:
    """Parse all YAML documents and return node trees."""
    ...


# Dumping functions
def dump(
    data: Any,
    stream: Optional[IO[str]] = None,
    Dumper: Type[BaseDumper] = ...,
    *,
    default_style: Optional[str] = None,
    default_flow_style: bool = False,
    canonical: Optional[bool] = None,
    indent: Optional[int] = None,
    width: Optional[int] = None,
    allow_unicode: bool = True,
    line_break: Optional[str] = None,
    encoding: Optional[str] = None,
    explicit_start: Optional[bool] = None,
    explicit_end: Optional[bool] = None,
    version: Optional[Tuple[int, int]] = None,
    tags: Optional[Dict[str, str]] = None,
    sort_keys: bool = True,
) -> Optional[str]:
    """Serialize Python object to YAML string.

    Args:
        data: Python object to serialize
        stream: File-like object to write to (optional)
        Dumper: Dumper class (default: SafeDumper)
        sort_keys: Sort dictionary keys (default: True)
        Other PyYAML parameters are accepted but may be ignored.

    Returns:
        YAML string if stream is None, otherwise None
    """
    ...


def safe_dump(
    data: Any,
    stream: Optional[IO[str]] = None,
    **kwargs: Any,
) -> Optional[str]:
    """Serialize Python object to YAML string safely.

    Equivalent to dump(data, stream, Dumper=SafeDumper, **kwargs).
    """
    ...


def dump_all(
    documents: Iterable[Any],
    stream: Optional[IO[str]] = None,
    Dumper: Type[BaseDumper] = ...,
    *,
    default_style: Optional[str] = None,
    default_flow_style: bool = False,
    canonical: Optional[bool] = None,
    indent: Optional[int] = None,
    width: Optional[int] = None,
    allow_unicode: bool = True,
    line_break: Optional[str] = None,
    encoding: Optional[str] = None,
    explicit_start: Optional[bool] = None,
    explicit_end: Optional[bool] = None,
    version: Optional[Tuple[int, int]] = None,
    tags: Optional[Dict[str, str]] = None,
    sort_keys: bool = True,
) -> Optional[str]:
    """Serialize sequence of Python objects to YAML string.

    Args:
        documents: Iterable of Python objects to serialize
        stream: File-like object to write to (optional)
        Other arguments same as dump()

    Returns:
        YAML string if stream is None, otherwise None
    """
    ...


def safe_dump_all(
    documents: Iterable[Any],
    stream: Optional[IO[str]] = None,
    **kwargs: Any,
) -> Optional[str]:
    """Serialize sequence of Python objects to YAML string safely.

    Equivalent to dump_all(documents, stream, Dumper=SafeDumper, **kwargs).
    """
    ...


# Streaming API stubs (not implemented)
def scan(stream: Union[str, bytes, IO[Any]], Loader: Type[BaseLoader] = ...) -> Iterator[Any]:
    """Scan YAML stream and yield tokens. (Not implemented)"""
    ...


def parse(stream: Union[str, bytes, IO[Any]], Loader: Type[BaseLoader] = ...) -> Iterator[Any]:
    """Parse YAML stream and yield events. (Not implemented)"""
    ...


def emit(
    events: Iterable[Any],
    stream: Optional[IO[str]] = None,
    Dumper: Type[BaseDumper] = ...,
    canonical: Optional[bool] = None,
    indent: Optional[int] = None,
    width: Optional[int] = None,
    allow_unicode: Optional[bool] = None,
    line_break: Optional[str] = None,
) -> Optional[str]:
    """Emit YAML events to a stream. (Not implemented)"""
    ...


def serialize(
    node: Node,
    stream: Optional[IO[str]] = None,
    Dumper: Type[BaseDumper] = ...,
    **kwargs: Any,
) -> Optional[str]:
    """Serialize a YAML node to a stream. (Not implemented)"""
    ...


def serialize_all(
    nodes: Iterable[Node],
    stream: Optional[IO[str]] = None,
    Dumper: Type[BaseDumper] = ...,
    **kwargs: Any,
) -> Optional[str]:
    """Serialize YAML nodes to a stream. (Not implemented)"""
    ...
