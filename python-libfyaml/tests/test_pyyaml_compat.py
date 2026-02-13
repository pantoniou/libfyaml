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
        """Load without explicit Loader raises TypeError (PyYAML 5.1+ behavior)."""
        import pytest
        with pytest.raises(TypeError):
            yaml.load("foo: bar")


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
        docs = list(yaml.load_all("---\na: 1\n---\nb: 2\n---\nc: 3\n", Loader=yaml.SafeLoader))
        assert len(docs) == 3
        assert docs[0] == {'a': 1}
        assert docs[1] == {'b': 2}
        assert docs[2] == {'c': 3}

    def test_load_all_single_doc(self):
        """Load single document returns single item."""
        docs = list(yaml.load_all("foo: bar", Loader=yaml.SafeLoader))
        assert len(docs) == 1
        assert docs[0] == {'foo': 'bar'}

    def test_load_all_from_file(self):
        """Load all from file-like object."""
        stream = io.StringIO("---\nx: 1\n---\ny: 2\n")
        docs = list(yaml.load_all(stream, Loader=yaml.SafeLoader))
        assert len(docs) == 2

    def test_load_all_without_loader(self):
        """Load all without explicit Loader raises TypeError (PyYAML 5.1+ behavior)."""
        import pytest
        with pytest.raises(TypeError):
            list(yaml.load_all("foo: bar"))


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

    def test_compose_returns_node(self):
        """compose() should return a PyYAML Node object."""
        from libfyaml.pyyaml_compat.nodes import MappingNode
        node = yaml.compose("foo: bar")
        assert isinstance(node, MappingNode)

    def test_compose_simple_mapping(self):
        """compose() returns a MappingNode with correct structure."""
        from libfyaml.pyyaml_compat.nodes import MappingNode, ScalarNode
        node = yaml.compose("foo: bar")
        assert isinstance(node, MappingNode)
        assert len(node.value) == 1
        key, val = node.value[0]
        assert isinstance(key, ScalarNode)
        assert isinstance(val, ScalarNode)
        assert key.value == 'foo'
        assert val.value == 'bar'

    def test_compose_simple_sequence(self):
        """compose() with sequence."""
        from libfyaml.pyyaml_compat.nodes import SequenceNode
        node = yaml.compose("[1, 2, 3]")
        assert isinstance(node, SequenceNode)
        assert len(node.value) == 3

    def test_compose_nested(self):
        """compose() with nested structure."""
        from libfyaml.pyyaml_compat.nodes import MappingNode
        node = yaml.compose("a:\n  b:\n    c: 1")
        assert isinstance(node, MappingNode)

    def test_compose_with_loader(self):
        """compose() accepts Loader parameter."""
        from libfyaml.pyyaml_compat.nodes import MappingNode
        node = yaml.compose("foo: bar", Loader=yaml.SafeLoader)
        assert isinstance(node, MappingNode)

    def test_compose_from_file(self):
        """compose() from file-like object."""
        from libfyaml.pyyaml_compat.nodes import MappingNode
        stream = io.StringIO("key: value")
        node = yaml.compose(stream)
        assert isinstance(node, MappingNode)

    def test_compose_empty_returns_none(self):
        """compose() with empty string returns None."""
        node = yaml.compose("")
        assert node is None

    def test_compose_null_returns_node(self):
        """compose() with null returns a ScalarNode with null tag."""
        from libfyaml.pyyaml_compat.nodes import ScalarNode
        node = yaml.compose("null")
        assert isinstance(node, ScalarNode)
        assert node.tag == 'tag:yaml.org,2002:null'

    def test_compose_empty_mapping(self):
        """compose() with empty mapping."""
        from libfyaml.pyyaml_compat.nodes import MappingNode
        node = yaml.compose("{}")
        assert isinstance(node, MappingNode)
        assert node.value == []

    def test_compose_empty_sequence(self):
        """compose() with empty sequence."""
        from libfyaml.pyyaml_compat.nodes import SequenceNode
        node = yaml.compose("[]")
        assert isinstance(node, SequenceNode)
        assert node.value == []


class TestComposeAll:
    """Test compose_all() function."""

    def test_compose_all_multi_doc(self):
        """compose_all() returns multiple nodes."""
        from libfyaml.pyyaml_compat.nodes import MappingNode
        result = list(yaml.compose_all("---\na: 1\n---\nb: 2\n"))
        assert len(result) == 2
        assert all(isinstance(n, MappingNode) for n in result)

    def test_compose_all_single_doc(self):
        """compose_all() with single document."""
        from libfyaml.pyyaml_compat.nodes import MappingNode
        result = list(yaml.compose_all("foo: bar"))
        assert len(result) == 1
        assert isinstance(result[0], MappingNode)

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


