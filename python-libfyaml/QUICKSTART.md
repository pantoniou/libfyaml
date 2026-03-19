# Quickstart

## Install From Source

Source builds are the expected path on Windows, macOS, FreeBSD, NetBSD, and
OpenBSD when a prebuilt wheel is not available.

On Windows, use a Clang-family C compiler (`clang` or `clang-cl`). The Python
binding depends on libfyaml generics, which require statement expressions not
supported by `cl.exe`.

From a full libfyaml checkout:

```bash
python3 -m pip install ./python-libfyaml
```

This builds a bundled static `libfyaml` and links the Python extension against
it.

From an environment with `libfyaml` already installed:

```bash
LIBFYAML_USE_SYSTEM=1 python3 -m pip install ./python-libfyaml
```

## Basic Usage

```python
import libfyaml as fy

doc = fy.loads("server: {host: localhost, port: 8080}")
server = doc["server"]

print(str(server["host"]))
print(int(server["port"]))
```

## Tests

```bash
cd python-libfyaml
python3 -m pytest tests/
```
