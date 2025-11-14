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
| **ansible** | 117 | 88 | 75.2% | Core loading/parsing works; failures in error messages, dump formatting |

## Running the Ansible Test Suite

Ansible tests require:
1. A yaml package shim to redirect `import yaml` to pyyaml_compat
2. Running pytest from the ansible-test directory (for conftest.py fixtures)

```bash
# 1. Clone ansible (one-time)
cd /tmp && git clone --depth 1 https://github.com/ansible/ansible.git ansible-test

# 2. Create yaml shim (one-time)
mkdir -p /tmp/yaml-shim/yaml
cat > /tmp/yaml-shim/yaml/__init__.py << 'EOF'
from libfyaml.pyyaml_compat import *
from libfyaml.pyyaml_compat import __version__, __with_libyaml__
EOF
# Create submodule redirects
for mod in representer nodes constructor parser cyaml resolver scanner composer error; do
    echo "from libfyaml.pyyaml_compat.${mod} import *" > /tmp/yaml-shim/yaml/${mod}.py
done

# 3. Run the yaml unit tests (MUST run from ansible-test directory)
cd /tmp/ansible-test
PYTHONPATH=/tmp/yaml-shim:/path/to/python-libfyaml:$(pwd)/lib:$(pwd)/test \
    python3 -m pytest test/units/parsing/yaml/ -q
```

### Failure Categories (as of Feb 2026)

| Category | Count | Root Cause |
|----------|-------|------------|
| Error message wording | 16 | libfyaml produces different error text than PyYAML/libyaml |
| Dump formatting | 10 | Block vs flow style, tag format, literal block scalars |
| Line/column positions | 2 | Off-by-one in marker column positions |
| Error message case | 1 | Ansible test expects "The" but code uses "the" (Ansible bug) |

None of the failures relate to scalar type resolution (booleans, integers,
floats, nulls) — that is fully handled by the C library's `yaml1.1-pyyaml` mode.

**Working features:**
- Vault tag type validation (rejects non-string values)
- CustomMapping/CustomSequence dump via multi_representers
- Tripwire pattern (raises on dump)
- Bytes/binary round-trip
- Factory function Dumpers (like AnsibleDumper)

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
