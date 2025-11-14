# Python FyGeneric Mutation Design

## Overview

FyGeneric objects in Python bindings support efficient mutation through a **hybrid representation** that minimizes materialization overhead. Objects can be in three states: **Frozen**, **Partially Thawed (Scalar)**, and **Partially Thawed (Collection)**.

## Module State Structure

All libfyaml Python binding state is stored in a **module state structure** (PEP 489), avoiding global variables:

```c
typedef struct {
    PyObject *thawed_roots;     // Dict: gb -> thawed root (for tracking mutations)
    PyTypeObject *FyGenericType; // FyGeneric type object
    PyTypeObject *FyDocumentType; // FyDocument type object
    // ... other types and module-level state ...
} libfyaml_state;

// Module definition with state
static PyModuleDef_Slot libfyaml_slots[] = {
    {Py_mod_exec, libfyaml_exec},
    {0, NULL}
};

static struct PyModuleDef libfyaml_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "libfyaml",
    .m_doc = "libfyaml Python bindings",
    .m_size = sizeof(libfyaml_state),
    .m_slots = libfyaml_slots,
    .m_traverse = libfyaml_traverse,
    .m_clear = libfyaml_clear,
    .m_free = libfyaml_free,
};
```

**Benefits**:
- ✅ No global variables - thread-safe with GIL
- ✅ Multiple module instances supported
- ✅ Clean separation of state
- ✅ Proper cleanup on module unload
- ✅ Modern Python extension best practice (Python 3.5+)

**Accessing state**:
```c
// From type method (has self)
PyObject *module = PyType_GetModule(Py_TYPE(self));
libfyaml_state *state = PyModule_GetState(module);

// From module function (has module pointer)
libfyaml_state *state = PyModule_GetState(module);
```

## State Machine

Two implementation strategies:

### Strategy A: Simple Global Mutated State (Recommended for Initial Implementation)

```
    ┌─────────────┐
    │   FROZEN    │
    │ (fy_generic)│
    │  global:    │
    │ mutated=false│
    └─────┬───────┘
          │
    [first mutation]
          │
          ▼
    ┌─────────────┐
    │ FULLY THAWED│
    │(Python dict/│
    │   list)     │
    │  global:    │
    │ mutated=true│
    └─────────────┘
```

**Key insight**: Once ANY mutation occurs, convert entire tree to native Python objects. No hybrid state, no parent tracking, no resolution - just standard Python dict/list.

**Characteristics**:
- Frozen: Fast, memory-efficient FyGeneric
- First mutation: Convert entire tree to Python dict/list
- Set global `mutated` flag
- All objects become regular Python - no special handling
- Can freeze() back to FyGeneric when done

### Strategy B: Hybrid State with Shallow Materialization (Advanced, Complex)

```
                    ┌─────────────┐
                    │   FROZEN    │
                    │ (fy_generic)│
                    └─────┬───────┘
                          │
          ┌───────────────┼───────────────┐
          │               │               │
    [read scalar]   [read collection] [mutate scalar]
          │               │               │
          ▼               ▼               ▼
    ┌─────────┐    ┌────────────┐  ┌────────────┐
    │ FROZEN  │    │  FROZEN    │  │ THAWED     │
    │(no change)   │(no change) │  │ SCALAR     │
    └─────────┘    └──────┬─────┘  │(fy_generic │
                          │         │+ PyObject*)│
                   [mutate item]    └────────────┘
                          │
                          ▼
                   ┌────────────────┐
                   │    THAWED      │
                   │  COLLECTION    │
                   │(array + bitmap)│
                   └────────────────┘
```

**Characteristics**: Only materializes mutation paths, requires root + path tracking, complex resolution logic.

## Strategy Comparison

| Aspect | Strategy A (Simple) | Strategy B (Hybrid) |
|--------|---------------------|---------------------|
| **Complexity** | Low | Very High |
| **First mutation cost** | High (full tree) | Low (path only) |
| **Reference semantics** | Perfect (native Python) | Perfect (with resolution) |
| **Memory after mutation** | High (full Python tree) | Low (hybrid) |
| **Access performance** | Fast (native Python) | Medium (resolution check) |
| **Implementation effort** | 1-2 days | 1-2 weeks |
| **Bugs/edge cases** | Few | Many |

## When to Use Each Strategy

