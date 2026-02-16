# conftest plugin to patch yaml module to use libfyaml
# Usage: pytest -p conftest_libfyaml test_yaml.py

import sys

# Remove any cached yaml imports
for _key in list(sys.modules.keys()):
    if _key == 'yaml' or _key.startswith('yaml.'):
        del sys.modules[_key]

import libfyaml.pyyaml_compat as _fy_yaml
from libfyaml.pyyaml_compat import cyaml as _cyaml, resolver as _resolver
from libfyaml.pyyaml_compat import constructor as _constructor, representer as _representer
from libfyaml.pyyaml_compat import nodes as _nodes, error as _error
from libfyaml.pyyaml_compat import composer as _composer, scanner as _scanner, parser as _parser

sys.modules['yaml'] = _fy_yaml
sys.modules['yaml.cyaml'] = _cyaml
sys.modules['yaml.resolver'] = _resolver
sys.modules['yaml.constructor'] = _constructor
sys.modules['yaml.representer'] = _representer
sys.modules['yaml.nodes'] = _nodes
sys.modules['yaml.error'] = _error
sys.modules['yaml.composer'] = _composer
sys.modules['yaml.scanner'] = _scanner
sys.modules['yaml.parser'] = _parser

# Map real sub-modules to yaml.* namespace
import types as _types
from libfyaml.pyyaml_compat import tokens as _tokens
from libfyaml.pyyaml_compat import events as _events
sys.modules['yaml.tokens'] = _tokens
sys.modules['yaml.events'] = _events

# emitter stub (no real sub-module yet)
_emitter = _types.ModuleType('yaml.emitter')
for _name in ['Emitter', 'EmitterError']:
    if hasattr(_fy_yaml, _name):
        setattr(_emitter, _name, getattr(_fy_yaml, _name))
sys.modules['yaml.emitter'] = _emitter

# reader stub
import types as _types2
_reader = _types2.ModuleType('yaml.reader')

class _ReaderError(_fy_yaml.YAMLError):
    def __init__(self, name=None, position=None, character=None, encoding=None, reason=None):
        self.name = name
        self.position = position
        self.character = character
        self.encoding = encoding
        self.reason = reason
    def __str__(self):
        return "reader error: %s" % (self.reason or "unknown")

class _Reader:
    def __init__(self, stream):
        if hasattr(stream, 'read'):
            self.stream = stream.read()
        else:
            self.stream = stream
        self.name = getattr(stream, 'name', None)
        self.index = 0
        # Validate encoding on init
        if isinstance(self.stream, bytes):
            try:
                self.stream = self.stream.decode('utf-8')
            except UnicodeDecodeError as e:
                raise _ReaderError(
                    name=self.name,
                    position=e.start,
                    character=self.stream[e.start],
                    encoding='utf-8',
                    reason=str(e)
                )
        # Check for invalid characters
        for i, ch in enumerate(self.stream):
            if ord(ch) not in range(0x09, 0x0E) and ord(ch) not in range(0x20, 0xD800) \
               and ord(ch) not in range(0xE000, 0xFFFE) and ord(ch) not in range(0x10000, 0x110000) \
               and ch != '\x85':
                raise _ReaderError(
                    name=self.name,
                    position=i,
                    character=ord(ch),
                    encoding='unicode',
                    reason="special characters are not allowed"
                )

    def peek(self, offset=0):
        idx = self.index + offset
        if idx < len(self.stream):
            return self.stream[idx]
        return ''

    def forward(self, length=1):
        self.index += length

_reader.Reader = _Reader
_reader.ReaderError = _ReaderError
sys.modules['yaml.reader'] = _reader
_fy_yaml.reader = _reader

print("\n=== USING LIBFYAML FOR PYYAML TESTS ===\n")

# ---------------------------------------------------------------------------
# xfail markers for known libfyaml differences vs PyYAML
# ---------------------------------------------------------------------------
import pytest as _pytest

# Mapping of (test_function, data_file) → reason for xfail.
# Uses precise matching to avoid marking passing tests as xfail.
_LIBFYAML_XFAILS = {}

def _xfail(test_func, data_file, reason):
    _LIBFYAML_XFAILS[(test_func, data_file)] = reason

