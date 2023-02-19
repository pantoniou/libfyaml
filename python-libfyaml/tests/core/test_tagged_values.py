"""Tests for YAML tagged values (!!int, !!float, !!str, !!bool, !!null).

These tests verify that explicit YAML tags correctly override the default
type inference, e.g., !!int "10" should produce an integer, not a string.
"""

import pytest
import libfyaml as fy


class TestIntTag:
    """Test !!int tag behavior."""

    def test_int_tag_quoted_string(self):
        """!!int with quoted string should produce integer."""
        doc = fy.loads('!!int "10"')
        assert doc.is_int()
        assert int(doc) == 10

    def test_int_tag_unquoted(self):
        """!!int with unquoted value should produce integer."""
        doc = fy.loads('!!int 42')
        assert doc.is_int()
        assert int(doc) == 42

    def test_int_tag_negative(self):
        """!!int with negative value."""
        doc = fy.loads('!!int "-123"')
        assert doc.is_int()
        assert int(doc) == -123

    def test_int_tag_zero(self):
        """!!int with zero."""
        doc = fy.loads('!!int "0"')
        assert doc.is_int()
        assert int(doc) == 0

    def test_int_tag_hex(self):
        """!!int with hex value."""
        doc = fy.loads('!!int 0x1F')
        assert doc.is_int()
        assert int(doc) == 31

    def test_int_tag_large_value(self):
        """!!int with large value."""
        doc = fy.loads('!!int 999999999')
        assert doc.is_int()
        assert int(doc) == 999999999


class TestFloatTag:
    """Test !!float tag behavior."""

    def test_float_tag_quoted_string(self):
        """!!float with quoted string should produce float."""
        doc = fy.loads('!!float "3.14"')
        assert doc.is_float()
        assert float(doc) == 3.14

    def test_float_tag_unquoted(self):
        """!!float with unquoted value should produce float."""
        doc = fy.loads('!!float 2.718')
        assert doc.is_float()
        assert abs(float(doc) - 2.718) < 0.0001

    def test_float_tag_negative(self):
        """!!float with negative value."""
        doc = fy.loads('!!float "-1.5"')
        assert doc.is_float()
        assert float(doc) == -1.5

    def test_float_tag_scientific(self):
        """!!float with scientific notation."""
        doc = fy.loads('!!float "1.5e10"')
        assert doc.is_float()
        assert float(doc) == 1.5e10

    def test_float_tag_infinity(self):
        """!!float with infinity."""
        doc = fy.loads('!!float .inf')
        assert doc.is_float()
        assert float(doc) == float('inf')

    def test_float_tag_neg_infinity(self):
        """!!float with negative infinity."""
        doc = fy.loads('!!float -.inf')
        assert doc.is_float()
        assert float(doc) == float('-inf')

    def test_float_tag_nan(self):
        """!!float with NaN."""
        doc = fy.loads('!!float .nan')
        assert doc.is_float()
        import math
        assert math.isnan(float(doc))


class TestStrTag:
    """Test !!str tag behavior."""

    def test_str_tag_number(self):
        """!!str with number should produce string."""
        doc = fy.loads('!!str 42')
        assert doc.is_string()
        assert str(doc) == "42"

    def test_str_tag_float(self):
        """!!str with float should produce string."""
        doc = fy.loads('!!str 3.14')
        assert doc.is_string()
        assert str(doc) == "3.14"

    def test_str_tag_bool_word(self):
        """!!str with bool word should produce string."""
        doc = fy.loads('!!str true')
        assert doc.is_string()
        assert str(doc) == "true"

    def test_str_tag_null_word(self):
        """!!str with null word should produce string."""
        doc = fy.loads('!!str null')
        assert doc.is_string()
        assert str(doc) == "null"

    def test_str_tag_quoted(self):
        """!!str with quoted string should produce string."""
        doc = fy.loads('!!str "hello"')
        assert doc.is_string()
        assert str(doc) == "hello"


class TestBoolTag:
    """Test !!bool tag behavior."""

    def test_bool_tag_true(self):
        """!!bool with 'true' should produce bool True."""
        doc = fy.loads('!!bool "true"')
        assert doc.is_bool()
        assert bool(doc) == True

    def test_bool_tag_false(self):
        """!!bool with 'false' should produce bool False."""
        doc = fy.loads('!!bool "false"')
        assert doc.is_bool()
        assert bool(doc) == False

    def test_bool_tag_unquoted_true(self):
        """!!bool with unquoted true."""
        doc = fy.loads('!!bool true')
        assert doc.is_bool()
        assert bool(doc) == True

    def test_bool_tag_unquoted_false(self):
        """!!bool with unquoted false."""
        doc = fy.loads('!!bool false')
        assert doc.is_bool()
        assert bool(doc) == False


