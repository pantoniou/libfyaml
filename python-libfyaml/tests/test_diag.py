"""Tests for diagnostic information collection (get_diag, has_diag, collect_diag)."""

import pytest
import libfyaml as fy


class TestDiagMethods:
    """Test diagnostic information methods."""

    def test_valid_yaml_no_diag(self):
        """Valid YAML should have no diagnostics."""
        doc = fy.loads('foo: bar', collect_diag=True)
        assert not doc.has_diag()
        assert doc.get_diag() is None

    def test_diag_methods_without_collect_diag(self):
        """Without collect_diag, there should be no diagnostics."""
        doc = fy.loads('foo: bar')
        assert not doc.has_diag()
        assert doc.get_diag() is None

    def test_nested_value_diag(self):
        """Nested values should also have diag methods."""
        doc = fy.loads('foo: {bar: baz}', collect_diag=True)
        assert not doc.has_diag()
        inner = doc['foo']
        assert not inner.has_diag()
        assert inner.get_diag() is None

    def test_sequence_diag(self):
        """Sequence values should have diag methods."""
        doc = fy.loads('[1, 2, 3]', collect_diag=True)
        assert not doc.has_diag()
        item = doc[0]
        assert not item.has_diag()

    def test_collect_diag_parameter(self):
        """Test that collect_diag parameter is accepted in various functions."""
        # loads
        doc = fy.loads('foo: bar', collect_diag=True)
        assert doc is not None

        # loads_all
        docs = list(fy.loads_all('foo: bar\n---\nbaz: qux', collect_diag=True))
        assert len(docs) == 2

    def test_complex_yaml_no_errors(self):
        """Complex but valid YAML should have no diagnostics."""
        yaml_str = '''
servers:
  - name: server1
    host: localhost
    port: 8080
  - name: server2
    host: 192.168.1.1
    port: 9090

config:
  debug: true
  timeout: 30
  features:
    - logging
    - metrics
    - tracing
'''
        doc = fy.loads(yaml_str, collect_diag=True)
        assert not doc.has_diag()
        assert doc.get_diag() is None

        # Check nested structures
        servers = doc['servers']
        assert not servers.has_diag()

        config = doc['config']
        assert not config.has_diag()


class TestDiagErrorCollection:
    """Test diagnostic error collection for invalid YAML."""

    def test_undefined_alias_returns_diag(self):
        """Undefined alias with collect_diag returns diagnostic info."""
        yaml_str = 'foo: *bar'
        doc = fy.loads(yaml_str, collect_diag=True)

        # Result should be a sequence of error mappings
        assert doc.is_sequence()
        errors = doc.to_python()
        assert len(errors) == 1

        error = errors[0]
        assert 'message' in error
        assert 'Unable to resolve alias' in error['message']
        assert 'line' in error
        assert 'column' in error
        assert 'start_mark' in error
        assert 'end_mark' in error

    def test_undefined_alias_without_collect_diag_raises(self):
        """Undefined alias without collect_diag raises ValueError."""
        yaml_str = 'foo: *bar'
        with pytest.raises(ValueError, match="Failed to parse"):
            fy.loads(yaml_str)

    def test_diag_contains_position_info(self):
        """Diagnostic info should contain line/column positions."""
        yaml_str = 'foo: *undefined'
        doc = fy.loads(yaml_str, collect_diag=True)

        errors = doc.to_python()
        error = errors[0]

        # Check position info
        assert error['line'] >= 1
        assert error['column'] >= 1
        assert 'start_mark' in error
        assert 'line' in error['start_mark']
        assert 'column' in error['start_mark']

    def test_diag_contains_content(self):
        """Diagnostic info should contain source content."""
        yaml_str = 'foo: *bar'
        doc = fy.loads(yaml_str, collect_diag=True)

        errors = doc.to_python()
        error = errors[0]

        # Content should contain the problematic line
        assert 'content' in error
        assert 'foo: *bar' in error['content']
