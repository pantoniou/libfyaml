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
import binascii
import datetime
import re
import types
from collections import OrderedDict

# Version info for compatibility
__version__ = '6.0.1'  # Mimic PyYAML version for compatibility checks
__with_libyaml__ = True  # We're using libfyaml as backend


# ============================================================================
# Standard YAML Tag Constructors and Representers
# ============================================================================

# ISO 8601 date/time patterns for YAML 1.1 compatibility
# Full timestamp pattern (date + time required, 1-digit month/day OK)
_TIMESTAMP_REGEXP = re.compile(
    r'^(?P<year>[0-9][0-9][0-9][0-9])'
    r'-(?P<month>[0-9][0-9]?)'
    r'-(?P<day>[0-9][0-9]?)'
    r'(?:[Tt]|[ \t]+)'  # Time separator REQUIRED (not optional like before)
    r'(?P<hour>[0-9][0-9]?)'
    r':(?P<minute>[0-9][0-9])'
    r':(?P<second>[0-9][0-9])'
    r'(?:\.(?P<fraction>[0-9]*))?'
    r'(?:[ \t]*(?P<tz>Z|(?P<tz_sign>[-+])(?P<tz_hour>[0-9][0-9]?)'
    r'(?::(?P<tz_minute>[0-9][0-9]))?))?$')

# Date-only requires 2-digit month/day (PyYAML behavior)
_DATE_REGEXP = re.compile(
    r'^(?P<year>[0-9][0-9][0-9][0-9])'
    r'-(?P<month>[0-9][0-9])'
    r'-(?P<day>[0-9][0-9])$')


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


def _construct_yaml_int(value):
    """Construct an int from a YAML 1.1 integer string."""
    value = value.replace('_', '')
    sign = 1
    if value.startswith('-'):
        sign = -1
        value = value[1:]
    elif value.startswith('+'):
        value = value[1:]
    if value == '0':
        return 0
    elif value.startswith('0b'):
        return sign * int(value[2:], 2)
    elif value.startswith('0x'):
        return sign * int(value[2:], 16)
    elif value.startswith('0o'):
        return sign * int(value[2:], 8)
    elif value.startswith('0') and len(value) > 1:
        # Octal (YAML 1.1 style)
        return sign * int(value, 8)
    elif ':' in value:
        # Sexagesimal (base 60)
        digits = [int(part) for part in value.split(':')]
        base = 1
        result = 0
        for digit in reversed(digits):
            result += digit * base
            base *= 60
        return sign * result
    else:
        return sign * int(value)


def _construct_yaml_float(value):
    """Construct a float from a YAML 1.1 float string."""
    value = value.replace('_', '').lower()
    sign = 1
    if value.startswith('-'):
        sign = -1
        value = value[1:]
    elif value.startswith('+'):
        value = value[1:]
    if value == '.inf':
        return sign * float('inf')
    elif value == '.nan':
        return float('nan')
    elif ':' in value:
        # Sexagesimal (base 60)
        digits = [float(part) for part in value.split(':')]
        base = 1
        result = 0.0
        for digit in reversed(digits):
            result += digit * base
            base *= 60
        return sign * result
    else:
        return sign * float(value)


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


# ============================================================================
# Scalar Resolution
# ============================================================================
#
# All scalar type resolution (booleans, integers, floats, nulls) is handled
# by the C library's 'yaml1.1-pyyaml' mode, which matches PyYAML's behavior
# including its deviations from the strict YAML 1.1 spec:
#   - Restricted boolean set (no single-letter y/Y/n/N)
#   - Required explicit sign on scientific notation exponents
#   - Required dot in float mantissa for scientific notation
#
# The only scalar type NOT handled by the C library is timestamps
# (ISO 8601 dates/datetimes), which are resolved in Python via
# _try_implicit_timestamp() since they produce Python-specific types.
# ============================================================================


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
    except (ValueError, binascii.Error) as exc:
        raise ConstructorError(
            None, None,
            "failed to decode base64 data: %s" % exc,
            getattr(node, 'start_mark', None))


def _construct_yaml_omap(loader, node):
    """Construct a list of tuples from a YAML !!omap sequence.

    !!omap is a sequence of single-key mappings.
    PyYAML returns a list of (key, value) tuples, not an OrderedDict.
    """
    if node is None:
        return []
    mark = getattr(node, 'start_mark', None)

    # Handle PyYAML SequenceNode
    if isinstance(node, nodes.SequenceNode):
        pairs = []
        for item_node in node.value:
            if not isinstance(item_node, nodes.MappingNode):
                raise ConstructorError(
                    "while constructing an ordered map", mark,
                    "expected a mapping of length 1, but found %s"
                    % type(item_node).__name__,
                    getattr(item_node, 'start_mark', None))
            if len(item_node.value) != 1:
                raise ConstructorError(
                    "while constructing an ordered map", mark,
                    "expected a single mapping item, but found %d items"
                    % len(item_node.value),
                    getattr(item_node, 'start_mark', None))
            for key_node, value_node in item_node.value:
                key = loader.construct_object(key_node)
                value = loader.construct_object(value_node)
                pairs.append((key, value))
        return pairs

    generic = node._generic if hasattr(node, '_generic') else node
    if hasattr(generic, 'value'):
        generic = generic.value

    # !!omap MUST be a sequence
    is_seq = False
    try:
        is_seq = hasattr(generic, 'is_sequence') and generic.is_sequence()
    except TypeError:
        pass
    if not is_seq and not isinstance(generic, list):
        raise ConstructorError(
            "while constructing an ordered map", mark,
            "expected a sequence, but found %s" % type(generic).__name__, mark)

    pairs = []
    if hasattr(generic, 'is_sequence') and generic.is_sequence():
        for item in generic:
            is_map = False
            try:
                is_map = hasattr(item, 'is_mapping') and item.is_mapping()
            except TypeError:
                pass
            if not is_map:
                raise ConstructorError(
                    "while constructing an ordered map", mark,
                    "expected a mapping of length 1, but found %s"
                    % type(item).__name__, mark)
            item_pairs = list(item.items())
            if len(item_pairs) != 1:
                raise ConstructorError(
                    "while constructing an ordered map", mark,
                    "expected a single mapping item, but found %d items"
                    % len(item_pairs), mark)
            k, v = item_pairs[0]
            key = k.to_python() if hasattr(k, 'to_python') else k
            value = v.to_python() if hasattr(v, 'to_python') else v
            pairs.append((key, value))
    elif isinstance(generic, list):
        for item in generic:
            if not isinstance(item, dict):
                raise ConstructorError(
                    "while constructing an ordered map", mark,
                    "expected a mapping of length 1, but found %s"
                    % type(item).__name__, mark)
            if len(item) != 1:
                raise ConstructorError(
                    "while constructing an ordered map", mark,
                    "expected a single mapping item, but found %d items"
                    % len(item), mark)
            for k, v in item.items():
                pairs.append((k, v))
    return pairs


def _construct_yaml_set(loader, node):
    """Construct a set from a YAML !!set mapping.

    !!set is a mapping where only keys matter (values are null).
    """
    if node is None:
        return set()

    # Handle PyYAML MappingNode - use generator for two-phase construction
    if isinstance(node, nodes.MappingNode):
        return loader.construct_yaml_set(node)

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
    mark = getattr(node, 'start_mark', None)

    # Handle PyYAML SequenceNode
    if isinstance(node, nodes.SequenceNode):
        pairs = []
        for item_node in node.value:
            if not isinstance(item_node, nodes.MappingNode):
                raise ConstructorError(
                    "while constructing pairs", mark,
                    "expected a mapping of length 1, but found %s"
                    % type(item_node).__name__,
                    getattr(item_node, 'start_mark', None))
            if len(item_node.value) != 1:
                raise ConstructorError(
                    "while constructing pairs", mark,
                    "expected a single mapping item, but found %d items"
                    % len(item_node.value),
                    getattr(item_node, 'start_mark', None))
            for key_node, value_node in item_node.value:
                key = loader.construct_object(key_node)
                value = loader.construct_object(value_node)
                pairs.append((key, value))
        return pairs

    generic = node._generic if hasattr(node, '_generic') else node
    if hasattr(generic, 'value'):
        generic = generic.value

    # !!pairs MUST be a sequence
    is_seq = False
    try:
        is_seq = hasattr(generic, 'is_sequence') and generic.is_sequence()
    except TypeError:
        pass
    if not is_seq and not isinstance(generic, list):
        raise ConstructorError(
            "while constructing pairs", mark,
            "expected a sequence, but found %s" % type(generic).__name__, mark)

    pairs = []
    if hasattr(generic, 'is_sequence') and generic.is_sequence():
        for item in generic:
            is_map = False
            try:
                is_map = hasattr(item, 'is_mapping') and item.is_mapping()
            except TypeError:
                pass
            if not is_map:
                raise ConstructorError(
                    "while constructing pairs", mark,
                    "expected a mapping of length 1, but found %s"
                    % type(item).__name__, mark)
            item_pairs = list(item.items())
            if len(item_pairs) != 1:
                raise ConstructorError(
                    "while constructing pairs", mark,
                    "expected a single mapping item, but found %d items"
                    % len(item_pairs), mark)
            k, v = item_pairs[0]
            key = k.to_python() if hasattr(k, 'to_python') else k
            value = v.to_python() if hasattr(v, 'to_python') else v
            pairs.append((key, value))
    elif isinstance(generic, list):
        for item in generic:
            if not isinstance(item, dict):
                raise ConstructorError(
                    "while constructing pairs", mark,
                    "expected a mapping of length 1, but found %s"
                    % type(item).__name__, mark)
            if len(item) != 1:
                raise ConstructorError(
                    "while constructing pairs", mark,
                    "expected a single mapping item, but found %d items"
                    % len(item), mark)
            for k, v in item.items():
                pairs.append((k, v))
    return pairs


def _construct_undefined(loader, node):
    """Raise an error for undefined/unknown tags."""
    tag = getattr(node, 'tag', None)
    # Don't raise for standard YAML tags - those are handled by default conversion
    if tag is None or tag.startswith('tag:yaml.org,2002:'):
        # Fall through to default conversion
        generic = node._generic if hasattr(node, '_generic') else node
        if hasattr(generic, 'to_python'):
            return generic.to_python()
        return getattr(node, 'value', None)
    raise ConstructorError(
        None, None,
        "could not determine a constructor for the tag %r" % tag,
        getattr(node, 'start_mark', None))


def _construct_yaml_null(loader, node):
    """Construct None from a !!null tagged node."""
    return None


def _construct_yaml_bool_tag(loader, node):
    """Construct a bool from a !!bool tagged node."""
    value = loader.construct_scalar(node)
    if isinstance(value, bool):
        return value
    return str(value).lower() in ('true', 'yes', 'on', '1')


def _construct_yaml_int_tag(loader, node):
    """Construct an int from a !!int tagged node."""
    value = loader.construct_scalar(node)
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    return _construct_yaml_int(str(value))


def _construct_yaml_float_tag(loader, node):
    """Construct a float from a !!float tagged node."""
    value = loader.construct_scalar(node)
    if isinstance(value, float):
        return value
    return _construct_yaml_float(str(value))


def _construct_yaml_seq_tag(loader, node):
    """Construct a list from a !!seq tagged node.

    For PyYAML Node objects, delegates to construct_yaml_seq (a generator
    for two-phase construction to support recursive structures).
    For FyGeneric nodes, returns directly.
    """
    if isinstance(node, nodes.SequenceNode):
        return loader.construct_yaml_seq(node)
    # Check FyGeneric-backed nodes
    generic = node._generic if hasattr(node, '_generic') else node
    try:
        is_seq = hasattr(generic, 'is_sequence') and generic.is_sequence()
    except TypeError:
        is_seq = False
    if not is_seq:
        node_type = 'scalar'
        try:
            if hasattr(generic, 'is_mapping') and generic.is_mapping():
                node_type = 'mapping'
        except TypeError:
            pass
        raise ConstructorError(
            None, None,
            "expected a sequence node, but found %s" % node_type,
            getattr(node, 'start_mark', None))
    # Construct sequence (no id-based caching for FyGeneric - ids can be reused)
    result = []
    result.extend(loader.construct_sequence(node, deep=True))
    return result


def _construct_yaml_map_tag(loader, node):
    """Construct a dict from a !!map tagged node."""
    if isinstance(node, nodes.MappingNode):
        return loader.construct_yaml_map(node)
    # Check FyGeneric-backed nodes
    generic = node._generic if hasattr(node, '_generic') else node
    try:
        is_map = hasattr(generic, 'is_mapping') and generic.is_mapping()
    except TypeError:
        is_map = False
    if not is_map:
        node_type = 'scalar'
        try:
            if hasattr(generic, 'is_sequence') and generic.is_sequence():
                node_type = 'sequence'
        except TypeError:
            pass
        raise ConstructorError(
            None, None,
            "expected a mapping node, but found %s" % node_type,
            getattr(node, 'start_mark', None))
    # Construct mapping (no id-based caching for FyGeneric - ids can be reused)
    result = {}
    result.update(loader.construct_mapping(node, deep=True))
    return result


def _construct_yaml_str(loader, node):
    """Construct a string from a !!str tagged node. Must be a scalar."""
    generic = node._generic if hasattr(node, '_generic') else node
    is_seq = False
    is_map = False
    try:
        is_seq = hasattr(generic, 'is_sequence') and generic.is_sequence()
    except TypeError:
        pass
    try:
        is_map = hasattr(generic, 'is_mapping') and generic.is_mapping()
    except TypeError:
        pass
    if is_seq or is_map:
        node_type = 'sequence' if is_seq else 'mapping'
        raise ConstructorError(
            None, None,
            "expected a scalar node, but found %s" % node_type,
            getattr(node, 'start_mark', None))
    return loader.construct_scalar(node)


# Representer functions for dump()
def _represent_datetime(dumper, data):
    """Represent a datetime object as a !!timestamp scalar node."""
    if data.tzinfo is not None:
        value = data.isoformat()
    else:
        value = data.strftime('%Y-%m-%d %H:%M:%S')
        if data.microsecond:
            value += '.%06d' % data.microsecond
    return ScalarNode('tag:yaml.org,2002:timestamp', value)


def _represent_date(dumper, data):
    """Represent a date object as a !!timestamp scalar node."""
    return ScalarNode('tag:yaml.org,2002:timestamp', data.isoformat())


def _represent_bytes(dumper, data):
    """Represent bytes as base64-encoded binary with literal block style."""
    encoded = base64.standard_b64encode(data).decode('ascii')
    return ScalarNode('tag:yaml.org,2002:binary', encoded, style='|')


def _represent_set(dumper, data):
    """Represent a set as a tagged mapping with null values."""
    value = []
    for item in data:
        item_node = dumper.represent_data(item)
        null_node = dumper.represent_data(None)
        value.append((item_node, null_node))
    return MappingNode('tag:yaml.org,2002:set', value)


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
from libfyaml.pyyaml_compat import tokens
from libfyaml.pyyaml_compat import events

# Import error classes for direct access (yaml.YAMLError, etc.)
from libfyaml.pyyaml_compat.error import YAMLError, MarkedYAMLError, Mark
from libfyaml.pyyaml_compat.constructor import ConstructorError
from libfyaml.pyyaml_compat.scanner import ScannerError
from libfyaml.pyyaml_compat.parser import ParserError
from libfyaml.pyyaml_compat.composer import ComposerError
from libfyaml.pyyaml_compat.representer import RepresenterError


class SerializerError(YAMLError):
    """YAML serializer error (e.g., serializer state violations)."""
    pass


class EmitterError(YAMLError):
    """YAML emitter error (e.g., invalid anchors, tags, event sequence)."""
    pass


# Import Token classes for direct access (yaml.ScalarToken, etc.)
from libfyaml.pyyaml_compat.tokens import (
    Token, DirectiveToken, DocumentStartToken, DocumentEndToken,
    StreamStartToken, StreamEndToken, BlockSequenceStartToken,
    BlockMappingStartToken, BlockEndToken, FlowSequenceStartToken,
    FlowMappingStartToken, FlowSequenceEndToken, FlowMappingEndToken,
    KeyToken, ValueToken, BlockEntryToken, FlowEntryToken,
    AliasToken, AnchorToken, TagToken, ScalarToken,
)

# Import Event classes for direct access (yaml.ScalarEvent, etc.)
from libfyaml.pyyaml_compat.events import (
    Event, NodeEvent, CollectionStartEvent, CollectionEndEvent,
    StreamStartEvent, StreamEndEvent, DocumentStartEvent, DocumentEndEvent,
    AliasEvent, ScalarEvent, SequenceStartEvent, SequenceEndEvent,
    MappingStartEvent, MappingEndEvent,
)


