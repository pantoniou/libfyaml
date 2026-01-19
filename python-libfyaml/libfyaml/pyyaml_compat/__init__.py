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
import base64
import datetime
import re
from collections import OrderedDict

# Version info for compatibility
__version__ = '6.0.1'  # Mimic PyYAML version for compatibility checks
__with_libyaml__ = True  # We're using libfyaml as backend


# ============================================================================
# Standard YAML Tag Constructors and Representers
# ============================================================================

# ISO 8601 date/time patterns for YAML 1.1 compatibility
_TIMESTAMP_REGEXP = re.compile(
    r'^(?P<year>[0-9][0-9][0-9][0-9])'
    r'-(?P<month>[0-9][0-9]?)'
    r'-(?P<day>[0-9][0-9]?)'
    r'(?:(?:[Tt]|[ \t]+)'
    r'(?P<hour>[0-9][0-9]?)'
    r':(?P<minute>[0-9][0-9])'
    r':(?P<second>[0-9][0-9])'
    r'(?:\.(?P<fraction>[0-9]*))?'
    r'(?:[ \t]*(?P<tz>Z|(?P<tz_sign>[-+])(?P<tz_hour>[0-9][0-9]?)'
    r'(?::(?P<tz_minute>[0-9][0-9]))?))?)?$')

_DATE_REGEXP = re.compile(
    r'^(?P<year>[0-9][0-9][0-9][0-9])'
    r'-(?P<month>[0-9][0-9]?)'
    r'-(?P<day>[0-9][0-9]?)$')


def _try_implicit_timestamp(value):
    """Try to parse a string as a YAML timestamp (for implicit resolution).

    This is called for untagged scalar strings to check if they match
    the ISO 8601 date/time patterns that PyYAML implicitly resolves.

    Args:
        value: String value to check

    Returns:
        datetime.date or datetime.datetime if pattern matches, None otherwise
    """
    if not isinstance(value, str):
        return None

    # Try date-only first (simpler pattern)
    match = _DATE_REGEXP.match(value)
    if match:
        values = match.groupdict()
        try:
            year = int(values['year'])
            month = int(values['month'])
            day = int(values['day'])
            return datetime.date(year, month, day)
        except (ValueError, TypeError):
            return None

    # Try full timestamp
    match = _TIMESTAMP_REGEXP.match(value)
    if match:
        values = match.groupdict()
        try:
            year = int(values['year'])
            month = int(values['month'])
            day = int(values['day'])
            if values['hour'] is None:
                return datetime.date(year, month, day)
            hour = int(values['hour'])
            minute = int(values['minute'])
            second = int(values['second'])
            fraction = 0
            if values['fraction']:
                fraction_s = values['fraction'][:6].ljust(6, '0')
                fraction = int(fraction_s)
            tz = None
            if values['tz']:
                if values['tz'] == 'Z':
                    tz = datetime.timezone.utc
                else:
                    tz_sign = -1 if values['tz_sign'] == '-' else 1
                    tz_hour = int(values['tz_hour'])
                    tz_minute = int(values['tz_minute']) if values['tz_minute'] else 0
                    tz = datetime.timezone(datetime.timedelta(
                        hours=tz_sign * tz_hour,
                        minutes=tz_sign * tz_minute))
            return datetime.datetime(year, month, day, hour, minute, second, fraction, tz)
        except (ValueError, TypeError):
            return None

    return None


def _construct_yaml_timestamp(loader, node):
    """Construct a datetime from a YAML timestamp.

    Supports both full timestamps (datetime) and date-only values (date).
    """
    value = loader.construct_scalar(node)
    if value is None:
        return None

    result = _try_implicit_timestamp(value)
    if result is not None:
        return result

    # Return as string if no pattern matched
    return value


def _construct_yaml_binary(loader, node):
    """Construct bytes from a base64-encoded YAML binary value."""
    value = loader.construct_scalar(node)
    if value is None:
        return b''
    if isinstance(value, bytes):
        return value
    # Remove whitespace (YAML allows line breaks in binary data)
    value = value.replace('\n', '').replace('\r', '').replace(' ', '')
    try:
        return base64.b64decode(value)
    except Exception:
        return value.encode('utf-8')


