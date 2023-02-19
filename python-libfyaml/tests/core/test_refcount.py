"""Tests for Python object reference counting and memory management.

These tests verify that FyGeneric and FyDocumentState objects properly manage
reference counts, avoiding memory leaks and circular references.
"""

import gc
import sys
import pytest
import libfyaml as fy


class TestDocStateSharing:
    """Test that doc_state is properly shared between parent and children."""

    def test_parent_child_share_doc_state(self):
        """Parent and child should share the same doc_state object."""
        doc = fy.loads('{"nested": {"value": 42}}')
        child = doc['nested']

        ds_doc = doc.document_state
        ds_child = child.document_state

        assert id(ds_doc) == id(ds_child), "Parent and child should share doc_state"

    def test_deeply_nested_share_doc_state(self):
        """All levels of nesting should share the same doc_state."""
        doc = fy.loads('{"a": {"b": {"c": {"d": 1}}}}')
        level1 = doc['a']
        level2 = level1['b']
        level3 = level2['c']
        level4 = level3['d']

        ds_ids = [
            id(doc.document_state),
            id(level1.document_state),
            id(level2.document_state),
            id(level3.document_state),
            id(level4.document_state),
        ]

        assert len(set(ds_ids)) == 1, "All nesting levels should share doc_state"

    def test_sequence_elements_share_doc_state(self):
        """Sequence elements should share doc_state with parent."""
        doc = fy.loads('[1, 2, 3, {"key": "value"}]')
        elem0 = doc[0]
        elem3 = doc[3]
        nested = elem3['key']

        assert id(doc.document_state) == id(elem0.document_state)
        assert id(doc.document_state) == id(elem3.document_state)
        assert id(doc.document_state) == id(nested.document_state)


class TestMultiDocSeparation:
    """Test that multi-document parsing creates separate doc_states."""

    def test_multi_doc_separate_doc_states(self):
        """Each document in multi-doc should have its own doc_state."""
        docs = fy.loads_all("---\na: 1\n---\nb: 2\n---\nc: 3\n")

        ds_ids = [id(d.document_state) for d in docs]

        assert len(set(ds_ids)) == len(docs), "Each document should have unique doc_state"

    def test_multi_doc_children_share_within_doc(self):
        """Children within each multi-doc document should share that doc's state."""
        docs = fy.loads_all("---\nnested: {value: 1}\n---\nnested: {value: 2}\n")

        # Children of doc0 share doc0's state
        doc0_ds = id(docs[0].document_state)
        child0_ds = id(docs[0]['nested'].document_state)
        assert doc0_ds == child0_ds

        # Children of doc1 share doc1's state
        doc1_ds = id(docs[1].document_state)
        child1_ds = id(docs[1]['nested'].document_state)
        assert doc1_ds == child1_ds

        # But doc0 and doc1 have different states
        assert doc0_ds != doc1_ds


class TestRefcountBasics:
    """Test basic reference counting behavior."""

    def test_child_releases_doc_state_on_delete(self):
        """Deleting a child should decrease doc_state refcount."""
        doc = fy.loads('{"a": 1, "b": 2}')
        child = doc['a']

        ds = doc.document_state
        refcount_with_child = sys.getrefcount(ds)

        del child
        gc.collect()

        refcount_after = sys.getrefcount(ds)
        assert refcount_after < refcount_with_child, \
            "doc_state refcount should decrease when child is deleted"

    def test_mutation_stable_refcount(self):
        """Mutations should not change doc_state refcount."""
        doc = fy.loads('{"items": [1, 2, 3]}', mutable=True)
        ds = doc.document_state

        initial_refcount = sys.getrefcount(ds)

        for i in range(10):
            doc['items'][0] = i

        final_refcount = sys.getrefcount(ds)
        assert final_refcount == initial_refcount, \
            f"Refcount changed from {initial_refcount} to {final_refcount} during mutations"

    def test_multiple_doc_state_access_stable(self):
        """Multiple accesses to document_state should return same object."""
        doc = fy.loads('{"test": 1}')

        ds1 = doc.document_state
        ds2 = doc.document_state
        ds3 = doc.document_state

        assert ds1 is ds2 is ds3, "Multiple accesses should return same object"


class TestNoCircularReferences:
    """Test that no circular references are created."""

    def test_no_uncollectable_objects(self):
        """Creating and destroying objects should not create uncollectable garbage."""
        gc.collect()
        garbage_before = len(gc.garbage)

        for _ in range(100):
            doc = fy.loads('{"a": {"b": {"c": [1, 2, 3]}}}')
            _ = doc['a']['b']['c'][0]
            del doc

        gc.collect()
        garbage_after = len(gc.garbage)

        assert garbage_after == garbage_before, \
            f"Created {garbage_after - garbage_before} uncollectable objects"

    def test_no_uncollectable_with_mutations(self):
        """Mutable objects should not create uncollectable garbage."""
        gc.collect()
        garbage_before = len(gc.garbage)

        for _ in range(50):
            doc = fy.loads('{"x": 0}', mutable=True)
            for i in range(5):
                doc['x'] = i
            del doc

        gc.collect()
        garbage_after = len(gc.garbage)

        assert garbage_after == garbage_before, \
            f"Mutations created {garbage_after - garbage_before} uncollectable objects"


class TestDataLifetime:
    """Test that data remains accessible as long as references exist."""

    def test_child_keeps_data_alive(self):
        """A child reference should keep the underlying data accessible."""
        def get_nested_value():
            doc = fy.loads('{"nested": {"deep": {"value": 42}}}')
            return doc['nested']['deep']['value']

        value = get_nested_value()
        gc.collect()

        # Data should still be accessible
        assert int(value) == 42

    def test_iterator_keeps_data_alive(self):
        """An iterator should keep the underlying data accessible."""
        def get_iterator():
            doc = fy.loads('[1, 2, 3, 4, 5]')
            return iter(doc)

        it = get_iterator()
        gc.collect()

        # Should be able to iterate
        values = list(it)
        assert values == [1, 2, 3, 4, 5]

    def test_nested_child_outlives_intermediate(self):
        """A deeply nested child should work even if intermediate refs are deleted."""
        doc = fy.loads('{"a": {"b": {"c": "value"}}}')

        # Get nested value
        a = doc['a']
        b = a['b']
        c = b['c']

        # Delete intermediate references
        del doc
        del a
        del b
        gc.collect()

        # c should still work
        assert str(c) == "value"


class TestCleanupOrder:
    """Test that cleanup happens in correct order."""

    def test_doc_state_outlives_children(self):
        """doc_state should remain valid while any child exists."""
        doc = fy.loads('{"key": "value"}')
        child = doc['key']
        ds = doc.document_state

        # Delete parent but keep child
        del doc
        gc.collect()

        # Child and its doc_state should still work
        assert str(child) == "value"
        assert child.document_state is ds

    def test_multi_doc_holder_cleanup(self):
        """Multi-doc holder should be cleaned up properly."""
        docs = fy.loads_all("---\na: 1\n---\nb: 2\n")
        doc0 = docs[0]
        doc1 = docs[1]

        # Delete the list but keep individual docs
        del docs
        gc.collect()

        # Individual docs should still work
        assert dict(doc0) == {'a': 1}
        assert dict(doc1) == {'b': 2}
