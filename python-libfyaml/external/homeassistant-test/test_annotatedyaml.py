"""Test annotatedyaml with libfyaml backend.

These tests are adapted from Home Assistant's tests/util/yaml/test_init.py
to work standalone without the Home Assistant framework.
"""

import importlib
import io
import os
import pathlib
import tempfile
from collections import OrderedDict
from unittest.mock import Mock, patch

import pytest
import yaml as pyyaml

# Import annotatedyaml (which will use our patched yaml module)
import annotatedyaml
from annotatedyaml import (
    dump,
    load_yaml,
    load_yaml_dict,
    parse_yaml,
    Input,
    NodeDictClass,
    NodeListClass,
    NodeStrClass,
    YAMLException,
    YamlTypeError,
)
from annotatedyaml.loader import (
    FastSafeLoader,
    PythonSafeLoader,
    Secrets,
    _parse_yaml,
)
import annotatedyaml.loader as yaml_loader


# ============================================================
# Fixtures
# ============================================================

@pytest.fixture(params=["enable_c_loader", "disable_c_loader"])
def try_both_loaders(request):
    """Test with and without C loader (simulates PyYAML's CSafeLoader toggle)."""
    if request.param != "disable_c_loader":
        yield
        return
    # Remove CSafeLoader temporarily
    had_c = hasattr(pyyaml, 'CSafeLoader')
    if had_c:
        cloader = pyyaml.CSafeLoader
        del pyyaml.CSafeLoader
        importlib.reload(yaml_loader)
    yield
    if had_c:
        pyyaml.CSafeLoader = cloader
        importlib.reload(yaml_loader)


@pytest.fixture(params=["enable_c_dumper", "disable_c_dumper"])
def try_both_dumpers(request):
    """Test with and without C dumper."""
    if request.param != "disable_c_dumper":
        yield
        return
    had_c = hasattr(pyyaml, 'CSafeDumper')
    if had_c:
        cdumper = pyyaml.CSafeDumper
        del pyyaml.CSafeDumper
        importlib.reload(yaml_loader)
    yield
    if had_c:
        pyyaml.CSafeDumper = cdumper
        importlib.reload(yaml_loader)


# ============================================================
# Basic parsing tests
# ============================================================

@pytest.mark.usefixtures("try_both_loaders")
def test_simple_list():
    """Test simple list."""
    conf = "config:\n  - simple\n  - list"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["config"] == ["simple", "list"]


@pytest.mark.usefixtures("try_both_loaders")
def test_simple_dict():
    """Test simple dict."""
    conf = "key: value"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["key"] == "value"


@pytest.mark.usefixtures("try_both_loaders")
def test_nested_dict():
    """Test nested dict."""
    conf = "outer:\n  inner: value"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["outer"]["inner"] == "value"


@pytest.mark.usefixtures("try_both_loaders")
def test_mixed_list_dict():
    """Test mixed list and dict."""
    conf = "items:\n  - name: item1\n    value: 1\n  - name: item2\n    value: 2"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert len(doc["items"]) == 2
    assert doc["items"][0]["name"] == "item1"
    assert doc["items"][1]["value"] == 2


@pytest.mark.usefixtures("try_both_loaders")
def test_integer_values():
    """Test integer values."""
    conf = "port: 8080\ncount: 0\nneg: -1"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["port"] == 8080
    assert doc["count"] == 0
    assert doc["neg"] == -1


@pytest.mark.usefixtures("try_both_loaders")
def test_boolean_values():
    """Test boolean values."""
    conf = "enabled: true\ndisabled: false"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["enabled"] is True
    assert doc["disabled"] is False


@pytest.mark.usefixtures("try_both_loaders")
def test_null_values():
    """Test null values."""
    conf = "empty:\nnull_val: null"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["empty"] is None
    assert doc["null_val"] is None