def _construct_yaml_omap(loader, node):
    """Construct an OrderedDict from a YAML !!omap sequence.

    !!omap is a sequence of single-key mappings.
    """
    if node is None:
        return OrderedDict()

    generic = node._generic if hasattr(node, '_generic') else node
    if hasattr(generic, 'value'):
        generic = generic.value

    omap = OrderedDict()
    if hasattr(generic, 'is_sequence') and generic.is_sequence():
        for item in generic:
            if hasattr(item, 'is_mapping') and item.is_mapping():
                for k, v in item.items():
                    key = k.to_python() if hasattr(k, 'to_python') else k
                    value = v.to_python() if hasattr(v, 'to_python') else v
                    omap[key] = value
    elif isinstance(generic, list):
        for item in generic:
            if isinstance(item, dict):
                for k, v in item.items():
                    omap[k] = v
    return omap


def _construct_yaml_set(loader, node):
    """Construct a set from a YAML !!set mapping.

    !!set is a mapping where only keys matter (values are null).
    """
    if node is None:
        return set()

    generic = node._generic if hasattr(node, '_generic') else node
    if hasattr(generic, 'value'):
        generic = generic.value

    result = set()
    if hasattr(generic, 'is_mapping') and generic.is_mapping():
        for k, v in generic.items():
            key = k.to_python() if hasattr(k, 'to_python') else k
            result.add(key)
    elif isinstance(generic, dict):
        result = set(generic.keys())
    return result


def _construct_yaml_pairs(loader, node):
    """Construct a list of (key, value) pairs from a YAML !!pairs sequence.

    Similar to !!omap but allows duplicate keys.
    """
    if node is None:
        return []

    generic = node._generic if hasattr(node, '_generic') else node
    if hasattr(generic, 'value'):
        generic = generic.value

    pairs = []
    if hasattr(generic, 'is_sequence') and generic.is_sequence():
        for item in generic:
            if hasattr(item, 'is_mapping') and item.is_mapping():
                for k, v in item.items():
                    key = k.to_python() if hasattr(k, 'to_python') else k
                    value = v.to_python() if hasattr(v, 'to_python') else v
                    pairs.append((key, value))
    elif isinstance(generic, list):
        for item in generic:
            if isinstance(item, dict):
                for k, v in item.items():
                    pairs.append((k, v))
    return pairs


# Representer functions for dump()
def _represent_datetime(dumper, data):
    """Represent a datetime object as ISO format string."""
    if data.tzinfo is not None:
        value = data.isoformat()
    else:
        value = data.strftime('%Y-%m-%d %H:%M:%S')
        if data.microsecond:
            value += '.%06d' % data.microsecond
    return value


def _represent_date(dumper, data):
    """Represent a date object as ISO format string."""
    return data.isoformat()


def _represent_bytes(dumper, data):
    """Represent bytes as base64-encoded binary."""
    encoded = base64.standard_b64encode(data).decode('ascii')
    # Use plain style because libfyaml doesn't handle literal style with tags properly
    return ScalarNode('tag:yaml.org,2002:binary', encoded, style=None)


def _represent_set(dumper, data):
    """Represent a set as a mapping with null values."""
    return {item: None for item in data}


def _represent_ordereddict(dumper, data):
    """Represent an OrderedDict as a regular mapping."""
    return dict(data)

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
            value = generic.to_python()
            # Try implicit timestamp resolution for untagged string scalars
            if not tag and isinstance(value, str):
                timestamp = _try_implicit_timestamp(value)
                if timestamp is not None:
                    return timestamp
            return value
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
    """Safe loader class for PyYAML API compatibility.

    Includes default constructors for standard YAML tags:
    - !!binary (base64-encoded bytes)
    - !!timestamp (ISO 8601 dates and times)
    - !!set (set type)
    - !!omap (ordered mapping)
    - !!pairs (list of pairs)
    """
    yaml_constructors = {
        # Standard YAML 1.1 tags
        'tag:yaml.org,2002:binary': _construct_yaml_binary,
        'tag:yaml.org,2002:timestamp': _construct_yaml_timestamp,
        'tag:yaml.org,2002:set': _construct_yaml_set,
        'tag:yaml.org,2002:omap': _construct_yaml_omap,
        'tag:yaml.org,2002:pairs': _construct_yaml_pairs,
        # Short forms
        '!!binary': _construct_yaml_binary,
        '!!timestamp': _construct_yaml_timestamp,
        '!!set': _construct_yaml_set,
        '!!omap': _construct_yaml_omap,
        '!!pairs': _construct_yaml_pairs,
    }
    yaml_multi_constructors = {}


