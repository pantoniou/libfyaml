# TODO: Full Implementation of Python libfyaml Bindings

Based on the working minimal prototype, here's what's needed for a production-ready implementation.

## Recent Updates (2025-12-29)

**Investigation completed on decoder/encoder flags and builder lifetime:**

1. **Builder Lifetime** - Clarified to be very simple:
   - Create builder, use it, destroy it - everything inside is destroyed automatically
   - No complex reference counting between parent/child needed
   - Root `FyGeneric` owns builder, children reference root via Python refcount

2. **Decoder Parse Flags** - Documented from `fy-generic-decoder.h`:
   - `FYGDPF_DISABLE_DIRECTORY` - Security: disable directory access during parsing
   - `FYGDPF_MULTI_DOCUMENT` - Parse multiple YAML documents from single input

3. **Encoder Emit Flags** - Documented from `fy-generic-encoder.h`:
   - `FYGEEF_DISABLE_DIRECTORY` - Security: disable directory access during emit
   - `FYGEEF_MULTI_DOCUMENT` - Emit multiple YAML documents to single output
   - Simple emit functions: `fy_generic_emit_default()`, `fy_generic_emit_compact()`

4. **Emit to String** - DISCOVERED from `parse_emit_ops()` test:
   - `fy_generic_op_args(gb, FYGBOPF_EMIT, value, &args)` **returns a string generic**!
   - This is the key API for Python `dumps()` - emit to string, not stdout
   - Example: `emitted = fy_generic_op_args(gb, FYGBOPF_EMIT, seq, &args);`
   - Then: `const char *yaml_str = fy_cast(emitted, "");`
   - Supports YAML (block/flow) and JSON modes via `args.emit.emit_flags`

## Critical Missing Features

### 1. Memory Management (HIGHEST PRIORITY)

**Problem**: Currently `fy_generic_builder` leaks in `libfyaml_loads()`:
```c
// TODO: Need to manage gb lifetime properly - for now this will leak
```

**Solution: Simple Builder Ownership**

Per the libfyaml API documentation: "It is very simple to manage the builder lifetime. You create it and then destroy it, that's it - everything that's contained in it is destroyed."

This means **no complex reference counting needed**. The approach is:
1. Root `FyGeneric` (from `loads()` or `load()`) owns the builder
2. Child `FyGeneric` objects (from subscripting/access) hold a reference to the root
3. When root is destroyed, builder is destroyed, invalidating all child values
4. Children keep root alive via Python's refcount

**Decoder Parse Flags** (from `fy-generic-decoder.h`):
```c
enum fy_generic_decoder_parse_flags {
    FYGDPF_DISABLE_DIRECTORY = FY_BIT(0),  /* Disable directory access */
    FYGDPF_MULTI_DOCUMENT    = FY_BIT(1),  /* Parse multiple documents */
};
```

**Implementation**:
```c
typedef struct {
    PyObject_HEAD
    fy_generic fyg;
    struct fy_generic_builder *gb;  /* Only set for root */
    PyObject *root;  /* Reference to root FyGeneric (or NULL if this is root) */
} FyGenericObject;

// Root creation (loads/load):
static PyObject *
libfyaml_loads(PyObject *self, PyObject *args)
{
    // ... parse YAML ...

    FyGenericObject *root = PyObject_New(FyGenericObject, &FyGenericType);
    root->fyg = parsed;
    root->gb = gb;      /* Root owns the builder */
    root->root = NULL;  /* This is the root */
    return (PyObject *)root;
}

// Child creation (from subscript/iteration):
static PyObject *
FyGeneric_from_parent(fy_generic fyg, FyGenericObject *parent)
{
    FyGenericObject *child = PyObject_New(FyGenericObject, &FyGenericType);
    child->fyg = fyg;
    child->gb = NULL;  /* Children don't own builder */

    /* Reference the root to keep builder alive */
    child->root = parent->root ? parent->root : (PyObject*)parent;
    Py_INCREF(child->root);

    return (PyObject *)child;
}

// Deallocation:
static void
FyGeneric_dealloc(FyGenericObject *self)
{
    if (self->gb) {
        /* This is the root - destroy builder (and everything in it) */
        fy_generic_builder_destroy(self->gb);
    }

    if (self->root) {
        /* This is a child - release reference to root */
        Py_DECREF(self->root);
    }

    Py_TYPE(self)->tp_free((PyObject *)self);
}
```