### Use Strategy A (Simple) when:
- ✅ Initial implementation / MVP
- ✅ Small to medium trees (< 1000 items)
- ✅ Mutations are rare (most config loading is read-only)
- ✅ Typical config files (99% of use cases)
- ✅ Want simple, maintainable code

### Use Strategy B (Hybrid) when:
- ⚠️ Very large trees (> 10k items) with sparse mutations
- ⚠️ Need to optimize the "change 1 key in 100k-key config" case
- ⚠️ Profiling shows materialization is a bottleneck
- ⚠️ Ready to handle complex edge cases and debugging

**Key insight**: Tree size matters! For small trees, converting to Python is cheap and probably faster than hybrid overhead.

**Recommendation**:
- Start with Strategy A for all trees
- Optionally add size-based threshold later (e.g., use Strategy A for < 10k items, Strategy B for larger)
- Only implement Strategy B if profiling shows it's needed

### Adaptive Strategy (Future Enhancement)

Once both strategies are implemented, choose automatically based on tree size:

```c
static int
FyGeneric_ensure_mutable(FyGenericObject *self)
{
    if (self->is_thawed || self->is_materialized)
        return 0;  // Already mutable

    // Count items in tree
    size_t total_items = fy_generic_tree_count(self->fg);

    if (total_items < STRATEGY_A_THRESHOLD) {  // e.g., 10000
        // Small tree: Use Strategy A (full conversion)
        return FyGeneric_ensure_thawed(self);
    } else {
        // Large tree: Use Strategy B (shallow materialization)
        return FyGeneric_materialize_collection(self);
    }
}
```

This gives optimal performance for all tree sizes without user intervention.

## Strategy A: Simple Implementation

### Structure

```c
struct FyGenericObject {
    PyObject_HEAD
    struct fy_generic_builder *gb;

    // Two states: either frozen OR fully materialized
    fy_generic fg;           // FyGeneric value (if frozen)
    PyObject *py_value;      // Native Python dict/list (if thawed)

    bool is_thawed;          // True if materialized to Python
};
```

### First Mutation Triggers Full Materialization

```c
static int
FyGeneric_ensure_thawed(FyGenericObject *self)
{
    if (self->is_thawed)
        return 0;  // Already thawed

    // Convert entire fy_generic tree to Python
    self->py_value = generic_to_python_recursive(self->gb, self->fg);
    if (self->py_value == NULL)
        return -1;

    // Release FyGeneric
    self->fg = fy_invalid;
    self->is_thawed = true;

    // Register in module state (if using option 3 for frozen reference tracking)
    PyObject *module = PyType_GetModule(Py_TYPE(self));
    libfyaml_state *state = get_module_state(module);

    if (state->thawed_roots == NULL) {
        state->thawed_roots = PyDict_New();
        if (state->thawed_roots == NULL)
            return -1;
    }

    // Store this object as the thawed root for this gb
    // (All objects from same load() share same gb)
    if (PyDict_SetItem(state->thawed_roots, (PyObject*)self->gb, (PyObject*)self) < 0)
        return -1;

    return 0;
}
```

**Note**: The module state registration is only needed if implementing option 3 (frozen reference tracking). For option 1 (accept limitation), omit the registration.

### Accessors Check State

```c
static PyObject *
FyGeneric_getitem(FyGenericObject *self, PyObject *key)
{
    if (self->is_thawed) {
        // Thawed: Use native Python
        return PyObject_GetItem(self->py_value, key);
    } else {
        // Frozen: Use FyGeneric
        fy_generic child_fg = fy_generic_op(self->gb, FYGBOPF_GET, self->fg, 1, &key_fg);
        return FyGeneric_from_generic(self->gb, child_fg);
    }
}

static int
FyGeneric_setitem(FyGenericObject *self, PyObject *key, PyObject *value)
{
    // Ensure fully thawed
    if (FyGeneric_ensure_thawed(self) < 0)
        return -1;

    // Now just use native Python
    return PyObject_SetItem(self->py_value, key, value);
}
```

### The Tricky Reference Case - SOLVED

```python
data = libfyaml.loads("foo: [ [1, 2], 11 ]")

x1 = data['foo'][0]
# x1 is frozen, wrapping [1, 2]

data['foo'][0][0] = 200
# Triggers: FyGeneric_ensure_thawed(data)
# Converts ENTIRE tree to Python
# Now data.py_value is a Python dict with native lists

# What about x1?
print(x1[0])
```