class FullLoader(SafeLoader):
    """Full loader class for PyYAML API compatibility."""
    yaml_constructors = SafeLoader.yaml_constructors.copy()
    yaml_multi_constructors = {}


class UnsafeLoader(SafeLoader):
    """Unsafe loader class for PyYAML API compatibility."""
    yaml_constructors = SafeLoader.yaml_constructors.copy()
    yaml_multi_constructors = {}


class Loader(SafeLoader):
    """Default loader class (same as SafeLoader in our safe implementation)."""
    yaml_constructors = SafeLoader.yaml_constructors.copy()
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

    def __init__(self, stream=None, default_style=None, default_flow_style=False,
                 canonical=None, indent=None, width=None, allow_unicode=None,
                 line_break=None, encoding=None, explicit_start=None,
                 explicit_end=None, version=None, tags=None, sort_keys=True):
        """Initialize dumper with optional parameters."""
        self._stream = stream
        self._default_style = default_style
        self._default_flow_style = default_flow_style
        self._canonical = canonical
        self._indent = indent
        self._width = width
        self._allow_unicode = allow_unicode
        self._line_break = line_break
        self._encoding = encoding
        self._explicit_start = explicit_start
        self._explicit_end = explicit_end
        self._version = version
        self._tags = tags
        self._sort_keys = sort_keys

    @classmethod
    def add_representer(cls, data_type, representer_func):
        """Add a representer for a specific type."""
        # Create a copy of the dict for this class if it's inherited
        if 'yaml_representers' not in cls.__dict__:
            cls.yaml_representers = cls.yaml_representers.copy()
        cls.yaml_representers[data_type] = representer_func

    @classmethod
    def add_multi_representer(cls, data_type, representer_func):
        """Add a multi-representer for a type and its subclasses."""
        # Create a copy of the dict for this class if it's inherited
        if 'yaml_multi_representers' not in cls.__dict__:
            cls.yaml_multi_representers = cls.yaml_multi_representers.copy()
        cls.yaml_multi_representers[data_type] = representer_func

    def represent_data(self, data):
        """Represent Python data using registered representers.

        This is the main entry point for representation. It checks for
        registered representers and dispatches appropriately.

        Args:
            data: Python object to represent

        Returns:
            Represented data (potentially modified by representer)
        """
        data_type = type(data)

        # Check for exact type match
        if data_type in self.yaml_representers:
            representer = self.yaml_representers[data_type]
            return representer(self, data)

        # Check for multi-representers (type hierarchy)
        for base_type, representer in self.yaml_multi_representers.items():
            if base_type is not None and isinstance(data, base_type):
                return representer(self, data)

        # Check for None type representer (catch-all)
        if None in self.yaml_representers:
            representer = self.yaml_representers[None]
            return representer(self, data)

        # Return data unchanged
        return data

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
    """Safe dumper class for PyYAML API compatibility.

    Includes default representers for standard Python types:
    - datetime.datetime
    - datetime.date
    - bytes (as base64)
    - set (as mapping with null values)
    - OrderedDict
    """
    yaml_representers = {
        datetime.datetime: _represent_datetime,
        datetime.date: _represent_date,
        bytes: _represent_bytes,
        set: _represent_set,
        frozenset: _represent_set,
        OrderedDict: _represent_ordereddict,
    }
    yaml_multi_representers = {}