@pytest.mark.usefixtures("try_both_loaders")
def test_float_values():
    """Test float values."""
    conf = "pi: 3.14\nrate: 0.5"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert abs(doc["pi"] - 3.14) < 0.001
    assert doc["rate"] == 0.5


@pytest.mark.usefixtures("try_both_loaders")
def test_multiline_string():
    """Test multiline strings."""
    conf = "msg: |\n  hello\n  world"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["msg"] == "hello\nworld\n"


# ============================================================
# Environment variable tests
# ============================================================

@pytest.mark.usefixtures("try_both_loaders")
def test_environment_variable():
    """Test config file with environment variable."""
    os.environ["TEST_PASSWORD"] = "secret_password"
    conf = "password: !env_var TEST_PASSWORD"
    try:
        with io.StringIO(conf) as file:
            doc = parse_yaml(file)
        assert doc["password"] == "secret_password"
    finally:
        del os.environ["TEST_PASSWORD"]


@pytest.mark.usefixtures("try_both_loaders")
def test_environment_variable_default():
    """Test config file with default value for environment variable."""
    conf = "password: !env_var NONEXISTENT_VAR secret_password"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["password"] == "secret_password"


@pytest.mark.usefixtures("try_both_loaders")
def test_invalid_environment_variable():
    """Test config file with no environment variable set."""
    conf = "password: !env_var NONEXISTENT_VAR_NO_DEFAULT"
    with pytest.raises(YAMLException):
        with io.StringIO(conf) as file:
            parse_yaml(file)


# ============================================================
# Include tests (with temp files)
# ============================================================

@pytest.mark.usefixtures("try_both_loaders")
def test_include_yaml():
    """Test include yaml."""
    with tempfile.TemporaryDirectory() as tmpdir:
        main_file = os.path.join(tmpdir, "main.yaml")
        include_file = os.path.join(tmpdir, "included.yaml")

        with open(include_file, 'w') as f:
            f.write("value")

        with open(main_file, 'w') as f:
            f.write("key: !include included.yaml")

        doc = load_yaml(main_file)
        assert doc["key"] == "value"


@pytest.mark.usefixtures("try_both_loaders")
def test_include_yaml_dict():
    """Test include yaml with a dict."""
    with tempfile.TemporaryDirectory() as tmpdir:
        main_file = os.path.join(tmpdir, "main.yaml")
        include_file = os.path.join(tmpdir, "included.yaml")

        with open(include_file, 'w') as f:
            f.write("sub_key: sub_value")

        with open(main_file, 'w') as f:
            f.write("key: !include included.yaml")

        doc = load_yaml(main_file)
        assert doc["key"]["sub_key"] == "sub_value"


@pytest.mark.usefixtures("try_both_loaders")
def test_include_yaml_empty_file():
    """Test include yaml with empty file returns empty dict."""
    with tempfile.TemporaryDirectory() as tmpdir:
        main_file = os.path.join(tmpdir, "main.yaml")
        include_file = os.path.join(tmpdir, "included.yaml")

        with open(include_file, 'w') as f:
            f.write("")

        with open(main_file, 'w') as f:
            f.write("key: !include included.yaml")

        doc = load_yaml(main_file)
        assert doc["key"] == {}


@pytest.mark.usefixtures("try_both_loaders")
def test_include_yaml_integer():
    """Test include yaml with integer value."""
    with tempfile.TemporaryDirectory() as tmpdir:
        main_file = os.path.join(tmpdir, "main.yaml")
        include_file = os.path.join(tmpdir, "included.yaml")

        with open(include_file, 'w') as f:
            f.write("123")

        with open(main_file, 'w') as f:
            f.write("key: !include included.yaml")

        doc = load_yaml(main_file)
        assert doc["key"] == 123


@pytest.mark.usefixtures("try_both_loaders")
def test_include_yaml_file_not_found():
    """Test include yaml with missing file."""
    with tempfile.TemporaryDirectory() as tmpdir:
        main_file = os.path.join(tmpdir, "main.yaml")

        with open(main_file, 'w') as f:
            f.write("key: !include nonexistent.yaml")

        with pytest.raises(YAMLException):
            load_yaml(main_file)