**Answer**: `x1` is still frozen, so it returns the original FyGeneric value (1).

**But wait** - is this wrong? Let's think about Python semantics:

```python
# Pure Python equivalent
original = {'foo': [[1, 2], 11]}
data = original
x1 = data['foo'][0]  # x1 = [1, 2]

# Now if we do this:
data['foo'][0] = [200, 2]  # REPLACE the list

# x1 still points to the old list
print(x1[0])  # 1 - x1 wasn't changed!

# But if we MUTATE the same list:
data['foo'][0][0] = 200  # Mutate in place

# Then x1 sees it
print(x1[0])  # 200 - because it's the SAME list object
```

**The issue**: In Strategy A, when we convert to Python, we create NEW Python objects. So `x1` (frozen) and the mutated tree have different underlying objects.

**Solution options for Strategy A**:

1. **Accept the limitation**: Document that frozen references don't see mutations (conservative, Clojure-like semantics)

2. **Add escape hatch**: Provide `x1 = x1.thaw()` or `x1 = x1.sync()` to manually refresh

3. **Track in module state**: When any object is thawed, store root in module's state dict. On access from a frozen object, check if its tree was thawed and **force thawing** of the frozen object:

```c
// Module state structure (PEP 489)
typedef struct {
    PyObject *thawed_roots;  // Dict: gb -> thawed root
    // ... other module state ...
} libfyaml_state;

// Get module state
static inline libfyaml_state *
get_module_state(PyObject *module)
{
    return (libfyaml_state *)PyModule_GetState(module);
}

static PyObject *
FyGeneric_getitem(FyGenericObject *self, PyObject *key)
{
    // Get module state
    PyObject *module = PyType_GetModule(Py_TYPE(self));
    libfyaml_state *state = get_module_state(module);

    // Check if our tree has been thawed
    if (!self->is_thawed && state->thawed_roots != NULL) {
        PyObject *root = PyDict_GetItem(state->thawed_roots, (PyObject*)self->gb);
        if (root != NULL) {
            // Our tree was thawed! Force thaw this frozen object too
            if (FyGeneric_ensure_thawed(self) < 0)
                return NULL;
        }
    }

    if (self->is_thawed) {
        return PyObject_GetItem(self->py_value, key);
    }

    // Still frozen
    fy_generic key_fg = python_to_generic(self->gb, key);
    fy_generic child_fg = fy_generic_op(self->gb, FYGBOPF_GET, self->fg, 1, &key_fg);
    return FyGeneric_from_generic(self->gb, child_fg);
}
```

**How it works**:
- Module state stores `thawed_roots` dict (gb -> thawed root)
- When ANY object in a tree is mutated, the root is thawed and stored in `state->thawed_roots[gb]`
- When accessing a frozen object, check if its `gb` is in module's `thawed_roots`
- If yes, thaw this object too (it will convert its local fy_generic to Python)
- Thread-safe via GIL, no global variables
- Minimal overhead: Single dict lookup per access from frozen references

**Alternative simpler approach**: Accept the limitation and document it.

For Strategy A, **Recommendation**: Start with option 1 (accept limitation), add option 3 (module state tracking) only if users complain.

## Strategy B: Hybrid Implementation (Detailed)

**Optimization**: Strategy B can also use the module state tracking from Strategy A! Instead of complex path walking for frozen reference resolution, just check the module's thawed_roots:

```c
static PyObject *
FyGeneric_getitem(FyGenericObject *self, PyObject *key)
{
    // Get module state
    PyObject *module = PyType_GetModule(Py_TYPE(self));
    libfyaml_state *state = get_module_state(module);

    // Quick check: Has this tree been mutated anywhere?
    if (!self->is_materialized && state->thawed_roots != NULL) {
        if (PyDict_Contains(state->thawed_roots, (PyObject*)self->gb)) {
            // Tree was mutated, but we're still frozen - materialize us
            if (FyGeneric_materialize_collection(self) < 0)
                return NULL;
        }
    }

    // Now proceed with normal hybrid logic...
    if (self->collection_array == NULL) {
        // FROZEN: Get from fy_generic
        // ...
    } else {
        // THAWED: Get from array
        // ...
    }
}
```