class TestBinaryTag:
    """Test !!binary tag support (base64 encoding)."""

    def test_binary_basic(self):
        """Load base64-encoded binary data."""
        import base64
        data = yaml.safe_load("data: !!binary R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7")
        assert isinstance(data['data'], bytes)
        # This is a 1x1 transparent GIF
        assert data['data'][:3] == b'GIF'

    def test_binary_with_newlines(self):
        """Load binary with line breaks (common in YAML files)."""
        import base64
        # Base64 for "Hello, World!"
        data = yaml.safe_load("""
data: !!binary |
  SGVsbG8s
  IFdvcmxk
  IQ==
""")
        assert isinstance(data['data'], bytes)
        assert data['data'] == b'Hello, World!'

    def test_binary_empty(self):
        """Load empty binary data."""
        data = yaml.safe_load("data: !!binary ''")
        assert data['data'] == b''

    def test_binary_full_tag(self):
        """Load binary with full YAML tag."""
        data = yaml.safe_load("data: !<tag:yaml.org,2002:binary> SGVsbG8=")
        assert isinstance(data['data'], bytes)
        assert data['data'] == b'Hello'


class TestTimestampTag:
    """Test timestamp/datetime parsing."""

    def test_date_only(self):
        """Parse date-only value."""
        import datetime
        data = yaml.safe_load("date: 2024-01-15")
        assert isinstance(data['date'], datetime.date)
        assert data['date'] == datetime.date(2024, 1, 15)

    def test_datetime_basic(self):
        """Parse basic datetime."""
        import datetime
        data = yaml.safe_load("time: 2024-01-15 10:30:00")
        assert isinstance(data['time'], datetime.datetime)
        assert data['time'].year == 2024
        assert data['time'].month == 1
        assert data['time'].day == 15
        assert data['time'].hour == 10
        assert data['time'].minute == 30

    def test_datetime_with_t(self):
        """Parse datetime with T separator."""
        import datetime
        data = yaml.safe_load("time: 2024-01-15T10:30:00")
        assert isinstance(data['time'], datetime.datetime)
        assert data['time'].hour == 10

    def test_datetime_with_timezone_utc(self):
        """Parse datetime with UTC timezone."""
        import datetime
        data = yaml.safe_load("time: 2024-01-15T10:30:00Z")
        assert isinstance(data['time'], datetime.datetime)
        assert data['time'].tzinfo == datetime.timezone.utc

    def test_datetime_with_timezone_offset(self):
        """Parse datetime with timezone offset."""
        import datetime
        data = yaml.safe_load("time: 2024-01-15T10:30:00+05:30")
        assert isinstance(data['time'], datetime.datetime)
        assert data['time'].tzinfo is not None

    def test_datetime_with_microseconds(self):
        """Parse datetime with microseconds."""
        import datetime
        data = yaml.safe_load("time: 2024-01-15T10:30:00.123456")
        assert isinstance(data['time'], datetime.datetime)
        assert data['time'].microsecond == 123456

    def test_timestamp_explicit_tag(self):
        """Parse with explicit !!timestamp tag."""
        import datetime
        data = yaml.safe_load("time: !!timestamp 2024-01-15")
        assert isinstance(data['time'], datetime.date)


class TestSetTag:
    """Test !!set tag support."""

    def test_set_basic(self):
        """Load a basic set."""
        data = yaml.safe_load("""
items: !!set
  a:
  b:
  c:
""")
        assert isinstance(data['items'], set)
        assert data['items'] == {'a', 'b', 'c'}

    def test_set_flow_style(self):
        """Load set in flow style."""
        data = yaml.safe_load("items: !!set {a: null, b: null, c: null}")
        assert isinstance(data['items'], set)
        assert data['items'] == {'a', 'b', 'c'}

    def test_set_empty(self):
        """Load empty set."""
        data = yaml.safe_load("items: !!set {}")
        assert isinstance(data['items'], set)
        assert data['items'] == set()


class TestOmapTag:
    """Test !!omap tag support (ordered mapping).

    Note: PyYAML returns a list of tuples for !!omap, not an OrderedDict.
    """

    def test_omap_basic(self):
        """Load a basic ordered map."""
        data = yaml.safe_load("""
items: !!omap
  - a: 1
  - b: 2
  - c: 3
""")
        # PyYAML returns list of tuples for !!omap
        assert isinstance(data['items'], list)
        assert data['items'] == [('a', 1), ('b', 2), ('c', 3)]

    def test_omap_flow_style(self):
        """Load omap in flow style."""
        data = yaml.safe_load("items: !!omap [{a: 1}, {b: 2}, {c: 3}]")
        assert isinstance(data['items'], list)
        assert data['items'] == [('a', 1), ('b', 2), ('c', 3)]

    def test_omap_preserves_order(self):
        """Verify omap preserves insertion order."""
        data = yaml.safe_load("""
items: !!omap
  - z: 26
  - a: 1
  - m: 13
""")
        assert data['items'] == [('z', 26), ('a', 1), ('m', 13)]


