"""PyYAML constructor module compatibility.

Provides a working SafeConstructor that properly handles Node trees,
including generator-based construction for mappings/sequences.
"""

import types

from libfyaml.pyyaml_compat.error import MarkedYAMLError
from libfyaml.pyyaml_compat.nodes import (
    Node,
    ScalarNode,
    SequenceNode,
    MappingNode,
    CollectionNode,
)


class ConstructorError(MarkedYAMLError):
    """YAML constructor error."""
    pass


class BaseConstructor:
    """Base class for YAML constructors with full construct_object support."""

    yaml_constructors = {}
    yaml_multi_constructors = {}

    def __init__(self):
        self.constructed_objects = {}
        self.recursive_objects = {}
        self.state_generators = []
        self.deep_construct = False

    def check_data(self):
        """Check if there is more data to construct."""
        return self.check_node()

    def get_data(self):
        """Construct and return the next document."""
        if self.check_node():
            return self.construct_document(self.get_node())

    def get_single_data(self):
        """Construct and return a single document."""
        node = self.get_single_node()
        if node is not None:
            return self.construct_document(node)
        return None

    def construct_document(self, node):
        """Construct a Python object from a root node."""
        data = self.construct_object(node, deep=True)
        # Run any pending generators (for two-phase construction)
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

    def construct_object(self, node, deep=False):
        """Construct a Python object from a node, dispatching by tag."""
        if node in self.constructed_objects:
            return self.constructed_objects[node]
        if deep:
            old_deep = self.deep_construct
            self.deep_construct = True
        if node in self.recursive_objects:
            raise ConstructorError(None, None,
                "found unconstructable recursive node", node.start_mark)
        self.recursive_objects[node] = None
        constructor = None
        tag_suffix = None
        if node.tag in self.yaml_constructors:
            constructor = self.yaml_constructors[node.tag]
        else:
            for tag_prefix in self.yaml_multi_constructors:
                if tag_prefix is not None and node.tag and node.tag.startswith(tag_prefix):
                    tag_suffix = node.tag[len(tag_prefix):]
                    constructor = self.yaml_multi_constructors[tag_prefix]
                    break
            else:
                if None in self.yaml_multi_constructors:
                    tag_suffix = node.tag
                    constructor = self.yaml_multi_constructors[None]
                elif None in self.yaml_constructors:
                    constructor = self.yaml_constructors[None]
                elif isinstance(node, ScalarNode):
                    constructor = self.__class__.construct_scalar
                elif isinstance(node, SequenceNode):
                    constructor = self.__class__.construct_sequence
                elif isinstance(node, MappingNode):
                    constructor = self.__class__.construct_mapping
        if constructor is None:
            # Fallback
            if isinstance(node, ScalarNode):
                data = node.value
            elif isinstance(node, SequenceNode):
                data = [self.construct_object(child, deep=deep) for child in (node.value or [])]
            elif isinstance(node, MappingNode):
                data = {}
                for key_node, value_node in (node.value or []):
                    key = self.construct_object(key_node, deep=deep)
                    value = self.construct_object(value_node, deep=deep)
                    data[key] = value
            else:
                data = node.value
        elif tag_suffix is not None:
            data = constructor(self, tag_suffix, node)
        else:
            data = constructor(self, node)
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

    def construct_scalar(self, node):
        """Construct a scalar value from a node."""
        if isinstance(node, ScalarNode):
            return node.value
        if node is None:
            return ''
        return str(node.value) if hasattr(node, 'value') else str(node)

    def construct_sequence(self, node, deep=False):
        """Construct a sequence from a node."""
        if isinstance(node, SequenceNode) and node.value:
            return [self.construct_object(child, deep=deep) for child in node.value]
        return []

    def construct_mapping(self, node, deep=False):
        """Construct a mapping from a node."""
        if not isinstance(node, MappingNode):
            raise ConstructorError(
                None, None,
                "expected a mapping node, but found %s" % node.id,
                node.start_mark,
            )
        if node.value:
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
        return {}

    def construct_pairs(self, node, deep=False):
        """Construct a list of (key, value) pairs from a mapping node."""
        if isinstance(node, MappingNode) and node.value:
            pairs = []
            for key_node, value_node in node.value:
                key = self.construct_object(key_node, deep=deep)
                value = self.construct_object(value_node, deep=deep)
                pairs.append((key, value))
            return pairs
        return []

    @classmethod
    def add_constructor(cls, tag, constructor):
        """Add a constructor for a specific tag."""
        if 'yaml_constructors' not in cls.__dict__:
            cls.yaml_constructors = cls.yaml_constructors.copy()
        cls.yaml_constructors[tag] = constructor

    @classmethod
    def add_multi_constructor(cls, tag_prefix, multi_constructor):
        """Add a multi-constructor for a tag prefix."""
        if 'yaml_multi_constructors' not in cls.__dict__:
            cls.yaml_multi_constructors = cls.yaml_multi_constructors.copy()
        cls.yaml_multi_constructors[tag_prefix] = multi_constructor