This gives Strategy B the same frozen reference correctness as Strategy A, without complex path resolution. Just a single dict lookup!

### State 1: Frozen

**Storage**:
```c
struct FyGenericObject {
    PyObject_HEAD
    struct fy_generic_builder *gb;
    fy_generic fg;           // The FyGeneric value
    PyObject *py_cache;      // NULL (frozen)
    void *collection_array;  // NULL (frozen)
    uint64_t *bitmap;        // NULL (frozen)
    size_t array_size;       // 0 (frozen)
};
```

**Characteristics**:
- Only `fg` (fy_generic) is populated
- Minimal memory: 8 bytes for the fy_generic value
- All reads go directly to C library
- Immutable: Any mutation triggers thawing

### State 2: Partially Thawed - Scalar

**Storage**:
```c
struct FyGenericObject {
    PyObject_HEAD
    struct fy_generic_builder *gb;
    fy_generic fg;           // Original FyGeneric value
    PyObject *py_cache;      // Cached Python object (INCREF'd)
    void *collection_array;  // NULL (not a collection)
    uint64_t *bitmap;        // NULL (not a collection)
    size_t array_size;       // 0 (not a collection)
};
```

**Characteristics**:
- Used for scalars (int, float, string, bool, null)
- `py_cache` holds the Python equivalent (PyLong, PyFloat, PyUnicode, etc.)
- Needed when scalar is mutated and needs to propagate as PyObject*
- Memory: +8 bytes for PyObject* pointer

**When created**:
- When a scalar value is set in a thawed collection
- The scalar is converted to PyObject* and cached

### State 3: Partially Thawed - Collection

**Storage**:
```c
struct FyGenericObject {
    PyObject_HEAD
    struct fy_generic_builder *gb;
    fy_generic fg;           // NULL (thawed, no longer needed)
    PyObject *py_cache;      // NULL (not a scalar)
    void *collection_array;  // Array of ItemValue unions
    uint64_t *bitmap;        // Bitmap tracking which items are materialized
    size_t array_size;       // Number of items
};

union ItemValue {
    fy_generic fg;           // 8 bytes: fy_generic value
    PyObject *py;            // 8 bytes: Python object pointer
};
```

**Characteristics**:
- Used for sequences and mappings
- `collection_array` is array of `ItemValue` unions
- Each item is either fy_generic OR PyObject*, tracked by bitmap
- Initially all items are fy_generic (bitmap all 0s)
- As items are mutated, they become PyObject* (bitmap bit set to 1)

**Memory layout for mapping** (key-value pairs):
```c
// Array layout: [key0, val0, key1, val1, ...]
// array_size = num_pairs * 2
// bitmap has num_pairs * 2 bits

ItemValue items[array_size];  // Pairs interleaved
uint64_t bitmap[ceil(array_size / 64)];
```

**Memory layout for sequence**:
```c
// Array layout: [item0, item1, item2, ...]
// array_size = num_items
// bitmap has num_items bits

ItemValue items[array_size];
uint64_t bitmap[ceil(array_size / 64)];
```

**Bitmap encoding**:
- Bit = 0: Item is `fy_generic` (unmaterialized)
- Bit = 1: Item is `PyObject*` (materialized)
- Packed into 64-bit words for efficiency

## Materialization Process

### Reading (No Mutation)

**Frozen state - no materialization**:
```python
data = libfyaml.load('config.yaml')  # Frozen
value = data['key']                  # Returns new Frozen FyGeneric
```

Implementation:
```c
static PyObject *
FyGeneric_getitem(FyGenericObject *self, PyObject *key)
{
    if (self->collection_array == NULL) {
        // FROZEN: Get from fy_generic
        fy_generic child = fy_generic_op(self->gb, FYGBOPF_GET, self->fg, 1, &key_fg);
        return FyGeneric_from_generic(self->gb, child);
    } else {
        // THAWED: Get from array
        // ... (see below)
    }
}
```

### First Mutation on Collection

**Triggers materialization**:
```python
data['key'] = 'value'  # First mutation
```

Process:
1. Allocate `collection_array` (size = number of items)
2. Copy all fy_generic values from original
3. Allocate bitmap, initialize all bits to 0
4. Set the specific item to PyObject*, set its bitmap bit to 1
5. Mark collection as thawed
6. Release original `fg` (set to NULL)