class TestPairsTag:
    """Test !!pairs tag support."""

    def test_pairs_basic(self):
        """Load basic pairs."""
        data = yaml.safe_load("""
items: !!pairs
  - a: 1
  - b: 2
  - a: 3
""")
        assert isinstance(data['items'], list)
        assert data['items'] == [('a', 1), ('b', 2), ('a', 3)]

    def test_pairs_allows_duplicates(self):
        """Pairs allows duplicate keys (unlike omap)."""
        data = yaml.safe_load("""
items: !!pairs
  - x: 1
  - x: 2
  - x: 3
""")
        assert data['items'] == [('x', 1), ('x', 2), ('x', 3)]


class TestCustomRepresenters:
    """Test custom representers with add_representer()."""

    def test_add_representer_basic(self):
        """Test basic add_representer functionality."""
        class Point:
            def __init__(self, x, y):
                self.x = x
                self.y = y

        def point_representer(dumper, data):
            return {'x': data.x, 'y': data.y}

        # Create a fresh dumper class to avoid polluting shared state
        class TestDumper(yaml.SafeDumper):
            yaml_representers = yaml.SafeDumper.yaml_representers.copy()

        TestDumper.add_representer(Point, point_representer)
        result = yaml.dump({'point': Point(10, 20)}, Dumper=TestDumper)
        assert 'x: 10' in result
        assert 'y: 20' in result

    def test_add_representer_nested(self):
        """Test representer with nested data."""
        class Config:
            def __init__(self, name, values):
                self.name = name
                self.values = values

        def config_representer(dumper, data):
            return {'name': data.name, 'values': data.values}

        class TestDumper(yaml.SafeDumper):
            yaml_representers = yaml.SafeDumper.yaml_representers.copy()

        TestDumper.add_representer(Config, config_representer)
        config = Config('test', [1, 2, 3])
        result = yaml.dump(config, Dumper=TestDumper)
        assert 'name: test' in result
        assert '1' in result and '2' in result and '3' in result

    def test_multi_representer(self):
        """Test multi-representer for type hierarchy."""
        class Animal:
            def __init__(self, name):
                self.name = name

        class Dog(Animal):
            pass

        class Cat(Animal):
            pass

        def animal_representer(dumper, data):
            return {'type': type(data).__name__, 'name': data.name}

        class TestDumper(yaml.SafeDumper):
            yaml_representers = yaml.SafeDumper.yaml_representers.copy()
            yaml_multi_representers = {}

        TestDumper.add_multi_representer(Animal, animal_representer)
        result = yaml.dump([Dog('Buddy'), Cat('Whiskers')], Dumper=TestDumper)
        assert 'Dog' in result
        assert 'Cat' in result
        assert 'Buddy' in result
        assert 'Whiskers' in result


class TestDatetimeRepresenter:
    """Test datetime representation in dump()."""

    def test_dump_datetime(self):
        """Dump datetime object."""
        import datetime
        dt = datetime.datetime(2024, 1, 15, 10, 30, 0)
        result = yaml.safe_dump({'time': dt})
        assert '2024-01-15' in result
        assert '10:30:00' in result

    def test_dump_date(self):
        """Dump date object."""
        import datetime
        d = datetime.date(2024, 1, 15)
        result = yaml.safe_dump({'date': d})
        assert '2024-01-15' in result

    def test_dump_datetime_with_timezone(self):
        """Dump datetime with timezone."""
        import datetime
        dt = datetime.datetime(2024, 1, 15, 10, 30, 0, tzinfo=datetime.timezone.utc)
        result = yaml.safe_dump({'time': dt})
        assert '2024-01-15' in result


class TestSetRepresenter:
    """Test set representation in dump()."""

    def test_dump_set(self):
        """Dump set object."""
        data = {'items': {'a', 'b', 'c'}}
        result = yaml.safe_dump(data)
        # Set is converted to mapping with null values
        assert 'a:' in result or 'a :' in result

    def test_dump_frozenset(self):
        """Dump frozenset object."""
        data = {'items': frozenset(['x', 'y'])}
        result = yaml.safe_dump(data)
        assert 'x' in result and 'y' in result


class TestOrderedDictRepresenter:
    """Test OrderedDict representation in dump()."""

    def test_dump_ordereddict(self):
        """Dump OrderedDict object."""
        from collections import OrderedDict
        data = OrderedDict([('z', 26), ('a', 1), ('m', 13)])
        result = yaml.safe_dump(data)
        # OrderedDict is converted to regular dict for YAML
        assert 'z:' in result
        assert 'a:' in result
        assert 'm:' in result


class TestBytesRepresenter:
    """Test bytes representation in dump()."""

    def test_dump_bytes(self):
        """Dump bytes object."""
        data = {'data': b'Hello, World!'}
        result = yaml.safe_dump(data)
        # Bytes should be represented (as base64 or escaped)
        assert 'data' in result


class TestDatetimeRoundTrip:
    """Test datetime round-trip (load -> dump -> load)."""

    def test_date_roundtrip(self):
        """Round-trip date value."""
        import datetime
        original = {'date': datetime.date(2024, 1, 15)}
        dumped = yaml.safe_dump(original)
        loaded = yaml.safe_load(dumped)
        assert loaded['date'] == datetime.date(2024, 1, 15)

    def test_datetime_roundtrip(self):
        """Round-trip datetime value."""
        import datetime
        original = {'time': datetime.datetime(2024, 1, 15, 10, 30, 0)}
        dumped = yaml.safe_dump(original)
        loaded = yaml.safe_load(dumped)
        assert isinstance(loaded['time'], datetime.datetime)
        assert loaded['time'].year == 2024
        assert loaded['time'].month == 1
        assert loaded['time'].day == 15


