"""
Tests for the mode parameter in load functions.

The mode parameter supports:
- 'yaml' or 'yaml1.2' or '1.2': YAML 1.2 (default)
- 'yaml1.1' or '1.1': YAML 1.1 (supports merge keys <<)
- 'json': JSON mode

Run with: python3 -m pytest tests/test_mode_parameter.py -v
"""

import sys
import os
import tempfile
import unittest

import libfyaml


class TestModeParameterLoads(unittest.TestCase):
    """Test mode parameter with loads()."""

    def test_default_mode(self):
        """Test default mode is YAML 1.2."""
        doc = libfyaml.loads("key: value")
        self.assertEqual(doc['key'], 'value')

    def test_mode_yaml(self):
        """Test explicit 'yaml' mode."""
        doc = libfyaml.loads("key: value", mode='yaml')
        self.assertEqual(doc['key'], 'value')

    def test_mode_yaml1_2(self):
        """Test 'yaml1.2' mode."""
        doc = libfyaml.loads("key: value", mode='yaml1.2')
        self.assertEqual(doc['key'], 'value')

    def test_mode_1_2(self):
        """Test '1.2' shorthand mode."""
        doc = libfyaml.loads("key: value", mode='1.2')
        self.assertEqual(doc['key'], 'value')

    def test_mode_yaml1_1(self):
        """Test 'yaml1.1' mode."""
        doc = libfyaml.loads("key: value", mode='yaml1.1')
        self.assertEqual(doc['key'], 'value')

    def test_mode_1_1(self):
        """Test '1.1' shorthand mode."""
        doc = libfyaml.loads("key: value", mode='1.1')
        self.assertEqual(doc['key'], 'value')

    def test_mode_json(self):
        """Test 'json' mode."""
        doc = libfyaml.loads('{"key": "value"}', mode='json')
        self.assertEqual(doc['key'], 'value')

    def test_invalid_mode(self):
        """Test that invalid mode raises ValueError."""
        with self.assertRaises(ValueError) as context:
            libfyaml.loads("key: value", mode='invalid')
        self.assertIn("Invalid mode", str(context.exception))
        self.assertIn("invalid", str(context.exception))

    def test_invalid_mode_message_lists_valid_modes(self):
        """Test that error message lists valid modes."""
        with self.assertRaises(ValueError) as context:
            libfyaml.loads("key: value", mode='bad_mode')
        error_msg = str(context.exception)
        self.assertIn("yaml", error_msg)
        self.assertIn("yaml1.1", error_msg)
        self.assertIn("yaml1.2", error_msg)
        self.assertIn("json", error_msg)


class TestYAML11MergeKeys(unittest.TestCase):
    """Test YAML 1.1 merge key (<<) functionality."""

    def setUp(self):
        """Set up test YAML with merge keys."""
        self.yaml_with_merge = """
defaults: &defaults
  host: localhost
  port: 8080
  timeout: 30

server:
  <<: *defaults
  name: production
"""

    def test_merge_key_yaml1_1_resolved(self):
        """Test that merge keys are resolved in YAML 1.1 mode."""
        doc = libfyaml.loads(self.yaml_with_merge, mode='yaml1.1')
        server = doc['server']

        # Merge key should be resolved - server has all merged keys
        keys = list(server.keys())
        self.assertEqual(len(keys), 4)  # host, port, timeout, name

        # Check merged values
        self.assertEqual(server['host'], 'localhost')
        self.assertEqual(server['port'], 8080)
        self.assertEqual(server['timeout'], 30)
        self.assertEqual(server['name'], 'production')

    def test_merge_key_yaml1_2_not_resolved(self):
        """Test that merge keys are NOT resolved in YAML 1.2 mode."""
        doc = libfyaml.loads(self.yaml_with_merge, mode='yaml1.2')
        server = doc['server']

        # Merge key should NOT be resolved - << is a literal key
        keys = [str(k) for k in server.keys()]
        self.assertEqual(len(keys), 2)  # << and name
        self.assertIn('<<', keys)
        self.assertIn('name', keys)

    def test_merge_key_default_mode_not_resolved(self):
        """Test that merge keys are NOT resolved in default mode (YAML 1.2)."""
        doc = libfyaml.loads(self.yaml_with_merge)  # No mode specified
        server = doc['server']

        keys = [str(k) for k in server.keys()]
        self.assertEqual(len(keys), 2)  # << and name
        self.assertIn('<<', keys)

    def test_multiple_merge_keys(self):
        """Test merging from multiple anchors in YAML 1.1."""
        yaml_multi = """
base1: &base1
  a: 1
  b: 2

base2: &base2
  c: 3
  d: 4

merged:
  <<: [*base1, *base2]
  e: 5
"""
        doc = libfyaml.loads(yaml_multi, mode='yaml1.1')
        merged = doc['merged']

        self.assertEqual(merged['a'], 1)
        self.assertEqual(merged['b'], 2)
        self.assertEqual(merged['c'], 3)
        self.assertEqual(merged['d'], 4)
        self.assertEqual(merged['e'], 5)

    def test_merge_key_override(self):
        """Test that local keys override merged keys in YAML 1.1.

        Note: In this implementation, keys appearing before the merge
        take precedence over merged keys.
        """
        yaml_override = """
defaults: &defaults
  host: localhost
  port: 8080

server:
  port: 9000
  <<: *defaults
"""
        doc = libfyaml.loads(yaml_override, mode='yaml1.1')
        server = doc['server']

        self.assertEqual(str(server['host']), 'localhost')  # From merge
        self.assertEqual(int(server['port']), 9000)  # Local key before merge wins


