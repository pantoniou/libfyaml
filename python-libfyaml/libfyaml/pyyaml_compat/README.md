# PyYAML Compatibility Layer

Drop-in replacement for PyYAML using libfyaml.

## Usage

```python
# Instead of: import yaml
from libfyaml import pyyaml_compat as yaml

data = yaml.safe_load("foo: bar")
output = yaml.safe_dump({"foo": "bar"})
```

## Compatibility Test Results

| Project | Tests | Passed | Pass Rate | Notes |
|---------|-------|--------|-----------|-------|
| **pre-commit** | manual | ✓ | 100% | Full functionality, hooks run correctly |
| **mkdocs** | 725 | 717 | 98.9% | 8 failures due to custom tags (!ENV, !relative) and auto date conversion |
| **ansible** | 117 | 80 | 68.4% | Core loading/parsing works; failures in error messages, dump formatting, vault/custom types, column offsets |

## Running the Ansible Test Suite

Ansible tests require a yaml package shim to redirect `import yaml` to our
pyyaml_compat layer. The shim is at `/tmp/yaml-shim/yaml/` and re-exports
all submodules (nodes, constructor, resolver, representer, parser, scanner,
composer, cyaml, reader, error).

```bash
# 1. Clone ansible (one-time)
cd /tmp && git clone --depth 1 https://github.com/ansible/ansible.git ansible-test

# 2. Run the yaml unit tests
PYTHONPATH=/tmp/yaml-shim:/path/to/python-libfyaml:/tmp/ansible-test/lib:/tmp/ansible-test/test \
    python3 -m pytest /tmp/ansible-test/test/units/parsing/yaml/ -q

# Run specific test files:
#   test_loader.py   — 35 pass, 12 fail (core parsing works; dump/vault/column-offset failures)
#   test_objects.py  — 20 pass, 0 fail (all pass)
#   test_errors.py   — 17 pass, 16 fail (error message wording differences)
#   test_dumper.py   — 6 pass, 6 fail (custom types, vault, formatting)
#   test_vault.py    — 2 pass, 1 fail, 2 errors (vault-specific)
```

### Failure Categories (as of Jan 2026)

| Category | Count | Root Cause |
|----------|-------|------------|
| Error message wording | 16 | libfyaml produces different error text than PyYAML/libyaml |
| Dump formatting | 9 | Block vs flow style, tag format (`!<tag:...>` vs `!!type`) |
| Ansible-specific types | 8 | Vault tags, UndefinedMarker, Tripwire, CustomMapping/Sequence |
| Column offsets | 2 | Off-by-one in marker column positions |

None of the failures relate to scalar type resolution (booleans, integers,
floats, nulls) — that is fully handled by the C library's `yaml1.1-pyyaml` mode.

## Supported API

### Core Functions
- `load(stream, Loader)` / `safe_load(stream)`
- `dump(data, stream, **kwargs)` / `safe_dump(data, stream, **kwargs)`
- `load_all` / `safe_load_all` / `dump_all` / `safe_dump_all`
- `compose(stream, Loader)` / `compose_all(stream, Loader)`

### Classes
- `BaseLoader`, `SafeLoader`, `Loader` (with `add_constructor()`)
- `BaseDumper`, `SafeDumper`, `Dumper`
- C-accelerated variants: `CSafeLoader`, `CLoader`, `CSafeDumper`, `CDumper`

### Submodules
- `yaml.nodes` - Node, ScalarNode, SequenceNode, MappingNode
- `yaml.constructor` - BaseConstructor, SafeConstructor, Constructor, ConstructorError
- `yaml.representer` - BaseRepresenter, SafeRepresenter, Representer
- `yaml.resolver` - BaseResolver, Resolver
- `yaml.parser` - Parser, ParserError
- `yaml.cyaml` - CParser, CSafeLoader, CSafeDumper, etc.

### Exceptions
- `YAMLError`, `ScannerError`, `ParserError`, `ComposerError`, etc.

## Known Limitations

1. **Streaming API**: `scan()`, `parse()`, `emit()`, `serialize()` are not implemented

2. **Sexagesimal numbers**: Base-60 integers (`190:20:30`) and floats (`1:30.5`) are not resolved (returned as strings)

3. **Dump formatting**: Output uses block style; flow style (`{foo: bar}`, `[1, 2]`) and literal block scalars (`|`) are not fully supported

4. **Error messages**: Different wording from PyYAML/libyaml; column offsets may differ by 1