class TestSetOmapRoundTrip:
    """Test set/omap round-trip."""

    def test_ordereddict_roundtrip(self):
        """Round-trip OrderedDict."""
        from collections import OrderedDict
        original = OrderedDict([('a', 1), ('b', 2), ('c', 3)])
        dumped = yaml.safe_dump(original)
        loaded = yaml.safe_load(dumped)
        # After round-trip, becomes regular dict (order preserved in Python 3.7+)
        assert loaded == {'a': 1, 'b': 2, 'c': 3}


class TestEdgeCasesAdvanced:
    """Additional edge case tests for comprehensive coverage."""

    def test_special_float_inf(self):
        """Parse infinity values."""
        data = yaml.safe_load("""
pos_inf: .inf
neg_inf: -.inf
""")
        import math
        assert math.isinf(data['pos_inf']) and data['pos_inf'] > 0
        assert math.isinf(data['neg_inf']) and data['neg_inf'] < 0

    def test_special_float_nan(self):
        """Parse NaN value."""
        data = yaml.safe_load("value: .nan")
        import math
        assert math.isnan(data['value'])

    def test_octal_integer_yaml11(self):
        """Parse octal integer (YAML 1.1 style).

        Note: YAML 1.1 uses 0 prefix (e.g., 0755) for octal.
        """
        data = yaml.safe_load("value: 0755")
        assert data['value'] == 0o755  # 493 in decimal

    def test_hex_integer(self):
        """Parse hexadecimal integer."""
        data = yaml.safe_load("value: 0xFF")
        assert data['value'] == 255

    def test_scientific_notation(self):
        """Parse scientific notation float.

        Note: PyYAML requires explicit sign on exponent (e+/e-).
        '1.5e10' is a string in PyYAML; '1.5e+10' is a float.
        """
        data = yaml.safe_load("value: 1.5e+10")
        assert data['value'] == 1.5e10

    def test_quoted_strings(self):
        """Parse single and double quoted strings."""
        data = yaml.safe_load("""
single: 'hello world'
double: "hello world"
single_escape: 'it''s ok'
double_escape: "hello\\nworld"
""")
        assert data['single'] == 'hello world'
        assert data['double'] == 'hello world'
        assert data['single_escape'] == "it's ok"
        assert data['double_escape'] == 'hello\nworld'

    def test_folded_string(self):
        """Parse folded string (>)."""
        data = yaml.safe_load("""
text: >
  This is a
  folded string
  on multiple lines.
""")
        # Folded strings join lines with spaces
        assert 'This is a folded string' in data['text']

    def test_literal_string(self):
        """Parse literal string (|)."""
        data = yaml.safe_load("""
text: |
  line1
  line2
  line3
""")
        # Literal strings preserve newlines
        assert 'line1\n' in data['text']
        assert 'line2\n' in data['text']

    def test_complex_keys(self):
        """Parse mapping with complex keys."""
        data = yaml.safe_load("""
? key with spaces
: value1
? 123
: value2
""")
        assert data['key with spaces'] == 'value1'
        assert data[123] == 'value2'

    def test_nested_empty_collections(self):
        """Parse nested empty collections."""
        data = yaml.safe_load("""
empty_list: []
empty_dict: {}
nested:
  list: []
  dict: {}
""")
        assert data['empty_list'] == []
        assert data['empty_dict'] == {}
        assert data['nested']['list'] == []
        assert data['nested']['dict'] == {}

    def test_deep_nesting(self):
        """Parse deeply nested structure."""
        data = yaml.safe_load("""
a:
  b:
    c:
      d:
        e:
          f: deep
""")
        assert data['a']['b']['c']['d']['e']['f'] == 'deep'

    def test_mixed_collection_types(self):
        """Parse mixed collection types."""
        data = yaml.safe_load("""
- key: value
- [1, 2, 3]
- nested:
    list: [a, b]
""")
        assert data[0] == {'key': 'value'}
        assert data[1] == [1, 2, 3]
        assert data[2]['nested']['list'] == ['a', 'b']


class TestAnchorsAndAliases:
    """Test YAML anchors and aliases."""

    def test_basic_anchor_alias(self):
        """Parse basic anchor and alias."""
        data = yaml.safe_load("""
default: &default
  timeout: 30
  retries: 3
production:
  <<: *default
  timeout: 60
""")
        assert data['default']['timeout'] == 30
        assert data['production']['timeout'] == 60
        assert data['production']['retries'] == 3

    def test_multiple_aliases(self):
        """Parse multiple aliases to same anchor."""
        data = yaml.safe_load("""
shared: &shared value
ref1: *shared
ref2: *shared
""")
        assert data['shared'] == 'value'
        assert data['ref1'] == 'value'
        assert data['ref2'] == 'value'

    def test_sequence_anchor(self):
        """Parse anchor on sequence."""
        data = yaml.safe_load("""
items: &items
  - a
  - b
  - c
copy: *items
""")
        assert data['items'] == ['a', 'b', 'c']
        assert data['copy'] == ['a', 'b', 'c']