**Key insight**: All `fy_generic` values created during parsing are owned by the builder. When the builder is destroyed, they all become invalid. This is exactly what we want - Python's refcounting ensures the root (and thus the builder) stays alive as long as any child exists.

### 2. Missing Core API Functions

#### `load(filename)` - Load from file
```python
doc = libfyaml.load("config.yaml")
```

**Implementation**:
```c
static PyObject *
libfyaml_load(PyObject *self, PyObject *args, PyObject *kwargs)
{
    const char *filename;
    const char *mode = "yaml";
    static char *kwlist[] = {"filename", "mode", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|s", kwlist, &filename, &mode))
        return NULL;

    /* Read file to string first, or use file-based parsing? */
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        PyErr_SetFromErrnoWithFilename(PyExc_FileNotFoundError, filename);
        return NULL;
    }

    /* Read entire file */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *content = malloc(size + 1);
    fread(content, 1, size, fp);
    content[size] = '\0';
    fclose(fp);

    /* Parse with fy_generic_op_args */
    struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
    fy_generic input = fy_gb_string_size_create(gb, content, size);
    free(content);

    /* ... rest similar to loads() ... */
}
```

#### Iterator Support for Sequences

Currently missing - sequences aren't iterable in Python.

**Implementation**: Already scaffolded but not included in minimal. Need to add:
```c
/* Already exists in _libfyaml.c but not in minimal version */
typedef struct {
    PyObject_HEAD
    PyObject *generic_obj;
    size_t index;
    size_t count;
} FyGenericSeqIterObject;

static PyObject *
FyGeneric_iter(FyGenericObject *self)
{
    enum fy_generic_type type = fy_get_type(self->fyg);

    if (type == FYGT_SEQUENCE) {
        FyGenericSeqIterObject *iter = PyObject_New(...);
        iter->generic_obj = (PyObject *)self;
        Py_INCREF(self);
        iter->index = 0;
        iter->count = fy_len(self->fyg);
        return (PyObject *)iter;
    }

    PyErr_SetString(PyExc_TypeError, "Object is not iterable");
    return NULL;
}
```

#### `keys()`, `values()`, `items()` for Mappings

**Implementation** - straightforward using `fy_generic_mapping_get_pairs()`:
```c
static PyObject *
FyGeneric_keys(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    if (fy_get_type(self->fyg) != FYGT_MAPPING) {
        PyErr_SetString(PyExc_TypeError, "keys() requires a mapping");
        return NULL;
    }

    size_t count;
    const fy_generic_map_pair *pairs = fy_generic_mapping_get_pairs(self->fyg, &count);

    PyObject *list = PyList_New(count);
    for (size_t i = 0; i < count; i++) {
        PyObject *key = FyGeneric_from_generic(pairs[i].key, self->gb);
        PyList_SET_ITEM(list, i, key);
    }
    return list;
}
```

### 3. Serialization (Python → YAML)

#### `dumps(obj, **options)` - Serialize to YAML string
```python
yaml_str = libfyaml.dumps({"name": "Alice", "age": 30})
```

**Encoder Emit Flags** (from `fy-generic-encoder.h`):
```c
enum fy_generic_encoder_emit_flags {
    FYGEEF_DISABLE_DIRECTORY = FY_BIT(0),  /* Disable directory access */
    FYGEEF_MULTI_DOCUMENT    = FY_BIT(1),  /* Emit multiple documents */
};
```

**Emit API** (from `libfyaml-test-generic.c::parse_emit_ops()`):

The key insight: **`FYGBOPF_EMIT` returns a STRING generic!**

```c
/* Simple emit to stdout */
int fy_generic_emit_default(fy_generic v);  /* Default YAML format */
int fy_generic_emit_compact(fy_generic v);  /* Compact YAML format */
int fy_generic_emit(fy_generic v, enum fy_emitter_cfg_flags flags);

/* Emit to string - THE KEY API FOR PYTHON */
fy_generic emitted = fy_generic_op_args(gb, FYGBOPF_EMIT, value, &args);
// where args.emit.emit_flags can be:
//   FYECF_MODE_BLOCK - YAML block format
//   FYECF_MODE_FLOW  - YAML flow format
//   FYECF_MODE_JSON  - JSON format
//   FYECF_INDENT_2   - 2-space indentation
//   ... and more

// Extract the string
const char *yaml_str = fy_cast(emitted, "");
```

