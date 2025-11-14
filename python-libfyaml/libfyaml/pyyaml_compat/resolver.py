"""PyYAML resolver module compatibility stubs."""

import re

from libfyaml.pyyaml_compat.error import YAMLError
from libfyaml.pyyaml_compat.nodes import ScalarNode, SequenceNode, MappingNode


class ResolverError(YAMLError):
    pass


class BaseResolver:
    """Base YAML tag resolver."""
    yaml_implicit_resolvers = {}
    yaml_path_resolvers = {}

    DEFAULT_SCALAR_TAG = 'tag:yaml.org,2002:str'
    DEFAULT_SEQUENCE_TAG = 'tag:yaml.org,2002:seq'
    DEFAULT_MAPPING_TAG = 'tag:yaml.org,2002:map'

    def __init__(self):
        self.resolver_exact_paths = []
        self.resolver_prefix_paths = []

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
        if 'yaml_path_resolvers' not in cls.__dict__:
            cls.yaml_path_resolvers = cls.yaml_path_resolvers.copy()
        new_path = []
        for element in path:
            if isinstance(element, (list, tuple)):
                if len(element) == 2:
                    node_check, index_check = element
                elif len(element) == 1:
                    node_check = element[0]
                    index_check = True
                else:
                    raise ResolverError("Invalid path element: %s" % element)
            else:
                node_check = None
                index_check = element
            if node_check is str:
                node_check = ScalarNode
            elif node_check is list:
                node_check = SequenceNode
            elif node_check is dict:
                node_check = MappingNode
            elif node_check not in [ScalarNode, SequenceNode, MappingNode] \
                    and not isinstance(node_check, str) \
                    and node_check is not None:
                raise ResolverError("Invalid node checker: %s" % node_check)
            if not isinstance(index_check, (str, int)) \
                    and index_check is not None:
                raise ResolverError("Invalid index checker: %s" % index_check)
            new_path.append((node_check, index_check))
        if kind is str:
            kind = ScalarNode
        elif kind is list:
            kind = SequenceNode
        elif kind is dict:
            kind = MappingNode
        elif kind not in [ScalarNode, SequenceNode, MappingNode] \
                and kind is not None:
            raise ResolverError("Invalid node kind: %s" % kind)
        cls.yaml_path_resolvers[tuple(new_path), kind] = tag

    def descend_resolver(self, current_node, current_index):
        if not self.yaml_path_resolvers:
            return
        exact_paths = {}
        prefix_paths = []
        if current_node:
            depth = len(self.resolver_prefix_paths)
            for path, kind in self.resolver_prefix_paths[-1]:
                if self.check_resolver_prefix(depth, path, kind,
                        current_node, current_index):
                    if len(path) > depth:
                        prefix_paths.append((path, kind))
                    else:
                        exact_paths[kind] = self.yaml_path_resolvers[path, kind]
        else:
            for path, kind in self.yaml_path_resolvers:
                if not path:
                    exact_paths[kind] = self.yaml_path_resolvers[path, kind]
                else:
                    prefix_paths.append((path, kind))
        self.resolver_exact_paths.append(exact_paths)
        self.resolver_prefix_paths.append(prefix_paths)

    def ascend_resolver(self):
        if not self.yaml_path_resolvers:
            return
        self.resolver_exact_paths.pop()
        self.resolver_prefix_paths.pop()

    def check_resolver_prefix(self, depth, path, kind,
            current_node, current_index):
        node_check, index_check = path[depth-1]
        if isinstance(node_check, str):
            if current_node.tag != node_check:
                return
        elif node_check is not None:
            if not isinstance(current_node, node_check):
                return
        if index_check is True and current_index is not None:
            return
        if (index_check is False or index_check is None) \
                and current_index is None:
            return
        if isinstance(index_check, str):
            if not (isinstance(current_index, ScalarNode)
                    and index_check == current_index.value):
                return
        elif isinstance(index_check, int) and not isinstance(index_check, bool):
            if index_check != current_index:
                return
        return True

    def resolve(self, kind, value, implicit):
        """Resolve a tag for a node based on its kind and value."""
        # Check path resolvers first
        if self.yaml_path_resolvers:
            exact_paths = self.resolver_exact_paths[-1] if self.resolver_exact_paths else {}
            if kind in exact_paths:
                return exact_paths[kind]
            if None in exact_paths:
                return exact_paths[None]

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
                    # C library treats '.' as float 0.0, but PyYAML treats
                    # bare '.' as a string
                    stripped = value.strip()
                    if stripped == '.':
                        pass  # Fall through to string
                    else:
                        return 'tag:yaml.org,2002:float'
                # If C library returns string, fall through to check Python-side
                # implicit resolvers (e.g., timestamp which needs Python datetime)
            except (ValueError, TypeError):
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
