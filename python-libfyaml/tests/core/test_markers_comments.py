"""Tests for marker and comment support in libfyaml Python bindings."""
import pytest
import libfyaml as fy


class TestMarkers:
    """Test position marker functionality."""

    def test_markers_disabled_by_default(self):
        """Without create_markers=True, no markers should be present."""
        doc = fy.loads('foo: bar')
        assert not doc.has_marker()
        assert doc.get_marker() is None
        assert not doc['foo'].has_marker()
        assert doc['foo'].get_marker() is None

    def test_markers_on_scalar_values(self):
        """Scalar values should have markers with create_markers=True."""
        doc = fy.loads('foo: bar\nbaz: 42\n', create_markers=True)
        # Values should have markers
        marker = doc['foo'].get_marker()
        assert marker is not None
        assert len(marker) == 6
        # start_byte, start_line, start_col, end_byte, end_line, end_col
        assert all(isinstance(x, int) for x in marker)

    def test_marker_positions_correct(self):
        """Marker positions should reflect actual source positions."""
        doc = fy.loads('foo: bar\n', create_markers=True)
        marker = doc['foo'].get_marker()
        # "bar" starts at column 5, line 0
        assert marker[1] == 0  # start_line
        assert marker[2] == 5  # start_col
        assert marker[4] == 0  # end_line
        assert marker[5] == 8  # end_col (after "bar")

    def test_markers_on_nested_values(self):
        """Nested values should have correct markers."""
        yaml = 'outer:\n  inner: value\n'
        doc = fy.loads(yaml, create_markers=True)
        marker = doc['outer']['inner'].get_marker()
        assert marker is not None
        assert marker[1] == 1  # second line (0-indexed)

    def test_markers_on_sequence_items(self):
        """Sequence items should have markers."""
        yaml = 'items:\n  - alpha\n  - beta\n'
        doc = fy.loads(yaml, create_markers=True)
        m0 = doc['items'][0].get_marker()
        m1 = doc['items'][1].get_marker()
        assert m0 is not None
        assert m1 is not None
        # Second item should be on a later line
        assert m1[1] > m0[1]

    def test_markers_root_mapping(self):
        """Root mapping itself may not have a marker (library behavior)."""
        doc = fy.loads('a: 1\nb: 2\n', create_markers=True)
        # Root typically doesn't have a marker
        # but values do
        assert doc['a'].has_marker()
        assert doc['b'].has_marker()

    def test_markers_with_load(self):
        """Markers should work with load() from file objects."""
        import io
        f = io.StringIO('key: value\n')
        doc = fy.load(f, create_markers=True)
        assert doc['key'].has_marker()

    def test_markers_with_loads_all(self):
        """Markers should work with loads_all() for multi-document."""
        yaml = '---\nfoo: 1\n---\nbar: 2\n'
        docs = fy.loads_all(yaml, create_markers=True)
        assert docs[0]['foo'].has_marker()
        assert docs[1]['bar'].has_marker()

    def test_marker_tuple_structure(self):
        """Marker should be a 6-tuple of ints."""
        doc = fy.loads('x: y\n', create_markers=True)
        marker = doc['x'].get_marker()
        assert isinstance(marker, tuple)
        assert len(marker) == 6
        start_byte, start_line, start_col, end_byte, end_line, end_col = marker
        assert start_byte >= 0
        assert start_line >= 0
        assert start_col >= 0
        assert end_byte >= start_byte
        assert end_line >= start_line


class TestComments:
    """Test comment support."""

    def test_comments_disabled_by_default(self):
        """Without keep_comments=True, no comments should be present."""
        doc = fy.loads('# comment\nfoo: bar\n')
        assert not doc.has_comment()
        assert doc.get_comment() is None

    def test_header_comment(self):
        """Header comments should be attached to following node."""
        doc = fy.loads('# header\nfoo: bar\n', keep_comments=True)
        comment = doc.get_comment()
        assert comment is not None
        assert 'header' in comment

    def test_inline_comment(self):
        """Inline comments should be attached to the value."""
        doc = fy.loads('foo: bar  # inline\nbaz: 42\n', keep_comments=True)
        comment = doc['foo'].get_comment()
        assert comment is not None
        assert 'inline' in comment

    def test_no_comment_on_uncommented_value(self):
        """Values without comments should return None."""
        doc = fy.loads('foo: bar  # inline\nbaz: 42\n', keep_comments=True)
        assert doc['baz'].get_comment() is None

    def test_comment_is_string(self):
        """Comments should be returned as strings."""
        doc = fy.loads('# my comment\nfoo: bar\n', keep_comments=True)
        comment = doc.get_comment()
        assert isinstance(comment, str)

    def test_comments_with_markers(self):
        """Comments and markers can be used together."""
        doc = fy.loads('foo: bar  # note\n', keep_comments=True, create_markers=True)
        assert doc['foo'].has_marker()
        assert doc['foo'].has_comment()
        marker = doc['foo'].get_marker()
        comment = doc['foo'].get_comment()
        assert marker is not None
        assert 'note' in comment