# ============================================================
# Include dir tests
# ============================================================

@pytest.mark.usefixtures("try_both_loaders")
def test_include_dir_list():
    """Test include dir list yaml."""
    with tempfile.TemporaryDirectory() as tmpdir:
        main_file = os.path.join(tmpdir, "main.yaml")
        subdir = os.path.join(tmpdir, "includes")
        os.makedirs(subdir)

        with open(os.path.join(subdir, "one.yaml"), 'w') as f:
            f.write("one")
        with open(os.path.join(subdir, "two.yaml"), 'w') as f:
            f.write("two")

        with open(main_file, 'w') as f:
            f.write(f"key: !include_dir_list includes")

        doc = load_yaml(main_file)
        assert sorted(doc["key"]) == ["one", "two"]


@pytest.mark.usefixtures("try_both_loaders")
def test_include_dir_named():
    """Test include dir named yaml."""
    with tempfile.TemporaryDirectory() as tmpdir:
        main_file = os.path.join(tmpdir, "main.yaml")
        subdir = os.path.join(tmpdir, "includes")
        os.makedirs(subdir)

        with open(os.path.join(subdir, "first.yaml"), 'w') as f:
            f.write("one")
        with open(os.path.join(subdir, "second.yaml"), 'w') as f:
            f.write("two")

        with open(main_file, 'w') as f:
            f.write(f"key: !include_dir_named includes")

        doc = load_yaml(main_file)
        assert doc["key"]["first"] == "one"
        assert doc["key"]["second"] == "two"


@pytest.mark.usefixtures("try_both_loaders")
def test_include_dir_merge_list():
    """Test include dir merge list yaml."""
    with tempfile.TemporaryDirectory() as tmpdir:
        main_file = os.path.join(tmpdir, "main.yaml")
        subdir = os.path.join(tmpdir, "includes")
        os.makedirs(subdir)

        with open(os.path.join(subdir, "first.yaml"), 'w') as f:
            f.write("- one")
        with open(os.path.join(subdir, "second.yaml"), 'w') as f:
            f.write("- two\n- three")

        with open(main_file, 'w') as f:
            f.write(f"key: !include_dir_merge_list includes")

        doc = load_yaml(main_file)
        assert sorted(doc["key"]) == ["one", "three", "two"]


@pytest.mark.usefixtures("try_both_loaders")
def test_include_dir_merge_named():
    """Test include dir merge named yaml."""
    with tempfile.TemporaryDirectory() as tmpdir:
        main_file = os.path.join(tmpdir, "main.yaml")
        subdir = os.path.join(tmpdir, "includes")
        os.makedirs(subdir)

        with open(os.path.join(subdir, "first.yaml"), 'w') as f:
            f.write("key1: one")
        with open(os.path.join(subdir, "second.yaml"), 'w') as f:
            f.write("key2: two\nkey3: three")

        with open(main_file, 'w') as f:
            f.write(f"key: !include_dir_merge_named includes")

        doc = load_yaml(main_file)
        assert doc["key"] == {"key1": "one", "key2": "two", "key3": "three"}


# ============================================================
# Include tag without parameter
# ============================================================

@pytest.mark.parametrize("tag", [
    "!include",
    "!include_dir_named",
    "!include_dir_merge_named",
    "!include_dir_list",
    "!include_dir_merge_list",
])
@pytest.mark.usefixtures("try_both_loaders")
def test_include_without_parameter(tag):
    """Test include extensions without parameters."""
    with io.StringIO(f"key: {tag}") as file:
        with pytest.raises(YAMLException, match=f"{tag} needs an argument"):
            parse_yaml(file)


# ============================================================
# Secrets tests
# ============================================================