class TestMergeKeys:
    """Test YAML merge key (<<) support."""

    def test_simple_merge(self):
        """Simple merge key."""
        data = yaml.safe_load("""
base: &base
  name: default
  value: 0
extended:
  <<: *base
  value: 100
""")
        assert data['extended']['name'] == 'default'
        assert data['extended']['value'] == 100

    def test_multiple_merge(self):
        """Multiple merge keys."""
        data = yaml.safe_load("""
a: &a {x: 1}
b: &b {y: 2}
c:
  <<: *a
  <<: *b
  z: 3
""")
        assert data['c']['x'] == 1
        assert data['c']['y'] == 2
        assert data['c']['z'] == 3


class TestErrorHandling:
    """Test error handling for invalid YAML."""

    def test_invalid_yaml_raises_error(self):
        """Invalid YAML should raise appropriate error."""
        with pytest.raises((yaml.YAMLError, yaml.ScannerError, yaml.ParserError)):
            yaml.safe_load("foo: [unclosed")

    def test_invalid_indentation_raises_error(self):
        """Invalid indentation should raise error."""
        with pytest.raises((yaml.YAMLError, yaml.ScannerError, yaml.ParserError)):
            yaml.safe_load("""
foo:
  bar: 1
 baz: 2
""")

    def test_duplicate_keys_handled(self):
        """Duplicate keys should be handled (last wins)."""
        # Note: Some parsers warn, others accept
        data = yaml.safe_load("""
key: first
key: second
""")
        # Should not raise, last value wins
        assert data['key'] == 'second'


class TestYAMLObjectClass:
    """Test YAMLObject class and metaclass."""

    def test_yaml_object_exists(self):
        """YAMLObject class should exist."""
        assert hasattr(yaml, 'YAMLObject')

    def test_yaml_object_metaclass_exists(self):
        """YAMLObjectMetaclass should exist."""
        assert hasattr(yaml, 'YAMLObjectMetaclass')

    def test_yaml_object_subclass(self):
        """Test creating YAMLObject subclass."""
        class MyObject(yaml.YAMLObject):
            yaml_tag = '!myobj'
            yaml_loader = yaml.SafeLoader
            yaml_dumper = yaml.SafeDumper

            def __init__(self, value=None):
                self.value = value

        # Should be able to instantiate
        obj = MyObject(42)
        assert obj.value == 42


class TestModuleLevelFunctions:
    """Test module-level constructor/representer functions."""

    def test_module_add_constructor(self):
        """Test module-level add_constructor."""
        # Create isolated loader to avoid pollution
        class IsolatedLoader(yaml.SafeLoader):
            yaml_constructors = yaml.SafeLoader.yaml_constructors.copy()

        def custom_ctor(loader, node):
            return f"CUSTOM:{node.value}"

        yaml.add_constructor('!test', custom_ctor, Loader=IsolatedLoader)
        result = yaml.load("value: !test hello", Loader=IsolatedLoader)
        assert result['value'] == 'CUSTOM:hello'

    def test_module_add_representer(self):
        """Test module-level add_representer."""
        class IsolatedDumper(yaml.SafeDumper):
            yaml_representers = yaml.SafeDumper.yaml_representers.copy()

        class CustomClass:
            def __init__(self, x):
                self.x = x

        def custom_rep(dumper, data):
            return {'x': data.x}

        yaml.add_representer(CustomClass, custom_rep, Dumper=IsolatedDumper)
        result = yaml.dump({'obj': CustomClass(99)}, Dumper=IsolatedDumper)
        assert 'x: 99' in result


class TestMultiConstructor:
    """Test multi-constructor functionality."""

    def test_multi_constructor_prefix(self):
        """Test multi-constructor with tag prefix."""
        class IsolatedLoader(yaml.SafeLoader):
            yaml_constructors = yaml.SafeLoader.yaml_constructors.copy()
            yaml_multi_constructors = {}

        def env_multi_ctor(loader, suffix, node):
            return f"ENV_{suffix}:{node.value}"

        IsolatedLoader.add_multi_constructor('!env:', env_multi_ctor)
        result = yaml.load("var: !env:HOME /home/user", Loader=IsolatedLoader)
        assert result['var'] == 'ENV_HOME:/home/user'

    def test_multi_constructor_none_catchall(self):
        """Test multi-constructor with None (catch-all)."""
        class IsolatedLoader(yaml.SafeLoader):
            yaml_constructors = {}
            yaml_multi_constructors = {}

        def catchall_ctor(loader, tag, node):
            return f"TAG[{tag}]:{node.value}"

        IsolatedLoader.add_multi_constructor(None, catchall_ctor)
        result = yaml.load("value: !anything test", Loader=IsolatedLoader)
        assert 'TAG[!anything]:test' == result['value']


