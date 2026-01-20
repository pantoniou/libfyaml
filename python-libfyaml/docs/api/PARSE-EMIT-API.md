# Parse and Emit API for Python Bindings

Discovered from `test/libfyaml-test-generic.c::parse_emit_ops()` test function.

## Key Insight

**Both PARSE and EMIT operations use `fy_generic_op_args()` and work with string generics!**

## Parse: String → Generic

Parse YAML/JSON string into native `fy_generic` format:

```c
// Create string generic from input
fy_generic yaml_str = fy_gb_string_create(gb, "- item1\n- item2\n- item3");

// Setup parse arguments
struct fy_generic_op_args args;
memset(&args, 0, sizeof(args));
args.parse.parser_mode = fypm_yaml_1_2;  // or fypm_json
args.parse.multi_document = false;

// Parse: returns the parsed data structure
fy_generic parsed = fy_generic_op_args(gb, FYGBOPF_PARSE, yaml_str, &args);

// Now parsed is a sequence/mapping/scalar that can be accessed with fy_get(), fy_len(), etc.
ck_assert(fy_generic_is_sequence(parsed));
ck_assert_str_eq(fy_get(parsed, 0, ""), "item1");
```

### Python Implementation

```c
static PyObject *
libfyaml_loads(PyObject *self, PyObject *args)
{
    const char *yaml_str;
    const char *mode = "yaml";

    if (!PyArg_ParseTuple(args, "s|s", &yaml_str, &mode))
        return NULL;

    struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
    fy_generic input = fy_gb_string_create(gb, yaml_str);

    struct fy_generic_op_args op_args;
    memset(&op_args, 0, sizeof(op_args));
    op_args.parse.parser_mode = strcmp(mode, "json") == 0 ? fypm_json : fypm_yaml_1_2;
    op_args.parse.multi_document = false;

    fy_generic parsed = fy_generic_op_args(gb, FYGBOPF_PARSE, input, &op_args);

    if (!fy_generic_is_valid(parsed)) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_ValueError, "Failed to parse YAML/JSON");
        return NULL;
    }

    // Create root FyGeneric object that owns the builder
    FyGenericObject *result = PyObject_New(FyGenericObject, &FyGenericType);
    result->fyg = parsed;
    result->gb = gb;      // Root owns builder
    result->root = NULL;  // This is the root

    return (PyObject *)result;
}
```

## Emit: Generic → String

Emit `fy_generic` data structure to YAML/JSON string:

```c
// Create data structure
fy_generic seq = fy_sequence(1, 2, 3, 4, 5);

// Setup emit arguments
struct fy_generic_op_args args;
memset(&args, 0, sizeof(args));
args.emit.emit_flags = FYECF_MODE_BLOCK;  // or FYECF_MODE_FLOW, FYECF_MODE_JSON

// Emit: returns a STRING generic!
fy_generic emitted = fy_generic_op_args(gb, FYGBOPF_EMIT, seq, &args);

ck_assert(fy_generic_is_string(emitted));  // ← It's a string!

// Extract the C string
const char *yaml_str = fy_cast(emitted, "");
printf("YAML output:\n%s\n", yaml_str);
```

### Emit Flags

```c
// Format options
FYECF_MODE_BLOCK     // YAML block format (default, multi-line)
FYECF_MODE_FLOW      // YAML flow format (compact, inline)
FYECF_MODE_JSON      // JSON format

// Indentation (combine with | operator)
FYECF_INDENT_2       // 2-space indent (JSON)
FYECF_INDENT_4       // 4-space indent
// ... more flags available
```

### Python Implementation

```c
static PyObject *
libfyaml_dumps(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *obj;
    int compact = 0;
    int json_mode = 0;
    static char *kwlist[] = {"obj", "compact", "json", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|pp", kwlist, &obj, &compact, &json_mode))
        return NULL;

    // Convert Python object to fy_generic
    struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
    fy_generic g = python_to_generic(gb, obj);

    if (!fy_generic_is_valid(g)) {
        fy_generic_builder_destroy(gb);
        return NULL;
    }

    // Setup emit arguments
    struct fy_generic_op_args op_args;
    memset(&op_args, 0, sizeof(op_args));

    if (json_mode) {
        op_args.emit.emit_flags = FYECF_MODE_JSON;
        if (!compact)
            op_args.emit.emit_flags |= FYECF_INDENT_2;
    } else if (compact) {
        op_args.emit.emit_flags = FYECF_MODE_FLOW;
    } else {
        op_args.emit.emit_flags = FYECF_MODE_BLOCK;
    }

    // Emit to string generic
    fy_generic emitted = fy_generic_op_args(gb, FYGBOPF_EMIT, g, &op_args);

    if (!fy_generic_is_valid(emitted)) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to emit YAML");
        return NULL;
    }

    // Extract string and create Python string
    const char *yaml_str = fy_cast(emitted, "");
    PyObject *result = PyUnicode_FromString(yaml_str);

    fy_generic_builder_destroy(gb);
    return result;
}
```

## Round-Trip Example from Test Suite

```c
// Start with YAML string
yaml_str = fy_gb_string_create(gb, "foo: bar\nbaz: 42");

// Parse
memset(&args, 0, sizeof(args));
args.parse.parser_mode = fypm_yaml_1_2;
parsed = fy_generic_op_args(gb, FYGBOPF_PARSE, yaml_str, &args);

// Emit back to string
memset(&args, 0, sizeof(args));
args.emit.emit_flags = FYECF_MODE_BLOCK;
emitted = fy_generic_op_args(gb, FYGBOPF_EMIT, parsed, &args);

// Parse the emitted string again (round-trip!)
const char *emitted_str = fy_cast(emitted, "");
fy_generic reparsed_str = fy_gb_string_create(gb, emitted_str);

memset(&args, 0, sizeof(args));
args.parse.parser_mode = fypm_yaml_1_2;
reparsed = fy_generic_op_args(gb, FYGBOPF_PARSE, reparsed_str, &args);

// reparsed should match original parsed structure
```

## Complete Python Workflow

```python
import libfyaml

# Parse YAML string to native format
doc = libfyaml.loads("name: Alice\nage: 30")

# Access lazily (stays in native format)
name = doc["name"]      # FyGeneric wrapper
age_value = int(doc["age"])  # Convert to Python int

# Serialize Python object to YAML
python_dict = {"name": "Bob", "age": 25}
yaml_str = libfyaml.dumps(python_dict)
# Output: "name: Bob\nage: 25\n"

# JSON mode
json_str = libfyaml.dumps(python_dict, json=True)
# Output: '{\n  "name": "Bob",\n  "age": 25\n}\n'

# Compact YAML
compact_yaml = libfyaml.dumps(python_dict, compact=True)
# Output: "{name: Bob, age: 25}\n"
```

## Key Takeaways

1. **Parse and emit are symmetric**: Both use `fy_generic_op_args()` with different operations
2. **Emit returns a string generic**: Not written to stdout, but returned as a `fy_generic` string
3. **All in one builder**: Parse input, emit output, both use the same builder
4. **Clean API**: String in → generic out (parse), generic in → string out (emit)

This makes Python bindings very straightforward:
- `loads()`: Parse string → return root FyGeneric that owns builder
- `dumps()`: Convert Python → generic → emit to string → return Python string