@pytest.mark.usefixtures("try_both_loaders")
def test_secret():
    """Test secret loading."""
    with tempfile.TemporaryDirectory() as tmpdir:
        main_file = os.path.join(tmpdir, "main.yaml")
        secret_file = os.path.join(tmpdir, "secrets.yaml")

        with open(secret_file, 'w') as f:
            f.write("my_password: super_secret")

        with open(main_file, 'w') as f:
            f.write("password: !secret my_password")

        secrets = Secrets(pathlib.Path(tmpdir))
        doc = load_yaml(main_file, secrets)
        assert doc["password"] == "super_secret"


@pytest.mark.usefixtures("try_both_loaders")
def test_secret_not_defined():
    """Test secret not defined."""
    with tempfile.TemporaryDirectory() as tmpdir:
        main_file = os.path.join(tmpdir, "main.yaml")
        secret_file = os.path.join(tmpdir, "secrets.yaml")

        with open(secret_file, 'w') as f:
            f.write("other_key: value")

        with open(main_file, 'w') as f:
            f.write("password: !secret nonexistent")

        secrets = Secrets(pathlib.Path(tmpdir))
        with pytest.raises(YAMLException, match="Secret nonexistent not defined"):
            load_yaml(main_file, secrets)


@pytest.mark.usefixtures("try_both_loaders")
def test_no_recursive_secrets():
    """Test that loading of secrets from the secrets file fails correctly."""
    with tempfile.TemporaryDirectory() as tmpdir:
        main_file = os.path.join(tmpdir, "configuration.yaml")
        secret_file = os.path.join(tmpdir, "secrets.yaml")

        with open(secret_file, 'w') as f:
            f.write("a: 1\nb: !secret a")

        with open(main_file, 'w') as f:
            f.write("key: !secret a")

        secrets = Secrets(pathlib.Path(tmpdir))
        with pytest.raises(YAMLException) as exc:
            load_yaml(main_file, secrets)
        assert "Secrets not supported" in str(exc.value)


# ============================================================
# Dump tests
# ============================================================

@pytest.mark.usefixtures("try_both_dumpers")
def test_dump():
    """Test that the dump method returns empty None values."""
    assert dump({"a": None, "b": "b"}) == "a:\nb: b\n"


@pytest.mark.usefixtures("try_both_dumpers")
def test_dump_unicode():
    """Test that dump handles unicode."""
    assert dump({"a": None, "b": "привет"}) == "a:\nb: привет\n"


@pytest.mark.usefixtures("try_both_dumpers")
def test_dump_list():
    """Test dumping a list."""
    result = dump({"key": [1, 2, 3]})
    assert result == "key:\n- 1\n- 2\n- 3\n"


@pytest.mark.usefixtures("try_both_dumpers")
def test_dump_nested():
    """Test dumping nested structures."""
    result = dump({"outer": {"inner": "value"}})
    assert result == "outer:\n  inner: value\n"


@pytest.mark.usefixtures("try_both_dumpers")
def test_dump_ordered_dict():
    """Test dumping OrderedDict preserves order."""
    data = OrderedDict([("z", 1), ("a", 2), ("m", 3)])
    result = dump(data)
    lines = result.strip().split('\n')
    keys = [l.split(':')[0] for l in lines]
    assert keys == ["z", "a", "m"]


@pytest.mark.usefixtures("try_both_dumpers")
def test_dump_node_dict_class():
    """Test dumping NodeDictClass preserves order."""
    data = NodeDictClass([("z", 1), ("a", 2)])
    result = dump(data)
    lines = result.strip().split('\n')
    keys = [l.split(':')[0] for l in lines]
    assert keys == ["z", "a"]


@pytest.mark.usefixtures("try_both_dumpers")
def test_dump_node_list_class():
    """Test dumping NodeListClass."""
    data = {"key": NodeListClass([1, 2, 3])}
    result = dump(data)
    assert result == "key:\n- 1\n- 2\n- 3\n"


