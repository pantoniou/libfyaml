"""Tests for PyYAML compatibility layer.

These tests verify that the pyyaml_compat module provides a drop-in
replacement for PyYAML's most common functionality.
"""

import io
import functools
import pytest

from libfyaml import pyyaml_compat as yaml


class TestStubClasses:
    """Test stub classes for API compatibility."""

    def test_safe_loader_exists(self):
        """SafeLoader class should exist."""
        assert hasattr(yaml, 'SafeLoader')

    def test_csafe_loader_exists(self):
        """CSafeLoader class should exist."""
        assert hasattr(yaml, 'CSafeLoader')

    def test_safe_dumper_exists(self):
        """SafeDumper class should exist."""
        assert hasattr(yaml, 'SafeDumper')

    def test_csafe_dumper_exists(self):
        """CSafeDumper class should exist."""
        assert hasattr(yaml, 'CSafeDumper')

    def test_csafe_loader_is_safe_loader(self):
        """CSafeLoader should be same as SafeLoader."""
        assert yaml.CSafeLoader is yaml.SafeLoader

    def test_csafe_dumper_is_safe_dumper(self):
        """CSafeDumper should be same as SafeDumper."""
        assert yaml.CSafeDumper is yaml.SafeDumper

    def test_loader_exists(self):
        """Loader class should exist and be subclass of SafeLoader."""
        assert hasattr(yaml, 'Loader')
        assert issubclass(yaml.Loader, yaml.SafeLoader)

    def test_dumper_exists(self):
        """Dumper class should exist and be subclass of SafeDumper."""
        assert hasattr(yaml, 'Dumper')
        assert issubclass(yaml.Dumper, yaml.SafeDumper)

    def test_base_loader_exists(self):
        """BaseLoader class should exist."""
        assert hasattr(yaml, 'BaseLoader')

    def test_loader_has_add_constructor(self):
        """Loader classes should have add_constructor method."""
        assert hasattr(yaml.Loader, 'add_constructor')
        assert hasattr(yaml.SafeLoader, 'add_constructor')
        assert hasattr(yaml.BaseLoader, 'add_constructor')


class TestLoad:
    """Test load() function."""

    def test_load_simple_mapping(self):
        """Load simple mapping."""
        data = yaml.load("foo: bar", Loader=yaml.SafeLoader)
        assert data == {'foo': 'bar'}

    def test_load_simple_sequence(self):
        """Load simple sequence."""
        data = yaml.load("[1, 2, 3]", Loader=yaml.SafeLoader)
        assert data == [1, 2, 3]

    def test_load_nested(self):
        """Load nested structure."""
        data = yaml.load("a:\n  b:\n    c: 1", Loader=yaml.SafeLoader)
        assert data == {'a': {'b': {'c': 1}}}

    def test_load_types(self):
        """Load various types."""
        data = yaml.load("""
string: hello
integer: 42
float: 3.14
bool_true: true
bool_false: false
null_value: null
""", Loader=yaml.SafeLoader)
        assert data['string'] == 'hello'
        assert data['integer'] == 42
        assert abs(data['float'] - 3.14) < 0.001
        assert data['bool_true'] is True
        assert data['bool_false'] is False
        assert data['null_value'] is None

    def test_load_from_file(self):
        """Load from file-like object."""
        stream = io.StringIO("foo: bar")
        data = yaml.load(stream, Loader=yaml.SafeLoader)
        assert data == {'foo': 'bar'}

    def test_load_without_loader(self):
        """Load without explicit Loader (uses default)."""
        data = yaml.load("foo: bar")
        assert data == {'foo': 'bar'}


class TestSafeLoad:
    """Test safe_load() function."""

    def test_safe_load_simple(self):
        """safe_load simple mapping."""
        data = yaml.safe_load("foo: bar")
        assert data == {'foo': 'bar'}

    def test_safe_load_from_file(self):
        """safe_load from file-like object."""
        stream = io.StringIO("items:\n  - a\n  - b")
        data = yaml.safe_load(stream)
        assert data == {'items': ['a', 'b']}


class TestLoadAll:
    """Test load_all() function."""

    def test_load_all_multi_doc(self):
        """Load multiple documents."""
        docs = list(yaml.load_all("---\na: 1\n---\nb: 2\n---\nc: 3\n"))
        assert len(docs) == 3
        assert docs[0] == {'a': 1}
        assert docs[1] == {'b': 2}
        assert docs[2] == {'c': 3}

    def test_load_all_single_doc(self):
        """Load single document returns single item."""
        docs = list(yaml.load_all("foo: bar"))
        assert len(docs) == 1
        assert docs[0] == {'foo': 'bar'}

    def test_load_all_from_file(self):
        """Load all from file-like object."""
        stream = io.StringIO("---\nx: 1\n---\ny: 2\n")
        docs = list(yaml.load_all(stream))
        assert len(docs) == 2