class Dumper(SafeDumper):
    """Full dumper class for PyYAML API compatibility."""
    yaml_representers = SafeDumper.yaml_representers.copy()
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
    # Scanner errors: tokenization issues (tabs, invalid characters, indentation)
    # Parser errors: grammar issues (unclosed braces, missing values, structure)
    msg_lower = message.lower()

    # Scanner error patterns (tokenization/lexing phase)
    scanner_patterns = [
        'tab', 'indentation', 'invalid character', 'escape', 'encoding',
        'byte order', 'invalid unicode', 'invalid multiline',
    ]
    # Parser error patterns (grammar/syntax phase)
    parser_patterns = [
        'expected', 'unexpected', 'found', 'while parsing', 'while scanning',
        'did not find', 'mapping', 'sequence', 'block', 'flow',
    ]

    if 'alias' in msg_lower or 'anchor' in msg_lower:
        return ComposerError(problem=message, problem_mark=problem_mark)

    # Check for scanner patterns first (more specific)
    for pattern in scanner_patterns:
        if pattern in msg_lower:
            return ScannerError(problem=message, problem_mark=problem_mark)

    # Check for parser patterns
    for pattern in parser_patterns:
        if pattern in msg_lower:
            return ParserError(problem=message, problem_mark=problem_mark)

    # Default to ScannerError for unknown tokenization issues
    return ScannerError(problem=message, problem_mark=problem_mark)


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
    try:
        doc = fy.loads(stream, mode='yaml1.1', collect_diag=True)
    except ValueError as e:
        # libfyaml throws ValueError for some parse errors without diagnostics
        # Convert to ParserError for PyYAML compatibility
        raise ParserError(problem=str(e))

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

    # Always use SafeLoader's constructors as baseline for standard tags like !!binary
    # Then overlay any Loader-specific constructors
    if not constructors and not multi_constructors:
        # Loader has no constructors (e.g., factory function like AnsibleLoader)
        # Use SafeLoader's constructors for standard tag handling
        constructors = SafeLoader.yaml_constructors
        multi_constructors = SafeLoader.yaml_multi_constructors

    if constructors or multi_constructors:
        # Create a lightweight proxy with the constructors
        # This avoids instantiating Loaders that inherit from parsers (like AnsibleLoader)
        loader = _ConstructorProxy(Loader, stream)
        # Ensure SafeLoader constructors are available as fallback
        if Loader is not SafeLoader:
            for tag, func in SafeLoader.yaml_constructors.items():
                if tag not in loader.yaml_constructors:
                    loader.yaml_constructors[tag] = func
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


def _node_to_python(node):
    """Convert a PyYAML Node back to a Python value for serialization.

    Args:
        node: A ScalarNode, MappingNode, or SequenceNode

    Returns:
        Python value that can be passed to from_python()
    """
    if isinstance(node, ScalarNode):
        tag = node.tag
        value = node.value

        # Handle standard YAML tags
        if tag == 'tag:yaml.org,2002:null' or value in ('null', 'Null', 'NULL', '~', ''):
            return None
        elif tag == 'tag:yaml.org,2002:bool':
            return value.lower() in ('true', 'yes', 'on')
        elif tag == 'tag:yaml.org,2002:int':
            return int(value)
        elif tag == 'tag:yaml.org,2002:float':
            return float(value)
        elif tag == 'tag:yaml.org,2002:binary':
            # Binary data - decode from base64
            return base64.standard_b64decode(value)
        elif tag and tag.startswith('!'):
            # Custom tag - wrap in TaggedScalar for later emission
            return TaggedScalar(tag, value, node.style)
        else:
            # Plain string
            return value

    elif isinstance(node, MappingNode):
        result = {}
        for key_node, value_node in node.value:
            key = _node_to_python(key_node)
            value = _node_to_python(value_node)
            result[key] = value
        return result

    elif isinstance(node, SequenceNode):
        return [_node_to_python(item) for item in node.value]

    # Unknown node type - return as-is
    return node


class TaggedScalar:
    """Wrapper for scalar values with custom YAML tags."""

    def __init__(self, tag, value, style=None):
        self.tag = tag
        self.value = value
        self.style = style

    def __repr__(self):
        return f"TaggedScalar({self.tag!r}, {self.value!r})"