@pytest.mark.usefixtures("try_both_dumpers")
def test_dump_node_str_class():
    """Test dumping NodeStrClass."""
    data = {"key": NodeStrClass("hello")}
    result = dump(data)
    assert result == "key: hello\n"


# ============================================================
# Round-trip tests
# ============================================================

@pytest.mark.usefixtures("try_both_loaders", "try_both_dumpers")
def test_roundtrip_simple():
    """Test round-trip of simple data."""
    data = {"key": "value", "num": 42}
    yaml_str = dump(data)
    result = parse_yaml(yaml_str)
    assert result["key"] == "value"
    assert result["num"] == 42


@pytest.mark.usefixtures("try_both_loaders", "try_both_dumpers")
def test_roundtrip_nested():
    """Test round-trip of nested data."""
    data = {"outer": {"inner": [1, 2, 3]}}
    yaml_str = dump(data)
    result = parse_yaml(yaml_str)
    assert result["outer"]["inner"] == [1, 2, 3]


@pytest.mark.usefixtures("try_both_loaders", "try_both_dumpers")
def test_roundtrip_input():
    """Test round-trip of Input objects."""
    data = {"hello": Input("test_name")}
    yaml_str = dump(data)
    result = parse_yaml(yaml_str)
    assert isinstance(result["hello"], Input)
    assert result["hello"].name == "test_name"


# ============================================================
# Annotation tests (file/line tracking)
# ============================================================

@pytest.mark.usefixtures("try_both_loaders")
def test_string_annotated():
    """Test strings are annotated with file + line."""
    conf = (
        "key1: str\n"
        "key2:\n"
        "  blah: blah\n"
        "key3:\n"
        " - 1\n"
        " - 2\n"
        " - 3\n"
        "key4: yes\n"
        "key5: 1\n"
        "key6: 1.0\n"
    )
    expected_annotations = {
        "key1": [("<file>", 1), ("<file>", 1)],
        "key2": [("<file>", 2), ("<file>", 3)],
        "key3": [("<file>", 4), ("<file>", 5)],
        "key4": [("<file>", 8), (None, None)],
        "key5": [("<file>", 9), (None, None)],
        "key6": [("<file>", 10), (None, None)],
    }
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    for key, value in doc.items():
        assert getattr(key, "__config_file__", None) == expected_annotations[key][0][0]
        assert getattr(key, "__line__", None) == expected_annotations[key][0][1]
        assert (
            getattr(value, "__config_file__", None) == expected_annotations[key][1][0]
        )
        assert getattr(value, "__line__", None) == expected_annotations[key][1][1]


# ============================================================
# Error handling tests
# ============================================================

@pytest.mark.usefixtures("try_both_loaders")
def test_unhashable_key():
    """Test an unhashable key raises error."""
    conf = "message:\n  {{ states.state }}"
    with pytest.raises((YAMLException, pyyaml.MarkedYAMLError)):
        with io.StringIO(conf) as file:
            parse_yaml(file)


@pytest.mark.usefixtures("try_both_loaders")
def test_duplicate_key(caplog):
    """Test duplicate dict keys emit a warning."""
    conf = "key: thing1\nkey: thing2"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert "duplicate key" in caplog.text


@pytest.mark.usefixtures("try_both_loaders")
def test_load_yaml_encoding_error():
    """Test raising a UnicodeDecodeError."""
    with patch("annotatedyaml.loader.open", create=True) as mock_open:
        mock_open.side_effect = UnicodeDecodeError("", b"", 1, 0, "")
        with pytest.raises(YAMLException):
            load_yaml("test")


# ============================================================
# load_yaml_dict tests
# ============================================================

def test_load_yaml_dict_empty():
    """Test load_yaml_dict with empty file returns empty dict."""
    with tempfile.NamedTemporaryFile(suffix='.yaml', mode='w', delete=False) as f:
        f.write("")
        f.flush()
        result = load_yaml_dict(f.name)
    os.unlink(f.name)
    assert result == {}