Implementation:
```c
static int
FyGeneric_materialize_collection(FyGenericObject *self)
{
    if (self->collection_array != NULL)
        return 0;  // Already materialized

    // Get size
    size_t num_items;
    if (fy_generic_is_mapping(self->fg)) {
        num_items = fy_generic_mapping_length(self->fg) * 2;  // key-value pairs
    } else if (fy_generic_is_sequence(self->fg)) {
        num_items = fy_generic_sequence_length(self->fg);
    } else {
        return -1;  // Not a collection
    }

    // Allocate array
    union ItemValue *array = PyMem_Malloc(sizeof(union ItemValue) * num_items);
    if (!array)
        return -1;

    // Allocate bitmap (1 bit per item, packed in 64-bit words)
    size_t bitmap_words = (num_items + 63) / 64;
    uint64_t *bitmap = PyMem_Calloc(bitmap_words, sizeof(uint64_t));
    if (!bitmap) {
        PyMem_Free(array);
        return -1;
    }

    // Copy fy_generic values to array
    if (fy_generic_is_mapping(self->fg)) {
        // Copy key-value pairs
        size_t idx = 0;
        fy_generic keys = fy_generic_op(self->gb, FYGBOPF_KEYS, self->fg, 0, NULL);
        for each key in keys {
            array[idx++].fg = key;  // Key as fy_generic
            fy_generic val = fy_generic_get(self->fg, key);
            array[idx++].fg = val;  // Value as fy_generic
        }
    } else {
        // Copy sequence items
        for (size_t i = 0; i < num_items; i++) {
            array[i].fg = fy_generic_get_at(self->fg, i);
        }
    }

    // Update object
    self->collection_array = array;
    self->bitmap = bitmap;
    self->array_size = num_items;
    self->fg = fy_invalid;  // Release FyGeneric

    return 0;
}
```

### Setting an Item

**Mutation on materialized collection**:
```python
data['key'] = 'new_value'
```

Implementation:
```c
static int
FyGeneric_setitem(FyGenericObject *self, PyObject *key, PyObject *value)
{
    // Materialize if needed
    if (self->collection_array == NULL) {
        if (FyGeneric_materialize_collection(self) < 0)
            return -1;
    }

    // Find item index
    size_t idx = find_key_index(self, key);
    if (idx == (size_t)-1)
        return -1;  // Key not found (or append)

    // For mapping, idx points to key, value is at idx+1
    if (fy_generic_is_mapping(self->fg))
        idx++;  // Point to value slot

    // Release old value if it was PyObject*
    if (IS_PYOBJECT(self->bitmap, idx)) {
        Py_DECREF(self->collection_array[idx].py);
    }

    // Set new value as PyObject*
    Py_INCREF(value);
    self->collection_array[idx].py = value;
    SET_PYOBJECT_BIT(self->bitmap, idx);

    // TODO: Propagate change upward to parent

    return 0;
}
```

### Getting an Item from Thawed Collection

**Read from thawed collection**:
```python
value = data['key']
```

Implementation:
```c
static PyObject *
FyGeneric_getitem_thawed(FyGenericObject *self, PyObject *key)
{
    // Find item index
    size_t idx = find_key_index(self, key);
    if (idx == (size_t)-1) {
        PyErr_SetObject(PyExc_KeyError, key);
        return NULL;
    }

    // For mapping, idx points to key, value is at idx+1
    if (self->is_mapping)
        idx++;

    // Check bitmap to see if it's fy_generic or PyObject*
    if (IS_PYOBJECT(self->bitmap, idx)) {
        // Return PyObject* (INCREF for new reference)
        PyObject *py = self->collection_array[idx].py;
        Py_INCREF(py);
        return py;
    } else {
        // Return fy_generic wrapped
        fy_generic fg = self->collection_array[idx].fg;
        return FyGeneric_from_generic(self->gb, fg);
    }
}
```

## Bitmap Operations

**Macros for bitmap manipulation**:
```c
// Check if item at index is PyObject*
#define IS_PYOBJECT(bitmap, idx) \
    ((bitmap)[(idx) / 64] & (1ULL << ((idx) % 64)))

// Set item at index as PyObject*
#define SET_PYOBJECT_BIT(bitmap, idx) \
    ((bitmap)[(idx) / 64] |= (1ULL << ((idx) % 64)))

// Clear item at index (set as fy_generic)
#define CLEAR_PYOBJECT_BIT(bitmap, idx) \
    ((bitmap)[(idx) / 64] &= ~(1ULL << ((idx) % 64)))
```