class SafeConstructor(BaseConstructor):
    """Safe constructor that doesn't allow arbitrary Python objects."""

    yaml_constructors = {}
    yaml_multi_constructors = {}

    def construct_yaml_null(self, node):
        return None

    def construct_yaml_bool(self, node):
        value = self.construct_scalar(node)
        if value.lower() in ('true', 'yes', 'on'):
            return True
        return False

    def construct_yaml_int(self, node):
        value = self.construct_scalar(node)
        # Handle various integer formats
        value = value.replace('_', '')
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
            # YAML 1.1 octal
            return sign * int(value, 8)
        else:
            return sign * int(value)

    def construct_yaml_float(self, node):
        value = self.construct_scalar(node)
        value = value.replace('_', '').lower()
        if value in ('.inf', '+.inf'):
            return float('inf')
        elif value == '-.inf':
            return float('-inf')
        elif value == '.nan':
            return float('nan')
        return float(value)

    def construct_yaml_str(self, node):
        return self.construct_scalar(node)

    def construct_yaml_seq(self, node):
        if not isinstance(node, SequenceNode):
            raise ConstructorError(
                None, None,
                "expected a sequence node, but found %s" % node.id,
                node.start_mark,
            )
        data = []
        yield data
        data.extend(self.construct_sequence(node, deep=True))

    def construct_yaml_map(self, node):
        if not isinstance(node, MappingNode):
            raise ConstructorError(
                None, None,
                "expected a mapping node, but found %s" % node.id,
                node.start_mark,
            )
        data = {}
        yield data
        value = self.construct_mapping(node, deep=True)
        data.update(value)

    def construct_yaml_set(self, node):
        data = set()
        yield data
        value = self.construct_mapping(node)
        data.update(value.keys())

    def construct_yaml_binary(self, node):
        import base64
        value = self.construct_scalar(node)
        if isinstance(value, str):
            value = value.replace('\n', '').replace('\r', '').replace(' ', '')
            return base64.b64decode(value)
        return value

    def construct_yaml_timestamp(self, node):
        import datetime
        import re
        value = self.construct_scalar(node)
        # Try date first
        match = re.match(r'^(\d{4})-(\d{1,2})-(\d{1,2})$', value)
        if match:
            return datetime.date(int(match.group(1)), int(match.group(2)), int(match.group(3)))
        # Try datetime
        match = re.match(
            r'^(\d{4})-(\d{1,2})-(\d{1,2})'
            r'[Tt\s]+(\d{1,2}):(\d{2}):(\d{2})'
            r'(?:\.(\d+))?'
            r'(?:\s*(Z|[-+]\d{1,2}(?::\d{2})?))?$', value)
        if match:
            year, month, day = int(match.group(1)), int(match.group(2)), int(match.group(3))
            hour, minute, second = int(match.group(4)), int(match.group(5)), int(match.group(6))
            fraction = 0
            if match.group(7):
                fraction_s = match.group(7)[:6].ljust(6, '0')
                fraction = int(fraction_s)
            tz = None
            if match.group(8):
                if match.group(8) == 'Z':
                    tz = datetime.timezone.utc
                else:
                    tz_str = match.group(8)
                    tz_sign = -1 if tz_str[0] == '-' else 1
                    parts = tz_str[1:].split(':')
                    tz_hour = int(parts[0])
                    tz_min = int(parts[1]) if len(parts) > 1 else 0
                    tz = datetime.timezone(datetime.timedelta(
                        hours=tz_sign * tz_hour, minutes=tz_sign * tz_min))
            return datetime.datetime(year, month, day, hour, minute, second, fraction, tz)
        return value

    def construct_yaml_omap(self, node):
        omap = []
        yield omap
        if isinstance(node, SequenceNode):
            for subnode in node.value:
                if isinstance(subnode, MappingNode):
                    for key_node, value_node in subnode.value:
                        key = self.construct_object(key_node)
                        value = self.construct_object(value_node)
                        omap.append((key, value))

    def construct_yaml_pairs(self, node):
        pairs = []
        yield pairs
        if isinstance(node, SequenceNode):
            for subnode in node.value:
                if isinstance(subnode, MappingNode):
                    for key_node, value_node in subnode.value:
                        key = self.construct_object(key_node)
                        value = self.construct_object(value_node)
                        pairs.append((key, value))

    def construct_undefined(self, node):
        raise ConstructorError(None, None,
            "could not determine a constructor for the tag %r" % node.tag,
            node.start_mark)


# Register default constructors
SafeConstructor.add_constructor('tag:yaml.org,2002:null', SafeConstructor.construct_yaml_null)
SafeConstructor.add_constructor('tag:yaml.org,2002:bool', SafeConstructor.construct_yaml_bool)
SafeConstructor.add_constructor('tag:yaml.org,2002:int', SafeConstructor.construct_yaml_int)
SafeConstructor.add_constructor('tag:yaml.org,2002:float', SafeConstructor.construct_yaml_float)
SafeConstructor.add_constructor('tag:yaml.org,2002:str', SafeConstructor.construct_yaml_str)
SafeConstructor.add_constructor('tag:yaml.org,2002:seq', SafeConstructor.construct_yaml_seq)
SafeConstructor.add_constructor('tag:yaml.org,2002:map', SafeConstructor.construct_yaml_map)
SafeConstructor.add_constructor('tag:yaml.org,2002:set', SafeConstructor.construct_yaml_set)
SafeConstructor.add_constructor('tag:yaml.org,2002:binary', SafeConstructor.construct_yaml_binary)
SafeConstructor.add_constructor('tag:yaml.org,2002:timestamp', SafeConstructor.construct_yaml_timestamp)
SafeConstructor.add_constructor('tag:yaml.org,2002:omap', SafeConstructor.construct_yaml_omap)
SafeConstructor.add_constructor('tag:yaml.org,2002:pairs', SafeConstructor.construct_yaml_pairs)


class Constructor(SafeConstructor):
    """Full constructor (same as SafeConstructor in our implementation)."""
    yaml_constructors = SafeConstructor.yaml_constructors.copy()
    yaml_multi_constructors = {}