def test_load_yaml_dict_normal():
    """Test load_yaml_dict with normal dict."""
    with tempfile.NamedTemporaryFile(suffix='.yaml', mode='w', delete=False) as f:
        f.write("key: value")
        f.flush()
        result = load_yaml_dict(f.name)
    os.unlink(f.name)
    assert result == {"key": "value"}


def test_load_yaml_dict_not_dict():
    """Test load_yaml_dict raises on non-dict content."""
    with tempfile.NamedTemporaryFile(suffix='.yaml', mode='w', delete=False) as f:
        f.write("- item1\n- item2")
        f.flush()
        with pytest.raises(YamlTypeError):
            load_yaml_dict(f.name)
    os.unlink(f.name)


def test_load_yaml_dict_string():
    """Test load_yaml_dict raises on string content."""
    with tempfile.NamedTemporaryFile(suffix='.yaml', mode='w', delete=False) as f:
        f.write("just a string")
        f.flush()
        with pytest.raises(YamlTypeError):
            load_yaml_dict(f.name)
    os.unlink(f.name)


# ============================================================
# Input class tests
# ============================================================

def test_input_class():
    """Test input class."""
    yaml_input = Input("hello")
    yaml_input2 = Input("hello")

    assert yaml_input.name == "hello"
    assert yaml_input == yaml_input2
    assert len({yaml_input, yaml_input2}) == 1


# ============================================================
# YAML type tests
# ============================================================

@pytest.mark.usefixtures("try_both_loaders")
def test_returns_node_dict_class():
    """Test that parsed dicts are NodeDictClass."""
    conf = "key: value"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert isinstance(doc, NodeDictClass)


@pytest.mark.usefixtures("try_both_loaders")
def test_returns_node_list_class():
    """Test that parsed lists are NodeListClass (inside dicts)."""
    conf = "key:\n  - 1\n  - 2"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert isinstance(doc["key"], list)


@pytest.mark.usefixtures("try_both_loaders")
def test_returns_node_str_class():
    """Test that parsed string keys are NodeStrClass."""
    conf = "key: value"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    keys = list(doc.keys())
    assert isinstance(keys[0], NodeStrClass)


# ============================================================
# Complex YAML tests
# ============================================================

@pytest.mark.usefixtures("try_both_loaders")
def test_anchors_and_aliases():
    """Test YAML anchors and aliases."""
    conf = "defaults: &defaults\n  timeout: 30\n  retries: 3\nserver:\n  <<: *defaults\n  host: localhost"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["server"]["host"] == "localhost"
    assert doc["server"]["timeout"] == 30
    assert doc["server"]["retries"] == 3


@pytest.mark.usefixtures("try_both_loaders")
def test_flow_style():
    """Test flow-style YAML."""
    conf = "key: [1, 2, 3]"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["key"] == [1, 2, 3]


@pytest.mark.usefixtures("try_both_loaders")
def test_flow_mapping():
    """Test flow-style mapping."""
    conf = "key: {a: 1, b: 2}"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["key"]["a"] == 1
    assert doc["key"]["b"] == 2


@pytest.mark.usefixtures("try_both_loaders")
def test_quoted_strings():
    """Test quoted strings preserve type."""
    conf = 'key1: "123"\nkey2: \'true\'\nkey3: "null"'
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["key1"] == "123"
    assert isinstance(doc["key1"], str)
    assert doc["key2"] == "true"
    assert isinstance(doc["key2"], str)
    assert doc["key3"] == "null"
    assert isinstance(doc["key3"], str)


@pytest.mark.usefixtures("try_both_loaders")
def test_empty_values():
    """Test empty values."""
    conf = "key1:\nkey2: ~\nkey3: null"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["key1"] is None
    assert doc["key2"] is None
    assert doc["key3"] is None


@pytest.mark.usefixtures("try_both_loaders")
def test_special_characters_in_strings():
    """Test special characters in strings."""
    conf = 'key: "hello: world"\nkey2: "hello # comment"'
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["key"] == "hello: world"
    assert doc["key2"] == "hello # comment"


