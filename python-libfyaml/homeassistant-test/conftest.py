# LIBFYAML YAML PATCHING - must be before any yaml/annotatedyaml imports
import sys as _sys
for _key in list(_sys.modules.keys()):
    if _key == 'yaml' or _key.startswith('yaml.'):
        del _sys.modules[_key]

import libfyaml.pyyaml_compat as _fy_yaml
from libfyaml.pyyaml_compat import cyaml as _cyaml, resolver as _resolver
from libfyaml.pyyaml_compat import constructor as _constructor, representer as _representer
from libfyaml.pyyaml_compat import nodes as _nodes, error as _error
from libfyaml.pyyaml_compat import composer as _composer, scanner as _scanner, parser as _parser

_sys.modules['yaml'] = _fy_yaml
_sys.modules['yaml.cyaml'] = _cyaml
_sys.modules['yaml.resolver'] = _resolver
_sys.modules['yaml.constructor'] = _constructor
_sys.modules['yaml.representer'] = _representer
_sys.modules['yaml.nodes'] = _nodes
_sys.modules['yaml.error'] = _error
_sys.modules['yaml.composer'] = _composer
_sys.modules['yaml.scanner'] = _scanner
_sys.modules['yaml.parser'] = _parser

class _ReaderModule:
    class Reader:
        def __init__(self, stream):
            self.stream = stream
            self.name = getattr(stream, 'name', None)

    class ReaderError(Exception):
        pass

_sys.modules['yaml.reader'] = _ReaderModule
print("\n=== USING LIBFYAML ===\n")
# === END LIBFYAML PATCHING ===
