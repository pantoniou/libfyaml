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
            # Empty string resolves to null (PyYAML behavior)
            if value == '':
                return 'tag:yaml.org,2002:null'

            # Use the C library to resolve scalar types - it handles all YAML 1.1
            # scalar resolution (bool, int, float, null) consistently
            import libfyaml as fy
            try:
                result = fy.loads(value, mode='yaml1.1-pyyaml')
                py_value = result.to_python()

                if py_value is None:
                    return 'tag:yaml.org,2002:null'
                elif isinstance(py_value, bool):
                    return 'tag:yaml.org,2002:bool'
                elif isinstance(py_value, int):
                    return 'tag:yaml.org,2002:int'
                elif isinstance(py_value, float):
                    return 'tag:yaml.org,2002:float'
                # If C library returns string, fall through to check Python-side
                # implicit resolvers (e.g., timestamp which needs Python datetime)
            except Exception:
                pass  # Fall through to check implicit resolvers

            # Check Python-side implicit resolvers (e.g., timestamp)
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


# Note: bool, int, float, and null resolution is handled by the C library's
# yaml1.1-pyyaml mode. Only timestamp resolution is needed here since the
# C library returns timestamps as strings (Python datetime conversion is
# language-specific).
Resolver.add_implicit_resolver(
    'tag:yaml.org,2002:timestamp',
    re.compile(r'^(?:[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]|[0-9][0-9][0-9][0-9]-[0-9][0-9]?-[0-9][0-9]?(?:[Tt]|[ \t]+)[0-9][0-9]?:[0-9][0-9]:[0-9][0-9](?:\.[0-9]*)?(?:[ \t]*(?:Z|[-+][0-9][0-9]?(?::[0-9][0-9])?))?)$'),
    list('0123456789'))
