"""PyYAML cyaml module compatibility stubs.

Provides C-accelerated parser/emitter stubs.
"""

from libfyaml.pyyaml_compat.parser import Parser


class CParser(Parser):
    """C-accelerated YAML parser stub."""
    pass


class CEmitter:
    """C-accelerated YAML emitter stub."""
    pass


# Import the actual loader/dumper classes from the main module
# These are delayed imports to avoid circular import issues
def _get_base_classes():
    """Get base classes from main module, handling circular imports."""
    import libfyaml.pyyaml_compat as _compat
    return _compat


# C-accelerated Loader classes
# These must be actual classes that inherit from the main implementations
# so that code using yaml.cyaml.CSafeDumper gets the full API

class CBaseLoader:
    """C-accelerated base loader."""
    yaml_constructors = {}
    yaml_multi_constructors = {}

    def __init__(self, stream=None):
        self._stream = stream

    @classmethod
    def add_constructor(cls, tag, constructor_func):
        if 'yaml_constructors' not in cls.__dict__:
            cls.yaml_constructors = cls.yaml_constructors.copy()
        cls.yaml_constructors[tag] = constructor_func

    @classmethod
    def add_multi_constructor(cls, tag_prefix, multi_constructor):
        if 'yaml_multi_constructors' not in cls.__dict__:
            cls.yaml_multi_constructors = cls.yaml_multi_constructors.copy()
        cls.yaml_multi_constructors[tag_prefix] = multi_constructor


class CSafeLoader(CBaseLoader):
    """C-accelerated safe loader."""
    yaml_constructors = {}
    yaml_multi_constructors = {}


class CFullLoader(CSafeLoader):
    """C-accelerated full loader."""
    yaml_constructors = {}
    yaml_multi_constructors = {}


class CUnsafeLoader(CSafeLoader):
    """C-accelerated unsafe loader."""
    yaml_constructors = {}
    yaml_multi_constructors = {}


class CLoader(CSafeLoader):
    """C-accelerated full loader."""
    yaml_constructors = {}
    yaml_multi_constructors = {}


# C-accelerated Dumper classes with full API implementation
from libfyaml.pyyaml_compat.nodes import ScalarNode, MappingNode, SequenceNode
import datetime
import base64
from collections import OrderedDict


# Representer functions for standard types
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
    """Represent bytes as base64-encoded binary with literal block style."""
    encoded = base64.standard_b64encode(data).decode('ascii')
    return ScalarNode('tag:yaml.org,2002:binary', encoded, style='|')


def _represent_set(dumper, data):
    """Represent a set as a tagged mapping with null values."""
    set_mapping = {item: None for item in data}
    return dumper.represent_mapping('tag:yaml.org,2002:set', set_mapping.items())


def _represent_ordereddict(dumper, data):
    """Represent an OrderedDict as a regular mapping."""
    return dict(data)


class CBaseDumper:
    """C-accelerated base dumper with full representer API."""
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
        if 'yaml_representers' not in cls.__dict__:
            cls.yaml_representers = cls.yaml_representers.copy()
        cls.yaml_representers[data_type] = representer_func

    @classmethod
    def add_multi_representer(cls, data_type, representer_func):
        """Add a multi-representer for a type and its subclasses."""
        if 'yaml_multi_representers' not in cls.__dict__:
            cls.yaml_multi_representers = cls.yaml_multi_representers.copy()
        cls.yaml_multi_representers[data_type] = representer_func

    def represent_data(self, data):
        """Represent Python data using registered representers.

        This is the main entry point for representation. It checks for
        registered representers and dispatches appropriately.
        """
        data_type = type(data)

        # Check for exact type match first
        if data_type in self.yaml_representers:
            representer = self.yaml_representers[data_type]
            return representer(self, data)

        # Handle basic scalar types before checking multi_representers
        if data is None:
            return ScalarNode('tag:yaml.org,2002:null', 'null')
        elif data_type is bool:
            return ScalarNode('tag:yaml.org,2002:bool', 'true' if data else 'false')
        elif data_type is int:
            return ScalarNode('tag:yaml.org,2002:int', str(data))
        elif data_type is float:
            if data != data:  # NaN
                return ScalarNode('tag:yaml.org,2002:float', '.nan')
            elif data == float('inf'):
                return ScalarNode('tag:yaml.org,2002:float', '.inf')
            elif data == float('-inf'):
                return ScalarNode('tag:yaml.org,2002:float', '-.inf')
            return ScalarNode('tag:yaml.org,2002:float', repr(data))
        elif data_type is str:
            return ScalarNode('tag:yaml.org,2002:str', data)

        # Check for multi-representers (type hierarchy)
        for base_type, representer in self.yaml_multi_representers.items():
            if base_type is not None and isinstance(data, base_type):
                return representer(self, data)

        # Check for None type representer (catch-all)
        if None in self.yaml_representers:
            representer = self.yaml_representers[None]
            return representer(self, data)

        # Default: represent as mapping or sequence if possible
        if isinstance(data, dict):
            return self.represent_dict(data)
        elif isinstance(data, (list, tuple)):
            return self.represent_list(data)

        # Return data unchanged
        return data

    def represent_mapping(self, tag, mapping):
        """Represent a mapping as a MappingNode."""
        value = []
        for key, val in mapping:
            key_node = self.represent_data(key)
            if not isinstance(key_node, (ScalarNode, MappingNode, SequenceNode)):
                key_node = ScalarNode('tag:yaml.org,2002:str', str(key))
            val_node = self.represent_data(val)
            if not isinstance(val_node, (ScalarNode, MappingNode, SequenceNode)):
                val_node = ScalarNode('tag:yaml.org,2002:str', str(val))
            value.append((key_node, val_node))
        return MappingNode(tag, value)

    def represent_sequence(self, tag, sequence):
        """Represent a sequence as a SequenceNode."""
        value = []
        for item in sequence:
            node = self.represent_data(item)
            if not isinstance(node, (ScalarNode, MappingNode, SequenceNode)):
                node = ScalarNode('tag:yaml.org,2002:str', str(item))
            value.append(node)
        return SequenceNode(tag, value)

    def represent_scalar(self, tag, value, style=None):
        """Represent a scalar as a ScalarNode."""
        return ScalarNode(tag, value, style=style)

    def represent_dict(self, data):
        """Represent a dict as a mapping."""
        return self.represent_mapping('tag:yaml.org,2002:map', data.items())

    def represent_list(self, data):
        """Represent a list/sequence."""
        return self.represent_sequence('tag:yaml.org,2002:seq', data)

    def represent_str(self, data):
        """Represent a string."""
        return self.represent_scalar('tag:yaml.org,2002:str', data)


class CSafeDumper(CBaseDumper):
    """C-accelerated safe dumper with standard type representers."""
    yaml_representers = {
        datetime.datetime: _represent_datetime,
        datetime.date: _represent_date,
        bytes: _represent_bytes,
        set: _represent_set,
        frozenset: _represent_set,
        OrderedDict: _represent_ordereddict,
    }
    yaml_multi_representers = {}


class CDumper(CSafeDumper):
    """C-accelerated full dumper."""
    yaml_representers = CSafeDumper.yaml_representers.copy()
    yaml_multi_representers = {}