**Example from test suite**:
```c
seq = fy_sequence(1, 2, 3, 4, 5);

memset(&args, 0, sizeof(args));
args.emit.emit_flags = FYECF_MODE_BLOCK;

emitted = fy_generic_op_args(gb, FYGBOPF_EMIT, seq, &args);
ck_assert(fy_generic_is_string(emitted));  // ← Returns a string!

const char *result_str = fy_cast(emitted, "");
printf("YAML output:\n%s\n", result_str);
```

**Implementation**:
```c
static fy_generic
python_to_generic(struct fy_generic_builder *gb, PyObject *obj)
{
    if (obj == Py_None) {
        return fy_gb_null_type_create(gb);
    } else if (PyBool_Check(obj)) {
        return fy_gb_bool_type_create(gb, obj == Py_True);
    } else if (PyLong_Check(obj)) {
        long long val = PyLong_AsLongLong(obj);
        return fy_gb_int_type_create(gb, val);
    } else if (PyFloat_Check(obj)) {
        double val = PyFloat_AsDouble(obj);
        return fy_gb_float_type_create(gb, val);
    } else if (PyUnicode_Check(obj)) {
        const char *str = PyUnicode_AsUTF8(obj);
        return fy_gb_string_create(gb, str);
    } else if (PyList_Check(obj) || PyTuple_Check(obj)) {
        /* Create sequence */
        size_t len = PySequence_Length(obj);
        fy_generic items[len];
        for (size_t i = 0; i < len; i++) {
            PyObject *item = PySequence_GetItem(obj, i);
            items[i] = python_to_generic(gb, item);
            Py_DECREF(item);
        }
        return fy_gb_sequence_create_array(gb, items, len);
    } else if (PyDict_Check(obj)) {
        /* Create mapping */
        PyObject *key, *value;
        Py_ssize_t pos = 0;

        size_t len = PyDict_Size(obj);
        fy_generic_map_pair pairs[len];
        size_t i = 0;

        while (PyDict_Next(obj, &pos, &key, &value)) {
            pairs[i].key = python_to_generic(gb, key);
            pairs[i].value = python_to_generic(gb, value);
            i++;
        }
        return fy_gb_mapping_create_array(gb, pairs, len);
    }

    PyErr_Format(PyExc_TypeError, "Unsupported type: %s", Py_TYPE(obj)->tp_name);
    return fy_invalid;
}

static PyObject *
libfyaml_dumps(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *obj;
    int compact = 0;
    int json_mode = 0;
    static char *kwlist[] = {"obj", "compact", "json", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|pp", kwlist, &obj, &compact, &json_mode))
        return NULL;

    struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
    fy_generic g = python_to_generic(gb, obj);

    if (!fy_generic_is_valid(g)) {
        fy_generic_builder_destroy(gb);
        return NULL;  /* Exception already set */
    }

    /* Emit to string using fy_generic_op_args */
    struct fy_generic_op_args op_args;
    memset(&op_args, 0, sizeof(op_args));

    /* Set emit flags based on options */
    if (json_mode) {
        op_args.emit.emit_flags = FYECF_MODE_JSON;
        if (!compact)
            op_args.emit.emit_flags |= FYECF_INDENT_2;
    } else if (compact) {
        op_args.emit.emit_flags = FYECF_MODE_FLOW;
    } else {
        op_args.emit.emit_flags = FYECF_MODE_BLOCK;
    }

    /* Emit returns a string generic! */
    fy_generic emitted = fy_generic_op_args(gb, FYGBOPF_EMIT, g, &op_args);

    if (!fy_generic_is_valid(emitted)) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to emit YAML");
        return NULL;
    }

    /* Extract the string from the generic */
    const char *yaml_str = fy_cast(emitted, "");
    if (!yaml_str) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to extract string from emitted generic");
        return NULL;
    }

    /* Create Python string (makes a copy) */
    PyObject *result = PyUnicode_FromString(yaml_str);

    fy_generic_builder_destroy(gb);
    return result;
}
```

### 4. Better Error Handling

**Current**: Generic error messages
**Needed**: YAML line numbers, context

**Investigation**:
- Does `fy_generic` preserve source location info?
- Can we extract line/column from parsing errors?
- How to get diagnostic info from `fy_generic_op_args()` failures?