class TestSortKeys:
    """Test sort_keys parameter in dump."""

    def test_dump_sort_keys_true(self):
        """Dump with sort_keys=True (default)."""
        data = {'z': 1, 'a': 2, 'm': 3}
        result = yaml.safe_dump(data, sort_keys=True)
        lines = result.strip().split('\n')
        # Keys should be sorted
        keys = [line.split(':')[0] for line in lines]
        assert keys == sorted(keys)

    def test_dump_sort_keys_false(self):
        """Dump with sort_keys=False."""
        from collections import OrderedDict
        data = OrderedDict([('z', 1), ('a', 2), ('m', 3)])
        result = yaml.safe_dump(data, sort_keys=False)
        # Keys should preserve order
        assert result.index('z:') < result.index('a:') < result.index('m:')


class TestNodeTypes:
    """Test node type classes."""

    def test_scalar_node_exists(self):
        """ScalarNode should exist."""
        assert hasattr(yaml, 'ScalarNode')

    def test_sequence_node_exists(self):
        """SequenceNode should exist."""
        assert hasattr(yaml, 'SequenceNode')

    def test_mapping_node_exists(self):
        """MappingNode should exist."""
        assert hasattr(yaml, 'MappingNode')

    def test_node_exists(self):
        """Node base class should exist."""
        assert hasattr(yaml, 'Node')

    def test_create_scalar_node(self):
        """Create ScalarNode instance."""
        node = yaml.ScalarNode('tag:yaml.org,2002:str', 'hello')
        assert node.tag == 'tag:yaml.org,2002:str'
        assert node.value == 'hello'

    def test_create_sequence_node(self):
        """Create SequenceNode instance."""
        node = yaml.SequenceNode('tag:yaml.org,2002:seq', [])
        assert node.tag == 'tag:yaml.org,2002:seq'
        assert node.value == []

    def test_create_mapping_node(self):
        """Create MappingNode instance."""
        node = yaml.MappingNode('tag:yaml.org,2002:map', [])
        assert node.tag == 'tag:yaml.org,2002:map'
        assert node.value == []


class TestVersionInfo:
    """Test version and compatibility info."""

    def test_version_exists(self):
        """__version__ should exist."""
        assert hasattr(yaml, '__version__')
        assert isinstance(yaml.__version__, str)

    def test_with_libyaml_exists(self):
        """__with_libyaml__ should exist."""
        assert hasattr(yaml, '__with_libyaml__')
        assert yaml.__with_libyaml__ is True


class TestUnhashableKeys:
    """Test handling of mappings with unhashable keys.

    In YAML, mappings and sequences can be used as mapping keys.
    This is valid YAML but can't be represented in Python dicts.
    These tests verify proper ConstructorError handling.
    """

    def test_mapping_key_raises_constructor_error(self):
        """A mapping used as a key should raise ConstructorError."""
        with pytest.raises(yaml.ConstructorError, match="unhashable key"):
            yaml.safe_load("{}: bad")

    def test_sequence_key_raises_constructor_error(self):
        """A sequence used as a key should raise ConstructorError."""
        with pytest.raises(yaml.ConstructorError, match="unhashable key"):
            yaml.safe_load("[]: bad")

    def test_template_braces_raises_constructor_error(self):
        """{{ bar }} is valid YAML (nested flow mappings) but has unhashable keys."""
        with pytest.raises(yaml.ConstructorError, match="unhashable key"):
            yaml.safe_load("{{ bar }}")

    def test_template_in_mapping_value(self):
        """foo: {{ bar }} - template in mapping value position."""
        with pytest.raises(yaml.ConstructorError, match="unhashable key"):
            yaml.safe_load("foo: {{ bar }}")

    def test_template_in_sequence(self):
        """- {{ bar }} - template in sequence item position."""
        with pytest.raises(yaml.ConstructorError, match="unhashable key"):
            yaml.safe_load("- {{ bar }}")

    def test_template_nested_in_list_and_dict(self):
        """Deeply nested template pattern."""
        with pytest.raises(yaml.ConstructorError, match="unhashable key"):
            yaml.safe_load(" -  - foo: {{ bar }}")

    def test_constructor_error_is_marked_yaml_error(self):
        """ConstructorError must be a MarkedYAMLError for Ansible compatibility."""
        assert issubclass(yaml.ConstructorError, yaml.MarkedYAMLError)
        # Must be a real subclass, not just an alias
        assert yaml.ConstructorError is not yaml.MarkedYAMLError

    def test_constructor_error_has_problem_mark(self):
        """ConstructorError should include problem_mark for error reporting."""
        try:
            yaml.safe_load("{}: bad")
            assert False, "Should have raised"
        except yaml.ConstructorError as e:
            assert hasattr(e, 'problem_mark')
            assert hasattr(e, 'problem')
            assert 'unhashable' in e.problem

    def test_unhashable_key_with_custom_loader(self):
        """Unhashable keys should be caught even with custom Loader classes."""
        class CustomLoader(yaml.SafeLoader):
            pass

        with pytest.raises(yaml.ConstructorError, match="unhashable key"):
            yaml.load("{{ bar }}", Loader=CustomLoader)