## Upward Propagation

**Challenge**: When a deeply nested value is mutated, the change needs to propagate to the root.

**Critical example showing why parent tracking is necessary**:
```python
data = libfyaml.loads("{'list': [1, 2, 3]}")
l = data['list']  # Returns NEW FyGenericObject wrapping the list
l[0] = 10         # Mutates l

# Without parent tracking:
print(l[0])           # Shows 10 (correct)
print(data['list'][0])  # Shows 1 (WRONG! - returns new wrapper from frozen data)

# With parent tracking:
print(l[0])           # Shows 10 (correct)
print(data['list'][0])  # Shows 10 (correct! - parent was updated)
```

**Problem with "accessor chain materialization"**: Each `__getitem__` returns a NEW FyGenericObject. Without parent tracking, mutations on the child object don't propagate back to the parent.

### Solution: Parent Reference Tracking (Required)

Each FyGenericObject tracks its parent and position:

```c
struct FyGenericObject {
    PyObject_HEAD
    struct fy_generic_builder *gb;
    fy_generic fg;           // FyGeneric value (NULL if thawed)
    PyObject *py_cache;      // Scalar PyObject* cache
    void *collection_array;  // Array of ItemValue unions
    uint64_t *bitmap;        // Bitmap tracking materialized items
    size_t array_size;       // Number of items

    // Parent tracking
    PyObject *parent;        // Parent FyGenericObject (borrowed ref)
    PyObject *parent_key;    // Key in parent (for mappings)
    Py_ssize_t parent_index; // Index in parent (for sequences), -1 if mapping
};
```