# --- loader_error / loader_error_string: libfyaml accepts YAML that PyYAML rejects ---
for _func in ("test_loader_error", "test_loader_error_string"):
    _xfail(_func, "duplicate-anchor-1.loader-error",
           "libfyaml follows YAML 1.2: anchor redefinition is allowed")
    _xfail(_func, "duplicate-anchor-2.loader-error",
           "libfyaml follows YAML 1.2: anchor redefinition is allowed")
    _xfail(_func, "invalid-directive-name-1.loader-error",
           "libfyaml treats malformed directives as warnings")
    _xfail(_func, "invalid-directive-name-2.loader-error",
           "libfyaml treats unknown directives as warnings (YAML 1.2 compliant)")
    _xfail(_func, "invalid-anchor-1.loader-error",
           "libfyaml accepts broader anchor character set")
    _xfail(_func, "invalid-tag-handle-2.loader-error",
           "libfyaml is lenient with tag handle syntax")
    _xfail(_func, "invalid-yaml-directive-version-1.loader-error",
           "libfyaml is lenient with YAML directive syntax")
    _xfail(_func, "no-block-mapping-end-2.loader-error",
           "libfyaml parses bare ': value' as null-key mapping entry")
    _xfail(_func, "no-document-start.loader-error",
           "libfyaml does not require --- after directives")
    _xfail(_func, "invalid-item-without-trailing-break.loader-error",
           "libfyaml parses ambiguous sequence item differently")
    _xfail(_func, "empty-python-module.loader-error",
           "valid YAML syntax; module validation is constructor-level")

# --- emitter_styles: C library block scalar emission bugs ---
for _f in ("spec-05-03.data", "spec-05-11.data", "spec-06-05.data", "spec-06-08.data",
           "spec-08-12.data", "spec-08-15.data", "spec-09-02.data", "spec-09-08.data",
           "spec-09-12.data", "spec-09-16.data", "spec-09-22.data", "spec-09-23.data",
           "spec-09-24.data", "spec-10-05.data", "spec-10-07.data", "spec-10-15.data"):
    _xfail("test_emitter_styles", _f, "libfyaml C emitter block scalar limitation")

# --- emitter_on_data: subset that also fails ---
for _f in ("spec-05-11.data", "spec-06-08.data", "spec-09-08.data",
           "spec-09-16.data", "spec-09-22.data"):
    _xfail("test_emitter_on_data", _f, "libfyaml C emitter block scalar limitation")

# --- constructor_types: C library resolution / parsing differences ---
_xfail("test_constructor_types", "construct-float.data",
       "libfyaml does not resolve sexagesimal floats (190:20:30.15)")
_xfail("test_constructor_types", "construct-int.data",
       "libfyaml does not resolve sexagesimal integers (190:20:30)")
_xfail("test_constructor_types", "timestamp-bugs.data",
       "libfyaml timestamp parsing differs on edge cases")
_xfail("test_constructor_types", "duplicate-mapping-key.former-loader-error.data",
       "libfyaml C parser cannot re-parse *anchor in mapping key context")
_xfail("test_constructor_types", "emitting-unacceptable-unicode-character-bug.data",
       "libfyaml unicode surrogate handling differs")
_xfail("test_constructor_types", "emitting-unacceptable-unicode-character-bug-py3.data",
       "libfyaml unicode surrogate handling differs")

# --- representer_types ---
_xfail("test_representer_types", "construct-python-tuple-list-dict.code",
       "libfyaml C emitter produces bare colon in complex mapping keys")
_xfail("test_representer_types", "emitting-unacceptable-unicode-character-bug.code",
       "libfyaml unicode surrogate handling differs")
_xfail("test_representer_types", "emitting-unacceptable-unicode-character-bug-py3.code",
       "libfyaml unicode surrogate handling differs")

# --- other specific test/data combinations ---
_xfail("test_composer", "spec-09-15.data",
       "libfyaml resolves '---' as null in flow context (PyYAML: string)")
_xfail("test_implicit_resolver", "yaml11.schema",
       "libfyaml implicit type resolution differs (sexagesimal, dot, etc.)")
_xfail("test_recursive", "recursive-set.recursive",
       "two-phase construction for !!python/object/new in recursive sets not implemented")
_xfail("test_unicode_transfer", "emoticons2.unicode",
       "libfyaml UTF-16 encoding round-trip differs")


def pytest_collection_modifyitems(session, config, items):
    """Mark known libfyaml-vs-PyYAML differences as xfail."""
    for item in items:
        parent_name = getattr(item.parent, 'name', '')
        key = (parent_name, item.name)
        if key in _LIBFYAML_XFAILS:
            item.add_marker(_pytest.mark.xfail(
                reason=_LIBFYAML_XFAILS[key],
                strict=False,
            ))