class TestFyGenericEdgeCases:
    """Test FyGeneric C extension edge cases.

    FyGeneric objects from the C extension have some surprising behaviors
    that differ from normal Python objects. These tests document and
    verify the workarounds.
    """

    def test_mapping_value_property_raises_typeerror_for_unhashable(self):
        """FyGeneric.value raises TypeError for mappings with unhashable keys.

        Regular mappings raise AttributeError (converted to dict first),
        but unhashable mappings can't be converted, triggering TypeError.
        """
        import libfyaml as fy
        g = fy.loads("{{ bar }}")
        assert g.is_mapping()
        with pytest.raises(TypeError, match="unhashable"):
            _ = g.value

    def test_mapping_hasattr_value_raises_typeerror_for_unhashable(self):
        """hasattr() on unhashable mapping raises TypeError, not returns False.

        hasattr() only catches AttributeError, so TypeError from the
        C extension's property getter propagates to the caller.
        """
        import libfyaml as fy
        g = fy.loads("{{ bar }}")
        # hasattr only catches AttributeError, not TypeError
        with pytest.raises(TypeError, match="unhashable"):
            hasattr(g, 'value')

    def test_regular_mapping_value_raises_attributeerror(self):
        """Regular mapping .value raises AttributeError (converts to dict)."""
        import libfyaml as fy
        g = fy.loads("{a: 1}")
        assert g.is_mapping()
        with pytest.raises(AttributeError):
            _ = g.value

    def test_items_raises_for_unhashable_keys(self):
        """FyGeneric.items() raises TypeError when keys are unhashable."""
        import libfyaml as fy
        g = fy.loads("{{ bar }}")
        assert g.is_mapping()
        with pytest.raises(TypeError, match="unhashable"):
            list(g.items())

    def test_to_python_raises_for_unhashable_keys(self):
        """FyGeneric.to_python() raises TypeError for unhashable keys."""
        import libfyaml as fy
        g = fy.loads("{{ bar }}")
        with pytest.raises(TypeError, match="unhashable"):
            g.to_python()

    def test_sequence_containing_unhashable_mapping(self):
        """A sequence containing a mapping with unhashable keys."""
        import libfyaml as fy
        g = fy.loads("- {{ bar }}")
        assert g.is_sequence()
        # Iterating the sequence works, but to_python fails
        with pytest.raises(TypeError, match="unhashable"):
            g.to_python()


class TestNodeConversion:
    """Test FyGeneric to PyYAML Node tree conversion.

    The _fygeneric_to_node function bridges FyGeneric objects to
    PyYAML-style Node trees for constructors that expect them.
    """

    def test_fygeneric_to_node_basic_mapping(self):
        """Basic mapping converts to MappingNode."""
        import libfyaml as fy
        doc = fy.loads("a: 1", mode='yaml1.1-pyyaml', create_markers=True)
        node = yaml._fygeneric_to_node(doc, '<test>')
        assert isinstance(node, yaml.MappingNode)
        assert node.tag == 'tag:yaml.org,2002:map'

    def test_fygeneric_to_node_basic_sequence(self):
        """Basic sequence converts to SequenceNode."""
        import libfyaml as fy
        doc = fy.loads("[1, 2, 3]", mode='yaml1.1-pyyaml', create_markers=True)
        node = yaml._fygeneric_to_node(doc, '<test>')
        assert isinstance(node, yaml.SequenceNode)
        assert node.tag == 'tag:yaml.org,2002:seq'

    def test_fygeneric_to_node_scalar(self):
        """Scalar converts to ScalarNode."""
        import libfyaml as fy
        doc = fy.loads("hello", mode='yaml1.1-pyyaml', create_markers=True)
        node = yaml._fygeneric_to_node(doc, '<test>')
        assert isinstance(node, yaml.ScalarNode)

    def test_fygeneric_to_node_unhashable_key_raises(self):
        """Converting a mapping with unhashable keys raises ConstructorError."""
        import libfyaml as fy
        doc = fy.loads("{{ bar }}", mode='yaml1.1-pyyaml', create_markers=True)
        with pytest.raises(yaml.ConstructorError, match="unhashable key"):
            yaml._fygeneric_to_node(doc, '<test>')

    def test_fygeneric_to_node_nested_unhashable(self):
        """Unhashable keys nested inside sequences are caught."""
        import libfyaml as fy
        doc = fy.loads("- {{ bar }}", mode='yaml1.1-pyyaml', create_markers=True)
        with pytest.raises(yaml.ConstructorError, match="unhashable key"):
            yaml._fygeneric_to_node(doc, '<test>')

    def test_fygeneric_to_node_preserves_marks(self):
        """Node conversion preserves start/end marks."""
        import libfyaml as fy
        doc = fy.loads("a: 1", mode='yaml1.1-pyyaml', create_markers=True)
        node = yaml._fygeneric_to_node(doc, 'test.yml')
        assert node.start_mark is not None
        assert node.start_mark.name == 'test.yml'

    def test_fygeneric_to_node_preserves_custom_tag(self):
        """Node conversion preserves custom tags."""
        import libfyaml as fy
        doc = fy.loads("!custom value", mode='yaml1.1-pyyaml', create_markers=True)
        node = yaml._fygeneric_to_node(doc, '<test>')
        assert node.tag == '!custom'