class TestSafeLoadAll:
    """Test safe_load_all() function."""

    def test_safe_load_all(self):
        """safe_load_all multiple documents."""
        docs = list(yaml.safe_load_all("---\na: 1\n---\nb: 2\n"))
        assert len(docs) == 2
        assert docs[0] == {'a': 1}
        assert docs[1] == {'b': 2}


class TestCompose:
    """Test compose() function - returns node tree instead of Python objects."""

    def test_compose_returns_fygeneric(self):
        """compose() should return FyGeneric node, not Python dict."""
        import libfyaml as fy
        node = yaml.compose("foo: bar")
        assert isinstance(node, fy.FyGeneric)

    def test_compose_simple_mapping(self):
        """compose() returns node that can be converted to Python."""
        node = yaml.compose("foo: bar")
        # Node should be convertible to Python
        assert node.to_python() == {'foo': 'bar'}

    def test_compose_simple_sequence(self):
        """compose() with sequence."""
        node = yaml.compose("[1, 2, 3]")
        assert node.to_python() == [1, 2, 3]

    def test_compose_nested(self):
        """compose() with nested structure."""
        node = yaml.compose("a:\n  b:\n    c: 1")
        assert node.to_python() == {'a': {'b': {'c': 1}}}

    def test_compose_with_loader(self):
        """compose() accepts Loader parameter."""
        node = yaml.compose("foo: bar", Loader=yaml.SafeLoader)
        assert node.to_python() == {'foo': 'bar'}

    def test_compose_from_file(self):
        """compose() from file-like object."""
        stream = io.StringIO("key: value")
        node = yaml.compose(stream)
        assert node.to_python() == {'key': 'value'}

    def test_compose_empty_returns_none(self):
        """compose() with empty string returns None."""
        node = yaml.compose("")
        assert node is None

    def test_compose_null_returns_none(self):
        """compose() with null returns None."""
        node = yaml.compose("null")
        assert node is None

    def test_compose_empty_mapping(self):
        """compose() with empty mapping."""
        import libfyaml as fy
        node = yaml.compose("{}")
        assert isinstance(node, fy.FyGeneric)
        assert node.to_python() == {}

    def test_compose_empty_sequence(self):
        """compose() with empty sequence."""
        import libfyaml as fy
        node = yaml.compose("[]")
        assert isinstance(node, fy.FyGeneric)
        assert node.to_python() == []


class TestComposeAll:
    """Test compose_all() function."""

    def test_compose_all_multi_doc(self):
        """compose_all() returns multiple nodes."""
        import libfyaml as fy
        nodes = list(yaml.compose_all("---\na: 1\n---\nb: 2\n"))
        assert len(nodes) == 2
        assert all(isinstance(n, fy.FyGeneric) for n in nodes)
        assert nodes[0].to_python() == {'a': 1}
        assert nodes[1].to_python() == {'b': 2}

    def test_compose_all_single_doc(self):
        """compose_all() with single document."""
        nodes = list(yaml.compose_all("foo: bar"))
        assert len(nodes) == 1
        assert nodes[0].to_python() == {'foo': 'bar'}

    def test_compose_all_from_file(self):
        """compose_all() from file-like object."""
        stream = io.StringIO("---\nx: 1\n---\ny: 2\n")
        nodes = list(yaml.compose_all(stream))
        assert len(nodes) == 2


class TestDump:
    """Test dump() function."""

    def test_dump_simple_mapping(self):
        """Dump simple mapping."""
        result = yaml.dump({'foo': 'bar'})
        assert 'foo' in result
        assert 'bar' in result

    def test_dump_simple_sequence(self):
        """Dump simple sequence."""
        result = yaml.dump([1, 2, 3])
        assert '1' in result
        assert '2' in result
        assert '3' in result

    def test_dump_nested(self):
        """Dump nested structure."""
        result = yaml.dump({'a': {'b': 1}})
        assert 'a' in result
        assert 'b' in result

    def test_dump_to_stream(self):
        """Dump to file-like object."""
        stream = io.StringIO()
        result = yaml.dump({'foo': 'bar'}, stream)
        assert result is None
        assert 'foo' in stream.getvalue()

    def test_dump_returns_string(self):
        """Dump returns string when no stream."""
        result = yaml.dump({'foo': 'bar'})
        assert isinstance(result, str)

    def test_dump_with_indent(self):
        """Dump with custom indent."""
        result = yaml.dump({'a': {'b': 1}}, indent=4)
        # Should have proper indentation
        assert 'b:' in result


class TestSafeDump:
    """Test safe_dump() function."""

    def test_safe_dump_simple(self):
        """safe_dump simple mapping."""
        result = yaml.safe_dump({'foo': 'bar'})
        assert 'foo' in result
        assert 'bar' in result

    def test_safe_dump_to_stream(self):
        """safe_dump to file-like object."""
        stream = io.StringIO()
        yaml.safe_dump({'foo': 'bar'}, stream)
        assert 'foo' in stream.getvalue()