def _apply_representers(data, dumper):
    """Recursively apply representers to data before serialization.

    This transforms Python objects using registered representers,
    producing data that can be serialized by libfyaml.

    Args:
        data: Python object to transform
        dumper: Dumper instance with representers

    Returns:
        Transformed data suitable for from_python()
    """
    # Handle Node objects returned by representers
    if isinstance(data, (ScalarNode, MappingNode, SequenceNode)):
        return _node_to_python(data)

    data_type = type(data)

    # Check for exact type match in representers
    representers = getattr(dumper, 'yaml_representers', {})
    multi_representers = getattr(dumper, 'yaml_multi_representers', {})

    if data_type in representers:
        representer = representers[data_type]
        result = representer(dumper, data)
        # Recursively apply to result
        return _apply_representers(result, dumper)

    # Check for multi-representers (type hierarchy)
    for base_type, representer in multi_representers.items():
        if base_type is not None and isinstance(data, base_type):
            result = representer(dumper, data)
            return _apply_representers(result, dumper)

    # Check for None type representer (catch-all)
    if None in representers:
        representer = representers[None]
        result = representer(dumper, data)
        return _apply_representers(result, dumper)

    # Recursively process containers
    if isinstance(data, dict):
        return {_apply_representers(k, dumper): _apply_representers(v, dumper)
                for k, v in data.items()}
    elif isinstance(data, (list, tuple)):
        return [_apply_representers(item, dumper) for item in data]

    # Return data unchanged
    return data


def _convert_tagged_values(data):
    """Convert TaggedScalar objects and bytes to tagged FyGeneric values.

    This function recursively walks the data structure and converts:
    - TaggedScalar objects to tagged FyGeneric using from_python(obj, tag=...)
    - bytes to tagged FyGeneric with !!binary tag

    The resulting structure can be passed to from_python(), which will
    recognize and internalize the FyGeneric objects.

    Args:
        data: Python object, possibly containing TaggedScalar or bytes

    Returns:
        Data with TaggedScalars/bytes replaced by tagged FyGeneric values
    """
    if isinstance(data, TaggedScalar):
        # Convert TaggedScalar to tagged FyGeneric
        return fy.from_python(data.value, tag=data.tag)
    elif isinstance(data, bytes):
        # Convert bytes to base64 and create tagged FyGeneric with !!binary tag
        encoded = base64.standard_b64encode(data).decode('ascii')
        return fy.from_python(encoded, tag='tag:yaml.org,2002:binary')
    elif isinstance(data, dict):
        return {_convert_tagged_values(k): _convert_tagged_values(v) for k, v in data.items()}
    elif isinstance(data, (list, tuple)):
        return [_convert_tagged_values(item) for item in data]
    else:
        return data


def dump(data, stream=None, Dumper=SafeDumper, default_flow_style=False,
         default_style=None, canonical=None, indent=None, width=None,
         allow_unicode=True, line_break=None, encoding=None,
         explicit_start=None, explicit_end=None, version=None, tags=None,
         sort_keys=True):
    """Serialize Python object to YAML string.

    Args:
        data: Python object to serialize
        stream: File-like object to write to (optional)
        Dumper: Dumper class (uses registered representers for custom types)
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
    # Apply representers to transform custom types
    representers = getattr(Dumper, 'yaml_representers', {})
    multi_representers = getattr(Dumper, 'yaml_multi_representers', {})

    if representers or multi_representers:
        # Create dumper instance for representer methods
        dumper = Dumper(stream, default_style=default_style,
                       default_flow_style=default_flow_style,
                       canonical=canonical, indent=indent, width=width,
                       allow_unicode=allow_unicode, line_break=line_break,
                       encoding=encoding, explicit_start=explicit_start,
                       explicit_end=explicit_end, version=version, tags=tags,
                       sort_keys=sort_keys)
        data = _apply_representers(data, dumper)

    # Convert TaggedScalars and bytes to tagged FyGeneric values
    data = _convert_tagged_values(data)

    # Convert Python object to FyGeneric and use libfyaml
    # from_python() will internalize any embedded FyGeneric objects
    doc = fy.from_python(data)
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
    # Apply representers to transform custom types
    representers = getattr(Dumper, 'yaml_representers', {})
    multi_representers = getattr(Dumper, 'yaml_multi_representers', {})

    if representers or multi_representers:
        # Create dumper instance for representer methods
        dumper = Dumper(stream, default_style=default_style,
                       default_flow_style=default_flow_style,
                       canonical=canonical, indent=indent, width=width,
                       allow_unicode=allow_unicode, line_break=line_break,
                       encoding=encoding, explicit_start=explicit_start,
                       explicit_end=explicit_end, version=version, tags=tags,
                       sort_keys=sort_keys)
        documents = [_apply_representers(data, dumper) for data in documents]

    # Convert TaggedScalars and bytes to tagged FyGeneric values
    documents = [_convert_tagged_values(data) for data in documents]

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