class TestAnchorsAndAliases(unittest.TestCase):
    """Test anchors and aliases work in all modes."""

    def setUp(self):
        """Set up YAML with anchors and aliases."""
        self.yaml_anchors = """
anchor: &anchor_value hello

uses_anchor: *anchor_value
"""

    def test_anchors_yaml1_2(self):
        """Test anchors work in YAML 1.2."""
        doc = libfyaml.loads(self.yaml_anchors, mode='yaml1.2')
        self.assertEqual(doc['anchor'], 'hello')
        self.assertEqual(doc['uses_anchor'], 'hello')

    def test_anchors_yaml1_1(self):
        """Test anchors work in YAML 1.1."""
        doc = libfyaml.loads(self.yaml_anchors, mode='yaml1.1')
        self.assertEqual(doc['anchor'], 'hello')
        self.assertEqual(doc['uses_anchor'], 'hello')

    def test_anchors_default(self):
        """Test anchors work in default mode."""
        doc = libfyaml.loads(self.yaml_anchors)
        self.assertEqual(doc['anchor'], 'hello')
        self.assertEqual(doc['uses_anchor'], 'hello')


class TestModeParameterLoad(unittest.TestCase):
    """Test mode parameter with load() (file loading)."""

    def test_load_yaml1_1_from_file(self):
        """Test loading YAML 1.1 from file."""
        yaml_content = """
defaults: &defaults
  host: localhost

server:
  <<: *defaults
  name: test
"""
        with tempfile.NamedTemporaryFile(mode='w', suffix='.yaml', delete=False) as f:
            f.write(yaml_content)
            f.flush()

            try:
                doc = libfyaml.load(f.name, mode='yaml1.1')
                self.assertEqual(doc['server']['host'], 'localhost')
                self.assertEqual(doc['server']['name'], 'test')
            finally:
                os.unlink(f.name)

    def test_load_json_from_file(self):
        """Test loading JSON from file."""
        json_content = '{"name": "test", "value": 42}'
        with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as f:
            f.write(json_content)
            f.flush()

            try:
                doc = libfyaml.load(f.name, mode='json')
                self.assertEqual(doc['name'], 'test')
                self.assertEqual(doc['value'], 42)
            finally:
                os.unlink(f.name)


