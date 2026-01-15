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
| **ansible** | manual | ✓ | ~100% | Playbooks parse and execute successfully |

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

1. **Custom tag constructors**: `add_constructor()` is available but constructors are not invoked during parsing (libfyaml handles tags internally)

2. **Implicit type conversion**: PyYAML auto-converts dates (`2018-07-10` → `datetime.date`); we return strings

3. **Full Loader features**: Some advanced Loader customization not supported
