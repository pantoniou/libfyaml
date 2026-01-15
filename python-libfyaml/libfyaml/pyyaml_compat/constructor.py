"""PyYAML constructor module compatibility stubs.

Provides minimal compatibility for code that imports yaml.constructor.
"""


class ConstructorError(Exception):
    """YAML constructor error."""
    pass


from libfyaml.pyyaml_compat.nodes import (
    Node,
    ScalarNode,
    SequenceNode,
    MappingNode,
    CollectionNode,
)


class BaseConstructor:
    """Base class for YAML constructors."""

    yaml_constructors = {}
    yaml_multi_constructors = {}

    def __init__(self):
        self.constructed_objects = {}
        self.recursive_objects = {}

    def construct_scalar(self, node):
        """Construct a scalar value from a node."""
        if isinstance(node, ScalarNode):
            return node.value
        return str(node.value) if node else ''

    def construct_sequence(self, node, deep=False):
        """Construct a sequence from a node."""
        if isinstance(node, SequenceNode):
            return list(node.value) if node.value else []
        return []

    def construct_mapping(self, node, deep=False):
        """Construct a mapping from a node."""
        if isinstance(node, MappingNode):
            return dict(node.value) if node.value else {}
        return {}

    @classmethod
    def add_constructor(cls, tag, constructor):
        """Add a constructor for a specific tag."""
        cls.yaml_constructors[tag] = constructor

    @classmethod
    def add_multi_constructor(cls, tag_prefix, multi_constructor):
        """Add a multi-constructor for a tag prefix."""
        cls.yaml_multi_constructors[tag_prefix] = multi_constructor


class SafeConstructor(BaseConstructor):
    """Safe constructor that doesn't allow arbitrary Python objects."""
    pass


class Constructor(SafeConstructor):
    """Full constructor (same as SafeConstructor in our implementation)."""
    pass
