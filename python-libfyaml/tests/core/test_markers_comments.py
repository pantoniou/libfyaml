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
        """Header comments attach to the first key, not the mapping root."""
        doc = fy.loads('# header\nfoo: bar\n', keep_comments=True)
        first_key = next(iter(doc.keys()))
        comment = first_key.get_comment()
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
        """Comments on the first key are returned as strings."""
        doc = fy.loads('# my comment\nfoo: bar\n', keep_comments=True)
        first_key = next(iter(doc.keys()))
        comment = first_key.get_comment()
        assert isinstance(comment, str)
        assert 'my comment' in first_key.get_comment(placement='top')
        assert first_key.get_comment(placement='right') is None
        assert first_key.get_comment(placement='bottom') is None

    def test_comments_with_markers(self):
        """Comments and markers can be used together."""
        doc = fy.loads('foo: bar  # note\n', keep_comments=True, create_markers=True)
        assert doc['foo'].has_marker()
        assert doc['foo'].has_comment()
        marker = doc['foo'].get_marker()
        comment = doc['foo'].get_comment()
        assert marker is not None
        assert 'note' in comment
        assert doc['foo'].get_comment(placement='top') is None
        assert 'note' in doc['foo'].get_comment(placement='right')
        assert doc['foo'].get_comment(placement='bottom') is None

    def test_doc_leading_comment_explicit(self):
        """A comment before '---' is a doc-level comment."""
        doc = fy.loads('# leading\n---\nfoo: bar\n', keep_comments=True)
        assert doc.get_comment() is None
        assert not doc.has_comment()
        assert doc.has_document_comment()
        assert doc.get_document_comment(placement='bottom') is None
        comment = doc.get_document_comment()
        assert comment is not None
        assert 'leading' in comment
        assert doc.get_document_comment(placement='top') == comment

    def test_doc_leading_comment_blank_separated(self):
        """A comment separated from implicit-doc content by a blank line is doc-level."""
        doc = fy.loads('# header\n\nfoo: bar\n', keep_comments=True)
        assert doc.get_comment() is None
        assert not doc.has_comment()
        comment = doc.get_document_comment()
        assert comment is not None
        assert 'header' in comment

    def test_doc_trailing_comment(self):
        """A trailing comment after all mapping content is doc-level."""
        doc = fy.loads('foo: bar\n# trailing\n', keep_comments=True)
        assert doc.get_comment() is None
        assert not doc.has_comment()
        assert doc.has_document_comment()
        assert doc.get_document_comment(placement='top') is None
        comment = doc.get_document_comment()
        assert comment is not None
        assert 'trailing' in comment
        assert doc.get_document_comment(placement='bottom') == comment
        assert doc['foo'].get_comment() is None
        assert not doc['foo'].has_comment()

    def test_inline_comment_not_doc_comment(self):
        """A comment on the same line as content (no blank line) attaches to the key, not doc."""
        doc = fy.loads('# header\nfoo: bar\n', keep_comments=True)
        assert doc.get_comment() is None
        assert doc.get_document_comment() is None
        assert not doc.has_document_comment()
        first_key = next(iter(doc.keys()))
        assert first_key.get_comment() is not None

    def test_invalid_comment_placement(self):
        doc = fy.loads('foo: bar\n', keep_comments=True)
        with pytest.raises(ValueError):
            doc.get_comment(placement='sideways')
        with pytest.raises(ValueError):
            doc.get_document_comment(placement='sideways')

    def test_set_node_comment(self):
        doc = fy.loads('foo: bar\n', mutable=True, keep_comments=True)
        doc['foo'].set_comment('lead')
        doc['foo'].set_comment('inline', placement='right')
        assert doc['foo'].get_comment(placement='top') == 'lead'
        assert doc['foo'].get_comment(placement='right') == 'inline'
        assert 'lead' in doc['foo'].get_comment()
        assert 'inline' in doc['foo'].get_comment()

        doc['foo'].set_comment(None, placement='top')
        assert doc['foo'].get_comment(placement='top') is None
        assert doc['foo'].get_comment(placement='right') == 'inline'

    def test_set_document_comment(self):
        doc = fy.loads('foo: bar\n', mutable=True, keep_comments=True)
        doc.set_document_comment('header')
        doc.set_document_comment('footer', placement='bottom')
        assert doc.get_document_comment(placement='top') == 'header'
        assert doc.get_document_comment(placement='bottom') == 'footer'
        assert doc.get_document_comment() == 'header'

        doc.set_document_comment(None, placement='top')
        assert doc.get_document_comment(placement='top') is None
        assert doc.get_document_comment() == 'footer'

    def test_set_comment_requires_mutable(self):
        doc = fy.loads('foo: bar\n', keep_comments=True)
        with pytest.raises(TypeError):
            doc['foo'].set_comment('nope')
        with pytest.raises(TypeError):
            doc.set_document_comment('nope')