**Reference counting considerations**:
- Parent is a **borrowed reference** (not INCREF'd) to avoid circular references
- Parent keeps the child alive via its collection_array
- When parent is deallocated, children are cleaned up

### Propagation Algorithm

When a value is mutated:

```c
static int
FyGeneric_setitem_with_propagation(FyGenericObject *self, PyObject *key, PyObject *value)
{
    // 1. Materialize this object if needed
    if (self->collection_array == NULL) {
        if (FyGeneric_materialize_collection(self) < 0)
            return -1;
    }

    // 2. Find item index
    size_t idx = find_key_index(self, key);
    if (idx == (size_t)-1)
        return -1;

    // For mapping, idx points to key, value is at idx+1
    if (self->is_mapping)
        idx++;

    // 3. Release old value if it was PyObject*
    if (IS_PYOBJECT(self->bitmap, idx)) {
        Py_DECREF(self->collection_array[idx].py);
    }

    // 4. Set new value as PyObject*
    Py_INCREF(value);
    self->collection_array[idx].py = value;
    SET_PYOBJECT_BIT(self->bitmap, idx);

    // 5. Update parent's reference to us
    if (self->parent != NULL) {
        FyGenericObject *parent = (FyGenericObject*)self->parent;

        // Materialize parent if needed
        if (parent->collection_array == NULL) {
            if (FyGeneric_materialize_collection(parent) < 0)
                return -1;
        }

        // Find our position in parent
        size_t parent_idx;
        if (self->parent_index >= 0) {
            // We're in a sequence
            parent_idx = self->parent_index;
        } else {
            // We're in a mapping, find our key
            parent_idx = find_key_index(parent, self->parent_key);
            if (parent_idx != (size_t)-1)
                parent_idx++;  // Point to value slot
        }

        if (parent_idx != (size_t)-1) {
            // Update parent's entry to point to us (as PyObject*)
            if (!IS_PYOBJECT(parent->bitmap, parent_idx)) {
                // Was fy_generic, now becomes PyObject*
                Py_INCREF((PyObject*)self);
                parent->collection_array[parent_idx].py = (PyObject*)self;
                SET_PYOBJECT_BIT(parent->bitmap, parent_idx);
            }
            // If already PyObject*, it's already pointing to us
        }

        // Recursively propagate to grandparent
        // (This is automatic when parent's parent also tracks back)
    }

    return 0;
}
```

### Creating Child References

When returning a child from `__getitem__`, set up parent tracking:

```c
static PyObject *
FyGeneric_getitem(FyGenericObject *self, PyObject *key)
{
    if (self->collection_array == NULL) {
        // FROZEN: Get from fy_generic and wrap
        fy_generic child_fg = fy_generic_op(self->gb, FYGBOPF_GET, self->fg, 1, &key_fg);
        if (fy_generic_is_invalid(child_fg)) {
            PyErr_SetObject(PyExc_KeyError, key);
            return NULL;
        }

        FyGenericObject *child = FyGeneric_from_generic(self->gb, child_fg);

        // Set up parent tracking
        child->parent = (PyObject*)self;  // Borrowed reference

        if (PyLong_Check(key)) {
            // Sequence index
            child->parent_index = PyLong_AsSsize_t(key);
            child->parent_key = NULL;
        } else {
            // Mapping key
            child->parent_index = -1;
            Py_INCREF(key);
            child->parent_key = key;
        }

        return (PyObject*)child;

    } else {
        // THAWED: Get from array
        size_t idx = find_key_index(self, key);
        if (idx == (size_t)-1) {
            PyErr_SetObject(PyExc_KeyError, key);
            return NULL;
        }

        if (self->is_mapping)
            idx++;  // Point to value

        if (IS_PYOBJECT(self->bitmap, idx)) {
            // Return PyObject* (already set up parent tracking)
            PyObject *child = self->collection_array[idx].py;
            Py_INCREF(child);
            return child;
        } else {
            // Wrap fy_generic and set up parent tracking
            fy_generic child_fg = self->collection_array[idx].fg;
            FyGenericObject *child = FyGeneric_from_generic(self->gb, child_fg);

            // Set up parent tracking
            child->parent = (PyObject*)self;  // Borrowed reference

            if (PyLong_Check(key)) {
                child->parent_index = PyLong_AsSsize_t(key);
                child->parent_key = NULL;
            } else {
                child->parent_index = -1;
                Py_INCREF(key);
                child->parent_key = key;
            }

            return (PyObject*)child;
        }
    }
}
```

### Memory Management

**Borrowed references for parent**:
- Child doesn't INCREF parent (avoids cycles)
- Parent keeps child alive via collection_array
- When parent is freed, children are properly cleaned up

**Parent key reference**:
- If parent is mapping: INCREF parent_key (we own it)
- If parent is sequence: parent_key is NULL, use parent_index

**Cleanup**:
```c
static void
FyGeneric_dealloc(FyGenericObject *self)
{
    // Clean up parent_key if owned
    Py_XDECREF(self->parent_key);

    // Clean up collection_array
    if (self->collection_array != NULL) {
        for (size_t i = 0; i < self->array_size; i++) {
            if (IS_PYOBJECT(self->bitmap, i)) {
                Py_DECREF(self->collection_array[i].py);
            }
        }
        PyMem_Free(self->collection_array);
        PyMem_Free(self->bitmap);
    }

    // ... rest of cleanup ...
}
```

## Iterator Updates

**Iteration must check bitmap**:

```c
static PyObject *
FyGeneric_iter_next(FyGenericIterObject *iter)
{
    FyGenericObject *container = iter->container;

    if (container->collection_array != NULL) {
        // THAWED: Iterate array
        if (iter->index >= container->array_size)
            return NULL;  // StopIteration

        size_t idx = iter->index;
        iter->index++;

        if (IS_PYOBJECT(container->bitmap, idx)) {
            // Return PyObject*
            PyObject *py = container->collection_array[idx].py;
            Py_INCREF(py);
            return py;
        } else {
            // Return fy_generic wrapped
            fy_generic fg = container->collection_array[idx].fg;
            return FyGeneric_from_generic(container->gb, fg);
        }
    } else {
        // FROZEN: Iterate fy_generic
        // ... existing frozen iteration ...
    }
}
```

## Freeze Operation

**Convert back to pure fy_generic**:

```python
data = data.freeze()
```

Implementation:
```c
static PyObject *
FyGeneric_freeze(FyGenericObject *self)
{
    if (self->collection_array == NULL)
        return self;  // Already frozen

    // Build new fy_generic from materialized array
    fy_generic new_fg;

    if (self->is_mapping) {
        // Build mapping from pairs
        size_t num_pairs = self->array_size / 2;
        fy_generic *pairs = alloca(sizeof(fy_generic) * self->array_size);

        for (size_t i = 0; i < num_pairs; i++) {
            size_t key_idx = i * 2;
            size_t val_idx = i * 2 + 1;

            // Convert key
            if (IS_PYOBJECT(self->bitmap, key_idx)) {
                pairs[key_idx] = python_to_generic(self->gb,
                    self->collection_array[key_idx].py);
            } else {
                pairs[key_idx] = self->collection_array[key_idx].fg;
            }

            // Convert value (recursively freeze if FyGeneric)
            if (IS_PYOBJECT(self->bitmap, val_idx)) {
                PyObject *val_py = self->collection_array[val_idx].py;
                if (FyGeneric_Check(val_py)) {
                    PyObject *frozen = FyGeneric_freeze((FyGenericObject*)val_py);
                    pairs[val_idx] = ((FyGenericObject*)frozen)->fg;
                } else {
                    pairs[val_idx] = python_to_generic(self->gb, val_py);
                }
            } else {
                pairs[val_idx] = self->collection_array[val_idx].fg;
            }
        }

        new_fg = fy_gb_mapping_create(self->gb, num_pairs, pairs);
    } else {
        // Build sequence from items
        fy_generic *items = alloca(sizeof(fy_generic) * self->array_size);

        for (size_t i = 0; i < self->array_size; i++) {
            if (IS_PYOBJECT(self->bitmap, i)) {
                PyObject *item_py = self->collection_array[i].py;
                if (FyGeneric_Check(item_py)) {
                    PyObject *frozen = FyGeneric_freeze((FyGenericObject*)item_py);
                    items[i] = ((FyGenericObject*)frozen)->fg;
                } else {
                    items[i] = python_to_generic(self->gb, item_py);
                }
            } else {
                items[i] = self->collection_array[i].fg;
            }
        }

        new_fg = fy_gb_sequence_create(self->gb, self->array_size, items);
    }

    // Release thawed state
    // (Need to DECREF all PyObject* entries first)
    for (size_t i = 0; i < self->array_size; i++) {
        if (IS_PYOBJECT(self->bitmap, i)) {
            Py_DECREF(self->collection_array[i].py);
        }
    }
    PyMem_Free(self->collection_array);
    PyMem_Free(self->bitmap);

    // Create new frozen FyGeneric
    return FyGeneric_from_generic(self->gb, new_fg);
}
```

## Memory Analysis

**Frozen state**:
- FyGenericObject: ~48 bytes (PyObject header + fields)
- fy_generic value: 8 bytes
- Total: ~56 bytes per object

**Thawed scalar**:
- FyGenericObject: ~48 bytes
- fy_generic value: 8 bytes
- PyObject* cache: 8 bytes
- PyObject itself: variable (e.g., PyLong ~32 bytes)
- Total: ~96 bytes

**Thawed collection** (N items):
- FyGenericObject: ~48 bytes
- Array: N * 8 bytes (ItemValue unions)
- Bitmap: ceil(N/64) * 8 bytes
- Total: ~48 + 8N + (N/64*8) bytes

**Example: 1000-item list**:
- Frozen: 56 bytes
- Thawed: 48 + 8000 + 128 = 8176 bytes (~146x more)
- But only thawed items use extra memory (PyObject*)

## Advantages of This Design

1. **Memory efficient**: Only materializes what's mutated
2. **No wrapper overhead**: Union-based, same size as pointer
3. **Bitmap is compact**: 1 bit per item (1KB for 8192 items)
4. **Natural propagation**: Accessor chain materializes path
5. **Can freeze back**: Restore full efficiency after mutations

## Implementation Checklist

- [ ] Add fields to FyGenericObject struct
- [ ] Implement bitmap macros
- [ ] Implement `FyGeneric_materialize_collection()`
- [ ] Update `__getitem__` to check state
- [ ] Update `__setitem__` to materialize and use bitmap
- [ ] Update `__iter__` to check bitmap
- [ ] Implement `freeze()` method
- [ ] Update all accessors (keys, values, items, len, contains)
- [ ] Add tests for all three states
- [ ] Benchmark memory usage

---

**Status**: Design specification for v0.3.0 implementation
**Author**: Design discussion with Claude Code
**Date**: 2025-12-29