class TestDumpAll:
    """Test dump_all() function."""

    def test_dump_all_multi_doc(self):
        """Dump multiple documents."""
        result = yaml.dump_all([{'a': 1}, {'b': 2}])
        assert 'a' in result
        assert 'b' in result
        assert '---' in result  # Document separators

    def test_dump_all_to_stream(self):
        """Dump all to file-like object."""
        stream = io.StringIO()
        yaml.dump_all([{'x': 1}, {'y': 2}], stream)
        content = stream.getvalue()
        assert 'x' in content
        assert 'y' in content


class TestSafeDumpAll:
    """Test safe_dump_all() function."""

    def test_safe_dump_all(self):
        """safe_dump_all multiple documents."""
        result = yaml.safe_dump_all([{'a': 1}, {'b': 2}])
        assert 'a' in result
        assert 'b' in result


class TestRoundTrip:
    """Test round-trip conversion (load -> dump -> load)."""

    def test_roundtrip_mapping(self):
        """Round-trip simple mapping."""
        original = {'foo': 'bar', 'baz': 42}
        dumped = yaml.safe_dump(original)
        loaded = yaml.safe_load(dumped)
        assert loaded == original

    def test_roundtrip_sequence(self):
        """Round-trip sequence."""
        original = [1, 'two', 3.0, True, None]
        dumped = yaml.safe_dump(original)
        loaded = yaml.safe_load(dumped)
        assert loaded == original

    def test_roundtrip_nested(self):
        """Round-trip nested structure."""
        original = {
            'list': [1, 2, 3],
            'nested': {'enabled': True, 'disabled': False},  # Avoid 'y' - it's a bool in YAML 1.1
            'string': 'hello world'
        }
        dumped = yaml.safe_dump(original)
        loaded = yaml.safe_load(dumped)
        assert loaded == original


class TestPreCommitPattern:
    """Test pre-commit's yaml.py usage pattern."""

    def test_precommit_loader_pattern(self):
        """Test pre-commit's Loader pattern."""
        # pre-commit does: Loader = getattr(yaml, 'CSafeLoader', yaml.SafeLoader)
        Loader = getattr(yaml, 'CSafeLoader', yaml.SafeLoader)
        assert Loader is yaml.SafeLoader

    def test_precommit_partial_load(self):
        """Test pre-commit's functools.partial pattern."""
        # pre-commit does: yaml_load = functools.partial(yaml.load, Loader=Loader)
        Loader = getattr(yaml, 'CSafeLoader', yaml.SafeLoader)
        yaml_load = functools.partial(yaml.load, Loader=Loader)

        data = yaml_load("foo: bar")
        assert data == {'foo': 'bar'}

    def test_precommit_dumper_pattern(self):
        """Test pre-commit's Dumper pattern."""
        # pre-commit does: Dumper = getattr(yaml, 'CSafeDumper', yaml.SafeDumper)
        Dumper = getattr(yaml, 'CSafeDumper', yaml.SafeDumper)
        assert Dumper is yaml.SafeDumper

    def test_precommit_dump_pattern(self):
        """Test pre-commit's yaml_dump pattern."""
        # pre-commit does:
        # def yaml_dump(o, **kwargs):
        #     return yaml.dump(o, Dumper=Dumper, default_flow_style=False,
        #                      indent=4, sort_keys=False, **kwargs)
        Dumper = getattr(yaml, 'CSafeDumper', yaml.SafeDumper)

        def yaml_dump(o, **kwargs):
            return yaml.dump(o, Dumper=Dumper, default_flow_style=False,
                             indent=4, sort_keys=False, **kwargs)

        result = yaml_dump({'foo': 'bar', 'baz': 42})
        assert 'foo' in result
        assert 'bar' in result


class TestErrorClasses:
    """Test error classes for API compatibility."""

    def test_yaml_error_exists(self):
        """YAMLError class should exist."""
        assert hasattr(yaml, 'YAMLError')

    def test_scanner_error_exists(self):
        """ScannerError class should exist."""
        assert hasattr(yaml, 'ScannerError')

    def test_parser_error_exists(self):
        """ParserError class should exist."""
        assert hasattr(yaml, 'ParserError')

    def test_errors_are_exceptions(self):
        """Error classes should be exceptions."""
        assert issubclass(yaml.YAMLError, Exception)
        assert issubclass(yaml.ScannerError, Exception)
        assert issubclass(yaml.ParserError, Exception)


