"""PyYAML Node classes compatibility stubs.

These classes provide API compatibility for code that uses PyYAML's
node tree representation. In libfyaml, FyGeneric serves as the node type.
"""


class Node:
    """Base class for YAML nodes."""

    def __init__(self, tag=None, value=None, start_mark=None, end_mark=None):
        self.tag = tag
        self.value = value
        self.start_mark = start_mark
        self.end_mark = end_mark


class ScalarNode(Node):
    """Scalar node (strings, numbers, etc.)."""
    id = 'scalar'

    def __init__(self, tag, value, start_mark=None, end_mark=None, style=None):
        super().__init__(tag, value, start_mark, end_mark)
        self.style = style


class CollectionNode(Node):
    """Base class for collection nodes."""

    def __init__(self, tag, value, start_mark=None, end_mark=None, flow_style=None):
        super().__init__(tag, value, start_mark, end_mark)
        self.flow_style = flow_style


class SequenceNode(CollectionNode):
    """Sequence node (lists/arrays)."""
    id = 'sequence'


class MappingNode(CollectionNode):
    """Mapping node (dicts/objects)."""
    id = 'mapping'