**Implementation Ideas**:
```c
/* Create custom exception with location info */
static PyObject *YAMLError;  /* Custom exception type */

static void
set_yaml_error(const char *msg, fy_generic location_info)
{
    /* Extract line/column if available */
    PyObject *exc = PyObject_CallFunction(
        YAMLError, "s(ii)", msg, line, column
    );
    PyErr_SetObject(YAMLError, exc);
}

/* In module init */
YAMLError = PyErr_NewException("libfyaml.YAMLError", NULL, NULL);
PyModule_AddObject(m, "YAMLError", YAMLError);
```

### 5. Performance Optimizations (Lower Priority)

#### Caching for Repeated Access
```c
typedef struct {
    PyObject_HEAD
    fy_generic fyg;
    struct fy_generic_builder *gb;
    PyObject *gb_owner;
    PyObject *cache;  /* Dict of cached Python objects */
} FyGenericObject;
```

#### Direct Handle Access for Hot Paths
```c
/* Avoid repeated type checks in loops */
fy_generic_sequence_handle seqh = fy_cast(seq, fy_seq_handle_null);
if (seqh) {
    for (size_t i = 0; i < seqh->count; i++) {
        fy_generic item = seqh->items[i];
        /* Process without fy_get() overhead */
    }
}
```

### 6. Testing & Validation

#### Unit Tests
```python
import unittest

class TestLibFYAML(unittest.TestCase):
    def test_parse_scalar(self):
        doc = libfyaml.loads("42")
        self.assertEqual(int(doc), 42)

    def test_parse_mapping(self):
        doc = libfyaml.loads("a: 1\nb: 2")
        self.assertEqual(len(doc), 2)
        self.assertEqual(int(doc["a"]), 1)

    def test_parse_sequence(self):
        doc = libfyaml.loads("[1, 2, 3]")
        self.assertEqual(len(doc), 3)
        self.assertEqual([int(x) for x in doc], [1, 2, 3])

    def test_nested_structures(self):
        yaml = """
        users:
          - name: Alice
            age: 30
          - name: Bob
            age: 25
        """
        doc = libfyaml.loads(yaml)
        users = list(doc["users"])
        self.assertEqual(str(users[0]["name"]), "Alice")

    def test_to_python_conversion(self):
        doc = libfyaml.loads("a: [1, 2, {b: c}]")
        py = doc.to_python()
        self.assertEqual(py, {"a": [1, 2, {"b": "c"}]})

    def test_type_checking(self):
        doc = libfyaml.loads("x: 42")
        x = doc["x"]
        self.assertTrue(x.is_int())
        self.assertFalse(x.is_string())

    def test_memory_management(self):
        # Create and destroy many objects
        for _ in range(1000):
            doc = libfyaml.loads("a: 1")
            _ = doc["a"]
        # Should not leak
```

#### Benchmark Tests
```python
def benchmark_lazy_vs_eager():
    """Compare lazy access vs full conversion"""
    import time

    # Large YAML
    yaml = "items:\n" + "\n".join([f"  - name: item{i}" for i in range(10000)])

    # Lazy access - only first item
    start = time.time()
    doc = libfyaml.loads(yaml)
    first = str(doc["items"][0]["name"])
    lazy_time = time.time() - start

    # Eager conversion - everything
    start = time.time()
    doc = libfyaml.loads(yaml)
    py_dict = doc.to_python()
    first = py_dict["items"][0]["name"]
    eager_time = time.time() - start

    print(f"Lazy: {lazy_time:.4f}s, Eager: {eager_time:.4f}s")
    print(f"Speedup: {eager_time/lazy_time:.2f}x")
```

### 7. Documentation

#### Docstrings in C Code
```c
PyDoc_STRVAR(libfyaml_loads_doc,
"loads(s, mode='yaml') -> FyGeneric\n\
\n\
Parse YAML or JSON from a string.\n\
\n\
The parsed data remains in native C format for memory efficiency.\n\
Access values lazily using dict/list syntax, or convert to Python\n\
objects explicitly using to_python().\n\
\n\
Args:\n\
    s: YAML or JSON string to parse\n\
    mode: 'yaml' or 'json' (default: 'yaml')\n\
\n\
Returns:\n\
    FyGeneric: Lazy wrapper around parsed data\n\
\n\
Example:\n\
    >>> doc = loads('name: Alice\\nage: 30')\n\
    >>> str(doc['name'])  # Lazy access\n\
    'Alice'\n\
    >>> doc.to_python()   # Full conversion\n\
    {'name': 'Alice', 'age': 30}\n\
");
```