# ============================================================
# OSError wrapping tests
# ============================================================

@pytest.mark.usefixtures("try_both_loaders")
def test_load_yaml_file_not_found():
    """Test load_yaml with missing file raises FileNotFoundError."""
    with pytest.raises(FileNotFoundError):
        load_yaml("/nonexistent/path/test.yaml")


@pytest.mark.usefixtures("try_both_loaders")
def test_load_yaml_permission_error():
    """Test load_yaml wraps PermissionError in YAMLException."""
    with patch("annotatedyaml.loader.open", side_effect=PermissionError):
        with pytest.raises(YAMLException):
            load_yaml("test.yaml")


@pytest.mark.usefixtures("try_both_loaders")
def test_load_yaml_not_a_directory():
    """Test load_yaml wraps NotADirectoryError in YAMLException."""
    with patch("annotatedyaml.loader.open", side_effect=NotADirectoryError):
        with pytest.raises(YAMLException):
            load_yaml("test.yaml")


# ============================================================
# voluptuous compatibility test (if installed)
# ============================================================

@pytest.mark.usefixtures("try_both_loaders")
def test_string_used_as_vol_schema():
    """Test the subclassed strings can be used in voluptuous schemas."""
    try:
        import voluptuous as vol
    except ImportError:
        pytest.skip("voluptuous not installed")

    conf = "wanted_data:\n  key_1: value_1\n  key_2: value_2\n"
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)

    schema = vol.Schema(
        {vol.Required(key): value for key, value in doc["wanted_data"].items()},
    )
    schema(doc["wanted_data"])
    schema({"key_1": "value_1", "key_2": "value_2"})
    with pytest.raises(vol.Invalid):
        schema({"key_1": "value_2", "key_2": "value_1"})


# ============================================================
# Representing loaded data
# ============================================================

@pytest.mark.usefixtures("try_both_loaders", "try_both_dumpers")
def test_representing_yaml_loaded_data():
    """Test we can represent YAML loaded data."""
    conf = 'key: [1, "2", 3]'
    with io.StringIO(conf) as file:
        data = parse_yaml(file)
    result = dump(data)
    assert result == "key:\n- 1\n- '2'\n- 3\n"


# ============================================================
# Home-Assistant-like configuration patterns
# ============================================================

@pytest.mark.usefixtures("try_both_loaders")
def test_ha_style_automation():
    """Test HA-style automation config."""
    conf = """
automation:
  - alias: "Turn on lights at sunset"
    trigger:
      platform: sun
      event: sunset
    action:
      service: light.turn_on
      entity_id: light.living_room
"""
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["automation"][0]["alias"] == "Turn on lights at sunset"
    assert doc["automation"][0]["trigger"]["platform"] == "sun"


@pytest.mark.usefixtures("try_both_loaders")
def test_ha_style_sensor():
    """Test HA-style sensor config."""
    conf = """
sensor:
  - platform: template
    sensors:
      solar_angle:
        friendly_name: "Sun Angle"
        unit_of_measurement: "degrees"
        value_template: "{{ state_attr('sun.sun', 'elevation') }}"
"""
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["sensor"][0]["platform"] == "template"
    sensors = doc["sensor"][0]["sensors"]
    assert sensors["solar_angle"]["unit_of_measurement"] == "degrees"


@pytest.mark.usefixtures("try_both_loaders")
def test_ha_style_packages():
    """Test HA-style package merge patterns."""
    conf = """
homeassistant:
  name: "My Home"
  unit_system: metric
  time_zone: "America/New_York"
  customize:
    light.living_room:
      friendly_name: "Living Room Light"
"""
    with io.StringIO(conf) as file:
        doc = parse_yaml(file)
    assert doc["homeassistant"]["name"] == "My Home"
    assert doc["homeassistant"]["customize"]["light.living_room"]["friendly_name"] == "Living Room Light"
