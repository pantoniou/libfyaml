"""PyYAML composer module compatibility.

Provides Composer class that converts event streams to Node trees.
Expects subclasses to provide check_event/get_event/peek_event methods
(from a Parser or similar) and a resolve() method (from a Resolver).
"""

from .error import MarkedYAMLError
from .nodes import ScalarNode, SequenceNode, MappingNode
from .events import (
    StreamStartEvent, StreamEndEvent,
    DocumentStartEvent, DocumentEndEvent,
    AliasEvent, ScalarEvent,
    SequenceStartEvent, SequenceEndEvent,
    MappingStartEvent, MappingEndEvent,
)


class ComposerError(MarkedYAMLError):
    """YAML composer error (e.g., undefined alias)."""
    pass


class Composer:
    """YAML composer - converts event streams to Node trees.

    Expects subclasses to provide:
    - check_event(*choices) -> bool
    - get_event() -> Event
    - peek_event() -> Event
    - resolve(kind, value, implicit) -> tag  (from Resolver)
    """

    def __init__(self):
        self.anchors = {}

    def check_node(self):
        # Drop StreamStartEvent
        if self.check_event(StreamStartEvent):
            self.get_event()
        return not self.check_event(StreamEndEvent)

    def get_node(self):
        if not self.check_event(StreamEndEvent):
            return self.compose_document()

    def get_single_node(self):
        # Drop StreamStartEvent
        self.get_event()
        document = None
        if not self.check_event(StreamEndEvent):
            document = self.compose_document()
        # Drop StreamEndEvent
        self.get_event()
        return document

    def compose_document(self):
        # Drop DocumentStartEvent
        self.get_event()
        node = self.compose_node(None, None)
        # Drop DocumentEndEvent
        self.get_event()
        self.anchors = {}
        return node

    def compose_node(self, parent, index):
        if self.check_event(AliasEvent):
            event = self.get_event()
            anchor = event.anchor
            if anchor not in self.anchors:
                raise ComposerError(
                    None, None,
                    "found undefined alias %r" % anchor,
                    event.start_mark)
            return self.anchors[anchor]
        event = self.peek_event()
        anchor = event.anchor
        if anchor is not None:
            if anchor in self.anchors:
                raise ComposerError(
                    "found duplicate anchor %r; first occurrence"
                    % anchor, self.anchors[anchor].start_mark,
                    "second occurrence", event.start_mark)
        if self.check_event(ScalarEvent):
            node = self.compose_scalar_node(anchor)
        elif self.check_event(SequenceStartEvent):
            node = self.compose_sequence_node(anchor)
        elif self.check_event(MappingStartEvent):
            node = self.compose_mapping_node(anchor)
        return node

    def compose_scalar_node(self, anchor):
        event = self.get_event()
        tag = event.tag
        if tag is None or tag == '!':
            tag = self.resolve(ScalarNode, event.value, event.implicit)
        node = ScalarNode(tag, event.value,
                          start_mark=event.start_mark,
                          end_mark=event.end_mark,
                          style=event.style)
        if anchor is not None:
            self.anchors[anchor] = node
        return node

    def compose_sequence_node(self, anchor):
        start_event = self.get_event()
        tag = start_event.tag
        if tag is None or tag == '!':
            tag = self.resolve(SequenceNode, None, start_event.implicit)
        node = SequenceNode(tag, [],
                            start_mark=start_event.start_mark,
                            end_mark=None,
                            flow_style=start_event.flow_style)
        if anchor is not None:
            self.anchors[anchor] = node
        index = 0
        while not self.check_event(SequenceEndEvent):
            node.value.append(self.compose_node(node, index))
            index += 1
        end_event = self.get_event()
        node.end_mark = end_event.end_mark
        return node

    def compose_mapping_node(self, anchor):
        start_event = self.get_event()
        tag = start_event.tag
        if tag is None or tag == '!':
            tag = self.resolve(MappingNode, None, start_event.implicit)
        node = MappingNode(tag, [],
                           start_mark=start_event.start_mark,
                           end_mark=None,
                           flow_style=start_event.flow_style)
        if anchor is not None:
            self.anchors[anchor] = node
        while not self.check_event(MappingEndEvent):
            key_node = self.compose_node(node, None)
            value_node = self.compose_node(node, key_node)
            node.value.append((key_node, value_node))
        end_event = self.get_event()
        node.end_mark = end_event.end_mark
        return node