class TestDiagnosticSequenceGuard:
    """Test that the diagnostic sequence check in load() handles edge cases.

    When libfyaml returns a sequence, load() checks if it's an error
    diagnostic. This check must not fail on valid sequences that
    contain unhashable types.
    """

    def test_sequence_with_unhashable_not_mistaken_for_diag(self):
        """A valid sequence with unhashable keys should not crash the diag check."""
        # This should raise ConstructorError (from unhashable key), not TypeError
        with pytest.raises(yaml.ConstructorError):
            yaml.safe_load("- {{ bar }}")

    def test_normal_sequence_not_affected(self):
        """Normal sequences still work after the diagnostic guard."""
        data = yaml.safe_load("- 1\n- 2\n- 3")
        assert data == [1, 2, 3]

    def test_sequence_of_mappings_works(self):
        """Sequences of regular mappings work fine."""
        data = yaml.safe_load("- a: 1\n- b: 2")
        assert data == [{'a': 1}, {'b': 2}]

    def test_empty_sequence_works(self):
        """Empty sequences work fine."""
        data = yaml.safe_load("[]")
        assert data == []


class TestTagNodeTypeMismatch:
    """Test that tag/node type mismatches raise ConstructorError.

    When a tag like !!map or !!seq is applied to the wrong node type
    (e.g., !!map on a scalar), the constructor should raise a proper
    ConstructorError rather than letting a ValueError or TypeError leak.
    """

    def test_map_tag_on_scalar_raises(self):
        """!!map on a scalar should raise ConstructorError."""
        with pytest.raises(yaml.ConstructorError, match="expected a mapping node, but found scalar"):
            yaml.safe_load("!!map 1")

    def test_map_tag_on_scalar_with_tabs(self):
        """!!map on a scalar containing tabs should raise ConstructorError."""
        with pytest.raises(yaml.ConstructorError, match="expected a mapping node, but found scalar"):
            yaml.safe_load("!!map 1\t2\t3")

    def test_map_tag_on_sequence_raises(self):
        """!!map on a sequence should raise ConstructorError."""
        with pytest.raises(yaml.ConstructorError, match="expected a mapping node, but found sequence"):
            yaml.safe_load("!!map [1, 2]")

    def test_seq_tag_on_scalar_raises(self):
        """!!seq on a scalar should raise ConstructorError."""
        with pytest.raises(yaml.ConstructorError, match="expected a sequence node, but found scalar"):
            yaml.safe_load("!!seq hello")

    def test_seq_tag_on_mapping_raises(self):
        """!!seq on a mapping should raise ConstructorError."""
        with pytest.raises(yaml.ConstructorError, match="expected a sequence node, but found mapping"):
            yaml.safe_load("!!seq {a: 1}")

    def test_map_tag_on_valid_mapping_works(self):
        """!!map on an actual mapping should work fine."""
        data = yaml.safe_load("!!map {a: 1}")
        assert data == {'a': 1}

    def test_seq_tag_on_valid_sequence_works(self):
        """!!seq on an actual sequence should work fine."""
        data = yaml.safe_load("!!seq [1, 2]")
        assert data == [1, 2]


class TestBareColonDetection:
    """Test that bare ':' is rejected for PyYAML compatibility.

    A bare ':' is valid YAML 1.2 (mapping with null key and null value),
    but PyYAML rejects it. libfyaml detects this in pyyaml mode and raises
    an error for compatibility.
    """

    def test_bare_colon_raises(self):
        """Bare ':' should raise a YAML error."""
        with pytest.raises(yaml.YAMLError, match="bare"):
            yaml.safe_load(":")

    def test_bare_colon_with_load(self):
        """Bare ':' should also fail with yaml.load."""
        with pytest.raises(yaml.YAMLError, match="bare"):
            yaml.load(":", Loader=yaml.SafeLoader)

    def test_explicit_null_key_works(self):
        """Explicit null key '~:' should work (PyYAML accepts it)."""
        data = yaml.safe_load("~:")
        assert data == {None: None}

    def test_explicit_null_key_value_works(self):
        """Explicit null key/value '~: ~' should work."""
        data = yaml.safe_load("~: ~")
        assert data == {None: None}

    def test_null_literal_key_works(self):
        """'null: null' should work."""
        data = yaml.safe_load("null: null")
        assert data == {None: None}

    def test_colon_with_value_works(self):
        """': value' is a valid mapping with null key."""
        data = yaml.safe_load(": value")
        assert data == {None: 'value'}