class TestEdgeCases:
    """Test edge cases and special values."""

    def test_empty_document(self):
        """Load empty document."""
        data = yaml.safe_load("")
        assert data is None

    def test_null_document(self):
        """Load null document."""
        data = yaml.safe_load("null")
        assert data is None

    def test_empty_mapping(self):
        """Load empty mapping."""
        data = yaml.safe_load("{}")
        assert data == {}

    def test_empty_sequence(self):
        """Load empty sequence."""
        data = yaml.safe_load("[]")
        assert data == []

    def test_unicode_string(self):
        """Load unicode string."""
        data = yaml.safe_load("emoji: \U0001F600")
        assert data['emoji'] == '\U0001F600'

    def test_multiline_string(self):
        """Load multiline string."""
        data = yaml.safe_load("""text: |
  line1
  line2
  line3
""")
        assert 'line1' in data['text']
        assert 'line2' in data['text']


class TestCustomConstructors:
    """Test custom tag constructors."""

    def test_add_constructor_basic(self):
        """Test basic add_constructor functionality."""
        class CustomType:
            def __init__(self, value):
                self.value = value

        def custom_constructor(loader, node):
            return CustomType(node.value)

        # Create a fresh loader class to avoid polluting shared state
        class TestLoader(yaml.SafeLoader):
            yaml_constructors = {}

        TestLoader.add_constructor('!custom', custom_constructor)
        result = yaml.load("value: !custom test", Loader=TestLoader)
        assert isinstance(result['value'], CustomType)
        assert result['value'].value == 'test'

    def test_constructor_with_mapping(self):
        """Test constructor with mapping value."""
        def env_constructor(loader, node):
            # node.value is the FyGeneric for mappings
            if hasattr(node.value, 'to_python'):
                return f"ENV:{node.value.to_python()}"
            return f"ENV:{node.value}"

        class TestLoader(yaml.SafeLoader):
            yaml_constructors = {}

        TestLoader.add_constructor('!ENV', env_constructor)
        result = yaml.load("config: !ENV {name: MY_VAR}", Loader=TestLoader)
        assert 'ENV:' in result['config']

    def test_constructor_with_sequence(self):
        """Test constructor with sequence value."""
        def list_constructor(loader, node):
            if hasattr(node.value, 'to_python'):
                items = node.value.to_python()
            else:
                items = node.value
            return {'items': items, 'count': len(items)}

        class TestLoader(yaml.SafeLoader):
            yaml_constructors = {}

        TestLoader.add_constructor('!counted', list_constructor)
        result = yaml.load("data: !counted [a, b, c]", Loader=TestLoader)
        assert result['data']['count'] == 3
        assert result['data']['items'] == ['a', 'b', 'c']

    def test_nested_custom_tags(self):
        """Test nested structures with custom tags."""
        def upper_constructor(loader, node):
            return node.value.upper()

        class TestLoader(yaml.SafeLoader):
            yaml_constructors = {}

        TestLoader.add_constructor('!upper', upper_constructor)
        result = yaml.load("""
items:
  - !upper hello
  - !upper world
  - normal
""", Loader=TestLoader)
        assert result['items'][0] == 'HELLO'
        assert result['items'][1] == 'WORLD'
        assert result['items'][2] == 'normal'

    def test_tag_access_methods(self):
        """Test get_tag(), has_tag() methods on FyGeneric."""
        import libfyaml as fy

        doc = fy.loads("value: !custom test")
        assert doc['value'].has_tag()
        assert doc['value'].get_tag() == '!custom'

        doc2 = fy.loads("value: plain")
        assert not doc2['value'].has_tag()
        assert doc2['value'].get_tag() is None

    def test_anchor_access_methods(self):
        """Test get_anchor(), has_anchor() methods on FyGeneric."""
        import libfyaml as fy

        doc = fy.loads("value: &myanchor test")
        assert doc['value'].has_anchor()
        assert doc['value'].get_anchor() == 'myanchor'

        doc2 = fy.loads("value: plain")
        assert not doc2['value'].has_anchor()
        assert doc2['value'].get_anchor() is None

    def test_standard_yaml_tags_preserved(self):
        """Test that standard YAML tags are accessible."""
        import libfyaml as fy

        doc = fy.loads('value: !!int "42"')
        # Standard tags use full URI form
        assert doc['value'].has_tag()
        tag = doc['value'].get_tag()
        assert 'int' in tag

    def test_constructor_not_invoked_without_tag(self):
        """Test that constructors don't interfere with untagged values."""
        call_count = [0]

        def counting_constructor(loader, node):
            call_count[0] += 1
            return node.value

        class TestLoader(yaml.SafeLoader):
            yaml_constructors = {}

        TestLoader.add_constructor('!count', counting_constructor)
        result = yaml.load("a: 1\nb: 2\nc: 3", Loader=TestLoader)

        assert call_count[0] == 0  # Constructor should not be called
        assert result == {'a': 1, 'b': 2, 'c': 3}
