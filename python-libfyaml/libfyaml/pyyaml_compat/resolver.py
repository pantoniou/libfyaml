"""PyYAML resolver module compatibility stubs."""

import re

from libfyaml.pyyaml_compat.nodes import ScalarNode, SequenceNode, MappingNode


class BaseResolver:
    """Base YAML tag resolver."""
    yaml_implicit_resolvers = {}
    yaml_path_resolvers = {}

    DEFAULT_SCALAR_TAG = 'tag:yaml.org,2002:str'
    DEFAULT_SEQUENCE_TAG = 'tag:yaml.org,2002:seq'
    DEFAULT_MAPPING_TAG = 'tag:yaml.org,2002:map'

    def __init__(self):
        pass

    @classmethod
    def add_implicit_resolver(cls, tag, regexp, first):
        """Add an implicit resolver."""
        if 'yaml_implicit_resolvers' not in cls.__dict__:
            cls.yaml_implicit_resolvers = cls.yaml_implicit_resolvers.copy()
        if first is None:
            first = [None]
        for ch in first:
            cls.yaml_implicit_resolvers.setdefault(ch, []).append((tag, regexp))

    @classmethod
    def add_path_resolver(cls, tag, path, kind=None):
        """Add a path resolver."""
        pass

    def resolve(self, kind, value, implicit):
        """Resolve a tag for a node based on its kind and value."""
        if kind is ScalarNode and implicit[0]:
            if value is None or value == '' or value == 'null' or value == '~':
                return 'tag:yaml.org,2002:null'
            # Check implicit resolvers
            for ch in [value[0] if value else None, None]:
                if ch in self.yaml_implicit_resolvers:
                    for tag, regexp in self.yaml_implicit_resolvers[ch]:
                        if regexp.match(value):
                            return tag
            return self.DEFAULT_SCALAR_TAG
        elif kind is SequenceNode:
            return self.DEFAULT_SEQUENCE_TAG
        elif kind is MappingNode:
            return self.DEFAULT_MAPPING_TAG
        return self.DEFAULT_SCALAR_TAG


class Resolver(BaseResolver):
    """Standard YAML resolver with implicit resolvers for common types."""
    yaml_implicit_resolvers = {}


# Register standard YAML 1.1 implicit resolvers
Resolver.add_implicit_resolver(
    'tag:yaml.org,2002:bool',
    re.compile(r'^(?:yes|Yes|YES|no|No|NO|true|True|TRUE|false|False|FALSE|on|On|ON|off|Off|OFF)$'),
    list('yYnNtTfFoO'))

Resolver.add_implicit_resolver(
    'tag:yaml.org,2002:int',
    re.compile(r'^(?:[-+]?0b[0-1_]+|[-+]?0[0-7_]+|[-+]?(?:0|[1-9][0-9_]*)|[-+]?0x[0-9a-fA-F_]+)$'),
    list('-+0123456789'))

Resolver.add_implicit_resolver(
    'tag:yaml.org,2002:float',
    re.compile(r'^(?:[-+]?(?:[0-9][0-9_]*)\.[0-9_]*(?:[eE][-+]?[0-9]+)?|\.[0-9_]+(?:[eE][-+]?[0-9]+)?|[-+]?\.(?:inf|Inf|INF)|\.(?:nan|NaN|NAN))$'),
    list('-+0123456789.'))

Resolver.add_implicit_resolver(
    'tag:yaml.org,2002:null',
    re.compile(r'^(?:~|null|Null|NULL|)$'),
    ['~', 'n', 'N', ''])

Resolver.add_implicit_resolver(
    'tag:yaml.org,2002:timestamp',
    re.compile(r'^(?:[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]|[0-9][0-9][0-9][0-9]-[0-9][0-9]?-[0-9][0-9]?(?:[Tt]|[ \t]+)[0-9][0-9]?:[0-9][0-9]:[0-9][0-9](?:\.[0-9]*)?(?:[ \t]*(?:Z|[-+][0-9][0-9]?(?::[0-9][0-9])?))?)$'),
    list('0123456789'))