#### User Guide (README.md)
- Installation instructions
- Quick start examples
- Performance comparison with PyYAML
- Memory usage examples
- API reference

### 8. Packaging & Distribution

#### Setup.py Improvements
```python
setup(
    name='libfyaml',
    version='0.1.0',
    description='Fast YAML parser with NumPy-like lazy access',
    long_description=open('README.md').read(),
    long_description_content_type='text/markdown',
    author='...',
    license='MIT',
    classifiers=[
        'Development Status :: 3 - Alpha',
        'Programming Language :: Python :: 3',
        'Programming Language :: C',
        'Topic :: Software Development :: Libraries :: Python Modules',
    ],
    python_requires='>=3.7',
    ext_modules=[ext_module],
    packages=['libfyaml'],
)
```

#### CI/CD
- GitHub Actions for building wheels
- Test on multiple Python versions (3.7-3.12)
- Test on multiple platforms (Linux, macOS, Windows?)

### 9. Advanced Features (Future)

#### Streaming Support
```python
for doc in libfyaml.load_all("multi_document.yaml"):
    process(doc)
```

#### Schema Validation
```python
# Using reflection/type-aware features
schema = libfyaml.load_schema("schema.h", "struct Config")
doc = libfyaml.loads("...", schema=schema)
```

#### Custom Constructors/Representers
```python
def datetime_constructor(value):
    return datetime.fromisoformat(value)

libfyaml.add_constructor('!timestamp', datetime_constructor)
```

## Priority Order

### Phase 1: Production Ready (Essential)
1. ✅ Memory management (builder lifetime)
2. ✅ `load(filename)`
3. ✅ Iterator support
4. ✅ `keys()`, `values()`, `items()`
5. ✅ Basic error handling
6. ✅ Unit tests

### Phase 2: Feature Complete
7. ⬜ `dumps()` / `dump()` (Python → YAML)
8. ⬜ Better error messages with line numbers
9. ⬜ Comprehensive test suite
10. ⬜ Documentation

### Phase 3: Polish
11. ⬜ Performance optimizations
12. ⬜ Packaging for PyPI
13. ⬜ CI/CD

### Phase 4: Advanced (Nice to Have)
14. ⬜ Streaming multi-document
15. ⬜ Schema validation
16. ⬜ Custom constructors

## Key Questions to Investigate

1. ✅ **Decoder parse flags** - SOLVED
   - `FYGDPF_DISABLE_DIRECTORY` - Disable directory access
   - `FYGDPF_MULTI_DOCUMENT` - Parse multiple documents

2. ✅ **Encoder emit flags** - SOLVED
   - `FYGEEF_DISABLE_DIRECTORY` - Disable directory access
   - `FYGEEF_MULTI_DOCUMENT` - Emit multiple documents

3. ✅ **Builder lifetime management** - SOLVED
   - Simple: create, use, destroy - everything inside is destroyed
   - No complex reference counting needed
   - Root FyGeneric owns builder, children reference root

4. ✅ **Emit to string (not stdout)** - SOLVED
   - `fy_generic_op_args(gb, FYGBOPF_EMIT, value, &args)` **returns a string generic**!
   - Extract with `fy_cast(emitted, "")`
   - Can emit as YAML block, YAML flow, or JSON via `args.emit.emit_flags`
   - Flags: `FYECF_MODE_BLOCK`, `FYECF_MODE_FLOW`, `FYECF_MODE_JSON`, `FYECF_INDENT_2`, etc.

5. ⬜ **Source location in errors**
   - Does `fy_generic` preserve line/column info from parsing?
   - How to extract it for better error messages?

6. ⬜ **Document metadata in generics**
   - User mentioned decoder can embed document metadata in `fy_generic` itself
   - What enables this? How to access it?
   - Related to avoiding `fy_document` wrapper

## Estimated Implementation Time

- **Phase 1** (Production Ready): ~2-3 days
- **Phase 2** (Feature Complete): ~2-3 days
- **Phase 3** (Polish): ~1-2 days
- **Phase 4** (Advanced): ~3-5 days

**Total for fully-featured library**: ~1-2 weeks

The minimal prototype has already proven the concept and solved the hardest problems (API discovery, PIC builds, LLVM linking). The remaining work is mostly straightforward expansion following the established patterns.