class TestModeParameterLoadsAll(unittest.TestCase):
    """Test mode parameter with loads_all() (multi-document)."""

    def test_loads_all_yaml1_1(self):
        """Test multi-document parsing with YAML 1.1."""
        yaml_multi = """---
defaults: &defaults
  host: localhost
server:
  <<: *defaults
  name: server1
---
server:
  host: remote
  name: server2
"""
        docs = libfyaml.loads_all(yaml_multi, mode='yaml1.1')
        self.assertEqual(len(docs), 2)

        # First doc has merge key resolved
        self.assertEqual(docs[0]['server']['host'], 'localhost')
        self.assertEqual(docs[0]['server']['name'], 'server1')

        # Second doc is standalone
        self.assertEqual(docs[1]['server']['host'], 'remote')
        self.assertEqual(docs[1]['server']['name'], 'server2')

    def test_loads_all_yaml1_2(self):
        """Test multi-document parsing with YAML 1.2."""
        yaml_multi = """---
doc: 1
---
doc: 2
"""
        docs = libfyaml.loads_all(yaml_multi, mode='yaml1.2')
        self.assertEqual(len(docs), 2)
        self.assertEqual(docs[0]['doc'], 1)
        self.assertEqual(docs[1]['doc'], 2)


class TestModeParameterLoadAll(unittest.TestCase):
    """Test mode parameter with load_all() (multi-document from file)."""

    def test_load_all_yaml1_1(self):
        """Test loading multi-document YAML 1.1 from file."""
        yaml_content = """---
defaults: &defaults
  value: 100
item:
  <<: *defaults
  name: first
---
item:
  value: 200
  name: second
"""
        with tempfile.NamedTemporaryFile(mode='w', suffix='.yaml', delete=False) as f:
            f.write(yaml_content)
            f.flush()

            try:
                docs = libfyaml.load_all(f.name, mode='yaml1.1')
                self.assertEqual(len(docs), 2)
                self.assertEqual(docs[0]['item']['value'], 100)
                self.assertEqual(docs[1]['item']['value'], 200)
            finally:
                os.unlink(f.name)


class TestJSONMode(unittest.TestCase):
    """Test JSON-specific parsing behavior."""

    def test_json_array(self):
        """Test parsing JSON array."""
        doc = libfyaml.loads('[1, 2, 3, "four"]', mode='json')
        self.assertEqual(len(doc), 4)
        self.assertEqual(doc[0], 1)
        self.assertEqual(doc[3], 'four')

    def test_json_nested(self):
        """Test parsing nested JSON."""
        json_str = '{"outer": {"inner": {"value": 42}}}'
        doc = libfyaml.loads(json_str, mode='json')
        self.assertEqual(doc['outer']['inner']['value'], 42)

    def test_json_booleans(self):
        """Test JSON boolean parsing."""
        doc = libfyaml.loads('{"t": true, "f": false}', mode='json')
        self.assertEqual(doc['t'], True)
        self.assertEqual(doc['f'], False)

    def test_json_null(self):
        """Test JSON null parsing."""
        doc = libfyaml.loads('{"value": null}', mode='json')
        self.assertTrue(doc['value'].is_null())

    def test_json_numbers(self):
        """Test JSON number parsing."""
        doc = libfyaml.loads('{"int": 42, "float": 3.14, "neg": -10}', mode='json')
        self.assertEqual(doc['int'], 42)
        self.assertAlmostEqual(float(doc['float']), 3.14, places=2)
        self.assertEqual(doc['neg'], -10)


class TestModeWithOtherParameters(unittest.TestCase):
    """Test mode parameter combined with other parameters."""

    def test_mode_with_dedup(self):
        """Test mode works with dedup parameter."""
        doc = libfyaml.loads("key: value", mode='yaml1.1', dedup=True)
        self.assertEqual(doc['key'], 'value')

    def test_mode_with_trim(self):
        """Test mode works with trim parameter."""
        doc = libfyaml.loads("key: value", mode='yaml1.1', trim=True)
        self.assertEqual(doc['key'], 'value')

    def test_mode_with_mutable(self):
        """Test mode works with mutable parameter."""
        doc = libfyaml.loads("key: value", mode='yaml1.1', mutable=True)
        self.assertEqual(doc['key'], 'value')

    def test_mode_with_all_parameters(self):
        """Test mode works with all parameters combined."""
        doc = libfyaml.loads(
            "key: value",
            mode='yaml1.1',
            dedup=True,
            trim=True,
            mutable=False
        )
        self.assertEqual(doc['key'], 'value')


if __name__ == '__main__':
    unittest.main()