# Loader classes with add_constructor support for custom tags
class BaseLoader:
    """Base loader class for PyYAML API compatibility.

    This class provides PyYAML-compatible constructor methods that work
    with FyGeneric nodes wrapped in _NodeWrapper objects.
    """
    yaml_constructors = {}
    yaml_multi_constructors = {}
    yaml_path_resolvers = {}

    @classmethod
    def add_path_resolver(cls, tag, path, kind=None):
        """Add a path-based tag resolver. Delegates to BaseResolver logic."""
        if 'yaml_path_resolvers' not in cls.__dict__:
            cls.yaml_path_resolvers = cls.yaml_path_resolvers.copy()
        # Reuse resolver's normalization and storage logic
        resolver.BaseResolver.add_path_resolver.__func__(cls, tag, path, kind)

    def __init__(self, stream=None):
        """Initialize loader with optional stream."""
        # Set name and stream attributes for PyYAML compatibility
        # (used by annotatedyaml's _LoaderMixin.get_name/get_stream_name)
        self.stream = stream
        if isinstance(stream, str):
            self.name = '<unicode string>'
        elif isinstance(stream, bytes):
            self.name = '<byte string>'
        else:
            self.name = getattr(stream, 'name', '<file>')
        if hasattr(stream, 'read'):
            self._stream = stream.read()
        else:
            self._stream = stream
        self._events = None
        self._event_idx = 0
        self._tokens = None
        self._token_idx = 0
        # Create resolver that inherits path resolvers from Loader class
        self._resolver = resolver.Resolver()
        # Copy class-level yaml_path_resolvers to instance resolver
        cls = type(self)
        if hasattr(cls, 'yaml_path_resolvers') and cls.yaml_path_resolvers:
            self._resolver.yaml_path_resolvers = cls.yaml_path_resolvers
        self._anchors = {}
        self.constructed_objects = {}
        self.recursive_objects = {}
        self.state_generators = []
        self.deep_construct = False

    def _ensure_events(self):
        if self._events is None:
            data = self._stream
            if isinstance(data, bytes):
                data = _decode_bytes_stream(data)
            if data is None:
                data = ''
            self._events = list(parse(data))
            self._event_idx = 0

    def _ensure_tokens(self):
        if self._tokens is None:
            data = self._stream
            if isinstance(data, bytes):
                data = _decode_bytes_stream(data)
            if data is None:
                data = ''
            self._tokens = list(scan(data))
            self._token_idx = 0

    def check_event(self, *choices):
        self._ensure_events()
        if self._event_idx >= len(self._events):
            return False
        if not choices:
            return True
        return isinstance(self._events[self._event_idx], choices)

    def peek_event(self):
        self._ensure_events()
        if self._event_idx < len(self._events):
            return self._events[self._event_idx]
        return None

    def get_event(self):
        self._ensure_events()
        if self._event_idx < len(self._events):
            ev = self._events[self._event_idx]
            self._event_idx += 1
            return ev
        return None

    def check_token(self, *choices):
        self._ensure_tokens()
        if self._token_idx >= len(self._tokens):
            return False
        if not choices:
            return True
        return isinstance(self._tokens[self._token_idx], choices)

    def peek_token(self):
        self._ensure_tokens()
        if self._token_idx < len(self._tokens):
            return self._tokens[self._token_idx]
        return None

    def get_token(self):
        self._ensure_tokens()
        if self._token_idx < len(self._tokens):
            tok = self._tokens[self._token_idx]
            self._token_idx += 1
            return tok
        return None

    def check_node(self):
        self._ensure_events()
        # Skip StreamStartEvent
        if self._event_idx < len(self._events) and \
                isinstance(self._events[self._event_idx], StreamStartEvent):
            self._event_idx += 1
        return self._event_idx < len(self._events) and \
            not isinstance(self._events[self._event_idx], StreamEndEvent)

    def get_node(self):
        if not self.check_node():
            return None
        return self.compose_node(None, None)

    def get_single_node(self):
        self._ensure_events()
        # Skip StreamStartEvent
        if self._event_idx < len(self._events) and \
                isinstance(self._events[self._event_idx], StreamStartEvent):
            self._event_idx += 1
        # Skip DocumentStartEvent
        if self._event_idx < len(self._events) and \
                isinstance(self._events[self._event_idx], DocumentStartEvent):
            self._event_idx += 1
        if self._event_idx >= len(self._events) or \
                isinstance(self._events[self._event_idx], (StreamEndEvent, DocumentEndEvent)):
            return None
        node = self.compose_node(None, None)
        # Skip DocumentEndEvent
        if self._event_idx < len(self._events) and \
                isinstance(self._events[self._event_idx], DocumentEndEvent):
            self._event_idx += 1
        return node

    def compose_node(self, parent, index):
        """Compose a Node from the event stream."""
        self._ensure_events()
        if self._event_idx >= len(self._events):
            return None

        # Skip DocumentStartEvent
        if isinstance(self._events[self._event_idx], DocumentStartEvent):
            self._event_idx += 1

        ev = self._events[self._event_idx]

        if isinstance(ev, AliasEvent):
            self._event_idx += 1
            anchor = ev.anchor
            if hasattr(self, '_anchors') and anchor in self._anchors:
                return self._anchors[anchor]
            return ScalarNode(tag='tag:yaml.org,2002:null', value='',
                              start_mark=ev.start_mark, end_mark=ev.end_mark)

        anchor = getattr(ev, 'anchor', None)

        if isinstance(ev, ScalarEvent):
            self._event_idx += 1
            tag = ev.tag
            if tag is None or tag == '!':
                # Use resolver for implicit tag resolution
                implicit = ev.implicit if ev.implicit else (True, False)
                if hasattr(self, '_resolver') and self._resolver:
                    tag = self._resolver.resolve(ScalarNode, ev.value, implicit)
                else:
                    tag = resolver.Resolver().resolve(ScalarNode, ev.value, implicit)
            node = ScalarNode(tag=tag, value=ev.value,
                              start_mark=ev.start_mark, end_mark=ev.end_mark,
                              style=ev.style)
            if anchor is not None:
                if not hasattr(self, '_anchors'):
                    self._anchors = {}
                self._anchors[anchor] = node
            return node
        elif isinstance(ev, SequenceStartEvent):
            self._event_idx += 1
            tag = ev.tag
            if tag is None or tag == '!':
                tag = 'tag:yaml.org,2002:seq'
            node = SequenceNode(tag=tag, value=[],
                                start_mark=ev.start_mark, end_mark=ev.end_mark,
                                flow_style=ev.flow_style)
            if anchor is not None:
                if not hasattr(self, '_anchors'):
                    self._anchors = {}
                self._anchors[anchor] = node
            while self._event_idx < len(self._events) and \
                    not isinstance(self._events[self._event_idx], SequenceEndEvent):
                item = self.compose_node(node, len(node.value))
                if item is not None:
                    node.value.append(item)
            if self._event_idx < len(self._events):
                node.end_mark = self._events[self._event_idx].end_mark
                self._event_idx += 1  # skip SequenceEndEvent
            return node
        elif isinstance(ev, MappingStartEvent):
            self._event_idx += 1
            tag = ev.tag
            if tag is None or tag == '!':
                tag = 'tag:yaml.org,2002:map'
            node = MappingNode(tag=tag, value=[],
                               start_mark=ev.start_mark, end_mark=ev.end_mark,
                               flow_style=ev.flow_style)
            if anchor is not None:
                if not hasattr(self, '_anchors'):
                    self._anchors = {}
                self._anchors[anchor] = node
            while self._event_idx < len(self._events) and \
                    not isinstance(self._events[self._event_idx], MappingEndEvent):
                key = self.compose_node(node, None)
                value = self.compose_node(node, key)
                if key is not None and value is not None:
                    node.value.append((key, value))
            if self._event_idx < len(self._events):
                node.end_mark = self._events[self._event_idx].end_mark
                self._event_idx += 1  # skip MappingEndEvent
            return node
        elif isinstance(ev, AliasEvent):
            self._event_idx += 1
            return ScalarNode('tag:yaml.org,2002:null', '',
                              start_mark=ev.start_mark, end_mark=ev.end_mark)
        elif isinstance(ev, (DocumentEndEvent, StreamEndEvent)):
            return None
        else:
            self._event_idx += 1
            return None

    def get_data(self):
        """Construct and return the next document."""
        node = self.get_node()
        if node is not None:
            return self.construct_document(node) if hasattr(self, 'construct_document') else \
                self.construct_object(node, deep=True)
        return None

    def get_single_data(self):
        """Construct and return a single document."""
        node = self.get_single_node()
        if node is not None:
            return self.construct_document(node) if hasattr(self, 'construct_document') else \
                self.construct_object(node, deep=True)
        return None

    def construct_document(self, node):
        """Construct a Python object from a document node.

        This is the standard PyYAML interface for constructing a document.
        It processes state generators for two-phase construction.
        """
        data = self.construct_object(node)
        # Process any pending state generators (for two-phase construction)
        while self.state_generators:
            state_generators = self.state_generators
            self.state_generators = []
            for generator in state_generators:
                for dummy in generator:
                    pass
        self.constructed_objects = {}
        self.recursive_objects = {}
        self.deep_construct = False
        return data

    def dispose(self):
        """Clean up resources."""
        pass

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
            node: A _NodeWrapper, FyGeneric, or PyYAML Node

        Returns:
            The scalar value (str, int, float, bool, or None)
        """
        if node is None:
            return ''
        # Handle PyYAML MappingNode — look for '=' default value key
        if isinstance(node, nodes.MappingNode):
            for key_node, value_node in node.value:
                if key_node.tag == 'tag:yaml.org,2002:value' or \
                   (isinstance(key_node, nodes.ScalarNode) and key_node.value == '='):
                    return self.construct_scalar(value_node)
            return ''
        # Handle PyYAML Node objects (ScalarNode has .id == 'scalar')
        if isinstance(node, nodes.ScalarNode):
            return node.value
        # Handle _NodeWrapper — check for mapping with '=' key
        if hasattr(node, '_generic'):
            generic = node._generic
            try:
                is_map = hasattr(generic, 'is_mapping') and generic.is_mapping()
            except TypeError:
                is_map = False
            if is_map:
                try:
                    for k, v in generic.items():
                        if k == '=':
                            return v
                except TypeError:
                    pass
                return ''
            return generic.to_python()
        # Handle FyGeneric directly
        if hasattr(node, 'to_python'):
            return node.to_python()
        # Handle raw value
        if hasattr(node, 'value'):
            return node.value
        return str(node)

    @staticmethod
    def _get_generic(node):
        """Safely extract the FyGeneric from a node wrapper.

        FyGeneric mapping objects raise TypeError (not AttributeError)
        from hasattr()/getattr() calls due to C extension quirks, so
        all attribute probing must be guarded with try/except TypeError.
        """
        try:
            generic = node._generic if hasattr(node, '_generic') else node
        except TypeError:
            generic = node
        try:
            if hasattr(generic, 'value'):
                generic = generic.value
        except TypeError:
            pass
        return generic

    @staticmethod
    def _get_start_mark(node):
        """Safely get start_mark from a node or FyGeneric.

        FyGeneric objects with unhashable keys may raise TypeError
        on attribute access, so this must be guarded.
        """
        try:
            if hasattr(node, 'start_mark'):
                return node.start_mark
        except TypeError:
            pass
        return None

    def construct_mapping(self, node, deep=False):
        """Construct a mapping (dict) from a node.

        Args:
            node: A _NodeWrapper, FyGeneric mapping node, or PyYAML MappingNode
            deep: If True, recursively construct nested objects

        Returns:
            A dictionary
        """
        if node is None:
            return {}

        # Handle PyYAML MappingNode objects
        if isinstance(node, nodes.MappingNode):
            mapping = {}
            for key_node, value_node in node.value:
                key = self.construct_object(key_node, deep=deep)
                try:
                    hash(key)
                except TypeError as exc:
                    raise ConstructorError(
                        "while constructing a mapping", node.start_mark,
                        "found unhashable key", key_node.start_mark,
                    ) from exc
                value = self.construct_object(value_node, deep=deep)
                mapping[key] = value
            return mapping

        generic = self._get_generic(node)

        if hasattr(generic, 'is_mapping') and generic.is_mapping():
            mapping = {}
            try:
                items = list(generic.items())
            except TypeError as exc:
                mark = self._get_start_mark(node)
                raise ConstructorError(
                    "while constructing a mapping", mark,
                    "found unhashable key", mark,
                ) from exc
            for k, v in items:
                # Check for custom tags that need constructor dispatch
                k_tag = k.get_tag() if hasattr(k, 'get_tag') else None
                v_tag = v.get_tag() if hasattr(v, 'get_tag') else None
                k_has_custom_tag = k_tag and not k_tag.startswith('tag:yaml.org,2002:')
                v_has_custom_tag = v_tag and not v_tag.startswith('tag:yaml.org,2002:')

                if deep or k_has_custom_tag:
                    key = self.construct_object(k, deep=True)
                else:
                    key = k.to_python() if hasattr(k, 'to_python') else k
                try:
                    hash(key)
                except TypeError as exc:
                    node_mark = self._get_start_mark(node)
                    key_mark = self._get_start_mark(k)
                    raise ConstructorError(
                        "while constructing a mapping", node_mark,
                        "found unhashable key", key_mark if key_mark else node_mark,
                    ) from exc
                if deep or v_has_custom_tag:
                    value = self.construct_object(v, deep=True)
                else:
                    value = v.to_python() if hasattr(v, 'to_python') else v
                mapping[key] = value
            return mapping
        elif isinstance(generic, dict):
            return generic
        return {}

    def construct_sequence(self, node, deep=False):
        """Construct a sequence (list) from a node.

        Args:
            node: A _NodeWrapper, FyGeneric sequence node, or PyYAML SequenceNode
            deep: If True, recursively construct nested objects

        Returns:
            A list
        """
        if node is None:
            return []

        # Handle PyYAML SequenceNode objects
        if isinstance(node, nodes.SequenceNode):
            return [self.construct_object(child, deep=deep) for child in node.value]

        generic = self._get_generic(node)

        try:
            is_seq = hasattr(generic, 'is_sequence') and generic.is_sequence()
        except TypeError:
            is_seq = False
        if is_seq:
            if deep:
                return [self.construct_object(item, deep=True) for item in generic]
            else:
                result = []
                for item in generic:
                    try:
                        has_to_python = hasattr(item, 'to_python')
                    except TypeError:
                        has_to_python = False
                    if has_to_python:
                        try:
                            result.append(item.to_python())
                        except TypeError:
                            # Item has unhashable keys - construct deeply
                            result.append(self.construct_object(item, deep=True))
                    else:
                        result.append(item)
                return result
        elif isinstance(generic, list):
            return generic
        return []

    def construct_object(self, node, deep=False):
        """Construct a Python object from a node."""
        if node is None:
            return None

        # Fast path for PyYAML Node objects (from streaming compose)
        if isinstance(node, nodes.Node):
            return self._construct_from_node(node, deep=deep)

        # Get the generic
        generic = self._get_generic(node)

        # Initialize constructed objects cache (used by tag constructors for
        # cycle detection with PyYAML Node objects only - NOT FyGeneric objects,
        # since FyGeneric iteration creates temporary wrappers whose id() can
        # be reused after garbage collection, causing false cache hits)
        if not hasattr(self, '_constructed_objects'):
            self._constructed_objects = {}

        # Get tag
        tag = None
        try:
            if hasattr(node, 'tag'):
                tag = node.tag
            elif hasattr(generic, 'get_tag'):
                tag = generic.get_tag()
        except TypeError:
            pass

        # Resolve implicit tags when no explicit tag is set
        # This mimics PyYAML's Composer which resolves tags on every node
        if tag is None and hasattr(self, '_resolver'):
            try:
                is_mapping = hasattr(generic, 'is_mapping') and generic.is_mapping()
            except TypeError:
                is_mapping = False
            try:
                is_sequence = hasattr(generic, 'is_sequence') and generic.is_sequence()
            except TypeError:
                is_sequence = False

            if is_mapping:
                tag = 'tag:yaml.org,2002:map'
            elif is_sequence:
                tag = 'tag:yaml.org,2002:seq'
            else:
                # Scalar: determine tag from to_python() result type.
                # The C library correctly handles quoting: quoted "123" returns
                # str, unquoted 123 returns int. For non-string types (int,
                # float, bool, None) we trust the C library's resolution.
                # For strings, we check the Resolver: if it resolves to a type
                # that the C library handles natively (int, float, bool, null)
                # then the scalar must have been quoted — keep as str. If the
                # Resolver matches something the C library doesn't handle
                # (e.g., timestamp), apply the resolver's tag.
                _C_LIB_NATIVE_TAGS = {
                    'tag:yaml.org,2002:int', 'tag:yaml.org,2002:float',
                    'tag:yaml.org,2002:bool', 'tag:yaml.org,2002:null',
                    'tag:yaml.org,2002:str',
                }
                try:
                    value = generic.to_python() if hasattr(generic, 'to_python') else str(generic)
                    if value is None:
                        tag = 'tag:yaml.org,2002:null'
                    elif isinstance(value, bool):
                        tag = 'tag:yaml.org,2002:bool'
                    elif isinstance(value, int):
                        tag = 'tag:yaml.org,2002:int'
                    elif isinstance(value, float):
                        tag = 'tag:yaml.org,2002:float'
                    elif isinstance(value, str):
                        resolved = self._resolver.resolve(nodes.ScalarNode, value, (True, False))
                        if resolved in _C_LIB_NATIVE_TAGS:
                            # C lib returned str but resolver says int/float/etc.
                            # → scalar was quoted, keep as str
                            tag = 'tag:yaml.org,2002:str'
                        else:
                            # Resolver matched something C lib doesn't handle
                            # (e.g., timestamp) → use the resolver's tag
                            tag = resolved
                    else:
                        tag = 'tag:yaml.org,2002:str'
                except (TypeError, AttributeError):
                    tag = 'tag:yaml.org,2002:str'

        # Helper to wrap raw FyGeneric in _NodeWrapper if needed
        def _ensure_wrapped(n, g):
            try:
                is_wrapped = hasattr(n, '_generic')
            except TypeError:
                is_wrapped = False
            return n if is_wrapped else _NodeWrapper(g)

        # Check for exact tag match in constructors
        if tag and tag in self.yaml_constructors:
            constructor = self.yaml_constructors[tag]
            return constructor(self, _ensure_wrapped(node, generic))

        # Check for multi-constructors (tag prefix matches)
        if tag and self.yaml_multi_constructors:
            for prefix, multi_constructor in self.yaml_multi_constructors.items():
                if prefix and tag.startswith(prefix):
                    return multi_constructor(self, tag[len(prefix):], _ensure_wrapped(node, generic))
            # Check for None prefix (catch-all multi-constructor)
            if None in self.yaml_multi_constructors:
                multi_constructor = self.yaml_multi_constructors[None]
                return multi_constructor(self, tag, _ensure_wrapped(node, generic))

        # Check for None tag constructor (catch-all)
        if None in self.yaml_constructors:
            constructor = self.yaml_constructors[None]
            return constructor(self, _ensure_wrapped(node, generic))

        # Check for tag/type mismatches (e.g., !!map on a scalar)
        if tag:
            try:
                is_mapping = hasattr(generic, 'is_mapping') and generic.is_mapping()
            except TypeError:
                is_mapping = False
            try:
                is_sequence = hasattr(generic, 'is_sequence') and generic.is_sequence()
            except TypeError:
                is_sequence = False
            node_type = 'mapping' if is_mapping else 'sequence' if is_sequence else 'scalar'
            if tag in ('tag:yaml.org,2002:map', '!!map') and not is_mapping:
                mark = self._get_start_mark(node)
                raise ConstructorError(
                    None, None,
                    "expected a mapping node, but found %s" % node_type,
                    mark,
                )
            if tag in ('tag:yaml.org,2002:seq', '!!seq') and not is_sequence:
                mark = self._get_start_mark(node)
                raise ConstructorError(
                    None, None,
                    "expected a sequence node, but found %s" % node_type,
                    mark,
                )
            # Raise for truly undefined/unknown tags (not standard YAML tags)
            if not tag.startswith('tag:yaml.org,2002:'):
                mark = self._get_start_mark(node)
                raise ConstructorError(
                    None, None,
                    "could not determine a constructor for the tag %r" % tag,
                    mark,
                )

        # Default: convert based on type
        # Guard all hasattr calls with try/except TypeError for FyGeneric C objects
        try:
            is_mapping = hasattr(generic, 'is_mapping') and generic.is_mapping()
        except TypeError:
            is_mapping = False
        if is_mapping:
            return self.construct_mapping(node, deep=deep)

        try:
            is_sequence = hasattr(generic, 'is_sequence') and generic.is_sequence()
        except TypeError:
            is_sequence = False
        if is_sequence:
            return self.construct_sequence(node, deep=deep)

        try:
            has_to_python = hasattr(generic, 'to_python')
        except TypeError:
            has_to_python = False
        if has_to_python:
            try:
                value = generic.to_python()
            except TypeError:
                # Unhashable mapping - can't convert to Python
                mark = self._get_start_mark(node)
                raise ConstructorError(
                    "while constructing a mapping", mark,
                    "found unhashable key", mark,
                )
            # Try implicit timestamp resolution for untagged string scalars
            if not tag and isinstance(value, str):
                timestamp = _try_implicit_timestamp(value)
                if timestamp is not None:
                    return timestamp
            return value

        try:
            has_value = hasattr(node, 'value')
        except TypeError:
            has_value = False
        if has_value:
            return node.value
        return node

    def _construct_from_node(self, node, deep=False):
        """Construct a Python object from a PyYAML Node.

        Follows PyYAML's construct_object pattern:
        - Two-phase generator construction for recursive structures
        - recursive_objects tracking for cycle detection
        - constructed_objects caching for alias resolution
        """
        if node in self.constructed_objects:
            return self.constructed_objects[node]
        if deep:
            old_deep = self.deep_construct
            self.deep_construct = True
        if node in self.recursive_objects:
            raise ConstructorError(None, None,
                "found unconstructable recursive node", node.start_mark)
        self.recursive_objects[node] = None

        tag = node.tag
        constructor = None
        tag_suffix = None

        # Find constructor
        if tag and tag in self.yaml_constructors:
            constructor = self.yaml_constructors[tag]
        elif tag and self.yaml_multi_constructors:
            for prefix, multi_constructor in self.yaml_multi_constructors.items():
                if prefix is not None and tag.startswith(prefix):
                    tag_suffix = tag[len(prefix):]
                    constructor = multi_constructor
                    break
            else:
                if None in self.yaml_multi_constructors:
                    tag_suffix = tag
                    constructor = self.yaml_multi_constructors[None]
                elif None in self.yaml_constructors:
                    constructor = self.yaml_constructors[None]
        elif None in self.yaml_constructors:
            constructor = self.yaml_constructors[None]

        if constructor is None:
            # Fallback: dispatch based on node type
            if isinstance(node, nodes.ScalarNode):
                constructor = self.__class__.construct_scalar
            elif isinstance(node, nodes.SequenceNode):
                constructor = self.__class__.construct_sequence
            elif isinstance(node, nodes.MappingNode):
                constructor = self.__class__.construct_mapping

        # Call constructor
        if constructor is not None:
            if tag_suffix is None:
                data = constructor(self, node)
            else:
                data = constructor(self, tag_suffix, node)
        else:
            data = node.value

        # Handle generator (two-phase construction)
        if isinstance(data, types.GeneratorType):
            generator = data
            data = next(generator)
            if self.deep_construct:
                for dummy in generator:
                    pass
            else:
                self.state_generators.append(generator)

        self.constructed_objects[node] = data
        del self.recursive_objects[node]
        if deep:
            self.deep_construct = old_deep
        return data

    def construct_pairs(self, node, deep=False):
        """Construct a list of (key, value) pairs from a mapping node."""
        if node is None:
            return []

        # Handle PyYAML MappingNode objects
        if isinstance(node, nodes.MappingNode):
            return [(self.construct_object(key_node, deep=deep),
                     self.construct_object(value_node, deep=deep))
                    for key_node, value_node in node.value]

        generic = self._get_generic(node)

        try:
            is_mapping = hasattr(generic, 'is_mapping') and generic.is_mapping()
        except TypeError:
            is_mapping = False
        if is_mapping:
            try:
                items_list = list(generic.items())
            except TypeError as exc:
                mark = self._get_start_mark(node)
                raise ConstructorError(
                    "while constructing a mapping", mark,
                    "found unhashable key", mark,
                ) from exc
            if deep:
                return [(self.construct_object(k, deep=True), self.construct_object(v, deep=True))
                        for k, v in items_list]
            else:
                pairs = []
                for k, v in items_list:
                    # Check for custom tags that need constructor dispatch
                    k_tag = k.get_tag() if hasattr(k, 'get_tag') else None
                    v_tag = v.get_tag() if hasattr(v, 'get_tag') else None
                    k_has_custom = k_tag and not k_tag.startswith('tag:yaml.org,2002:')
                    v_has_custom = v_tag and not v_tag.startswith('tag:yaml.org,2002:')

                    if k_has_custom:
                        pk = self.construct_object(k, deep=True)
                    else:
                        try:
                            pk = k.to_python() if hasattr(k, 'to_python') else k
                        except TypeError:
                            pk = self.construct_object(k, deep=True)
                    if v_has_custom:
                        pv = self.construct_object(v, deep=True)
                    else:
                        try:
                            pv = v.to_python() if hasattr(v, 'to_python') else v
                        except TypeError:
                            pv = self.construct_object(v, deep=True)
                    pairs.append((pk, pv))
                return pairs
        return []

    def flatten_mapping(self, node):
        """Handle merge keys (<<) in mapping nodes.

        This processes PyYAML MappingNode objects for merge key support.
        For FyGeneric nodes, this is a no-op since the C library handles merges.
        """
        if not isinstance(node, nodes.MappingNode):
            return
        merge = []
        index = 0
        while index < len(node.value):
            key_node, value_node = node.value[index]
            if key_node.tag == 'tag:yaml.org,2002:merge':
                del node.value[index]
                if isinstance(value_node, nodes.MappingNode):
                    self.flatten_mapping(value_node)
                    merge.extend(value_node.value)
                elif isinstance(value_node, nodes.SequenceNode):
                    submerge = []
                    for subnode in value_node.value:
                        if not isinstance(subnode, nodes.MappingNode):
                            raise ConstructorError(
                                "while constructing a mapping",
                                node.start_mark,
                                "expected a mapping for merging, but found %s"
                                % subnode.id, subnode.start_mark)
                        self.flatten_mapping(subnode)
                        submerge.append(subnode.value)
                    submerge.reverse()
                    for value in submerge:
                        merge.extend(value)
                else:
                    raise ConstructorError(
                        "while constructing a mapping", node.start_mark,
                        "expected a mapping or list of mappings for merging, "
                        "but found %s" % value_node.id, value_node.start_mark)
            elif key_node.tag == 'tag:yaml.org,2002:value':
                key_node.tag = 'tag:yaml.org,2002:str'
                index += 1
            else:
                index += 1
        if merge:
            node.value = merge + node.value

    def construct_yaml_seq(self, node):
        """Construct a YAML sequence as a generator (PyYAML compatible).

        PyYAML's construct_yaml_seq is a generator that yields the list first
        (for two-phase construction), then fills it in. We match that interface.
        """
        data = []
        yield data
        data.extend(self.construct_sequence(node, deep=True))

    def construct_yaml_map(self, node):
        """Construct a YAML mapping as a generator (PyYAML compatible).

        Two-phase construction: yield empty dict first, fill in later.
        """
        data = {}
        yield data
        value = self.construct_mapping(node, deep=True)
        data.update(value)

    def construct_yaml_set(self, node):
        """Construct a YAML set as a generator (PyYAML compatible).

        Two-phase construction: yield empty set first, fill in later.
        """
        data = set()
        yield data
        value = self.construct_mapping(node, deep=True)
        data.update(value)

    def construct_yaml_object(self, node, cls):
        """Construct a Python object from a YAML node using __setstate__.

        This is used by YAMLObject subclasses for default deserialization.

        Args:
            node: A YAML node (mapping)
            cls: The class to instantiate

        Returns:
            An instance of cls with state set from the mapping
        """
        # Get the state from the mapping
        state = self.construct_mapping(node, deep=True)

        # Create instance
        instance = cls.__new__(cls)

        # Set state
        if hasattr(instance, '__setstate__'):
            instance.__setstate__(state)
        else:
            # Default: update __dict__
            if isinstance(state, dict):
                instance.__dict__.update(state)

        return instance


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
        # Standard YAML 1.1 tags — all types must be registered so that
        # custom catch-all (None) constructors don't accidentally receive
        # standard-tagged nodes (e.g. EventsLoader expects only custom tags).
        'tag:yaml.org,2002:null': _construct_yaml_null,
        'tag:yaml.org,2002:bool': _construct_yaml_bool_tag,
        'tag:yaml.org,2002:int': _construct_yaml_int_tag,
        'tag:yaml.org,2002:float': _construct_yaml_float_tag,
        'tag:yaml.org,2002:str': _construct_yaml_str,
        'tag:yaml.org,2002:seq': _construct_yaml_seq_tag,
        'tag:yaml.org,2002:map': _construct_yaml_map_tag,
        'tag:yaml.org,2002:binary': _construct_yaml_binary,
        'tag:yaml.org,2002:timestamp': _construct_yaml_timestamp,
        'tag:yaml.org,2002:set': _construct_yaml_set,
        'tag:yaml.org,2002:omap': _construct_yaml_omap,
        'tag:yaml.org,2002:pairs': _construct_yaml_pairs,
    }
    yaml_multi_constructors = {}


def _construct_python_tag(loader, suffix, node):
    """Handle !!python/* tags with validation.

    Supports basic Python types and validates Python-specific tags.
    """
    mark = getattr(node, 'start_mark', None)
    generic = node._generic if hasattr(node, '_generic') else node

    # Check node type
    is_scalar = True
    is_seq = False
    is_map = False
    try:
        if hasattr(generic, 'is_mapping') and generic.is_mapping():
            is_scalar = False
            is_map = True
        elif hasattr(generic, 'is_sequence') and generic.is_sequence():
            is_scalar = False
            is_seq = True
    except TypeError:
        pass

    # Basic Python types - value may already be converted by C library
    if suffix in ('bool', 'bool:True', 'bool:False'):
        value = loader.construct_scalar(node)
        if isinstance(value, bool):
            return value
        return str(value).lower() in ('true', 'yes', '1', 'on')
    elif suffix == 'none' or suffix == 'none:':
        return None
    elif suffix.startswith('int:') or suffix == 'int':
        value = loader.construct_scalar(node)
        if isinstance(value, int) and not isinstance(value, bool):
            return value
        value = str(value)
        if suffix.startswith('int:'):
            value = suffix[4:]
        return int(value.strip())
    elif suffix.startswith('float:') or suffix == 'float':
        value = loader.construct_scalar(node)
        if isinstance(value, float):
            return value
        value = str(value)
        if suffix.startswith('float:'):
            value = suffix[6:]
        return float(value.strip())
    elif suffix.startswith('complex:') or suffix == 'complex':
        value = loader.construct_scalar(node)
        if isinstance(value, complex):
            return value
        value = str(value)
        if suffix.startswith('complex:'):
            value = suffix[8:]
        return complex(value.strip())
    elif suffix.startswith('str:') or suffix == 'str':
        value = loader.construct_scalar(node)
        return str(value) if value is not None else ''
    elif suffix.startswith('unicode:') or suffix == 'unicode':
        value = loader.construct_scalar(node)
        return str(value) if value is not None else ''
    elif suffix.startswith('long:') or suffix == 'long':
        value = loader.construct_scalar(node)
        if isinstance(value, int):
            return value
        value = str(value)
        if suffix.startswith('long:'):
            value = suffix[5:]
        return int(value.strip())
    elif suffix == 'tuple':
        if isinstance(node, nodes.SequenceNode):
            return tuple(loader.construct_sequence(node))
        items = loader.construct_sequence(node)
        return tuple(items) if isinstance(items, list) else (items,)
    elif suffix == 'list':
        return loader.construct_sequence(node)
    elif suffix == 'dict':
        return loader.construct_mapping(node)

    elif suffix.startswith('name:'):
        name = suffix[5:]
        if not name:
            raise ConstructorError(
                "while constructing a Python name", mark,
                "expected non-empty name", mark)
        if not is_scalar:
            raise ConstructorError(
                "while constructing a Python name", mark,
                "expected a scalar node, but found non-scalar", mark)
        value = loader.construct_scalar(node)
        if value:
            raise ConstructorError(
                "while constructing a Python name", mark,
                "expected the empty value, but found %r" % value, mark)
        if '.' not in name:
            # Try builtin first
            import builtins
            if hasattr(builtins, name):
                return getattr(builtins, name)
            raise ConstructorError(
                "while constructing a Python name", mark,
                "expected a module name separated by '.'", mark)
        module_name, attr_name = name.rsplit('.', 1)
        try:
            import importlib
            module = importlib.import_module(module_name)
        except ImportError as exc:
            raise ConstructorError(
                "while constructing a Python name", mark,
                "cannot find module %r (%s)" % (module_name, exc), mark)
        if not hasattr(module, attr_name):
            raise ConstructorError(
                "while constructing a Python name", mark,
                "module %r has no attribute %r" % (module_name, attr_name), mark)
        return getattr(module, attr_name)

    elif suffix.startswith('module:'):
        module_name = suffix[7:]
        if not module_name:
            raise ConstructorError(
                "while constructing a Python module", mark,
                "expected non-empty module name", mark)
        if not is_scalar:
            raise ConstructorError(
                "while constructing a Python module", mark,
                "expected a scalar node, but found non-scalar", mark)
        value = loader.construct_scalar(node)
        if value:
            raise ConstructorError(
                "while constructing a Python module", mark,
                "expected the empty value, but found %r" % value, mark)
        try:
            import importlib
            return importlib.import_module(module_name)
        except ImportError as exc:
            raise ConstructorError(
                "while constructing a Python module", mark,
                "cannot find module %r (%s)" % (module_name, exc), mark)

    elif suffix == 'bytes':
        value = loader.construct_scalar(node)
        if isinstance(value, bytes):
            return value
        value = value.replace('\n', '').replace('\r', '').replace(' ', '')
        try:
            return base64.b64decode(value)
        except (ValueError, binascii.Error) as exc:
            raise ConstructorError(
                "while constructing Python bytes", mark,
                "failed to decode base64 data: %s" % exc, mark)

    elif suffix.startswith('object/new:'):
        cls_name = suffix[11:]
        # Resolve class and construct
        return _construct_python_object_new(loader, cls_name, node, mark)

    elif suffix.startswith('object/apply:'):
        cls_name = suffix[13:]
        return _construct_python_object_apply(loader, cls_name, node, mark)

    elif suffix.startswith('object:'):
        cls_name = suffix[7:]
        return _construct_python_object(loader, cls_name, node, mark)

    else:
        raise ConstructorError(
            None, None,
            "could not determine a constructor for the tag "
            "'tag:yaml.org,2002:python/%s'" % suffix, mark)


def _resolve_python_class(cls_name, mark):
    """Resolve a Python class name like 'module.Class' to the actual class."""
    parts = cls_name.rsplit('.', 1)
    if len(parts) < 2:
        raise ConstructorError(
            "while constructing a Python object", mark,
            "expected a module name separated by '.'", mark)
    module_name, attr_name = parts
    try:
        import importlib
        module = importlib.import_module(module_name)
    except ImportError as exc:
        raise ConstructorError(
            "while constructing a Python object", mark,
            "cannot find module %r (%s)" % (module_name, exc), mark)
    if not hasattr(module, attr_name):
        raise ConstructorError(
            "while constructing a Python object", mark,
            "module %r has no attribute %r" % (module_name, attr_name), mark)
    return getattr(module, attr_name)


def _construct_python_object(loader, cls_name, node, mark):
    """Construct !!python/object:cls_name from a mapping node."""
    cls = _resolve_python_class(cls_name, mark)
    deep = True  # need deep for __setstate__ check
    state = loader.construct_mapping(node, deep=deep)
    instance = cls.__new__(cls)
    _set_python_instance_state(loader, instance, state)
    return instance


def _set_python_instance_state(loader, instance, state, unsafe=False):
    """Set instance state, with optional blacklist checking for FullLoader."""
    if hasattr(instance, '__setstate__'):
        instance.__setstate__(state)
    else:
        slotstate = {}
        if isinstance(state, tuple) and len(state) == 2:
            state, slotstate = state
        if hasattr(instance, '__dict__'):
            if not unsafe and state and isinstance(state, dict):
                if hasattr(loader, 'check_state_key'):
                    for key in state.keys():
                        loader.check_state_key(key)
            if isinstance(state, dict):
                instance.__dict__.update(state)
        elif state:
            slotstate.update(state) if isinstance(state, dict) and isinstance(slotstate, dict) else None
        if isinstance(slotstate, dict):
            for key, value in slotstate.items():
                if not unsafe and hasattr(loader, 'check_state_key'):
                    loader.check_state_key(key)
                setattr(instance, key, value)


def _construct_python_object_new(loader, cls_name, node, mark):
    """Construct !!python/object/new:cls_name from a mapping or sequence node."""
    cls = _resolve_python_class(cls_name, mark)
    # If it's a sequence node, treat it as args directly
    if isinstance(node, nodes.SequenceNode):
        args = loader.construct_sequence(node, deep=True)
        instance = cls.__new__(cls, *args)
        if hasattr(loader, '_constructed_objects'):
            loader._constructed_objects[id(node)] = instance
        return instance
    # Check if it's a sequence-type FyGeneric
    generic = node._generic if hasattr(node, '_generic') else node
    try:
        is_seq = hasattr(generic, 'is_sequence') and generic.is_sequence()
    except TypeError:
        is_seq = False
    if is_seq:
        args = loader.construct_sequence(node, deep=True)
        instance = cls.__new__(cls, *args)
        if hasattr(loader, '_constructed_objects'):
            loader._constructed_objects[id(node)] = instance
        return instance
    # Create instance skeleton first, register for alias detection,
    # then fill state (which may reference this instance via aliases)
    instance = cls.__new__(cls)
    if hasattr(loader, '_constructed_objects'):
        loader._constructed_objects[id(node)] = instance
    mapping = loader.construct_mapping(node, deep=True)
    args = mapping.get('args') or []
    kwargs = mapping.get('kwds') or {}
    state = mapping.get('state') or {}
    listitems = mapping.get('listitems') or []
    dictitems = mapping.get('dictitems') or {}
    # Re-create with args/kwargs if provided
    if args or kwargs:
        instance = cls.__new__(cls, *args, **kwargs)
        if hasattr(loader, '_constructed_objects'):
            loader._constructed_objects[id(node)] = instance
    if state:
        _set_python_instance_state(loader, instance, state)
    if listitems:
        instance.extend(listitems)
    if dictitems:
        for key, value in dictitems.items():
            instance[key] = value
    return instance


def _construct_python_object_apply(loader, cls_name, node, mark):
    """Construct !!python/object/apply:cls_name from a mapping or sequence node."""
    cls = _resolve_python_class(cls_name, mark)
    # If it's a sequence node, treat it as args directly
    if isinstance(node, nodes.SequenceNode):
        args = loader.construct_sequence(node, deep=True)
        return cls(*args)
    generic = node._generic if hasattr(node, '_generic') else node
    try:
        is_seq = hasattr(generic, 'is_sequence') and generic.is_sequence()
    except TypeError:
        is_seq = False
    if is_seq:
        args = loader.construct_sequence(node, deep=True)
        return cls(*args)
    mapping = loader.construct_mapping(node, deep=True)
    args = mapping.get('args') or []
    kwargs = mapping.get('kwds') or {}
    state = mapping.get('state') or {}
    listitems = mapping.get('listitems') or []
    dictitems = mapping.get('dictitems') or {}
    instance = cls(*args, **kwargs)
    if state:
        _set_python_instance_state(loader, instance, state)
    if listitems:
        instance.extend(listitems)
    if dictitems:
        for key in dictitems:
            instance[key] = dictitems[key]
    return instance


class FullLoader(SafeLoader):
    """Full loader class for PyYAML API compatibility."""
    yaml_constructors = SafeLoader.yaml_constructors.copy()
    yaml_multi_constructors = {
        'tag:yaml.org,2002:python/': _construct_python_tag,
    }

    def get_state_keys_blacklist(self):
        return ['^extend$', '^__.*__$']

    def get_state_keys_blacklist_regexp(self):
        if not hasattr(self, 'state_keys_blacklist_regexp'):
            self.state_keys_blacklist_regexp = re.compile(
                '(' + '|'.join(self.get_state_keys_blacklist()) + ')')
        return self.state_keys_blacklist_regexp

    def check_state_key(self, key):
        if self.get_state_keys_blacklist_regexp().match(key):
            raise ConstructorError(
                None, None,
                "blacklisted key %r in instance state found" % key, None)


class UnsafeLoader(SafeLoader):
    """Unsafe loader class for PyYAML API compatibility."""
    yaml_constructors = SafeLoader.yaml_constructors.copy()
    yaml_multi_constructors = {
        'tag:yaml.org,2002:python/': _construct_python_tag,
    }


class Loader(SafeLoader):
    """Default loader class (same as SafeLoader in our safe implementation)."""
    yaml_constructors = SafeLoader.yaml_constructors.copy()
    yaml_multi_constructors = {
        'tag:yaml.org,2002:python/': _construct_python_tag,
    }


# C-accelerated versions (same as regular versions in libfyaml)
CSafeLoader = SafeLoader
CFullLoader = FullLoader
CUnsafeLoader = UnsafeLoader
CLoader = Loader
CBaseLoader = BaseLoader


# Module-level functions for adding constructors/representers
def add_constructor(tag, constructor, Loader=None):
    """Add a constructor for the given tag.

    Args:
        tag: YAML tag to match
        constructor: Function(loader, node) -> object
        Loader: Loader class to add constructor to (default: Loader)
    """
    if Loader is None:
        Loader = globals()['Loader']
    Loader.add_constructor(tag, constructor)


def add_multi_constructor(tag_prefix, multi_constructor, Loader=None):
    """Add a multi-constructor for the given tag prefix.

    Args:
        tag_prefix: YAML tag prefix to match
        multi_constructor: Function(loader, tag_suffix, node) -> object
        Loader: Loader class to add constructor to (default: Loader)
    """
    if Loader is None:
        Loader = globals()['Loader']
    Loader.add_multi_constructor(tag_prefix, multi_constructor)


def add_implicit_resolver(tag, regexp, first=None, Loader=None, Dumper=None):
    """Add an implicit resolver based on a regular expression."""
    from libfyaml.pyyaml_compat.resolver import Resolver as _Resolver
    if Loader is not None:
        if hasattr(Loader, 'add_implicit_resolver'):
            Loader.add_implicit_resolver(tag, regexp, first)
        elif issubclass(Loader, BaseLoader):
            _Resolver.add_implicit_resolver(tag, regexp, first)
    else:
        _Resolver.add_implicit_resolver(tag, regexp, first)


def add_path_resolver(tag, path, kind=None, Loader=None, Dumper=None):
    """Add a path-based resolver for the given tag."""
    from libfyaml.pyyaml_compat.resolver import Resolver as _Resolver
    if Loader is not None and hasattr(Loader, 'add_path_resolver'):
        Loader.add_path_resolver(tag, path, kind)
    else:
        _Resolver.add_path_resolver(tag, path, kind)
    if Dumper is not None and hasattr(Dumper, 'add_path_resolver'):
        Dumper.add_path_resolver(tag, path, kind)


def add_representer(data_type, representer, Dumper=None):
    """Add a representer for the given data type.

    Args:
        data_type: Python type to represent
        representer: Function(dumper, data) -> node
        Dumper: Dumper class to add representer to (default: Dumper)
    """
    if Dumper is None:
        Dumper = globals()['Dumper']
    Dumper.add_representer(data_type, representer)


def add_multi_representer(data_type, multi_representer, Dumper=None):
    """Add a multi-representer for the given data type.

    Args:
        data_type: Python type (and subclasses) to represent
        multi_representer: Function(dumper, data) -> node
        Dumper: Dumper class to add representer to (default: Dumper)
    """
    if Dumper is None:
        Dumper = globals()['Dumper']
    Dumper.add_multi_representer(data_type, multi_representer)


class YAMLObjectMetaclass(type):
    """Metaclass for YAMLObject that auto-registers constructors/representers."""

    def __init__(cls, name, bases, kwds):
        super().__init__(name, bases, kwds)
        if 'yaml_tag' in kwds and kwds['yaml_tag'] is not None:
            # Get the loader and dumper classes
            yaml_loader = getattr(cls, 'yaml_loader', None)
            yaml_dumper = getattr(cls, 'yaml_dumper', None)

            # Register constructor
            if yaml_loader is not None:
                if isinstance(yaml_loader, (list, tuple)):
                    loaders = yaml_loader
                else:
                    loaders = [yaml_loader]
                for loader in loaders:
                    loader.add_constructor(cls.yaml_tag, cls.from_yaml)

            # Register representer
            if yaml_dumper is not None:
                if isinstance(yaml_dumper, (list, tuple)):
                    dumpers = yaml_dumper
                else:
                    dumpers = [yaml_dumper]
                for dumper in dumpers:
                    dumper.add_representer(cls, cls.to_yaml)


class YAMLObject(metaclass=YAMLObjectMetaclass):
    """Base class for YAML-serializable objects.

    Subclasses should define:
        yaml_tag: The YAML tag for this class (e.g., '!myobject')
        yaml_loader: Loader class(es) to register with (default: Loader)
        yaml_dumper: Dumper class(es) to register with (default: Dumper)

    And optionally override:
        from_yaml(cls, loader, node): Construct object from YAML node
        to_yaml(cls, dumper, data): Represent object as YAML node
    """
    yaml_tag = None
    yaml_loader = None  # Will be set after Loader is defined
    yaml_dumper = None  # Will be set after Dumper is defined

    @classmethod
    def from_yaml(cls, loader, node):
        """Construct an instance from a YAML node."""
        return loader.construct_yaml_object(node, cls)

    @classmethod
    def to_yaml(cls, dumper, data):
        """Represent an instance as a YAML node."""
        return dumper.represent_yaml_object(cls.yaml_tag, data, cls)


# Dumper classes with add_representer support
class BaseDumper:
    """Base dumper class for PyYAML API compatibility."""
    yaml_representers = {}
    yaml_multi_representers = {}
    yaml_path_resolvers = {}

    @classmethod
    def add_path_resolver(cls, tag, path, kind=None):
        """Add a path-based tag resolver."""
        if 'yaml_path_resolvers' not in cls.__dict__:
            cls.yaml_path_resolvers = cls.yaml_path_resolvers.copy()
        resolver.BaseResolver.add_path_resolver.__func__(cls, tag, path, kind)

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
        self._events = []
        self._serialized_nodes = {}
        self._anchors = {}
        self._anchor_count = 0
        self._opened = False
        self._closed = False
        self._represented_objects = {}
        self.alias_key = None

    @property
    def default_flow_style(self):
        return self._default_flow_style

    @default_flow_style.setter
    def default_flow_style(self, value):
        self._default_flow_style = value

    @property
    def represented_objects(self):
        return self._represented_objects

    @represented_objects.setter
    def represented_objects(self, value):
        self._represented_objects = value

    def ignore_aliases(self, data):
        """Return True if aliases should not be used for this data."""
        if data is None:
            return True
        if isinstance(data, (str, bytes, bool, int, float)):
            return True
        return False

    def open(self):
        if self._closed:
            raise SerializerError("serializer is closed")
        if self._opened:
            raise SerializerError("serializer is already opened")
        self._opened = True
        self._events.append(StreamStartEvent())

    def close(self):
        if self._closed:
            raise SerializerError("serializer is closed")
        if not self._opened:
            raise SerializerError("serializer is not opened")
        self._events.append(StreamEndEvent())
        self._closed = True

    def represent(self, data):
        """Represent data and emit as YAML."""
        node = self.represent_data(data)
        self.serialize(node)

    ANCHOR_TEMPLATE = 'id%03d'

    def serialize(self, node):
        """Serialize a Node to the stream."""
        if self._closed:
            raise SerializerError("serializer is closed")
        if not self._opened:
            raise SerializerError("serializer is not opened")
        self._events.append(DocumentStartEvent(explicit=self._explicit_start))
        self._anchor_node(node)
        self._serialize_node(node)
        self._events.append(DocumentEndEvent(explicit=self._explicit_end))
        self._serialized_nodes = {}
        self._anchors = {}
        self._anchor_count = 0

    def _anchor_node(self, node):
        """Pre-pass to detect nodes appearing more than once."""
        node_id = id(node)
        if node_id in self._anchors:
            if self._anchors[node_id] is None:
                self._anchor_count += 1
                self._anchors[node_id] = self.ANCHOR_TEMPLATE % self._anchor_count
        else:
            self._anchors[node_id] = None
            if isinstance(node, SequenceNode):
                for item in node.value:
                    self._anchor_node(item)
            elif isinstance(node, MappingNode):
                for key_node, val_node in node.value:
                    self._anchor_node(key_node)
                    self._anchor_node(val_node)

    def _serialize_node(self, node):
        node_id = id(node)
        anchor = self._anchors.get(node_id)
        if node_id in self._serialized_nodes:
            self._events.append(AliasEvent(anchor=anchor))
            return
        self._serialized_nodes[node_id] = True
        if isinstance(node, ScalarNode):
            tag = node.tag
            implicit = (tag is None or tag == 'tag:yaml.org,2002:str', tag is None)
            style = getattr(node, 'style', None)
            self._events.append(ScalarEvent(
                anchor=anchor, tag=tag, implicit=implicit,
                value=node.value, style=style))
        elif isinstance(node, SequenceNode):
            tag = node.tag
            implicit = tag is None or tag == 'tag:yaml.org,2002:seq'
            flow_style = getattr(node, 'flow_style', None)
            self._events.append(SequenceStartEvent(
                anchor=anchor, tag=tag, implicit=implicit,
                flow_style=flow_style))
            for item in node.value:
                self._serialize_node(item)
            self._events.append(SequenceEndEvent())
        elif isinstance(node, MappingNode):
            tag = node.tag
            implicit = tag is None or tag == 'tag:yaml.org,2002:map'
            flow_style = getattr(node, 'flow_style', None)
            self._events.append(MappingStartEvent(
                anchor=anchor, tag=tag, implicit=implicit,
                flow_style=flow_style))
            for key_node, val_node in node.value:
                self._serialize_node(key_node)
                self._serialize_node(val_node)
            self._events.append(MappingEndEvent())

    def dispose(self):
        pass

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
        Tracks object identity for alias detection (circular references).
        """
        if self.ignore_aliases(data):
            alias_key = None
        else:
            alias_key = id(data)
        if alias_key is not None:
            if alias_key in self._represented_objects:
                return self._represented_objects[alias_key]
        self.alias_key = alias_key

        data_type = type(data)

        # Check for exact type match first
        if data_type in self.yaml_representers:
            node = self.yaml_representers[data_type](self, data)
        elif data is None:
            node = ScalarNode('tag:yaml.org,2002:null', 'null')
        elif data_type is bool:
            node = ScalarNode('tag:yaml.org,2002:bool', 'true' if data else 'false')
        elif data_type is int:
            node = ScalarNode('tag:yaml.org,2002:int', str(data))
        elif data_type is float:
            if data != data:  # NaN
                node = ScalarNode('tag:yaml.org,2002:float', '.nan')
            elif data == float('inf'):
                node = ScalarNode('tag:yaml.org,2002:float', '.inf')
            elif data == float('-inf'):
                node = ScalarNode('tag:yaml.org,2002:float', '-.inf')
            else:
                node = ScalarNode('tag:yaml.org,2002:float', repr(data))
        elif data_type is str:
            node = ScalarNode('tag:yaml.org,2002:str', data)
        elif data_type is list:
            node = self.represent_sequence('tag:yaml.org,2002:seq', data)
        elif data_type is tuple:
            node = self.represent_sequence('tag:yaml.org,2002:python/tuple', data)
        elif data_type is dict:
            items = data.items()
            if getattr(self, '_sort_keys', True):
                try:
                    items = sorted(items)
                except TypeError:
                    pass
            node = self.represent_mapping('tag:yaml.org,2002:map', items)
        else:
            # Check for multi-representers (type hierarchy)
            node = None
            for base_type, representer in self.yaml_multi_representers.items():
                if base_type is not None and isinstance(data, base_type):
                    node = representer(self, data)
                    break

            if node is None and None in self.yaml_representers:
                node = self.yaml_representers[None](self, data)

            if node is None:
                node = data

        # Register for alias detection (if representer didn't already)
        if alias_key is not None and isinstance(node, (ScalarNode, SequenceNode, MappingNode)):
            if alias_key not in self._represented_objects:
                self._represented_objects[alias_key] = node

        return node

    def represent_mapping(self, tag, mapping, flow_style=None):
        """Represent a mapping as a MappingNode."""
        value = []
        node = MappingNode(tag, value, flow_style=flow_style)
        if self.alias_key is not None:
            self._represented_objects[self.alias_key] = node
        if isinstance(mapping, dict):
            if hasattr(self, 'default_flow_style') and self.default_flow_style is not None:
                node.flow_style = self.default_flow_style
            mapping = list(mapping.items())
            if hasattr(self, 'sort_keys') and self.sort_keys:
                try:
                    mapping = sorted(mapping)
                except TypeError:
                    pass
        for key, val in mapping:
            key_node = self.represent_data(key)
            if not isinstance(key_node, (ScalarNode, MappingNode, SequenceNode)):
                key_node = ScalarNode('tag:yaml.org,2002:str', str(key))
            val_node = self.represent_data(val)
            if not isinstance(val_node, (ScalarNode, MappingNode, SequenceNode)):
                val_node = ScalarNode('tag:yaml.org,2002:str', str(val))
            value.append((key_node, val_node))
        return node

    def represent_sequence(self, tag, sequence, flow_style=None):
        """Represent a sequence as a SequenceNode."""
        value = []
        node = SequenceNode(tag, value, flow_style=flow_style)
        if self.alias_key is not None:
            self._represented_objects[self.alias_key] = node
        for item in sequence:
            item_node = self.represent_data(item)
            if not isinstance(item_node, (ScalarNode, MappingNode, SequenceNode)):
                item_node = ScalarNode('tag:yaml.org,2002:str', str(item))
            value.append(item_node)
        return node

    def represent_scalar(self, tag, value, style=None):
        """Represent a scalar as a ScalarNode.

        Args:
            tag: YAML tag for the scalar
            value: Scalar value
            style: Optional style for the scalar

        Returns:
            ScalarNode with the value
        """
        return ScalarNode(tag, value, style=style)

    def represent_yaml_object(self, tag, data, cls, flow_style=None):
        """Represent a Python object as a YAML mapping with a tag.

        This is used by YAMLObject subclasses for default serialization.

        Args:
            tag: YAML tag for the object
            data: The object to represent
            cls: The class of the object
            flow_style: Optional flow style

        Returns:
            MappingNode with the object's state
        """
        # Get state
        if hasattr(data, '__getstate__'):
            state = data.__getstate__()
        else:
            state = data.__dict__.copy()

        # Represent as mapping
        return self.represent_mapping(tag, state.items())


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

    def represent_dict(self, data):
        """Represent a dict as a mapping."""
        return self.represent_mapping('tag:yaml.org,2002:map', data.items())

    def represent_list(self, data):
        """Represent a list/sequence."""
        return self.represent_sequence('tag:yaml.org,2002:seq', data)

    def represent_str(self, data):
        """Represent a string."""
        return self.represent_scalar('tag:yaml.org,2002:str', data)

    def represent_undefined(self, data):
        """Represent an undefined object using __reduce__ protocol.

        This is the catch-all representer for arbitrary Python objects,
        matching PyYAML's Representer.represent_object behavior.
        """
        data_type = type(data)
        if data_type.__reduce_ex__:
            reduce = data.__reduce_ex__(2)
        elif data_type.__reduce__:
            reduce = data.__reduce__()
        else:
            raise RepresenterError("cannot represent an object: %s" % data)

        if isinstance(reduce, str):
            return self.represent_scalar(
                'tag:yaml.org,2002:python/name:' + reduce, '')

        reduce = list(reduce)
        function = reduce[0]
        args = reduce[1]
        state = reduce[2] if len(reduce) > 2 else None
        listitems = reduce[3] if len(reduce) > 3 else None
        dictitems = reduce[4] if len(reduce) > 4 else None

        # Determine function name
        function_name = '%s.%s' % (function.__module__, function.__qualname__)

        # Determine tag
        if function.__name__ == '__newobj__':
            function = args[0]
            args = args[1:]
            tag = 'tag:yaml.org,2002:python/object/new:' + \
                '%s.%s' % (function.__module__, function.__qualname__)
            newobj = True
        else:
            tag = 'tag:yaml.org,2002:python/object/apply:' + function_name
            newobj = False

        # Build the mapping for complex representations
        if not args and not state and not listitems and not dictitems and newobj:
            return self.represent_mapping(tag, {})

        value = {}
        if args:
            value['args'] = list(args)
        if state or isinstance(state, dict) and state:
            value['state'] = state
        if listitems:
            value['listitems'] = list(listitems)
        if dictitems:
            value['dictitems'] = dict(dictitems)

        return self.represent_mapping(tag, value.items())


class Dumper(SafeDumper):
    """Full dumper class for PyYAML API compatibility."""
    yaml_representers = SafeDumper.yaml_representers.copy()
    yaml_multi_representers = {}

    def represent_name(self, data):
        """Represent a Python name (class, function, etc.) as !!python/name:..."""
        name = '%s.%s' % (data.__module__, data.__qualname__)
        return self.represent_scalar('tag:yaml.org,2002:python/name:' + name, '')

    def represent_module(self, data):
        """Represent a Python module as !!python/module:..."""
        return self.represent_scalar('tag:yaml.org,2002:python/module:' + data.__name__, '')

import types as _types_module
# Register object multi-representer after class definition
Dumper.yaml_multi_representers[object] = SafeDumper.represent_undefined
Dumper.yaml_multi_representers[type] = Dumper.represent_name
Dumper.yaml_representers[_types_module.FunctionType] = Dumper.represent_name
Dumper.yaml_representers[_types_module.BuiltinFunctionType] = Dumper.represent_name
Dumper.yaml_representers[_types_module.ModuleType] = Dumper.represent_module


_default_Dumper = Dumper

# C-accelerated dumper versions
CSafeDumper = SafeDumper
CDumper = Dumper


_BARE_TAGGED_BLOCK_SCALAR_RE = re.compile(r'^![^\s]+\s+[|>]')


def _normalize_yaml(text):
    """Normalize YAML text to handle edge cases libfyaml doesn't parse directly.

    Only two cases need Python-level normalization:
    1. Empty/whitespace-only input (libfyaml raises "No documents found")
    2. Bare tagged block scalars without --- (invalid YAML that PyYAML accepts)

    Note: libfyaml natively handles ~, null, [], {}, "", '' etc.
    """
    stripped = text.strip() if isinstance(text, (str, bytes)) else text

    # Empty or whitespace-only -> None (PyYAML returns None, libfyaml raises)
    if not stripped:
        return None, True

    # Tagged block scalar without document start marker (e.g. "!!binary |\n  data")
    # is not valid YAML but PyYAML accepts it. Prepend "--- " to make it valid.
    match_text = stripped.decode('utf-8', errors='replace') if isinstance(stripped, bytes) else stripped
    if _BARE_TAGGED_BLOCK_SCALAR_RE.match(match_text):
        prefix = b'--- ' if isinstance(text, bytes) else '--- '
        return prefix + text, False

    return text, False


class _NodeWrapper:
    """Wrap FyGeneric to look like PyYAML node for constructors.

    This provides compatibility with PyYAML's Node interface, allowing
    constructors written for PyYAML to work with libfyaml's FyGeneric.
    """

    def __init__(self, generic):
        self._generic = generic
        self.tag = generic.get_tag() if hasattr(generic, 'get_tag') else None
        # Determine node kind
        is_map = False
        is_seq = False
        try:
            is_map = hasattr(generic, 'is_mapping') and generic.is_mapping()
        except TypeError:
            pass
        try:
            is_seq = hasattr(generic, 'is_sequence') and generic.is_sequence()
        except TypeError:
            pass
        # For scalars, provide the value directly
        if not is_map and not is_seq:
            try:
                self.value = generic.to_python() if hasattr(generic, 'to_python') else generic
            except TypeError:
                self.value = generic
        else:
            self.value = generic
        # Resolve tag if None (PyYAML's Composer always resolves tags)
        if self.tag is None:
            if is_map:
                self.tag = 'tag:yaml.org,2002:map'
            elif is_seq:
                self.tag = 'tag:yaml.org,2002:seq'
            else:
                # Scalar: use resolver for implicit tag resolution
                str_value = str(self.value) if self.value is not None else ''
                _res = resolver.Resolver()
                self.tag = _res.resolve(ScalarNode, str_value, (True, False))
        # Extract start_mark/end_mark from marker if available
        self.start_mark = None
        self.end_mark = None
        if hasattr(generic, 'get_marker'):
            marker = generic.get_marker()
            if marker is not None:
                # marker is (start_byte, start_line, start_col, end_byte, end_line, end_col)
                self.start_mark = Mark('<string>', marker[0], marker[1], marker[2])
                self.end_mark = Mark('<string>', marker[3], marker[4], marker[5])

    def __repr__(self):
        return f"<_NodeWrapper tag={self.tag!r} value={self.value!r}>"


def _fygeneric_to_node(generic, stream_name='<string>'):
    """Convert an FyGeneric tree to a PyYAML Node tree with marks.

    This enables proper integration with PyYAML-compatible constructors
    (like Ansible's AnsibleConstructor) that expect Node objects with
    tag, value, start_mark, and end_mark attributes.

    Args:
        generic: FyGeneric object from libfyaml
        stream_name: Name for the stream in Mark objects

    Returns:
        A ScalarNode, SequenceNode, or MappingNode with proper marks
    """
    # Extract marks from the generic's marker
    start_mark = None
    end_mark = None
    if hasattr(generic, 'get_marker'):
        marker = generic.get_marker()
        if marker is not None:
            start_mark = Mark(stream_name, marker[0], marker[1], marker[2])
            end_mark = Mark(stream_name, marker[3], marker[4], marker[5])

    # Get custom tag if any
    custom_tag = None
    if hasattr(generic, 'get_tag'):
        custom_tag = generic.get_tag()

    if hasattr(generic, 'is_mapping') and generic.is_mapping():
        tag = custom_tag or 'tag:yaml.org,2002:map'
        pairs = []
        try:
            for k, v in generic.items():
                key_node = _fygeneric_to_node(k, stream_name)
                val_node = _fygeneric_to_node(v, stream_name)
                pairs.append((key_node, val_node))
        except TypeError as exc:
            raise ConstructorError(
                "while constructing a mapping", start_mark,
                "found unhashable key", start_mark,
            ) from exc
        return MappingNode(tag, pairs, start_mark=start_mark, end_mark=end_mark)

    elif hasattr(generic, 'is_sequence') and generic.is_sequence():
        tag = custom_tag or 'tag:yaml.org,2002:seq'
        items = [_fygeneric_to_node(item, stream_name) for item in generic]
        return SequenceNode(tag, items, start_mark=start_mark, end_mark=end_mark)

    else:
        # Scalar - determine tag from Python value
        value = generic.to_python() if hasattr(generic, 'to_python') else generic
        if custom_tag:
            tag = custom_tag
            str_value = str(value) if value is not None else ''
        elif value is None:
            tag = 'tag:yaml.org,2002:null'
            str_value = 'null'
        elif isinstance(value, bool):
            tag = 'tag:yaml.org,2002:bool'
            str_value = 'true' if value else 'false'
        elif isinstance(value, int):
            tag = 'tag:yaml.org,2002:int'
            str_value = str(value)
        elif isinstance(value, float):
            tag = 'tag:yaml.org,2002:float'
            if value != value:  # NaN
                str_value = '.nan'
            elif value == float('inf'):
                str_value = '.inf'
            elif value == float('-inf'):
                str_value = '-.inf'
            else:
                str_value = repr(value)
        else:
            str_value = str(value) if value is not None else ''
            # Use resolver to detect implicit types (timestamps, etc.)
            # but only for tags the C library doesn't handle natively.
            # If the resolver says int/float/bool/null but to_python()
            # returned str, the scalar was quoted — keep as str.
            from libfyaml.pyyaml_compat.resolver import Resolver as _Resolver
            resolved = _Resolver().resolve(ScalarNode, str_value, (True, False))
            _C_LIB_NATIVE = {
                'tag:yaml.org,2002:int', 'tag:yaml.org,2002:float',
                'tag:yaml.org,2002:bool', 'tag:yaml.org,2002:null',
                'tag:yaml.org,2002:str',
            }
            if resolved in _C_LIB_NATIVE:
                tag = 'tag:yaml.org,2002:str'
            else:
                tag = resolved
        return ScalarNode(tag, str_value, start_mark=start_mark, end_mark=end_mark)


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
        self._loader_class = loader_class
        # Copy constructors from the target class
        self.yaml_constructors = getattr(loader_class, 'yaml_constructors', {})
        self.yaml_multi_constructors = getattr(loader_class, 'yaml_multi_constructors', {})
        # Forward blacklist methods from FullLoader-derived classes
        if hasattr(loader_class, 'get_state_keys_blacklist'):
            # Create a temporary instance to get the bound methods
            # We can't just copy class methods because they need 'self'
            self._blacklist_source = loader_class.__new__(loader_class)
            self.check_state_key = self._blacklist_source.check_state_key
            self.get_state_keys_blacklist = self._blacklist_source.get_state_keys_blacklist
            self.get_state_keys_blacklist_regexp = self._blacklist_source.get_state_keys_blacklist_regexp

    # Provide SafeConstructor-compatible methods that user code may call directly
    def construct_yaml_null(self, node):
        return None

    def construct_yaml_bool(self, node):
        value = self.construct_scalar(node)
        if isinstance(value, bool):
            return value
        return str(value).lower() in ('true', 'yes', 'on', '1')

    def construct_yaml_int(self, node):
        value = self.construct_scalar(node)
        if isinstance(value, int) and not isinstance(value, bool):
            return value
        value = str(value).replace('_', '')
        sign = 1
        if value.startswith('-'):
            sign = -1
            value = value[1:]
        elif value.startswith('+'):
            value = value[1:]
        if value == '0':
            return 0
        elif value.startswith('0x') or value.startswith('0X'):
            return sign * int(value[2:], 16)
        elif value.startswith('0b') or value.startswith('0B'):
            return sign * int(value[2:], 2)
        elif value.startswith('0o') or value.startswith('0O'):
            return sign * int(value[2:], 8)
        elif value.startswith('0') and len(value) > 1:
            return sign * int(value, 8)
        elif ':' in value:
            # Sexagesimal
            digits = value.split(':')
            result = 0
            for digit in digits:
                result = result * 60 + int(digit)
            return sign * result
        return sign * int(value)

    def construct_yaml_float(self, node):
        value = self.construct_scalar(node)
        if isinstance(value, float):
            return value
        value = str(value).replace('_', '').lower()
        if value in ('.inf', '+.inf'):
            return float('inf')
        elif value == '-.inf':
            return float('-inf')
        elif value == '.nan':
            return float('nan')
        elif ':' in value:
            # Sexagesimal
            sign = 1.0
            if value.startswith('-'):
                sign = -1.0
                value = value[1:]
            elif value.startswith('+'):
                value = value[1:]
            digits = value.split(':')
            result = 0.0
            for digit in digits:
                result = result * 60.0 + float(digit)
            return sign * result
        return float(value)

    def construct_yaml_str(self, node):
        return self.construct_scalar(node)

    def construct_yaml_seq(self, node):
        data = []
        yield data
        data.extend(self.construct_sequence(node, deep=True))

    def construct_yaml_map(self, node):
        data = {}
        yield data
        value = self.construct_mapping(node, deep=True)
        data.update(value)


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


def _is_constructor_loader(Loader):
    """Check if Loader has a real construct_document method from an external hierarchy.

    This detects Loaders like AnsibleLoader that inherit from PyYAML's SafeConstructor
    and need proper Node trees with start_mark/end_mark for construction.
    Our own BaseLoader subclasses are NOT constructor loaders — they use the
    _convert_with_loader path which is faster (no Node tree conversion needed).
    """
    if not isinstance(Loader, type):
        # Not a class - could be a factory function
        return False
    # Skip our own loader classes — they work with _convert_with_loader
    if issubclass(Loader, BaseLoader):
        return False
    from libfyaml.pyyaml_compat.constructor import BaseConstructor as OurBaseConstructor
    # Check if the Loader inherits from our BaseConstructor (which has construct_document)
    # or has construct_document from PyYAML's SafeConstructor
    try:
        if issubclass(Loader, OurBaseConstructor):
            return True
    except TypeError:
        pass
    if hasattr(Loader, 'construct_document') and hasattr(Loader, 'construct_object'):
        return True
    return False


def _is_foreign_loader(Loader):
    """Check if Loader is a foreign (non-libfyaml) loader class.

    Returns True if the Loader has its own scanning/parsing that should
    be used instead of the C library fast path.  Foreign loaders include
    CanonicalLoader and any other custom loader not derived from our BaseLoader.
    """
    if Loader is None:
        return False
    if not isinstance(Loader, type):
        return False  # Factory function, not a class
    # Our known loaders use the C fast path
    try:
        if issubclass(Loader, BaseLoader):
            return False
    except TypeError:
        return False
    # Foreign loader - has its own scanner/parser
    return True


import codecs

def _decode_bytes_stream(stream):
    """Decode bytes stream to string, handling BOM markers.

    PyYAML supports UTF-8, UTF-16-BE, and UTF-16-LE with BOM markers.
    libfyaml only handles UTF-8, so we decode here.

    Returns:
        Decoded string

    Raises:
        YAMLError: If the encoding cannot be determined
    """
    if isinstance(stream, bytes):
        try:
            if stream.startswith(codecs.BOM_UTF16_BE):
                return stream[len(codecs.BOM_UTF16_BE):].decode('utf-16-be')
            elif stream.startswith(codecs.BOM_UTF16_LE):
                return stream[len(codecs.BOM_UTF16_LE):].decode('utf-16-le')
            elif stream.startswith(codecs.BOM_UTF8):
                return stream[len(codecs.BOM_UTF8):].decode('utf-8')
            else:
                return stream.decode('utf-8')
        except (UnicodeDecodeError, UnicodeError) as e:
            raise YAMLError("failed to decode stream: %s" % e)
    return stream


def _load_via_compose(stream, Loader, original_stream):
    """Load YAML via streaming compose path (handles self-referential aliases).

    When the C library can't resolve cyclic aliases, fall back to:
    _parse() → events → _events_to_node() → Node tree → construct
    """
    events = list(parse(stream))
    root_node = _events_to_node(events)
    if root_node is None:
        return None

    # Create loader and construct
    constructors = getattr(Loader, 'yaml_constructors', {})
    multi_constructors = getattr(Loader, 'yaml_multi_constructors', {})
    if not constructors and not multi_constructors:
        constructors = SafeLoader.yaml_constructors
        multi_constructors = SafeLoader.yaml_multi_constructors

    loader = _ConstructorProxy(Loader, original_stream)
    for tag, func in SafeLoader.yaml_constructors.items():
        if tag not in loader.yaml_constructors:
            loader.yaml_constructors[tag] = func
    return loader.construct_document(root_node)


def _load_all_via_compose(stream, Loader):
    """Load all YAML documents via streaming compose path (handles unhashable keys).

    When the C library can't handle unhashable mapping keys, fall back to:
    _parse() → events → _events_to_nodes() → Node trees → construct
    """
    events = list(parse(stream))
    constructors = getattr(Loader, 'yaml_constructors', {})
    multi_constructors = getattr(Loader, 'yaml_multi_constructors', {})
    if not constructors and not multi_constructors:
        constructors = SafeLoader.yaml_constructors
        multi_constructors = SafeLoader.yaml_multi_constructors

    loader = _ConstructorProxy(Loader, stream)
    for tag, func in SafeLoader.yaml_constructors.items():
        if tag not in loader.yaml_constructors:
            loader.yaml_constructors[tag] = func

    for root_node in _events_to_nodes(events):
        if root_node is not None:
            yield loader.construct_document(root_node)


def load(stream, Loader=None):
    """Parse YAML stream and return Python object.

    Args:
        stream: String or file-like object containing YAML
        Loader: Loader class (required - use SafeLoader, FullLoader, or UnsafeLoader)

    Returns:
        Python object (dict, list, str, int, float, bool, or None)

    Raises:
        TypeError: If Loader is not specified
        YAMLError: If parsing fails (ComposerError, ScannerError, ParserError, etc.)

    Example:
        >>> load("foo: bar", Loader=SafeLoader)
        {'foo': 'bar'}
    """
    if Loader is None:
        raise TypeError("load() missing 1 required positional argument: 'Loader'")
    # Foreign loaders (e.g. CanonicalLoader) have their own scanner/parser
    if _is_foreign_loader(Loader):
        loader = Loader(stream)
        try:
            return loader.get_single_data()
        finally:
            loader.dispose()
    # Get stream name for error messages
    stream_name = getattr(stream, 'name', '<string>')
    original_stream = stream
    if hasattr(stream, 'read'):
        stream = stream.read()

    # Decode bytes with BOM detection
    stream = _decode_bytes_stream(stream)

    # Handle edge cases that libfyaml doesn't parse directly
    result, handled = _normalize_yaml(stream)
    if handled:
        return result
    stream = result  # Use normalized text (e.g. with --- prepended for bare tagged block scalars)

    # Use yaml1.1 mode for PyYAML compatibility (merge keys, octal, etc.)
    # Use collect_diag=True to get error details for PyYAML-style exceptions
    # Use create_markers=True to provide start_mark/end_mark on nodes
    try:
        doc = fy.loads(stream, mode='yaml1.1-pyyaml', collect_diag=True, create_markers=True)
    except ValueError as e:
        # libfyaml throws ValueError for some parse errors without diagnostics
        # Convert to ParserError for PyYAML compatibility
        raise ParserError(problem=str(e))

    # Check if we got an error (collect_diag returns diag sequence on error)
    if doc.is_sequence():
        # This is a diagnostic sequence, not a document
        try:
            diag_list = doc.to_python()
        except TypeError:
            diag_list = None  # Not a diagnostic - contains unhashable types
        if diag_list and isinstance(diag_list, list) and len(diag_list) > 0:
            if isinstance(diag_list[0], dict) and 'message' in diag_list[0]:
                # Check if this is an alias resolution error — fall back to
                # streaming compose which handles self-referential aliases
                error_msg = diag_list[0].get('message', '')
                if 'alias' in error_msg.lower():
                    return _load_via_compose(stream, Loader, original_stream)
                raise _diag_to_exception(diag_list, stream_name)

    # Check for multiple documents (yaml.load expects exactly one)
    try:
        docs = list(fy.loads_all(stream, mode='yaml1.1-pyyaml'))
        if len(docs) > 1:
            raise ComposerError(
                context="expected a single document in the stream",
                problem="but found another document",
                problem_mark=Mark(stream_name, 0, 0, 0, None, None))
    except ValueError:
        pass  # Already handled above

    # For Loaders that have construct_document (like AnsibleLoader), use
    # the Node-tree path: convert FyGeneric → Node tree → feed to constructor
    if _is_constructor_loader(Loader):
        root_node = _fygeneric_to_node(doc, stream_name)
        # Instantiate the Loader (parser won't actually parse since we provide nodes)
        loader = Loader(original_stream)
        try:
            return loader.construct_document(root_node)
        finally:
            if hasattr(loader, 'dispose'):
                loader.dispose()
    elif callable(Loader) and not isinstance(Loader, type):
        # Factory function (e.g., annotatedyaml's lambda stream: loader(stream, secrets))
        # Create an instance and use it directly for construction
        loader = Loader(original_stream)
        if hasattr(loader, 'construct_document'):
            root_node = _fygeneric_to_node(doc, stream_name)
            try:
                return loader.construct_document(root_node)
            finally:
                if hasattr(loader, 'dispose'):
                    loader.dispose()
        # Use the loader instance's constructors if available
        loader_constructors = getattr(loader, 'yaml_constructors', {})
        loader_multi = getattr(loader, 'yaml_multi_constructors', {})
        if loader_constructors or loader_multi:
            # Use this loader directly — it has the right constructors registered
            try:
                return _convert_with_loader(doc, loader)
            except ConstructorError as exc:
                if 'unhashable' in str(exc):
                    return _load_via_compose(stream, Loader, original_stream)
                raise

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
        try:
            return _convert_with_loader(doc, loader)
        except ConstructorError as exc:
            if 'unhashable' in str(exc):
                return _load_via_compose(stream, Loader, original_stream)
            raise

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


def full_load(stream):
    """Parse YAML stream with FullLoader and return Python object.

    This is equivalent to load(stream, Loader=FullLoader).
    """
    return load(stream, FullLoader)


def full_load_all(stream):
    """Parse all YAML documents with FullLoader.

    This is equivalent to load_all(stream, Loader=FullLoader).
    """
    return load_all(stream, FullLoader)


def unsafe_load(stream):
    """Parse YAML stream with UnsafeLoader and return Python object.

    This is equivalent to load(stream, Loader=UnsafeLoader).
    """
    return load(stream, UnsafeLoader)


def unsafe_load_all(stream):
    """Parse all YAML documents with UnsafeLoader.

    This is equivalent to load_all(stream, Loader=UnsafeLoader).
    """
    return load_all(stream, UnsafeLoader)


def compose(stream, Loader=SafeLoader):
    """Parse YAML stream and return a Node tree.

    Args:
        stream: String or file-like object containing YAML
        Loader: Loader class

    Returns:
        A ScalarNode, SequenceNode, or MappingNode, or None for empty documents
    """
    if _is_foreign_loader(Loader):
        loader = Loader(stream)
        try:
            return loader.get_single_node()
        finally:
            loader.dispose()
    if hasattr(stream, 'read'):
        stream = stream.read()
    stream = _decode_bytes_stream(stream)

    result, handled = _normalize_yaml(stream)
    if handled:
        if result is None:
            return None
        stream = str(result)
    else:
        stream = result

    events = list(parse(stream))
    # Create resolver with Loader's path resolvers if any
    _path_resolver = None
    if Loader is not None and hasattr(Loader, 'yaml_path_resolvers') and Loader.yaml_path_resolvers:
        _path_resolver = resolver.Resolver()
        _path_resolver.yaml_path_resolvers = Loader.yaml_path_resolvers
    return _events_to_node(events, _path_resolver=_path_resolver)


def compose_all(stream, Loader=SafeLoader):
    """Parse all YAML documents and return Node trees.

    Args:
        stream: String or file-like object containing YAML
        Loader: Loader class

    Yields:
        Node trees, one per document
    """
    if _is_foreign_loader(Loader):
        loader = Loader(stream)
        try:
            while loader.check_node():
                yield loader.get_node()
        finally:
            loader.dispose()
        return
    if hasattr(stream, 'read'):
        stream = stream.read()
    stream = _decode_bytes_stream(stream)

    events = list(parse(stream))
    # Create resolver with Loader's path resolvers if any
    _path_resolver = None
    if Loader is not None and hasattr(Loader, 'yaml_path_resolvers') and Loader.yaml_path_resolvers:
        _path_resolver = resolver.Resolver()
        _path_resolver.yaml_path_resolvers = Loader.yaml_path_resolvers
    for node in _events_to_nodes(events, _path_resolver=_path_resolver):
        yield node


def load_all(stream, Loader=None):
    """Parse all YAML documents in stream.

    Args:
        stream: String or file-like object containing YAML
        Loader: Loader class (required - use SafeLoader, FullLoader, or UnsafeLoader)

    Yields:
        Python objects, one per YAML document

    Raises:
        TypeError: If Loader is not specified

    Example:
        >>> list(load_all("---\\nfoo: 1\\n---\\nbar: 2\\n", Loader=SafeLoader))
        [{'foo': 1}, {'bar': 2}]
    """
    if Loader is None:
        raise TypeError("load_all() missing 1 required positional argument: 'Loader'")
    # Foreign loaders (e.g. CanonicalLoader) have their own scanner/parser
    if _is_foreign_loader(Loader):
        loader = Loader(stream)
        try:
            while loader.check_data():
                yield loader.get_data()
        finally:
            loader.dispose()
        return
    if hasattr(stream, 'read'):
        stream = stream.read()
    stream = _decode_bytes_stream(stream)

    # Check for registered constructors on the Loader class
    constructors = getattr(Loader, 'yaml_constructors', {})
    multi_constructors = getattr(Loader, 'yaml_multi_constructors', {})

    # Always use SafeLoader's constructors as baseline for standard tags
    if not constructors and not multi_constructors:
        constructors = SafeLoader.yaml_constructors
        multi_constructors = SafeLoader.yaml_multi_constructors

    # Create a lightweight proxy with the constructors if needed
    loader = None
    if constructors or multi_constructors:
        loader = _ConstructorProxy(Loader, stream)
        # Ensure SafeLoader constructors are available as fallback
        if Loader is not SafeLoader:
            for tag, func in SafeLoader.yaml_constructors.items():
                if tag not in loader.yaml_constructors:
                    loader.yaml_constructors[tag] = func

    # Use yaml1.1 mode for PyYAML compatibility
    try:
        for doc in fy.loads_all(stream, mode='yaml1.1-pyyaml', create_markers=True):
            if loader:
                try:
                    yield _convert_with_loader(doc, loader)
                except ConstructorError as exc:
                    if 'unhashable' in str(exc):
                        # Fall back to streaming compose for unhashable keys
                        yield from _load_all_via_compose(stream, Loader)
                        return
                    raise
            else:
                yield doc.to_python()
    except ValueError as e:
        raise ParserError(problem=str(e))


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

        # Handle by explicit tag first
        if tag == 'tag:yaml.org,2002:null':
            return None
        elif tag == 'tag:yaml.org,2002:bool':
            return value.lower() in ('true', 'yes', 'on')
        elif tag == 'tag:yaml.org,2002:int':
            return int(value)
        elif tag == 'tag:yaml.org,2002:float':
            return float(value)
        elif tag == 'tag:yaml.org,2002:binary':
            return TaggedScalar(tag, value, node.style)
        elif tag == 'tag:yaml.org,2002:timestamp':
            # Emit as plain scalar (will be implicitly resolved on re-parse)
            return value
        elif tag and tag.startswith('!'):
            # Custom tag (shorthand) - preserve as TaggedScalar
            return TaggedScalar(tag, value, node.style)
        elif tag and tag not in ('tag:yaml.org,2002:str', None):
            # Non-standard full tag (e.g. !!python/name:...) - preserve
            return TaggedScalar(tag, value, node.style)
        # No explicit tag or !!str — use implicit value resolution
        elif value in ('null', 'Null', 'NULL', '~', ''):
            return None
        else:
            return value

    elif isinstance(node, MappingNode):
        result = {}
        for key_node, value_node in node.value:
            key = _node_to_python(key_node)
            value = _node_to_python(value_node)
            result[key] = value
        # Preserve tag for non-standard mappings (like !!set)
        tag = node.tag
        if tag and tag not in ('tag:yaml.org,2002:map', None):
            # Sort keys for consistent output (sets have arbitrary order)
            sorted_result = dict(sorted(result.items(), key=lambda x: str(x[0])))
            return fy.from_python(sorted_result, tag=tag)
        return result

    elif isinstance(node, SequenceNode):
        result = [_node_to_python(item) for item in node.value]
        # Preserve tag for non-standard sequences (like !!omap)
        tag = node.tag
        if tag and tag not in ('tag:yaml.org,2002:seq', None):
            return fy.from_python(result, tag=tag)
        return result

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

    # FyGeneric objects are already in the right format - don't process further
    if isinstance(data, fy.FyGeneric):
        return data

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
    # Check specific multi_representers FIRST to allow custom types like
    # AnsibleTaggedStr (which inherits from str) to be handled before plain str
    # Skip catch-all types (object) and collection types
    import collections.abc
    collection_types = (collections.abc.Mapping, collections.abc.Sequence,
                        collections.abc.Collection, collections.abc.Iterable)
    catchall_types = (object,)

    for base_type, representer in multi_representers.items():
        # Skip collection types and catch-all - check them later
        if base_type in collection_types or base_type in catchall_types:
            continue
        if base_type is not None and isinstance(data, base_type):
            result = representer(dumper, data)
            return _apply_representers(result, dumper)

    # Handle basic scalar types BEFORE collection multi_representers
    # This prevents str/bytes from matching Collection/Sequence/Iterable
    # Also handle datetime objects which shouldn't match Collection
    if isinstance(data, (str, bytes, int, float, bool, type(None), datetime.date, datetime.datetime)):
        return data

    # Recursively process containers BEFORE catch-all object representer
    if isinstance(data, dict):
        return {_apply_representers(k, dumper): _apply_representers(v, dumper)
                for k, v in data.items()}
    elif isinstance(data, (list, tuple)):
        return [_apply_representers(item, dumper) for item in data]

    # Now check collection multi_representers
    for base_type, representer in multi_representers.items():
        if base_type not in collection_types:
            continue
        if base_type is not None and isinstance(data, base_type):
            result = representer(dumper, data)
            return _apply_representers(result, dumper)

    # Check catch-all multi_representers (object)
    for base_type, representer in multi_representers.items():
        if base_type not in catchall_types:
            continue
        if base_type is not None and isinstance(data, base_type):
            result = representer(dumper, data)
            return _apply_representers(result, dumper)

    # Check for None type representer (catch-all)
    if None in representers:
        representer = representers[None]
        result = representer(dumper, data)
        return _apply_representers(result, dumper)

    # Return data unchanged
    return data


def _convert_tagged_values(data):
    """Convert TaggedScalar objects, bytes, and sets to tagged FyGeneric values.

    This function recursively walks the data structure and converts:
    - TaggedScalar objects to tagged FyGeneric using from_python(obj, tag=...)
    - bytes to tagged FyGeneric with !!binary tag
    - sets/frozensets to tagged FyGeneric with !!set tag

    The resulting structure can be passed to from_python(), which will
    recognize and internalize the FyGeneric objects.

    Args:
        data: Python object, possibly containing TaggedScalar, bytes, or sets

    Returns:
        Data with TaggedScalars/bytes/sets replaced by tagged FyGeneric values
    """
    # FyGeneric objects are already in the right format - don't process
    if isinstance(data, fy.FyGeneric):
        return data

    if isinstance(data, TaggedScalar):
        # Convert TaggedScalar to tagged FyGeneric, preserving style
        return fy.from_python(data.value, tag=data.tag, style=data.style)
    elif isinstance(data, bytes):
        # Convert bytes to base64 and create tagged FyGeneric with !!binary tag
        encoded = base64.standard_b64encode(data).decode('ascii')
        return fy.from_python(encoded, tag='tag:yaml.org,2002:binary')
    elif isinstance(data, (set, frozenset)):
        # Convert set to mapping with null values and !!set tag
        set_mapping = {_convert_tagged_values(item): None for item in data}
        return fy.from_python(set_mapping, tag='tag:yaml.org,2002:set')
    elif isinstance(data, str):
        # Check if this string would be misinterpreted as a type that changes
        # the value's meaning (int, float, bool, null). Only these need quoting.
        # Timestamps and other string-compatible resolved types are fine plain.
        from libfyaml.pyyaml_compat.resolver import Resolver as _Resolver
        _NEEDS_QUOTING = {
            'tag:yaml.org,2002:int', 'tag:yaml.org,2002:float',
            'tag:yaml.org,2002:bool', 'tag:yaml.org,2002:null',
        }
        resolved = _Resolver().resolve(ScalarNode, data, (True, False))
        if resolved in _NEEDS_QUOTING:
            return fy.from_python(data, style="'")
        return data
    elif isinstance(data, dict):
        return {_convert_tagged_values(k): _convert_tagged_values(v) for k, v in data.items()}
    elif isinstance(data, (list, tuple)):
        return [_convert_tagged_values(item) for item in data]
    else:
        return data


def _sort_keys_recursive(data, sort_keys):
    """Recursively sort dictionary keys if sort_keys is True.

    Args:
        data: Python object to process
        sort_keys: Whether to sort dictionary keys

    Returns:
        Data with sorted keys (if sort_keys is True)
    """
    if not sort_keys:
        return data

    # FyGeneric objects are already in the right format - don't process
    if isinstance(data, fy.FyGeneric):
        return data

    if isinstance(data, dict):
        # Sort keys and recursively process values
        sorted_items = sorted(data.items(), key=lambda x: (
            # Sort by type first (strings before booleans), then by value
            (0, str(x[0])) if isinstance(x[0], str) else
            (1, str(x[0])) if isinstance(x[0], bool) else
            (2, str(x[0]))
        ))
        return {k: _sort_keys_recursive(v, sort_keys) for k, v in sorted_items}
    elif isinstance(data, (list, tuple)):
        return [_sort_keys_recursive(item, sort_keys) for item in data]
    else:
        return data


def dump(data, stream=None, Dumper=None, default_flow_style=False,
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
    if Dumper is None:
        Dumper = _default_Dumper
    # Apply representers to transform custom types
    # Handle both classes and factory functions (like AnsibleDumper)
    dumper = None
    if callable(Dumper) and not isinstance(Dumper, type):
        # Factory function - try calling with no args first (some factory functions
        # like AnsibleDumper create dumper instances that don't take the standard params)
        try:
            dumper = Dumper()
        except TypeError:
            # Try with standard params
            try:
                dumper = Dumper(stream, default_style=default_style,
                               default_flow_style=default_flow_style,
                               canonical=canonical, indent=indent, width=width,
                               allow_unicode=allow_unicode, line_break=line_break,
                               encoding=encoding, explicit_start=explicit_start,
                               explicit_end=explicit_end, version=version, tags=tags,
                               sort_keys=sort_keys)
            except TypeError:
                dumper = None
        representers = getattr(dumper, 'yaml_representers', {}) if dumper else {}
        multi_representers = getattr(dumper, 'yaml_multi_representers', {}) if dumper else {}
    else:
        representers = getattr(Dumper, 'yaml_representers', {})
        multi_representers = getattr(Dumper, 'yaml_multi_representers', {})

    if representers or multi_representers:
        # Create dumper instance if not already created (for class-based Dumpers)
        if dumper is None:
            dumper = Dumper(stream, default_style=default_style,
                           default_flow_style=default_flow_style,
                           canonical=canonical, indent=indent, width=width,
                           allow_unicode=allow_unicode, line_break=line_break,
                           encoding=encoding, explicit_start=explicit_start,
                           explicit_end=explicit_end, version=version, tags=tags,
                           sort_keys=sort_keys)

        # When Dumper has an `object` multi-representer (full Representer), use
        # Node → Events → emit path. This handles complex representations like
        # !!python/object/new: that _apply_representers can't convert to plain dicts.
        _use_node_path = object in multi_representers
        if _use_node_path:
            try:
                node = dumper.represent_data(data)
                if isinstance(node, (ScalarNode, SequenceNode, MappingNode)):
                    emit_kwargs = {}
                    emit_kwargs['canonical'] = canonical or False
                    if indent is not None:
                        emit_kwargs['indent'] = indent
                    if width is not None:
                        emit_kwargs['width'] = width
                    emit_kwargs['allow_unicode'] = allow_unicode if allow_unicode is not None else True
                    events = list(_node_to_events(node, explicit_start=explicit_start))
                    try:
                        result = fy._emit([_event_to_tuple(e) for e in events], **emit_kwargs)
                    except (RuntimeError, ValueError) as exc:
                        raise EmitterError(str(exc)) from exc
                    if explicit_end:
                        result = result + '...\n'
                    if encoding is not None:
                        encoded = result.encode(encoding)
                        if encoding in ('utf-16-be', 'utf-16-le', 'utf-16'):
                            bom = codecs.BOM_UTF16_BE if encoding == 'utf-16-be' else codecs.BOM_UTF16_LE
                            encoded = bom + encoded
                        if stream is not None:
                            try:
                                stream.write(encoded)
                            except TypeError:
                                stream.write(result)
                            return None
                        return encoded
                    if stream is not None:
                        stream.write(result)
                        return None
                    return result
            except (AttributeError, TypeError):
                pass  # Fall through to _apply_representers path

        data = _apply_representers(data, dumper)

    # Sort dictionary keys if requested
    data = _sort_keys_recursive(data, sort_keys)

    # Convert TaggedScalars and bytes to tagged FyGeneric values
    data = _convert_tagged_values(data)

    # Convert Python object to FyGeneric and use libfyaml
    # from_python() will internalize any embedded FyGeneric objects
    try:
        doc = fy.from_python(data)
    except (ValueError, TypeError) as e:
        from libfyaml.pyyaml_compat.representer import RepresenterError
        raise RepresenterError("cannot represent %r: %s" % (type(data).__name__, e))

    # Determine style based on default_flow_style parameter
    # PyYAML's default_flow_style:
    #   - None: let the library decide (block style)
    #   - True: force flow style (oneline for compact output)
    #   - False: force block style
    if default_flow_style is True:
        style = 'oneline'
    else:
        style = 'block'

    # Pass indent if specified (default is 2 in libfyaml)
    try:
        result = fy.dumps(doc, style=style, indent=indent if indent else 0)
    except ValueError as e:
        from libfyaml.pyyaml_compat.representer import RepresenterError
        raise RepresenterError(str(e))

    # Handle explicit_end: append "...\n" if requested
    if explicit_end:
        result = result + '...\n'

    # Handle encoding: when encoding is specified, return bytes with BOM for UTF-16
    if encoding is not None:
        encoded = result.encode(encoding)
        # Add BOM for UTF-16 encodings (PyYAML behavior)
        if encoding in ('utf-16-be', 'utf-16-le', 'utf-16'):
            bom = codecs.BOM_UTF16_BE if encoding == 'utf-16-be' else codecs.BOM_UTF16_LE
            encoded = bom + encoded
        if stream is not None:
            # Write bytes if stream accepts bytes, otherwise write string
            try:
                stream.write(encoded)
            except TypeError:
                stream.write(result)
            return None
        return encoded

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


def dump_all(documents, stream=None, Dumper=None, default_flow_style=False,
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
    if Dumper is None:
        Dumper = _default_Dumper
    # Apply representers to transform custom types
    # Handle both classes and factory functions (like AnsibleDumper)
    dumper = None
    if callable(Dumper) and not isinstance(Dumper, type):
        # Factory function - try calling with no args first (some factory functions
        # like AnsibleDumper create dumper instances that don't take the standard params)
        try:
            dumper = Dumper()
        except TypeError:
            # Try with standard params
            try:
                dumper = Dumper(stream, default_style=default_style,
                               default_flow_style=default_flow_style,
                               canonical=canonical, indent=indent, width=width,
                               allow_unicode=allow_unicode, line_break=line_break,
                               encoding=encoding, explicit_start=explicit_start,
                               explicit_end=explicit_end, version=version, tags=tags,
                               sort_keys=sort_keys)
            except TypeError:
                dumper = None
        representers = getattr(dumper, 'yaml_representers', {}) if dumper else {}
        multi_representers = getattr(dumper, 'yaml_multi_representers', {}) if dumper else {}
    else:
        representers = getattr(Dumper, 'yaml_representers', {})
        multi_representers = getattr(Dumper, 'yaml_multi_representers', {})

    if representers or multi_representers:
        # Create dumper instance if not already created (for class-based Dumpers)
        if dumper is None:
            dumper = Dumper(stream, default_style=default_style,
                           default_flow_style=default_flow_style,
                           canonical=canonical, indent=indent, width=width,
                           allow_unicode=allow_unicode, line_break=line_break,
                           encoding=encoding, explicit_start=explicit_start,
                           explicit_end=explicit_end, version=version, tags=tags,
                           sort_keys=sort_keys)
        documents = [_apply_representers(data, dumper) for data in documents]

    # Sort dictionary keys if requested
    documents = [_sort_keys_recursive(data, sort_keys) for data in documents]

    # Convert TaggedScalars and bytes to tagged FyGeneric values
    documents = [_convert_tagged_values(data) for data in documents]

    # Convert all documents to FyGeneric
    docs = [fy.from_python(data) for data in documents]

    # Determine style based on default_flow_style parameter
    if default_flow_style is True:
        style = 'oneline'
    else:
        style = 'block'

    # Use module-level dumps_all function
    result = fy.dumps_all(docs, style=style)

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


# ============================================================================
# Streaming API: parse(), scan(), emit(), serialize(), serialize_all()
# ============================================================================

def _mk(mark_tuple):
    """Convert a (line, col, index) tuple from C to a Mark object, or None."""
    if mark_tuple is None:
        return None
    line, col, index = mark_tuple
    return Mark('<unicode string>', index, line, col, None, None)


def _tuple_to_event(t):
    """Convert an event tuple from C _parse() to a PyYAML Event object."""
    etype = t[0]
    if etype == 1:  # STREAM_START
        return StreamStartEvent(start_mark=_mk(t[1]), end_mark=_mk(t[2]))
    elif etype == 2:  # STREAM_END
        return StreamEndEvent(start_mark=_mk(t[1]), end_mark=_mk(t[2]))
    elif etype == 3:  # DOCUMENT_START
        # (3, implicit, version, tags, sm, em)
        implicit = t[1]
        version = t[2]
        tags = t[3]
        return DocumentStartEvent(
            start_mark=_mk(t[4]), end_mark=_mk(t[5]),
            explicit=not implicit,
            version=version,
            tags=tags)
    elif etype == 4:  # DOCUMENT_END
        # (4, implicit, sm, em)
        implicit = t[1]
        return DocumentEndEvent(
            start_mark=_mk(t[2]), end_mark=_mk(t[3]),
            explicit=not implicit)
    elif etype == 5:  # MAPPING_START
        # (5, anchor, tag, implicit, flow_style, sm, em)
        return MappingStartEvent(
            anchor=t[1], tag=t[2], implicit=t[3],
            start_mark=_mk(t[5]), end_mark=_mk(t[6]),
            flow_style=t[4])
    elif etype == 6:  # MAPPING_END
        return MappingEndEvent(start_mark=_mk(t[1]), end_mark=_mk(t[2]))
    elif etype == 7:  # SEQUENCE_START
        # (7, anchor, tag, implicit, flow_style, sm, em)
        return SequenceStartEvent(
            anchor=t[1], tag=t[2], implicit=t[3],
            start_mark=_mk(t[5]), end_mark=_mk(t[6]),
            flow_style=t[4])
    elif etype == 8:  # SEQUENCE_END
        return SequenceEndEvent(start_mark=_mk(t[1]), end_mark=_mk(t[2]))
    elif etype == 9:  # SCALAR
        # (9, anchor, tag, implicit_tuple, value, style, sm, em)
        style = t[5]
        return ScalarEvent(
            anchor=t[1], tag=t[2], implicit=t[3],
            value=t[4],
            start_mark=_mk(t[6]), end_mark=_mk(t[7]),
            style=style)
    elif etype == 10:  # ALIAS
        # (10, anchor, sm, em)
        return AliasEvent(
            anchor=t[1],
            start_mark=_mk(t[2]), end_mark=_mk(t[3]))
    else:
        raise ValueError("Unknown event type: %d" % etype)


def _tuple_to_token(t):
    """Convert a token tuple from C _scan() to a PyYAML Token object."""
    ttype = t[0]
    if ttype == 1:  # STREAM_START
        # (1, encoding, sm, em)
        return StreamStartToken(start_mark=_mk(t[2]), end_mark=_mk(t[3]),
                                encoding=t[1])
    elif ttype == 2:  # STREAM_END
        return StreamEndToken(_mk(t[1]), _mk(t[2]))
    elif ttype == 3:  # VERSION_DIRECTIVE
        # (3, ("YAML", (major, minor)), sm, em)
        return DirectiveToken(name=t[1][0], value=t[1][1],
                              start_mark=_mk(t[2]), end_mark=_mk(t[3]))
    elif ttype == 4:  # TAG_DIRECTIVE
        # (4, ("TAG", (handle, prefix)), sm, em)
        return DirectiveToken(name=t[1][0], value=t[1][1],
                              start_mark=_mk(t[2]), end_mark=_mk(t[3]))
    elif ttype == 5:  # DOCUMENT_START
        return DocumentStartToken(_mk(t[1]), _mk(t[2]))
    elif ttype == 6:  # DOCUMENT_END
        return DocumentEndToken(_mk(t[1]), _mk(t[2]))
    elif ttype == 7:  # BLOCK_SEQUENCE_START
        return BlockSequenceStartToken(_mk(t[1]), _mk(t[2]))
    elif ttype == 8:  # BLOCK_MAPPING_START
        return BlockMappingStartToken(_mk(t[1]), _mk(t[2]))
    elif ttype == 9:  # BLOCK_END
        return BlockEndToken(_mk(t[1]), _mk(t[2]))
    elif ttype == 10:  # FLOW_SEQUENCE_START
        return FlowSequenceStartToken(_mk(t[1]), _mk(t[2]))
    elif ttype == 11:  # FLOW_SEQUENCE_END
        return FlowSequenceEndToken(_mk(t[1]), _mk(t[2]))
    elif ttype == 12:  # FLOW_MAPPING_START
        return FlowMappingStartToken(_mk(t[1]), _mk(t[2]))
    elif ttype == 13:  # FLOW_MAPPING_END
        return FlowMappingEndToken(_mk(t[1]), _mk(t[2]))
    elif ttype == 14:  # BLOCK_ENTRY
        return BlockEntryToken(_mk(t[1]), _mk(t[2]))
    elif ttype == 15:  # FLOW_ENTRY
        return FlowEntryToken(_mk(t[1]), _mk(t[2]))
    elif ttype == 16:  # KEY
        return KeyToken(_mk(t[1]), _mk(t[2]))
    elif ttype == 17:  # VALUE
        return ValueToken(_mk(t[1]), _mk(t[2]))
    elif ttype == 18:  # ALIAS
        # (18, value, sm, em)
        return AliasToken(value=t[1], start_mark=_mk(t[2]), end_mark=_mk(t[3]))
    elif ttype == 19:  # ANCHOR
        # (19, value, sm, em)
        return AnchorToken(value=t[1], start_mark=_mk(t[2]), end_mark=_mk(t[3]))
    elif ttype == 20:  # TAG
        # (20, (handle, suffix), sm, em)
        return TagToken(value=t[1], start_mark=_mk(t[2]), end_mark=_mk(t[3]))
    elif ttype == 21:  # SCALAR
        # (21, value, plain, style, sm, em)
        return ScalarToken(value=t[1], plain=t[2],
                           start_mark=_mk(t[4]), end_mark=_mk(t[5]),
                           style=t[3])
    else:
        raise ValueError("Unknown token type: %d" % ttype)


def _event_to_tuple(event):
    """Convert a PyYAML Event object to a tuple for C _emit()."""
    if isinstance(event, StreamStartEvent):
        return (1,)
    elif isinstance(event, StreamEndEvent):
        return (2,)
    elif isinstance(event, DocumentStartEvent):
        implicit = not event.explicit if event.explicit is not None else True
        version = getattr(event, 'version', None)
        tags = getattr(event, 'tags', None)
        return (3, implicit, version, tags, None, None)
    elif isinstance(event, DocumentEndEvent):
        implicit = not event.explicit if event.explicit is not None else True
        return (4, implicit, None, None)
    elif isinstance(event, MappingStartEvent):
        return (5, event.anchor, event.tag, event.implicit,
                event.flow_style, None, None)
    elif isinstance(event, MappingEndEvent):
        return (6, None, None)
    elif isinstance(event, SequenceStartEvent):
        return (7, event.anchor, event.tag, event.implicit,
                event.flow_style, None, None)
    elif isinstance(event, SequenceEndEvent):
        return (8, None, None)
    elif isinstance(event, ScalarEvent):
        return (9, event.anchor, event.tag, event.implicit,
                event.value, event.style, None, None)
    elif isinstance(event, AliasEvent):
        return (10, event.anchor, None, None)
    else:
        raise ValueError("Unknown event type: %s" % type(event).__name__)


def _resolve_tag_handle(tag, tag_handles):
    """Resolve a tag handle like '!yaml!str' to its full form using tag directives.

    Args:
        tag: Tag string that may contain an unresolved handle
        tag_handles: Dict mapping handles to prefixes (from DocumentStartEvent)

    Returns:
        Resolved tag string
    """
    if tag is None or not tag_handles:
        return tag
    # Already resolved (starts with a known URI scheme)
    if tag.startswith('tag:') or tag.startswith('http:') or tag.startswith('https:'):
        return tag
    # Try to match tag handles (longest first for specificity)
    for handle in sorted(tag_handles.keys(), key=len, reverse=True):
        if not handle:
            continue
        if tag.startswith(handle):
            prefix = tag_handles[handle]
            suffix = tag[len(handle):]
            return prefix + suffix
    # Single '!' prefix (primary tag handle)
    if tag.startswith('!') and '!' in tag_handles:
        # Only for local tags like '!foo' where '!' maps to a prefix
        handle = '!'
        prefix = tag_handles[handle]
        if prefix != '!':
            suffix = tag[1:]
            return prefix + suffix
    return tag


def parse(data, Loader=None):
    """Parse YAML stream and yield Event objects."""
    if _is_foreign_loader(Loader):
        loader = Loader(data)
        try:
            while loader.check_event():
                yield loader.get_event()
        finally:
            loader.dispose()
        return
    if hasattr(data, 'read'):
        data = data.read()
    data = _decode_bytes_stream(data)

    tag_handles = {}
    events = fy._parse(data, mode='yaml1.1-pyyaml')
    for ev in events:
        event = _tuple_to_event(ev)
        # Track tag directives from DocumentStartEvent
        if isinstance(event, DocumentStartEvent) and event.tags:
            tag_handles = dict(event.tags) if isinstance(event.tags, dict) else {}
        elif isinstance(event, DocumentEndEvent):
            tag_handles = {}
        # Resolve tag handles in events that carry tags
        if tag_handles and hasattr(event, 'tag') and event.tag is not None:
            event.tag = _resolve_tag_handle(event.tag, tag_handles)
        yield event


def scan(data, Loader=None):
    """Scan YAML stream and yield Token objects."""
    if _is_foreign_loader(Loader):
        loader = Loader(data)
        try:
            while loader.check_token():
                yield loader.get_token()
        finally:
            loader.dispose()
        return
    if hasattr(data, 'read'):
        data = data.read()
    data = _decode_bytes_stream(data)

    tokens = fy._scan(data, mode='yaml1.1-pyyaml')
    for tok in tokens:
        yield _tuple_to_token(tok)


def _validate_emit_events(events):
    """Validate event sequence before emitting, matching PyYAML's emitter checks."""
    state = 'expect_stream_start'
    depth = 0  # nesting depth for sequences/mappings
    has_root_node = False
    for event in events:
        etype = type(event).__name__
        if state == 'expect_stream_start':
            if etype != 'StreamStartEvent':
                raise EmitterError("expected StreamStartEvent, but got %s" % etype)
            state = 'expect_document_or_stream_end'
        elif state == 'expect_nothing':
            raise EmitterError("expected nothing, but got %s" % etype)
        elif state == 'expect_document_or_stream_end':
            if etype == 'StreamEndEvent':
                state = 'expect_nothing'
            elif etype == 'DocumentStartEvent':
                # Validate version
                version = getattr(event, 'version', None)
                if version is not None:
                    major, minor = version
                    if major != 1:
                        raise EmitterError(
                            "unsupported YAML version: %d.%d" % (major, minor))
                # Validate tag handles
                tags = getattr(event, 'tags', None)
                if tags:
                    for handle, prefix in tags.items():
                        # Skip default tag entries added by C library parser
                        if handle == '' and prefix == '':
                            continue
                        if not handle:
                            raise EmitterError("tag handle must not be empty")
                        if not prefix:
                            raise EmitterError("tag prefix must not be empty")
                        if handle != '!' and handle != '!!':
                            if not (handle.startswith('!') and handle.endswith('!') and
                                    len(handle) >= 3 and
                                    all(c.isalnum() or c == '-' for c in handle[1:-1])):
                                raise EmitterError(
                                    "tag handle must be '!', '!!' or '!word!': %r" % handle)
                state = 'in_document'
                depth = 0
                has_root_node = False
            else:
                raise EmitterError("expected DocumentStartEvent, but got %s" % etype)
        elif state == 'in_document':
            if etype == 'DocumentEndEvent':
                if depth > 0:
                    raise EmitterError("unexpected DocumentEndEvent inside collection")
                if not has_root_node:
                    raise EmitterError("expected a node, but got DocumentEndEvent")
                state = 'expect_document_or_stream_end'
            elif etype in ('ScalarEvent', 'AliasEvent'):
                has_root_node = True
                _validate_emit_node_event(event)
            elif etype in ('SequenceStartEvent', 'MappingStartEvent'):
                has_root_node = True
                depth += 1
            elif etype in ('SequenceEndEvent', 'MappingEndEvent'):
                depth -= 1
            else:
                raise EmitterError("unexpected %s in document" % etype)


def _validate_emit_node_event(event):
    """Validate a single node event (scalar or alias)."""
    etype = type(event).__name__
    anchor = getattr(event, 'anchor', None)
    if etype == 'AliasEvent':
        if not anchor:
            raise EmitterError("anchor is not specified for alias")
    elif etype == 'ScalarEvent':
        tag = getattr(event, 'tag', None)
        implicit = getattr(event, 'implicit', (True, True))
        if tag is not None and tag == '':
            raise EmitterError("tag must not be empty")
        if not implicit[0] and not implicit[1] and tag is None:
            raise EmitterError("tag is not specified")


def emit(events, stream=None, Dumper=None,
         canonical=None, indent=None, width=None,
         allow_unicode=None, line_break=None):
    """Emit Event objects as a YAML stream."""
    events_list = list(events)
    _validate_emit_events(events_list)
    events_data = [_event_to_tuple(e) for e in events_list]
    kwargs = {}
    kwargs['canonical'] = canonical or False
    if indent is not None:
        kwargs['indent'] = indent
    if width is not None:
        kwargs['width'] = width
    kwargs['allow_unicode'] = allow_unicode if allow_unicode is not None else True
    if line_break is not None:
        kwargs['line_break'] = line_break
    try:
        result = fy._emit(events_data, **kwargs)
    except (RuntimeError, ValueError) as exc:
        raise EmitterError(str(exc)) from exc
    if stream is not None:
        # Check if stream expects bytes (BytesIO, binary file, etc.)
        # If so, encode the result using the encoding from StreamStartEvent
        encoding = None
        if events_list and hasattr(events_list[0], 'encoding'):
            encoding = events_list[0].encoding
        if encoding:
            stream.write(result.encode(encoding))
        else:
            try:
                stream.write(result)
            except TypeError:
                # Stream expects bytes but no encoding specified — use utf-8
                stream.write(result.encode('utf-8'))
        return None
    return result


_serialize_resolver = resolver.Resolver()

def _anchor_node_pass(node, anchors, counter):
    """Pre-pass to detect nodes appearing more than once and assign anchors."""
    node_id = id(node)
    if node_id in anchors:
        if anchors[node_id] is None:
            counter[0] += 1
            anchors[node_id] = 'id%03d' % counter[0]
        return
    anchors[node_id] = None
    if isinstance(node, SequenceNode):
        for item in node.value:
            _anchor_node_pass(item, anchors, counter)
    elif isinstance(node, MappingNode):
        for key_node, val_node in node.value:
            _anchor_node_pass(key_node, anchors, counter)
            _anchor_node_pass(val_node, anchors, counter)


def _serialize_node_events(node, anchors, serialized):
    """Recursively yield events from a Node tree, handling anchors and aliases."""
    node_id = id(node)
    anchor = anchors.get(node_id)
    if node_id in serialized:
        yield AliasEvent(anchor=anchor)
        return
    serialized.add(node_id)
    tag = getattr(node, 'tag', None)
    if isinstance(node, ScalarNode):
        resolved_tag = _serialize_resolver.resolve(ScalarNode, node.value, (True, False))
        implicit = (tag == resolved_tag, tag is None)
        style = getattr(node, 'style', None)
        # When tag is !!str but the value looks like int/float/bool/null,
        # use quoting instead of an explicit tag (matches PyYAML behavior).
        # Only quote for types that would change the value's meaning on
        # re-parse; timestamps and other string-compatible types are fine plain.
        _NEEDS_QUOTING_TAGS = {
            'tag:yaml.org,2002:int', 'tag:yaml.org,2002:float',
            'tag:yaml.org,2002:bool', 'tag:yaml.org,2002:null',
        }
        if (tag == 'tag:yaml.org,2002:str' and resolved_tag in _NEEDS_QUOTING_TAGS
                and style is None):
            style = "'"
            implicit = (True, True)
        # Suppress tag when implicit to avoid verbose output from C emitter
        emit_tag = None if implicit[0] else tag
        yield ScalarEvent(anchor=anchor, tag=emit_tag, implicit=implicit,
                          value=node.value, style=style)
    elif isinstance(node, SequenceNode):
        implicit = tag is None or tag == 'tag:yaml.org,2002:seq'
        emit_tag = None if implicit else tag
        flow_style = getattr(node, 'flow_style', None)
        yield SequenceStartEvent(anchor=anchor, tag=emit_tag, implicit=implicit,
                                 flow_style=flow_style)
        for item in node.value:
            yield from _serialize_node_events(item, anchors, serialized)
        yield SequenceEndEvent()
    elif isinstance(node, MappingNode):
        implicit = tag is None or tag == 'tag:yaml.org,2002:map'
        emit_tag = None if implicit else tag
        flow_style = getattr(node, 'flow_style', None)
        yield MappingStartEvent(anchor=anchor, tag=emit_tag, implicit=implicit,
                                flow_style=flow_style)
        for key_node, val_node in node.value:
            yield from _serialize_node_events(key_node, anchors, serialized)
            yield from _serialize_node_events(val_node, anchors, serialized)
        yield MappingEndEvent()


def _node_to_events(node, stream_name='<unicode string>', explicit_start=None):
    """Walk a Node tree and yield Event objects, handling anchors/aliases."""
    anchors = {}
    counter = [0]
    _anchor_node_pass(node, anchors, counter)
    # Use explicit document start only when anchors exist (so aliases work)
    has_anchors = any(v is not None for v in anchors.values())
    explicit = explicit_start if explicit_start is not None else has_anchors
    serialized = set()
    yield StreamStartEvent()
    yield DocumentStartEvent(explicit=explicit)
    yield from _serialize_node_events(node, anchors, serialized)
    yield DocumentEndEvent(explicit=False)
    yield StreamEndEvent()


def _node_to_events_inner(node, _path_resolver=None, _parent=None, _index=None):
    """Recursively yield events from a Node tree (simple, no alias handling)."""
    if _path_resolver:
        _path_resolver.descend_resolver(_parent, _index)
    anchor = getattr(node, 'anchor', None)
    tag = getattr(node, 'tag', None)
    if isinstance(node, ScalarNode):
        r = _path_resolver if _path_resolver else _serialize_resolver
        resolved_tag = r.resolve(ScalarNode, node.value, (True, False))
        implicit = (tag == resolved_tag, tag is None)
        style = getattr(node, 'style', None)
        # When tag is !!str but the value looks like int/float/bool/null,
        # use quoting instead of an explicit tag (matches PyYAML behavior)
        _NEEDS_QUOTING_TAGS = {
            'tag:yaml.org,2002:int', 'tag:yaml.org,2002:float',
            'tag:yaml.org,2002:bool', 'tag:yaml.org,2002:null',
        }
        if (tag == 'tag:yaml.org,2002:str' and resolved_tag in _NEEDS_QUOTING_TAGS
                and style is None):
            style = "'"
            implicit = (True, True)
            tag = resolved_tag
        # Suppress tag when implicit to avoid verbose output from C emitter
        emit_tag = None if implicit[0] else tag
        yield ScalarEvent(anchor=anchor, tag=emit_tag, implicit=implicit,
                          value=node.value, style=style)
    elif isinstance(node, SequenceNode):
        if _path_resolver:
            resolved_tag = _path_resolver.resolve(SequenceNode, None, (True, True))
            implicit = (tag == resolved_tag)
        else:
            implicit = tag is None or tag == 'tag:yaml.org,2002:seq'
        flow_style = getattr(node, 'flow_style', None)
        emit_tag = None if implicit else tag
        yield SequenceStartEvent(anchor=anchor, tag=emit_tag, implicit=implicit,
                                 flow_style=flow_style)
        for idx, item in enumerate(node.value):
            yield from _node_to_events_inner(item, _path_resolver, node, idx)
        yield SequenceEndEvent()
    elif isinstance(node, MappingNode):
        if _path_resolver:
            resolved_tag = _path_resolver.resolve(MappingNode, None, (True, True))
            implicit = (tag == resolved_tag)
        else:
            implicit = tag is None or tag == 'tag:yaml.org,2002:map'
        flow_style = getattr(node, 'flow_style', None)
        emit_tag = None if implicit else tag
        yield MappingStartEvent(anchor=anchor, tag=emit_tag, implicit=implicit,
                                flow_style=flow_style)
        for key_node, val_node in node.value:
            yield from _node_to_events_inner(key_node, _path_resolver, node, None)
            yield from _node_to_events_inner(val_node, _path_resolver, node, key_node)
        yield MappingEndEvent()
    if _path_resolver:
        _path_resolver.ascend_resolver()


def serialize(node, stream=None, Dumper=None, **kwargs):
    """Serialize a Node tree to YAML."""
    return emit(_node_to_events(node), stream=stream, **kwargs)


def serialize_all(nodes, stream=None, Dumper=None, **kwargs):
    """Serialize multiple Node trees to YAML."""
    # Create path resolver from Dumper if available
    _path_resolver = None
    if Dumper is not None and hasattr(Dumper, 'yaml_path_resolvers') and Dumper.yaml_path_resolvers:
        _path_resolver = resolver.Resolver()
        _path_resolver.yaml_path_resolvers = Dumper.yaml_path_resolvers
    def _gen():
        yield StreamStartEvent()
        for node in nodes:
            yield DocumentStartEvent(explicit=True)
            yield from _node_to_events_inner(node, _path_resolver)
            yield DocumentEndEvent(explicit=False)
        yield StreamEndEvent()
    return emit(_gen(), stream=stream, **kwargs)


def _events_to_node(events, _path_resolver=None):
    """Build a Node tree from a list of Event objects.

    Handles anchors and aliases. Returns the root Node for the first document.
    """
    anchors = {}
    idx = [0]  # mutable index for closure
    _resolver = _path_resolver if _path_resolver is not None else resolver.Resolver()

    def _next():
        if idx[0] < len(events):
            e = events[idx[0]]
            idx[0] += 1
            return e
        return None

    def _resolve_scalar_tag(ev):
        tag = ev.tag
        if tag is None or tag == '!':
            # When tag is '!' (non-specific tag), PyYAML treats it as
            # plain-implicit for resolver purposes (type resolution applies)
            if tag == '!':
                implicit = (True, False)
            else:
                implicit = ev.implicit if ev.implicit else (True, False)
            tag = _resolver.resolve(ScalarNode, ev.value, implicit)
        return tag

    def _resolve_seq_tag(ev):
        tag = ev.tag
        if tag is None or tag == '!':
            tag = _resolver.resolve(SequenceNode, None, (True, True))
        return tag or 'tag:yaml.org,2002:seq'

    def _resolve_map_tag(ev):
        tag = ev.tag
        if tag is None or tag == '!':
            tag = _resolver.resolve(MappingNode, None, (True, True))
        return tag or 'tag:yaml.org,2002:map'

    def _build_node():
        while True:
            ev = _next()
            if ev is None:
                return None
            if isinstance(ev, (StreamStartEvent, StreamEndEvent)):
                continue
            if isinstance(ev, DocumentStartEvent):
                continue
            if isinstance(ev, DocumentEndEvent):
                continue
            return _compose_node(None, None, ev)

    def _compose_node(parent, index, ev=None):
        if ev is None:
            ev = _next()
            if ev is None:
                return None
        if isinstance(ev, AliasEvent):
            if ev.anchor in anchors:
                return anchors[ev.anchor]
            return ScalarNode('tag:yaml.org,2002:null', '', start_mark=ev.start_mark)
        _resolver.descend_resolver(parent, index)
        node = _build_from_event(ev)
        _resolver.ascend_resolver()
        return node

    def _build_from_event(ev):
        if isinstance(ev, ScalarEvent):
            tag = _resolve_scalar_tag(ev)
            node = ScalarNode(
                tag=tag,
                value=ev.value,
                start_mark=ev.start_mark,
                end_mark=ev.end_mark,
                style=ev.style)
            if ev.anchor:
                anchors[ev.anchor] = node
            return node
        elif isinstance(ev, SequenceStartEvent):
            tag = _resolve_seq_tag(ev)
            node = SequenceNode(
                tag=tag,
                value=[],
                start_mark=ev.start_mark,
                end_mark=ev.end_mark,
                flow_style=ev.flow_style)
            if ev.anchor:
                anchors[ev.anchor] = node
            item_index = 0
            while True:
                next_ev = _next()
                if next_ev is None or isinstance(next_ev, SequenceEndEvent):
                    if next_ev:
                        node.end_mark = next_ev.end_mark
                    break
                item = _compose_node(node, item_index, next_ev)
                if item is not None:
                    node.value.append(item)
                item_index += 1
            return node
        elif isinstance(ev, MappingStartEvent):
            tag = _resolve_map_tag(ev)
            node = MappingNode(
                tag=tag,
                value=[],
                start_mark=ev.start_mark,
                end_mark=ev.end_mark,
                flow_style=ev.flow_style)
            if ev.anchor:
                anchors[ev.anchor] = node
            while True:
                key_ev = _next()
                if key_ev is None or isinstance(key_ev, MappingEndEvent):
                    if key_ev:
                        node.end_mark = key_ev.end_mark
                    break
                key_node = _compose_node(node, None, key_ev)
                val_node = _compose_node(node, key_node)
                if key_node is not None and val_node is not None:
                    node.value.append((key_node, val_node))
            return node
        return None

    return _build_node()


def _events_to_nodes(events, _path_resolver=None):
    """Build multiple Node trees from events (one per document)."""
    anchors = {}
    idx = [0]
    _resolver = _path_resolver if _path_resolver is not None else resolver.Resolver()

    def _next():
        if idx[0] < len(events):
            e = events[idx[0]]
            idx[0] += 1
            return e
        return None

    def _resolve_scalar_tag(ev):
        tag = ev.tag
        if tag is None or tag == '!':
            if tag == '!':
                implicit = (True, False)
            else:
                implicit = ev.implicit if ev.implicit else (True, False)
            tag = _resolver.resolve(ScalarNode, ev.value, implicit)
        return tag

    def _resolve_seq_tag(ev):
        tag = ev.tag
        if tag is None or tag == '!':
            tag = _resolver.resolve(SequenceNode, None, (True, True))
        return tag or 'tag:yaml.org,2002:seq'

    def _resolve_map_tag(ev):
        tag = ev.tag
        if tag is None or tag == '!':
            tag = _resolver.resolve(MappingNode, None, (True, True))
        return tag or 'tag:yaml.org,2002:map'

    def _compose_node(parent, index, ev=None):
        if ev is None:
            ev = _next()
            if ev is None:
                return None
        if isinstance(ev, AliasEvent):
            if ev.anchor in anchors:
                return anchors[ev.anchor]
            return ScalarNode('tag:yaml.org,2002:null', '', start_mark=ev.start_mark)
        _resolver.descend_resolver(parent, index)
        node = _build_from_event(ev)
        _resolver.ascend_resolver()
        return node

    def _build_from_event(ev):
        if isinstance(ev, ScalarEvent):
            tag = _resolve_scalar_tag(ev)
            node = ScalarNode(
                tag=tag,
                value=ev.value,
                start_mark=ev.start_mark,
                end_mark=ev.end_mark,
                style=ev.style)
            if ev.anchor:
                anchors[ev.anchor] = node
            return node
        elif isinstance(ev, SequenceStartEvent):
            tag = _resolve_seq_tag(ev)
            node = SequenceNode(
                tag=tag,
                value=[],
                start_mark=ev.start_mark,
                end_mark=ev.end_mark,
                flow_style=ev.flow_style)
            if ev.anchor:
                anchors[ev.anchor] = node
            item_index = 0
            while True:
                next_ev = _next()
                if next_ev is None or isinstance(next_ev, SequenceEndEvent):
                    if next_ev:
                        node.end_mark = next_ev.end_mark
                    break
                item = _compose_node(node, item_index, next_ev)
                if item is not None:
                    node.value.append(item)
                item_index += 1
            return node
        elif isinstance(ev, MappingStartEvent):
            tag = _resolve_map_tag(ev)
            node = MappingNode(
                tag=tag,
                value=[],
                start_mark=ev.start_mark,
                end_mark=ev.end_mark,
                flow_style=ev.flow_style)
            if ev.anchor:
                anchors[ev.anchor] = node
            while True:
                key_ev = _next()
                if key_ev is None or isinstance(key_ev, MappingEndEvent):
                    if key_ev:
                        node.end_mark = key_ev.end_mark
                    break
                key_node = _compose_node(node, None, key_ev)
                val_node = _compose_node(node, key_node)
                if key_node is not None and val_node is not None:
                    node.value.append((key_node, val_node))
            return node
        return None

    while idx[0] < len(events):
        ev = _next()
        if ev is None:
            break
        if isinstance(ev, (StreamStartEvent, StreamEndEvent)):
            continue
        if isinstance(ev, DocumentStartEvent):
            # Build one document
            anchors.clear()
            root_ev = _next()
            if root_ev is None or isinstance(root_ev, DocumentEndEvent):
                yield ScalarNode('tag:yaml.org,2002:null', '')
                continue
            node = _compose_node(None, None, root_ev)
            # Consume until DocumentEndEvent
            while idx[0] < len(events):
                ev2 = events[idx[0]]
                if isinstance(ev2, DocumentEndEvent):
                    idx[0] += 1
                    break
                elif isinstance(ev2, (StreamEndEvent,)):
                    break
                idx[0] += 1
            if node is not None:
                yield node


# Additional compatibility - these are less commonly used but may be needed

class YAMLLoadWarning(UserWarning):
    """Warning for unsafe YAML loading."""
    pass


# Reader error and other less common errors
ReaderError = MarkedYAMLError
# ConstructorError is imported from constructor.py (line ~299) - must be a real subclass
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