class TestBoolTagYAML11:
    """Test !!bool tag with YAML 1.1 extended bool values."""

    @pytest.mark.parametrize("value", ["yes", "Yes", "YES"])
    def test_bool_yes_variants(self, value):
        """!!bool with 'yes' variants in YAML 1.1 mode."""
        doc = fy.loads(f'!!bool "{value}"', mode='yaml1.1')
        assert doc.is_bool()
        assert bool(doc) == True

    @pytest.mark.parametrize("value", ["no", "No", "NO"])
    def test_bool_no_variants(self, value):
        """!!bool with 'no' variants in YAML 1.1 mode."""
        doc = fy.loads(f'!!bool "{value}"', mode='yaml1.1')
        assert doc.is_bool()
        assert bool(doc) == False

    @pytest.mark.parametrize("value", ["on", "On", "ON"])
    def test_bool_on_variants(self, value):
        """!!bool with 'on' variants in YAML 1.1 mode."""
        doc = fy.loads(f'!!bool "{value}"', mode='yaml1.1')
        assert doc.is_bool()
        assert bool(doc) == True

    @pytest.mark.parametrize("value", ["off", "Off", "OFF"])
    def test_bool_off_variants(self, value):
        """!!bool with 'off' variants in YAML 1.1 mode."""
        doc = fy.loads(f'!!bool "{value}"', mode='yaml1.1')
        assert doc.is_bool()
        assert bool(doc) == False

    @pytest.mark.parametrize("value", ["y", "Y"])
    def test_bool_y_variants(self, value):
        """!!bool with 'y' variants in YAML 1.1 mode."""
        doc = fy.loads(f'!!bool "{value}"', mode='yaml1.1')
        assert doc.is_bool()
        assert bool(doc) == True

    @pytest.mark.parametrize("value", ["n", "N"])
    def test_bool_n_variants(self, value):
        """!!bool with 'n' variants in YAML 1.1 mode."""
        doc = fy.loads(f'!!bool "{value}"', mode='yaml1.1')
        assert doc.is_bool()
        assert bool(doc) == False


class TestNullTag:
    """Test !!null tag behavior."""

    def test_null_tag_quoted(self):
        """!!null with quoted 'null' should produce null."""
        doc = fy.loads('!!null "null"')
        assert doc.is_null()

    def test_null_tag_unquoted(self):
        """!!null with unquoted null should produce null."""
        doc = fy.loads('!!null null')
        assert doc.is_null()

    def test_null_tag_tilde(self):
        """!!null with tilde should produce null."""
        doc = fy.loads('!!null ~')
        assert doc.is_null()

    def test_null_tag_empty(self):
        """!!null with empty value should produce null."""
        doc = fy.loads('!!null ""')
        assert doc.is_null()


class TestTaggedInContainers:
    """Test tagged values within sequences and mappings."""

    def test_tagged_in_mapping(self):
        """Tagged values in a mapping."""
        yaml = '''
port: !!int "8080"
rate: !!float "0.5"
name: !!str 123
enabled: !!bool "true"
empty: !!null ""
'''
        doc = fy.loads(yaml)

        assert doc['port'].is_int()
        assert int(doc['port']) == 8080

        assert doc['rate'].is_float()
        assert float(doc['rate']) == 0.5

        assert doc['name'].is_string()
        assert str(doc['name']) == "123"

        assert doc['enabled'].is_bool()
        assert bool(doc['enabled']) == True

        assert doc['empty'].is_null()

    def test_tagged_in_sequence(self):
        """Tagged values in a sequence."""
        yaml = '''
- !!int "1"
- !!float "2.0"
- !!str 3
- !!bool "true"
- !!null ~
'''
        doc = fy.loads(yaml)

        assert doc[0].is_int()
        assert int(doc[0]) == 1

        assert doc[1].is_float()
        assert float(doc[1]) == 2.0

        assert doc[2].is_string()
        assert str(doc[2]) == "3"

        assert doc[3].is_bool()
        assert bool(doc[3]) == True

        assert doc[4].is_null()

    def test_tagged_nested(self):
        """Tagged values in nested structures."""
        yaml = '''
config:
  values:
    - !!int "100"
    - !!str 200
  settings:
    threshold: !!float "0.95"
'''
        doc = fy.loads(yaml)

        assert doc['config']['values'][0].is_int()
        assert int(doc['config']['values'][0]) == 100

        assert doc['config']['values'][1].is_string()
        assert str(doc['config']['values'][1]) == "200"

        assert doc['config']['settings']['threshold'].is_float()
        assert float(doc['config']['settings']['threshold']) == 0.95


class TestTaggedEdgeCases:
    """Test edge cases for tagged values."""

    def test_int_tag_leading_zeros(self):
        """!!int with leading zeros."""
        doc = fy.loads('!!int 010')
        assert doc.is_int()
        # Leading zeros are treated as decimal in default mode
        assert int(doc) == 10

    def test_str_tag_preserves_quotes(self):
        """!!str preserves the actual string content."""
        doc = fy.loads('!!str "hello world"')
        assert doc.is_string()
        assert str(doc) == "hello world"

    def test_float_tag_integer_like_value(self):
        """!!float with integer-like value (10.0) produces float."""
        doc = fy.loads('!!float "10.0"')
        assert doc.is_float()
        assert float(doc) == 10.0

    def test_multiple_tagged_keys(self):
        """Multiple tagged values as mapping keys."""
        yaml = '''
!!str 1: one
!!str 2: two
!!int "3": three
'''
        doc = fy.loads(yaml)
        # Keys should be properly typed
        keys = list(doc.keys())
        assert len(keys) == 3
