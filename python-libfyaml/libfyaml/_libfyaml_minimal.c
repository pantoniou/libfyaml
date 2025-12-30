/**
 * _libfyaml_minimal.c - Minimal prototype of Python bindings for libfyaml generics
 *
 * Demonstrates NumPy-like lazy conversion with the actual generic API.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <math.h>

/* Include libfyaml headers */
#include <libfyaml.h>

/* Include exported generic API */
#include <libfyaml/fy-internal-generic.h>

/* ========== FyGeneric Type ========== */

typedef struct {
    PyObject_HEAD
    fy_generic fyg;
    struct fy_generic_builder *gb;  /* Non-NULL only for root (owner) */
    PyObject *root;  /* Reference to root object (NULL if this is root) */
    PyObject *path;  /* List of path elements from root (NULL if root or not mutable) */
    int mutable;     /* Whether this object supports mutation (only root sets this) */
} FyGenericObject;

static PyTypeObject FyGenericType;
static PyTypeObject FyGenericIteratorType;

/* Forward declarations */
static PyObject *FyGeneric_from_parent(fy_generic fyg, FyGenericObject *parent, PyObject *path_elem);

/* ========== FyGenericIterator Type ========== */

typedef struct {
    PyObject_HEAD
    FyGenericObject *generic_obj;  /* The FyGeneric being iterated */
    size_t index;                   /* Current position */
    size_t count;                   /* Total items */
    enum fy_generic_type iter_type; /* SEQUENCE or MAPPING */
    const fy_generic_map_pair *pairs; /* For mapping iteration */
} FyGenericIteratorObject;

/* FyGenericIterator: Deallocation */
static void
FyGenericIterator_dealloc(FyGenericIteratorObject *self)
{
    Py_XDECREF(self->generic_obj);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* FyGenericIterator: __iter__ (returns self) */
static PyObject *
FyGenericIterator_iter(PyObject *self)
{
    Py_INCREF(self);
    return self;
}

/* FyGenericIterator: __next__ */
static PyObject *
FyGenericIterator_next(FyGenericIteratorObject *self)
{
    if (self->index >= self->count) {
        PyErr_SetNone(PyExc_StopIteration);
        return NULL;
    }

    PyObject *result = NULL;

    if (self->iter_type == FYGT_SEQUENCE) {
        /* Iterate over sequence items */
        fy_generic item = fy_get(self->generic_obj->fyg, (int)self->index, fy_invalid);
        if (!fy_generic_is_valid(item)) {
            PyErr_SetString(PyExc_RuntimeError, "Invalid sequence item");
            return NULL;
        }
        PyObject *index_obj = PyLong_FromSize_t(self->index);
        if (index_obj == NULL)
            return NULL;
        result = FyGeneric_from_parent(item, self->generic_obj, index_obj);
        Py_DECREF(index_obj);

    } else if (self->iter_type == FYGT_MAPPING) {
        /* Iterate over mapping keys */
        fy_generic key = self->pairs[self->index].key;
        /* Convert key to Python object for path */
        PyObject *key_obj = NULL;
        if (fy_generic_is_string(key)) {
            const char *key_str = fy_cast(key, "");
            key_obj = PyUnicode_FromString(key_str);
        } else if (fy_generic_is_int(key)) {
            long long key_int = fy_cast(key, (long long)0);
            key_obj = PyLong_FromLongLong(key_int);
        } else {
            key_obj = PyUnicode_FromString("<key>");  /* Fallback */
        }
        if (key_obj == NULL)
            return NULL;
        result = FyGeneric_from_parent(key, self->generic_obj, key_obj);
        Py_DECREF(key_obj);
    }

    self->index++;
    return result;
}

/* FyGenericIterator type object */
static PyTypeObject FyGenericIteratorType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "libfyaml.FyGenericIterator",
    .tp_doc = "Iterator for FyGeneric sequences and mappings",
    .tp_basicsize = sizeof(FyGenericIteratorObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_dealloc = (destructor)FyGenericIterator_dealloc,
    .tp_iter = FyGenericIterator_iter,
    .tp_iternext = (iternextfunc)FyGenericIterator_next,
};

/* ========== FyGeneric Type ========== */

/* FyGeneric: Deallocation */
static void
FyGeneric_dealloc(FyGenericObject *self)
{
    if (self->gb != NULL) {
        /* This is the root - destroy the builder */
        fy_generic_builder_destroy(self->gb);
    }

    if (self->root != NULL) {
        /* This is a child - release reference to root */
        Py_DECREF(self->root);
    }

    if (self->path != NULL) {
        /* Release path list */
        Py_DECREF(self->path);
    }

    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* FyGeneric: __repr__ */
static PyObject *
FyGeneric_repr(FyGenericObject *self)
{
    enum fy_generic_type type = fy_get_type(self->fyg);
    const char *type_name;

    switch (type) {
        case FYGT_NULL: type_name = "null"; break;
        case FYGT_BOOL: type_name = "bool"; break;
        case FYGT_INT: type_name = "int"; break;
        case FYGT_FLOAT: type_name = "float"; break;
        case FYGT_STRING: type_name = "string"; break;
        case FYGT_SEQUENCE: type_name = "sequence"; break;
        case FYGT_MAPPING: type_name = "mapping"; break;
        default: type_name = "unknown"; break;
    }

    return PyUnicode_FromFormat("<FyGeneric:%s>", type_name);
}

/* FyGeneric: __str__ - convert to string */
static PyObject *
FyGeneric_str(FyGenericObject *self)
{
    enum fy_generic_type type = fy_get_type(self->fyg);

    switch (type) {
        case FYGT_STRING: {
            const char *str = fy_cast(self->fyg, "");
            return PyUnicode_FromString(str);
        }

        case FYGT_INT: {
            long long value = fy_cast(self->fyg, (long long)0);
            return PyUnicode_FromFormat("%lld", value);
        }

        case FYGT_FLOAT: {
            double value = fy_cast(self->fyg, (double)0.0);
            PyObject *float_obj = PyFloat_FromDouble(value);
            if (float_obj == NULL)
                return NULL;
            PyObject *str_obj = PyObject_Str(float_obj);
            Py_DECREF(float_obj);
            return str_obj;
        }

        case FYGT_BOOL: {
            _Bool value = fy_cast(self->fyg, (_Bool)0);
            return PyUnicode_FromString(value ? "true" : "false");
        }

        case FYGT_NULL:
            return PyUnicode_FromString("null");

        default:
            return FyGeneric_repr(self);
    }
}

/* FyGeneric: __int__ */
static PyObject *
FyGeneric_int(FyGenericObject *self)
{
    long long value = fy_cast(self->fyg, (long long)0);
    return PyLong_FromLongLong(value);
}

/* FyGeneric: __float__ */
static PyObject *
FyGeneric_float(FyGenericObject *self)
{
    double value = fy_cast(self->fyg, (double)0.0);
    return PyFloat_FromDouble(value);
}

/* FyGeneric: __bool__ */
static int
FyGeneric_bool(FyGenericObject *self)
{
    enum fy_generic_type type = fy_get_type(self->fyg);

    switch (type) {
        case FYGT_NULL:
            return 0;
        case FYGT_BOOL:
            return fy_cast(self->fyg, (_Bool)0) ? 1 : 0;
        case FYGT_INT:
            return fy_cast(self->fyg, (long long)0) != 0 ? 1 : 0;
        case FYGT_FLOAT:
            return fy_cast(self->fyg, (double)0.0) != 0.0 ? 1 : 0;
        case FYGT_STRING: {
            const char *str = fy_cast(self->fyg, "");
            return (str && str[0]) ? 1 : 0;
        }
        case FYGT_SEQUENCE:
        case FYGT_MAPPING:
            return fy_len(self->fyg) > 0 ? 1 : 0;
        default:
            return 1;
    }
}

/* FyGeneric: __len__ */
static Py_ssize_t
FyGeneric_length(FyGenericObject *self)
{
    enum fy_generic_type type = fy_get_type(self->fyg);

    if (type == FYGT_SEQUENCE || type == FYGT_MAPPING) {
        return (Py_ssize_t)fy_len(self->fyg);
    }

    PyErr_SetString(PyExc_TypeError, "Object has no len()");
    return -1;
}

/* Helper: Create FyGeneric wrapper from parent */
static PyObject *
FyGeneric_from_parent(fy_generic fyg, FyGenericObject *parent, PyObject *path_elem)
{
    FyGenericObject *self;

    self = (FyGenericObject *)FyGenericType.tp_alloc(&FyGenericType, 0);
    if (self == NULL)
        return NULL;

    self->fyg = fyg;
    self->gb = NULL;  /* Children don't own the builder */

    /* Reference the root to keep builder alive */
    self->root = parent->root ? parent->root : (PyObject *)parent;
    Py_INCREF(self->root);

    /* Get mutable flag from root */
    FyGenericObject *root_obj = (FyGenericObject *)self->root;
    self->mutable = root_obj->mutable;

    /* Only build path if root is mutable */
    if (self->mutable) {
        /* Build path from parent's path + this element */
        if (parent->path == NULL) {
            /* Parent is root - create new path with just this element */
            self->path = PyList_New(1);
            if (self->path == NULL) {
                Py_DECREF(self->root);
                Py_TYPE(self)->tp_free((PyObject *)self);
                return NULL;
            }
            Py_INCREF(path_elem);
            PyList_SET_ITEM(self->path, 0, path_elem);
        } else {
            /* Parent has path - copy and append */
            Py_ssize_t parent_len = PyList_Size(parent->path);
            self->path = PyList_New(parent_len + 1);
            if (self->path == NULL) {
                Py_DECREF(self->root);
                Py_TYPE(self)->tp_free((PyObject *)self);
                return NULL;
            }

            /* Copy parent's path */
            for (Py_ssize_t i = 0; i < parent_len; i++) {
                PyObject *item = PyList_GET_ITEM(parent->path, i);
                Py_INCREF(item);
                PyList_SET_ITEM(self->path, i, item);
            }

            /* Append new element */
            Py_INCREF(path_elem);
            PyList_SET_ITEM(self->path, parent_len, path_elem);
        }
    } else {
        /* Read-only mode - no path tracking */
        self->path = NULL;
    }

    return (PyObject *)self;
}

/* Helper: Create root FyGeneric wrapper (owns builder) */
static PyObject *
FyGeneric_from_generic(fy_generic fyg, struct fy_generic_builder *gb, int mutable)
{
    FyGenericObject *self;

    self = (FyGenericObject *)FyGenericType.tp_alloc(&FyGenericType, 0);
    if (self == NULL)
        return NULL;

    self->fyg = fyg;
    self->gb = gb;       /* Root owns the builder */
    self->root = NULL;   /* This is the root */
    self->path = NULL;   /* Root has no path */
    self->mutable = mutable;  /* Set mutability */

    return (PyObject *)self;
}

/* FyGeneric: __getitem__ for sequences and mappings */
static PyObject *
FyGeneric_subscript(FyGenericObject *self, PyObject *key)
{
    enum fy_generic_type type = fy_get_type(self->fyg);

    if (type == FYGT_SEQUENCE) {
        /* Sequence indexing */
        if (!PyLong_Check(key)) {
            PyErr_SetString(PyExc_TypeError, "Sequence indices must be integers");
            return NULL;
        }

        Py_ssize_t index = PyLong_AsSsize_t(key);
        if (index == -1 && PyErr_Occurred())
            return NULL;

        size_t count = fy_len(self->fyg);

        /* Handle negative indices */
        if (index < 0)
            index += count;

        if (index < 0 || (size_t)index >= count) {
            PyErr_SetString(PyExc_IndexError, "Sequence index out of range");
            return NULL;
        }

        /* Use fy_get() to get item by index */
        fy_generic item = fy_get(self->fyg, (int)index, fy_invalid);
        if (!fy_generic_is_valid(item)) {
            PyErr_SetString(PyExc_IndexError, "Invalid item at index");
            return NULL;
        }

        return FyGeneric_from_parent(item, self, key);

    } else if (type == FYGT_MAPPING) {
        /* Mapping key lookup */
        PyObject *key_str = PyObject_Str(key);
        if (key_str == NULL)
            return NULL;

        const char *key_cstr = PyUnicode_AsUTF8(key_str);
        if (key_cstr == NULL) {
            Py_DECREF(key_str);
            return NULL;
        }

        /* Use fy_get() with string key */
        fy_generic value = fy_get(self->fyg, key_cstr, fy_invalid);
        Py_DECREF(key_str);

        if (!fy_generic_is_valid(value)) {
            PyErr_SetObject(PyExc_KeyError, key);
            return NULL;
        }

        return FyGeneric_from_parent(value, self, key);
    }

    PyErr_SetString(PyExc_TypeError, "Object is not subscriptable");
    return NULL;
}

/* FyGeneric: to_python() - recursive conversion */
static PyObject *
FyGeneric_to_python(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    enum fy_generic_type type = fy_get_type(self->fyg);

    switch (type) {
        case FYGT_NULL:
            Py_RETURN_NONE;

        case FYGT_BOOL: {
            _Bool value = fy_cast(self->fyg, (_Bool)0);
            if (value)
                Py_RETURN_TRUE;
            else
                Py_RETURN_FALSE;
        }

        case FYGT_INT:
            return PyLong_FromLongLong(fy_cast(self->fyg, (long long)0));

        case FYGT_FLOAT:
            return PyFloat_FromDouble(fy_cast(self->fyg, (double)0.0));

        case FYGT_STRING:
            return PyUnicode_FromString(fy_cast(self->fyg, ""));

        case FYGT_SEQUENCE: {
            size_t count = fy_len(self->fyg);
            PyObject *list = PyList_New(count);
            if (list == NULL)
                return NULL;

            for (size_t i = 0; i < count; i++) {
                fy_generic item = fy_get(self->fyg, (int)i, fy_invalid);
                if (!fy_generic_is_valid(item)) {
                    Py_DECREF(list);
                    PyErr_SetString(PyExc_RuntimeError, "Invalid sequence item");
                    return NULL;
                }

                PyObject *index_obj = PyLong_FromSize_t(i);
                if (index_obj == NULL) {
                    Py_DECREF(list);
                    return NULL;
                }

                PyObject *item_obj = FyGeneric_from_parent(item, self, index_obj);
                Py_DECREF(index_obj);
                if (item_obj == NULL) {
                    Py_DECREF(list);
                    return NULL;
                }

                PyObject *converted = FyGeneric_to_python((FyGenericObject *)item_obj, NULL);
                Py_DECREF(item_obj);
                if (converted == NULL) {
                    Py_DECREF(list);
                    return NULL;
                }

                PyList_SET_ITEM(list, i, converted);
            }
            return list;
        }

        case FYGT_MAPPING: {
            PyObject *dict = PyDict_New();
            if (dict == NULL)
                return NULL;

            size_t count;
            const fy_generic_map_pair *pairs = fy_generic_mapping_get_pairs(self->fyg, &count);

            for (size_t i = 0; i < count; i++) {
                /* First get the key as a Python object for path */
                PyObject *path_key = NULL;
                if (fy_generic_is_string(pairs[i].key)) {
                    const char *key_str = fy_cast(pairs[i].key, "");
                    path_key = PyUnicode_FromString(key_str);
                } else if (fy_generic_is_int(pairs[i].key)) {
                    long long key_int = fy_cast(pairs[i].key, (long long)0);
                    path_key = PyLong_FromLongLong(key_int);
                } else {
                    path_key = PyUnicode_FromString("<key>");
                }
                if (path_key == NULL) {
                    Py_DECREF(dict);
                    return NULL;
                }

                /* Convert key */
                PyObject *key_obj = FyGeneric_from_parent(pairs[i].key, self, path_key);
                if (key_obj == NULL) {
                    Py_DECREF(path_key);
                    Py_DECREF(dict);
                    return NULL;
                }
                PyObject *conv_key = FyGeneric_to_python((FyGenericObject *)key_obj, NULL);
                Py_DECREF(key_obj);
                if (conv_key == NULL) {
                    Py_DECREF(path_key);
                    Py_DECREF(dict);
                    return NULL;
                }

                /* Convert value */
                PyObject *val_obj = FyGeneric_from_parent(pairs[i].value, self, path_key);
                Py_DECREF(path_key);  /* Done with path_key */
                if (val_obj == NULL) {
                    Py_DECREF(conv_key);
                    Py_DECREF(dict);
                    return NULL;
                }
                PyObject *conv_val = FyGeneric_to_python((FyGenericObject *)val_obj, NULL);
                Py_DECREF(val_obj);
                if (conv_val == NULL) {
                    Py_DECREF(conv_key);
                    Py_DECREF(dict);
                    return NULL;
                }

                /* Add to dict */
                if (PyDict_SetItem(dict, conv_key, conv_val) < 0) {
                    Py_DECREF(conv_key);
                    Py_DECREF(conv_val);
                    Py_DECREF(dict);
                    return NULL;
                }

                Py_DECREF(conv_key);
                Py_DECREF(conv_val);
            }

            return dict;
        }

        default:
            PyErr_Format(PyExc_TypeError, "Unknown generic type: %d", type);
            return NULL;
    }
}

/* FyGeneric: Type check methods */
static PyObject *
FyGeneric_is_null(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    return PyBool_FromLong(fy_generic_is_null_type(self->fyg));
}

static PyObject *
FyGeneric_is_bool(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    return PyBool_FromLong(fy_generic_is_bool_type(self->fyg));
}

static PyObject *
FyGeneric_is_int(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    return PyBool_FromLong(fy_generic_is_int_type(self->fyg));
}

static PyObject *
FyGeneric_is_float(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    return PyBool_FromLong(fy_generic_is_float_type(self->fyg));
}

static PyObject *
FyGeneric_is_string(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    return PyBool_FromLong(fy_generic_is_string(self->fyg));
}

static PyObject *
FyGeneric_is_sequence(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    return PyBool_FromLong(fy_generic_is_sequence(self->fyg));
}

static PyObject *
FyGeneric_is_mapping(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    return PyBool_FromLong(fy_generic_is_mapping(self->fyg));
}

/* FyGeneric: __iter__ */
static PyObject *
FyGeneric_iter(FyGenericObject *self)
{
    enum fy_generic_type type = fy_get_type(self->fyg);

    if (type != FYGT_SEQUENCE && type != FYGT_MAPPING) {
        PyErr_SetString(PyExc_TypeError, "FyGeneric is not iterable");
        return NULL;
    }

    FyGenericIteratorObject *iter = PyObject_New(FyGenericIteratorObject, &FyGenericIteratorType);
    if (iter == NULL)
        return NULL;

    Py_INCREF(self);
    iter->generic_obj = self;
    iter->index = 0;
    iter->count = fy_len(self->fyg);
    iter->iter_type = type;

    if (type == FYGT_MAPPING) {
        /* Get pairs for mapping iteration */
        iter->pairs = fy_generic_mapping_get_pairs(self->fyg, &iter->count);
    } else {
        iter->pairs = NULL;
    }

    return (PyObject *)iter;
}

/* FyGeneric: __richcompare__ - implements ==, !=, <, <=, >, >= */
static PyObject *
FyGeneric_richcompare(PyObject *self, PyObject *other, int op)
{
    FyGenericObject *self_obj = (FyGenericObject *)self;
    enum fy_generic_type self_type = fy_get_type(self_obj->fyg);

    /* Helper macro for comparisons */
    #define DO_COMPARE(py_op, c_op) \
        case py_op: \
            switch (self_type) { \
                case FYGT_INT: { \
                    long long self_val = fy_cast(self_obj->fyg, (long long)0); \
                    long long other_val; \
                    if (PyLong_Check(other)) { \
                        other_val = PyLong_AsLongLong(other); \
                    } else if (Py_TYPE(other) == &FyGenericType) { \
                        other_val = fy_cast(((FyGenericObject *)other)->fyg, (long long)0); \
                    } else { \
                        Py_RETURN_NOTIMPLEMENTED; \
                    } \
                    if (self_val c_op other_val) Py_RETURN_TRUE; \
                    else Py_RETURN_FALSE; \
                } \
                case FYGT_FLOAT: { \
                    double self_val = fy_cast(self_obj->fyg, (double)0.0); \
                    double other_val; \
                    if (PyFloat_Check(other)) { \
                        other_val = PyFloat_AsDouble(other); \
                    } else if (PyLong_Check(other)) { \
                        other_val = (double)PyLong_AsLongLong(other); \
                    } else if (Py_TYPE(other) == &FyGenericType) { \
                        other_val = fy_cast(((FyGenericObject *)other)->fyg, (double)0.0); \
                    } else { \
                        Py_RETURN_NOTIMPLEMENTED; \
                    } \
                    if (self_val c_op other_val) Py_RETURN_TRUE; \
                    else Py_RETURN_FALSE; \
                } \
                case FYGT_STRING: { \
                    const char *self_val = fy_cast(self_obj->fyg, ""); \
                    const char *other_val = NULL; \
                    if (PyUnicode_Check(other)) { \
                        other_val = PyUnicode_AsUTF8(other); \
                    } else if (Py_TYPE(other) == &FyGenericType) { \
                        other_val = fy_cast(((FyGenericObject *)other)->fyg, ""); \
                    } else { \
                        Py_RETURN_NOTIMPLEMENTED; \
                    } \
                    int cmp = strcmp(self_val, other_val); \
                    if (cmp c_op 0) Py_RETURN_TRUE; \
                    else Py_RETURN_FALSE; \
                } \
                case FYGT_BOOL: { \
                    _Bool self_val = fy_cast(self_obj->fyg, (_Bool)0); \
                    _Bool other_val; \
                    if (PyBool_Check(other)) { \
                        other_val = (other == Py_True); \
                    } else if (Py_TYPE(other) == &FyGenericType) { \
                        other_val = fy_cast(((FyGenericObject *)other)->fyg, (_Bool)0); \
                    } else { \
                        Py_RETURN_NOTIMPLEMENTED; \
                    } \
                    if (self_val c_op other_val) Py_RETURN_TRUE; \
                    else Py_RETURN_FALSE; \
                } \
                default: \
                    Py_RETURN_NOTIMPLEMENTED; \
            }

    switch (op) {
        DO_COMPARE(Py_EQ, ==)
        DO_COMPARE(Py_NE, !=)
        DO_COMPARE(Py_LT, <)
        DO_COMPARE(Py_LE, <=)
        DO_COMPARE(Py_GT, >)
        DO_COMPARE(Py_GE, >=)
        default:
            Py_RETURN_NOTIMPLEMENTED;
    }

    #undef DO_COMPARE
}

/* Mapping-specific methods */

/* Helper: Convert fy_generic key to Python object for path tracking */
static PyObject *
key_to_python_obj(fy_generic key)
{
    if (fy_generic_is_string(key)) {
        const char *key_str = fy_cast(key, "");
        return PyUnicode_FromString(key_str);
    } else if (fy_generic_is_int(key)) {
        long long key_int = fy_cast(key, (long long)0);
        return PyLong_FromLongLong(key_int);
    } else {
        return PyUnicode_FromString("<key>");
    }
}

/* FyGeneric: keys() - return list of keys for mappings */
static PyObject *
FyGeneric_keys(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    if (!fy_generic_is_mapping(self->fyg)) {
        PyErr_SetString(PyExc_TypeError, "keys() requires a mapping");
        return NULL;
    }

    size_t count;
    const fy_generic_map_pair *pairs = fy_generic_mapping_get_pairs(self->fyg, &count);

    PyObject *result = PyList_New(count);
    if (result == NULL)
        return NULL;

    for (size_t i = 0; i < count; i++) {
        PyObject *path_key = key_to_python_obj(pairs[i].key);
        if (path_key == NULL) {
            Py_DECREF(result);
            return NULL;
        }

        PyObject *key = FyGeneric_from_parent(pairs[i].key, self, path_key);
        Py_DECREF(path_key);
        if (key == NULL) {
            Py_DECREF(result);
            return NULL;
        }
        PyList_SET_ITEM(result, i, key);
    }

    return result;
}

/* FyGeneric: values() - return list of values for mappings */
static PyObject *
FyGeneric_values(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    if (!fy_generic_is_mapping(self->fyg)) {
        PyErr_SetString(PyExc_TypeError, "values() requires a mapping");
        return NULL;
    }

    size_t count;
    const fy_generic_map_pair *pairs = fy_generic_mapping_get_pairs(self->fyg, &count);

    PyObject *result = PyList_New(count);
    if (result == NULL)
        return NULL;

    for (size_t i = 0; i < count; i++) {
        PyObject *path_key = key_to_python_obj(pairs[i].key);
        if (path_key == NULL) {
            Py_DECREF(result);
            return NULL;
        }

        PyObject *value = FyGeneric_from_parent(pairs[i].value, self, path_key);
        Py_DECREF(path_key);
        if (value == NULL) {
            Py_DECREF(result);
            return NULL;
        }
        PyList_SET_ITEM(result, i, value);
    }

    return result;
}

/* FyGeneric: items() - return list of (key, value) tuples for mappings */
static PyObject *
FyGeneric_items(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    if (!fy_generic_is_mapping(self->fyg)) {
        PyErr_SetString(PyExc_TypeError, "items() requires a mapping");
        return NULL;
    }

    size_t count;
    const fy_generic_map_pair *pairs = fy_generic_mapping_get_pairs(self->fyg, &count);

    PyObject *result = PyList_New(count);
    if (result == NULL)
        return NULL;

    for (size_t i = 0; i < count; i++) {
        PyObject *path_key = key_to_python_obj(pairs[i].key);
        if (path_key == NULL) {
            Py_DECREF(result);
            return NULL;
        }

        PyObject *key = FyGeneric_from_parent(pairs[i].key, self, path_key);
        if (key == NULL) {
            Py_DECREF(path_key);
            Py_DECREF(result);
            return NULL;
        }

        PyObject *value = FyGeneric_from_parent(pairs[i].value, self, path_key);
        Py_DECREF(path_key);  /* Done with path_key */
        if (value == NULL) {
            Py_DECREF(key);
            Py_DECREF(result);
            return NULL;
        }

        PyObject *tuple = PyTuple_Pack(2, key, value);
        Py_DECREF(key);
        Py_DECREF(value);

        if (tuple == NULL) {
            Py_DECREF(result);
            return NULL;
        }

        PyList_SET_ITEM(result, i, tuple);
    }

    return result;
}

/* FyGeneric: __format__ */
static PyObject *
FyGeneric_format(FyGenericObject *self, PyObject *format_spec)
{
    /* Convert to appropriate Python type and delegate formatting */
    PyObject *py_obj = NULL;
    enum fy_generic_type type = fy_get_type(self->fyg);

    switch (type) {
        case FYGT_NULL:
            py_obj = Py_None;
            Py_INCREF(py_obj);
            break;
        case FYGT_BOOL:
            py_obj = fy_cast(self->fyg, (_Bool)0) ? Py_True : Py_False;
            Py_INCREF(py_obj);
            break;
        case FYGT_INT:
            py_obj = PyLong_FromLongLong(fy_cast(self->fyg, (long long)0));
            break;
        case FYGT_FLOAT:
            py_obj = PyFloat_FromDouble(fy_cast(self->fyg, (double)0.0));
            break;
        case FYGT_STRING: {
            const char *str = fy_cast(self->fyg, "");
            py_obj = PyUnicode_FromString(str);
            break;
        }
        default:
            /* For complex types, use to_python() */
            py_obj = FyGeneric_to_python(self, NULL);
            break;
    }

    if (py_obj == NULL)
        return NULL;

    /* Delegate to Python's __format__ */
    PyObject *result = PyObject_Format(py_obj, format_spec);
    Py_DECREF(py_obj);

    return result;
}

/* FyGeneric: __getattribute__ - delegate type-specific methods */
static PyObject *
FyGeneric_getattro(FyGenericObject *self, PyObject *name)
{
    /* First try the normal attribute lookup (for our own methods/attributes) */
    PyObject *attr = PyObject_GenericGetAttr((PyObject *)self, name);

    if (attr != NULL || !PyErr_ExceptionMatches(PyExc_AttributeError)) {
        /* Found it in our type, or got a different error */
        return attr;
    }

    /* Not found in our type - clear the AttributeError and try delegating */
    PyErr_Clear();

    /* Convert to underlying Python type and delegate */
    enum fy_generic_type type = fy_get_type(self->fyg);
    PyObject *py_obj = NULL;

    switch (type) {
        case FYGT_STRING: {
            /* Delegate to str methods */
            const char *str = fy_cast(self->fyg, "");
            py_obj = PyUnicode_FromString(str);
            break;
        }
        case FYGT_INT:
            /* Delegate to int methods */
            py_obj = PyLong_FromLongLong(fy_cast(self->fyg, (long long)0));
            break;
        case FYGT_FLOAT:
            /* Delegate to float methods */
            py_obj = PyFloat_FromDouble(fy_cast(self->fyg, (double)0.0));
            break;
        case FYGT_BOOL:
            /* Delegate to bool methods */
            py_obj = fy_cast(self->fyg, (_Bool)0) ? Py_True : Py_False;
            Py_INCREF(py_obj);
            break;
        case FYGT_SEQUENCE:
            /* Delegate to list methods - convert to Python list */
            py_obj = FyGeneric_to_python(self, NULL);
            break;
        case FYGT_MAPPING:
            /* Delegate to dict methods - convert to Python dict */
            py_obj = FyGeneric_to_python(self, NULL);
            break;
        default: {
            /* No delegation possible for this type */
            const char *name_str = PyUnicode_AsUTF8(name);
            if (name_str) {
                PyErr_Format(PyExc_AttributeError,
                    "'FyGeneric' object (type %d) has no attribute '%.400s'",
                    type, name_str);
            } else {
                PyErr_SetString(PyExc_AttributeError,
                    "'FyGeneric' object has no such attribute");
            }
            return NULL;
        }
    }

    if (py_obj == NULL)
        return NULL;

    /* Get the attribute from the converted Python object */
    attr = PyObject_GetAttr(py_obj, name);
    Py_DECREF(py_obj);

    return attr;
}

/* Forward declaration - needed for set_at_path() */
static fy_generic python_to_generic(struct fy_generic_builder *gb, PyObject *obj);

/* FyGeneric: trim() - trim allocator to release unused memory */
static PyObject *
FyGeneric_trim(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    /* Only root objects own the generic builder */
    if (self->gb) {
        fy_gb_trim(self->gb);
    }
    Py_RETURN_NONE;
}

/* FyGeneric: clone() - Create a clone of this FyGeneric object */
static PyObject *
FyGeneric_clone(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    /* Get the root object (source of truth for mutable flag and generic value) */
    FyGenericObject *root_obj = self->root ? (FyGenericObject *)self->root : self;

    /* Create a new builder for the clone */
    struct fy_generic_builder *new_gb = fy_generic_builder_create(NULL);
    if (new_gb == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create builder for clone");
        return NULL;
    }

    /* Internalize (copy) the generic value into the new builder */
    fy_generic cloned = fy_gb_internalize(new_gb, root_obj->fyg);
    if (fy_generic_is_invalid(cloned)) {
        fy_generic_builder_destroy(new_gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to clone generic value");
        return NULL;
    }

    /* Create a new FyGeneric Python object with the cloned value */
    PyObject *result = FyGeneric_from_generic(cloned, new_gb, root_obj->mutable);
    if (result == NULL) {
        fy_generic_builder_destroy(new_gb);
        return NULL;
    }

    return result;
}

/* FyGeneric: get_path() - Get the path from root to this object */
static PyObject *
FyGeneric_get_path(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    /* If this is root, return empty list */
    if (self->root == NULL) {
        return PyList_New(0);
    }

    /* If path tracking is disabled (mutable=False), return None */
    if (self->path == NULL) {
        Py_RETURN_NONE;
    }

    /* Return a copy of the path list */
    Py_INCREF(self->path);
    return self->path;
}

/* FyGeneric: get_at_path(path) - Get value at path (root only) */
static PyObject *
FyGeneric_get_at_path(FyGenericObject *self, PyObject *path_obj)
{
    /* This method only works on root objects */
    if (self->root != NULL) {
        PyErr_SetString(PyExc_TypeError,
                      "get_at_path() can only be called on root FyGeneric objects");
        return NULL;
    }

    if (!self->gb) {
        PyErr_SetString(PyExc_RuntimeError, "No builder available");
        return NULL;
    }

    /* Convert Python path to fy_generic array */
    if (!PyList_Check(path_obj) && !PyTuple_Check(path_obj)) {
        PyErr_SetString(PyExc_TypeError, "Path must be a list or tuple");
        return NULL;
    }

    Py_ssize_t path_len = PySequence_Size(path_obj);
    if (path_len < 0)
        return NULL;

    fy_generic *path_array = alloca(sizeof(fy_generic) * path_len);

    /* Convert each path element */
    for (Py_ssize_t i = 0; i < path_len; i++) {
        PyObject *elem = PySequence_GetItem(path_obj, i);
        if (elem == NULL)
            return NULL;

        if (PyLong_Check(elem)) {
            long idx = PyLong_AsLong(elem);
            Py_DECREF(elem);
            if (idx == -1 && PyErr_Occurred())
                return NULL;
            path_array[i] = fy_value((int)idx);
        } else if (PyUnicode_Check(elem)) {
            const char *str = PyUnicode_AsUTF8(elem);
            Py_DECREF(elem);
            if (str == NULL)
                return NULL;
            path_array[i] = fy_value(str);
        } else {
            Py_DECREF(elem);
            PyErr_SetString(PyExc_TypeError, "Path elements must be integers or strings");
            return NULL;
        }
    }

    /* Call GET_AT_PATH operation */
    fy_generic result = fy_generic_op(self->gb, FYGBOPF_GET_AT_PATH, self->fyg,
                                     path_len, path_array);

    if (fy_generic_is_invalid(result)) {
        PyErr_SetString(PyExc_KeyError, "Path not found");
        return NULL;
    }

    /* Convert result to Python - create a child FyGeneric */
    FyGenericObject *child;

    child = (FyGenericObject *)PyObject_New(FyGenericObject, &FyGenericType);
    if (child == NULL)
        return NULL;

    child->fyg = result;
    child->gb = NULL;
    child->root = (PyObject *)self;
    Py_INCREF(child->root);
    child->mutable = self->mutable;

    /* Build the path as a Python list for the child */
    if (self->mutable && path_len > 0) {
        child->path = PyList_New(path_len);
        if (child->path == NULL) {
            Py_DECREF(child);
            return NULL;
        }

        for (Py_ssize_t i = 0; i < path_len; i++) {
            PyObject *elem = PySequence_GetItem(path_obj, i);
            if (elem == NULL) {
                Py_DECREF(child);
                return NULL;
            }
            PyList_SET_ITEM(child->path, i, elem);
        }
    } else {
        child->path = NULL;
    }

    return (PyObject *)child;
}

/* FyGeneric: set_at_path(path, value) - Set value at path (root only) */
static PyObject *
FyGeneric_set_at_path(FyGenericObject *self, PyObject *args)
{
    PyObject *path_obj, *value_obj;

    if (!PyArg_ParseTuple(args, "OO", &path_obj, &value_obj))
        return NULL;

    /* This method only works on root objects */
    if (self->root != NULL) {
        PyErr_SetString(PyExc_TypeError,
                      "set_at_path() can only be called on root FyGeneric objects");
        return NULL;
    }

    /* Check if mutation is allowed */
    if (!self->mutable) {
        PyErr_SetString(PyExc_TypeError,
                      "This FyGeneric object is read-only. Create with mutable=True to enable mutation.");
        return NULL;
    }

    if (!self->gb) {
        PyErr_SetString(PyExc_RuntimeError, "No builder available");
        return NULL;
    }

    /* Convert Python path to fy_generic array */
    if (!PyList_Check(path_obj) && !PyTuple_Check(path_obj)) {
        PyErr_SetString(PyExc_TypeError, "Path must be a list or tuple");
        return NULL;
    }

    Py_ssize_t path_len = PySequence_Size(path_obj);
    if (path_len < 0)
        return NULL;

    if (path_len == 0) {
        PyErr_SetString(PyExc_ValueError, "Path cannot be empty");
        return NULL;
    }

    /* Convert value to generic */
    fy_generic new_value = python_to_generic(self->gb, value_obj);
    if (fy_generic_is_invalid(new_value)) {
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_ValueError, "Failed to convert value to generic");
        return NULL;
    }

    /* Allocate path array: path elements + value */
    fy_generic *path_array = alloca(sizeof(fy_generic) * (path_len + 1));

    /* Convert each path element */
    for (Py_ssize_t i = 0; i < path_len; i++) {
        PyObject *elem = PySequence_GetItem(path_obj, i);
        if (elem == NULL)
            return NULL;

        if (PyLong_Check(elem)) {
            long idx = PyLong_AsLong(elem);
            Py_DECREF(elem);
            if (idx == -1 && PyErr_Occurred())
                return NULL;
            path_array[i] = fy_value((int)idx);
        } else if (PyUnicode_Check(elem)) {
            const char *str = PyUnicode_AsUTF8(elem);
            Py_DECREF(elem);
            if (str == NULL)
                return NULL;
            path_array[i] = fy_value(str);
        } else {
            Py_DECREF(elem);
            PyErr_SetString(PyExc_TypeError, "Path elements must be integers or strings");
            return NULL;
        }
    }

    /* Add value as last element */
    path_array[path_len] = new_value;

    /* Call SET_AT_PATH operation */
    fy_generic new_root = fy_generic_op(self->gb, FYGBOPF_SET_AT_PATH, self->fyg,
                                       path_len + 1, path_array);

    if (fy_generic_is_invalid(new_root)) {
        PyErr_SetString(PyExc_RuntimeError, "SET_AT_PATH operation failed");
        return NULL;
    }

    /* Update the root's fyg */
    self->fyg = new_root;

    Py_RETURN_NONE;
}

/* FyGeneric method table */
static PyMethodDef FyGeneric_methods[] = {
    {"to_python", (PyCFunction)FyGeneric_to_python, METH_NOARGS,
     "Convert to Python object (recursive)"},
    {"trim", (PyCFunction)FyGeneric_trim, METH_NOARGS,
     "Trim allocator to release unused memory"},
    {"clone", (PyCFunction)FyGeneric_clone, METH_NOARGS,
     "Create a clone of this FyGeneric object"},
    {"get_path", (PyCFunction)FyGeneric_get_path, METH_NOARGS,
     "Get the path from root to this object"},
    {"get_at_path", (PyCFunction)FyGeneric_get_at_path, METH_O,
     "Get value at path (root only)"},
    {"set_at_path", (PyCFunction)FyGeneric_set_at_path, METH_VARARGS,
     "Set value at path (root only)"},
    {"__format__", (PyCFunction)FyGeneric_format, METH_O,
     "Format the value according to format specification"},
    {"is_null", (PyCFunction)FyGeneric_is_null, METH_NOARGS,
     "Check if value is null"},
    {"is_bool", (PyCFunction)FyGeneric_is_bool, METH_NOARGS,
     "Check if value is boolean"},
    {"is_int", (PyCFunction)FyGeneric_is_int, METH_NOARGS,
     "Check if value is integer"},
    {"is_float", (PyCFunction)FyGeneric_is_float, METH_NOARGS,
     "Check if value is float"},
    {"is_string", (PyCFunction)FyGeneric_is_string, METH_NOARGS,
     "Check if value is string"},
    {"is_sequence", (PyCFunction)FyGeneric_is_sequence, METH_NOARGS,
     "Check if value is sequence"},
    {"is_mapping", (PyCFunction)FyGeneric_is_mapping, METH_NOARGS,
     "Check if value is mapping"},
    {"keys", (PyCFunction)FyGeneric_keys, METH_NOARGS,
     "Return list of keys (for mappings)"},
    {"values", (PyCFunction)FyGeneric_values, METH_NOARGS,
     "Return list of values (for mappings)"},
    {"items", (PyCFunction)FyGeneric_items, METH_NOARGS,
     "Return list of (key, value) tuples (for mappings)"},
    {NULL}
};

/* FyGeneric as sequence */
static PySequenceMethods FyGeneric_as_sequence = {
    .sq_length = (lenfunc)FyGeneric_length,
};

/* FyGeneric: __setitem__ for sequences and mappings */
static int
FyGeneric_ass_subscript(FyGenericObject *self, PyObject *key, PyObject *value)
{
    enum fy_generic_type type = fy_get_type(self->fyg);

    /* Get the root object that owns the builder */
    FyGenericObject *root_obj = self->root ? (FyGenericObject *)self->root : self;

    /* Check if mutation is allowed */
    if (!root_obj->mutable) {
        PyErr_SetString(PyExc_TypeError,
                      "This FyGeneric object is read-only. Create with mutable=True to enable mutation.");
        return -1;
    }

    if (!root_obj->gb) {
        PyErr_SetString(PyExc_RuntimeError, "No builder available for mutation");
        return -1;
    }

    if (value == NULL) {
        PyErr_SetString(PyExc_NotImplementedError, "Deletion not yet supported");
        return -1;
    }

    /* Convert Python value to generic */
    fy_generic new_value = python_to_generic(root_obj->gb, value);
    if (fy_generic_is_invalid(new_value)) {
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_ValueError, "Failed to convert value to generic");
        return -1;
    }

    /* Build full path: self->path + [key] + [value] */
    Py_ssize_t existing_path_len = self->path ? PyList_Size(self->path) : 0;
    Py_ssize_t total_path_len = existing_path_len + 1;  /* +1 for new key */
    Py_ssize_t total_len = total_path_len + 1;  /* +1 for value */

    /* Allocate path array on stack */
    fy_generic *path_array = alloca(sizeof(fy_generic) * total_len);

    /* Copy existing path elements */
    for (Py_ssize_t i = 0; i < existing_path_len; i++) {
        PyObject *elem = PyList_GET_ITEM(self->path, i);
        if (PyLong_Check(elem)) {
            long idx = PyLong_AsLong(elem);
            path_array[i] = fy_value((int)idx);
        } else if (PyUnicode_Check(elem)) {
            const char *str = PyUnicode_AsUTF8(elem);
            if (str == NULL)
                return -1;
            path_array[i] = fy_value(str);
        } else {
            PyErr_SetString(PyExc_TypeError, "Invalid path element type");
            return -1;
        }
    }

    /* Add current key to path */
    fy_generic new_root;

    if (type == FYGT_SEQUENCE) {
        /* Sequence indexing */
        if (!PyLong_Check(key)) {
            PyErr_SetString(PyExc_TypeError, "Sequence indices must be integers");
            return -1;
        }

        Py_ssize_t index = PyLong_AsSsize_t(key);
        if (index == -1 && PyErr_Occurred())
            return -1;

        size_t count = fy_len(self->fyg);

        /* Handle negative indices */
        if (index < 0)
            index += count;

        if (index < 0 || (size_t)index >= count) {
            PyErr_SetString(PyExc_IndexError, "Sequence index out of range");
            return -1;
        }

        path_array[existing_path_len] = fy_value((int)index);

    } else if (type == FYGT_MAPPING) {
        /* Mapping key lookup */
        PyObject *key_str = PyObject_Str(key);
        if (key_str == NULL)
            return -1;

        const char *key_cstr = PyUnicode_AsUTF8(key_str);
        if (key_cstr == NULL) {
            Py_DECREF(key_str);
            return -1;
        }

        path_array[existing_path_len] = fy_value(key_cstr);
        Py_DECREF(key_str);

    } else {
        PyErr_SetString(PyExc_TypeError, "Object does not support item assignment");
        return -1;
    }

    /* Add value as last element */
    path_array[total_path_len] = new_value;

    /* Call SET_AT_PATH on root with full path */
    new_root = fy_generic_op(root_obj->gb, FYGBOPF_SET_AT_PATH, root_obj->fyg,
                            total_len, path_array);

    if (fy_generic_is_invalid(new_root)) {
        PyErr_SetString(PyExc_RuntimeError, "SET_AT_PATH operation failed");
        return -1;
    }

    /* Update the root's fyg */
    root_obj->fyg = new_root;

    return 0;
}

/* FyGeneric as mapping */
static PyMappingMethods FyGeneric_as_mapping = {
    .mp_length = (lenfunc)FyGeneric_length,
    .mp_subscript = (binaryfunc)FyGeneric_subscript,
    .mp_ass_subscript = (objobjargproc)FyGeneric_ass_subscript,
};

/* Arithmetic operators for scalar types */

/* Helper: Extract numeric value as double */
static int
FyGeneric_as_double(FyGenericObject *obj, double *result)
{
    enum fy_generic_type type = fy_get_type(obj->fyg);

    switch (type) {
        case FYGT_INT:
            *result = (double)fy_cast(obj->fyg, (long long)0);
            return 0;
        case FYGT_FLOAT:
            *result = fy_cast(obj->fyg, (double)0.0);
            return 0;
        default:
            PyErr_SetString(PyExc_TypeError, "Arithmetic only supported on numeric types");
            return -1;
    }
}

/* FyGeneric: __add__ */
static PyObject *
FyGeneric_add(PyObject *left, PyObject *right)
{
    double left_val, right_val;

    /* Convert left operand */
    if (Py_TYPE(left) == &FyGenericType) {
        if (FyGeneric_as_double((FyGenericObject *)left, &left_val) < 0)
            return NULL;
    } else if (PyLong_Check(left)) {
        left_val = (double)PyLong_AsLongLong(left);
    } else if (PyFloat_Check(left)) {
        left_val = PyFloat_AsDouble(left);
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    /* Convert right operand */
    if (Py_TYPE(right) == &FyGenericType) {
        if (FyGeneric_as_double((FyGenericObject *)right, &right_val) < 0)
            return NULL;
    } else if (PyLong_Check(right)) {
        right_val = (double)PyLong_AsLongLong(right);
    } else if (PyFloat_Check(right)) {
        right_val = PyFloat_AsDouble(right);
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    double result = left_val + right_val;

    /* Return int if both were ints and result fits */
    if ((Py_TYPE(left) == &FyGenericType && fy_get_type(((FyGenericObject *)left)->fyg) == FYGT_INT || PyLong_Check(left)) &&
        (Py_TYPE(right) == &FyGenericType && fy_get_type(((FyGenericObject *)right)->fyg) == FYGT_INT || PyLong_Check(right)) &&
        result == (long long)result) {
        return PyLong_FromLongLong((long long)result);
    }

    return PyFloat_FromDouble(result);
}

/* FyGeneric: __sub__ */
static PyObject *
FyGeneric_sub(PyObject *left, PyObject *right)
{
    double left_val, right_val;

    if (Py_TYPE(left) == &FyGenericType) {
        if (FyGeneric_as_double((FyGenericObject *)left, &left_val) < 0)
            return NULL;
    } else if (PyLong_Check(left)) {
        left_val = (double)PyLong_AsLongLong(left);
    } else if (PyFloat_Check(left)) {
        left_val = PyFloat_AsDouble(left);
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    if (Py_TYPE(right) == &FyGenericType) {
        if (FyGeneric_as_double((FyGenericObject *)right, &right_val) < 0)
            return NULL;
    } else if (PyLong_Check(right)) {
        right_val = (double)PyLong_AsLongLong(right);
    } else if (PyFloat_Check(right)) {
        right_val = PyFloat_AsDouble(right);
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    double result = left_val - right_val;

    if ((Py_TYPE(left) == &FyGenericType && fy_get_type(((FyGenericObject *)left)->fyg) == FYGT_INT || PyLong_Check(left)) &&
        (Py_TYPE(right) == &FyGenericType && fy_get_type(((FyGenericObject *)right)->fyg) == FYGT_INT || PyLong_Check(right)) &&
        result == (long long)result) {
        return PyLong_FromLongLong((long long)result);
    }

    return PyFloat_FromDouble(result);
}

/* FyGeneric: __mul__ */
static PyObject *
FyGeneric_mul(PyObject *left, PyObject *right)
{
    double left_val, right_val;

    if (Py_TYPE(left) == &FyGenericType) {
        if (FyGeneric_as_double((FyGenericObject *)left, &left_val) < 0)
            return NULL;
    } else if (PyLong_Check(left)) {
        left_val = (double)PyLong_AsLongLong(left);
    } else if (PyFloat_Check(left)) {
        left_val = PyFloat_AsDouble(left);
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    if (Py_TYPE(right) == &FyGenericType) {
        if (FyGeneric_as_double((FyGenericObject *)right, &right_val) < 0)
            return NULL;
    } else if (PyLong_Check(right)) {
        right_val = (double)PyLong_AsLongLong(right);
    } else if (PyFloat_Check(right)) {
        right_val = PyFloat_AsDouble(right);
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    double result = left_val * right_val;

    if ((Py_TYPE(left) == &FyGenericType && fy_get_type(((FyGenericObject *)left)->fyg) == FYGT_INT || PyLong_Check(left)) &&
        (Py_TYPE(right) == &FyGenericType && fy_get_type(((FyGenericObject *)right)->fyg) == FYGT_INT || PyLong_Check(right)) &&
        result == (long long)result) {
        return PyLong_FromLongLong((long long)result);
    }

    return PyFloat_FromDouble(result);
}

/* FyGeneric: __truediv__ */
static PyObject *
FyGeneric_truediv(PyObject *left, PyObject *right)
{
    double left_val, right_val;

    if (Py_TYPE(left) == &FyGenericType) {
        if (FyGeneric_as_double((FyGenericObject *)left, &left_val) < 0)
            return NULL;
    } else if (PyLong_Check(left)) {
        left_val = (double)PyLong_AsLongLong(left);
    } else if (PyFloat_Check(left)) {
        left_val = PyFloat_AsDouble(left);
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    if (Py_TYPE(right) == &FyGenericType) {
        if (FyGeneric_as_double((FyGenericObject *)right, &right_val) < 0)
            return NULL;
    } else if (PyLong_Check(right)) {
        right_val = (double)PyLong_AsLongLong(right);
    } else if (PyFloat_Check(right)) {
        right_val = PyFloat_AsDouble(right);
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    if (right_val == 0.0) {
        PyErr_SetString(PyExc_ZeroDivisionError, "division by zero");
        return NULL;
    }

    return PyFloat_FromDouble(left_val / right_val);
}

/* FyGeneric: __floordiv__ */
static PyObject *
FyGeneric_floordiv(PyObject *left, PyObject *right)
{
    double left_val, right_val;

    if (Py_TYPE(left) == &FyGenericType) {
        if (FyGeneric_as_double((FyGenericObject *)left, &left_val) < 0)
            return NULL;
    } else if (PyLong_Check(left)) {
        left_val = (double)PyLong_AsLongLong(left);
    } else if (PyFloat_Check(left)) {
        left_val = PyFloat_AsDouble(left);
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    if (Py_TYPE(right) == &FyGenericType) {
        if (FyGeneric_as_double((FyGenericObject *)right, &right_val) < 0)
            return NULL;
    } else if (PyLong_Check(right)) {
        right_val = (double)PyLong_AsLongLong(right);
    } else if (PyFloat_Check(right)) {
        right_val = PyFloat_AsDouble(right);
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    if (right_val == 0.0) {
        PyErr_SetString(PyExc_ZeroDivisionError, "integer division or modulo by zero");
        return NULL;
    }

    return PyLong_FromLongLong((long long)(left_val / right_val));
}

/* FyGeneric: __mod__ */
static PyObject *
FyGeneric_mod(PyObject *left, PyObject *right)
{
    double left_val, right_val;

    if (Py_TYPE(left) == &FyGenericType) {
        if (FyGeneric_as_double((FyGenericObject *)left, &left_val) < 0)
            return NULL;
    } else if (PyLong_Check(left)) {
        left_val = (double)PyLong_AsLongLong(left);
    } else if (PyFloat_Check(left)) {
        left_val = PyFloat_AsDouble(left);
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    if (Py_TYPE(right) == &FyGenericType) {
        if (FyGeneric_as_double((FyGenericObject *)right, &right_val) < 0)
            return NULL;
    } else if (PyLong_Check(right)) {
        right_val = (double)PyLong_AsLongLong(right);
    } else if (PyFloat_Check(right)) {
        right_val = PyFloat_AsDouble(right);
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    if (right_val == 0.0) {
        PyErr_SetString(PyExc_ZeroDivisionError, "integer division or modulo by zero");
        return NULL;
    }

    /* Use fmod for floating point modulo */
    return PyFloat_FromDouble(fmod(left_val, right_val));
}

/* FyGeneric as number */
static PyNumberMethods FyGeneric_as_number = {
    .nb_add = FyGeneric_add,
    .nb_subtract = FyGeneric_sub,
    .nb_multiply = FyGeneric_mul,
    .nb_true_divide = FyGeneric_truediv,
    .nb_floor_divide = FyGeneric_floordiv,
    .nb_remainder = FyGeneric_mod,
    .nb_bool = (inquiry)FyGeneric_bool,
    .nb_int = (unaryfunc)FyGeneric_int,
    .nb_float = (unaryfunc)FyGeneric_float,
};

/* FyGeneric type object */
static PyTypeObject FyGenericType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "libfyaml.FyGeneric",
    .tp_doc = "Wrapper for fy_generic value with lazy conversion",
    .tp_basicsize = sizeof(FyGenericObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_dealloc = (destructor)FyGeneric_dealloc,
    .tp_repr = (reprfunc)FyGeneric_repr,
    .tp_as_number = &FyGeneric_as_number,
    .tp_as_sequence = &FyGeneric_as_sequence,
    .tp_as_mapping = &FyGeneric_as_mapping,
    .tp_str = (reprfunc)FyGeneric_str,
    .tp_getattro = (getattrofunc)FyGeneric_getattro,
    .tp_richcompare = FyGeneric_richcompare,
    .tp_iter = (getiterfunc)FyGeneric_iter,
    .tp_methods = FyGeneric_methods,
};

/* ========== Module Functions ========== */

/* loads(string, mode='yaml', dedup=True, trim=True, mutable=False) - Parse YAML/JSON from string */
static PyObject *
libfyaml_loads(PyObject *self, PyObject *args, PyObject *kwargs)
{
    const char *yaml_str;
    Py_ssize_t yaml_len;
    const char *mode = "yaml";
    int dedup = 1;  /* Default to True */
    int trim = 1;   /* Default to True */
    int mutable = 0;  /* Default to False (read-only) */
    static char *kwlist[] = {"s", "mode", "dedup", "trim", "mutable", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s#|sppp", kwlist, &yaml_str, &yaml_len, &mode, &dedup, &trim, &mutable))
        return NULL;

    /* Create auto allocator with appropriate scenario based on dedup parameter */
    struct fy_auto_allocator_cfg auto_cfg;
    memset(&auto_cfg, 0, sizeof(auto_cfg));
    auto_cfg.scenario = dedup ? FYAST_PER_TAG_FREE_DEDUP : FYAST_PER_TAG_FREE;
    auto_cfg.estimated_max_size = yaml_len * 2;

    struct fy_allocator *allocator = fy_allocator_create("auto", &auto_cfg);
    if (allocator == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create allocator");
        return NULL;
    }

    /* Configure generic builder with allocator and size estimate */
    struct fy_generic_builder_cfg gb_cfg;
    memset(&gb_cfg, 0, sizeof(gb_cfg));
    gb_cfg.allocator = allocator;
    gb_cfg.estimated_max_size = yaml_len * 2;  /* Estimate 2x for parsed structure */
    gb_cfg.flags = FYGBCF_OWNS_ALLOCATOR;  /* Own allocator */

    /* Create generic builder */
    struct fy_generic_builder *gb = fy_generic_builder_create(&gb_cfg);
    if (gb == NULL) {
        fy_allocator_destroy(allocator);
        PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
        return NULL;
    }

    /* Determine parse flags based on mode */
    unsigned int parse_flags = FYOPPF_DISABLE_DIRECTORY | FYOPPF_INPUT_TYPE_STRING;
    if (strcmp(mode, "json") == 0) {
        parse_flags |= FYOPPF_MODE_JSON;
    } else {
        parse_flags |= FYOPPF_MODE_YAML_1_2;
    }

    /* Parse using new API */
    fy_generic parsed = fy_gb_parse(gb, yaml_str, parse_flags, NULL);
    if (!fy_generic_is_valid(parsed)) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_ValueError, "Failed to parse YAML/JSON");
        return NULL;
    }

    /* Create root wrapper - transfers gb ownership to Python object */
    PyObject *result = FyGeneric_from_generic(parsed, gb, mutable);
    if (result == NULL) {
        fy_generic_builder_destroy(gb);
        return NULL;
    }

    /* Auto-trim if requested (default behavior) */
    if (trim && gb) {
        fy_gb_trim(gb);
    }

    return result;
}

/* Helper: Convert Python object to fy_generic (recursive) */
static fy_generic
python_to_generic(struct fy_generic_builder *gb, PyObject *obj)
{
    /* Handle FyGeneric objects - internalize into the new builder */
    if (Py_TYPE(obj) == &FyGenericType) {
        FyGenericObject *fyobj = (FyGenericObject *)obj;
        /* Internalize the generic into the new builder */
        return fy_gb_internalize(gb, fyobj->fyg);
    }

    if (obj == Py_None) {
        return fy_gb_null_create(gb, NULL);
    }

    if (PyBool_Check(obj)) {
        return fy_gb_bool_create(gb, (_Bool)(obj == Py_True));
    }

    if (PyLong_Check(obj)) {
        long long val = PyLong_AsLongLong(obj);
        if (val == -1 && PyErr_Occurred())
            return fy_invalid;
        return fy_gb_long_long_create(gb, val);
    }

    if (PyFloat_Check(obj)) {
        double val = PyFloat_AsDouble(obj);
        if (val == -1.0 && PyErr_Occurred())
            return fy_invalid;
        return fy_gb_double_create(gb, val);
    }

    if (PyUnicode_Check(obj)) {
        const char *str = PyUnicode_AsUTF8(obj);
        if (str == NULL)
            return fy_invalid;
        return fy_gb_string_create(gb, str);
    }

    if (PyList_Check(obj) || PyTuple_Check(obj)) {
        Py_ssize_t len = PySequence_Length(obj);
        if (len < 0)
            return fy_invalid;

        if (len == 0) {
            /* Empty sequence */
            return fy_gb_sequence_create(gb, 0, NULL);
        }

        /* Build array of fy_generic items */
        fy_generic *items = malloc(len * sizeof(fy_generic));
        if (items == NULL) {
            PyErr_NoMemory();
            return fy_invalid;
        }

        for (Py_ssize_t i = 0; i < len; i++) {
            PyObject *item = PySequence_GetItem(obj, i);
            if (item == NULL) {
                free(items);
                return fy_invalid;
            }

            items[i] = python_to_generic(gb, item);
            Py_DECREF(item);

            if (!fy_generic_is_valid(items[i])) {
                free(items);
                return fy_invalid;
            }
        }

        fy_generic result = fy_gb_sequence_create(gb, len, items);
        free(items);
        return result;
    }

    if (PyDict_Check(obj)) {
        Py_ssize_t len = PyDict_Size(obj);
        if (len < 0)
            return fy_invalid;

        if (len == 0) {
            /* Empty mapping */
            return fy_gb_mapping_create(gb, 0, NULL);
        }

        /* Build array of key-value pairs */
        fy_generic *pairs = malloc(len * 2 * sizeof(fy_generic));
        if (pairs == NULL) {
            PyErr_NoMemory();
            return fy_invalid;
        }

        PyObject *key, *value;
        Py_ssize_t pos = 0;
        Py_ssize_t idx = 0;

        while (PyDict_Next(obj, &pos, &key, &value)) {
            pairs[idx * 2] = python_to_generic(gb, key);
            if (!fy_generic_is_valid(pairs[idx * 2])) {
                free(pairs);
                return fy_invalid;
            }

            pairs[idx * 2 + 1] = python_to_generic(gb, value);
            if (!fy_generic_is_valid(pairs[idx * 2 + 1])) {
                free(pairs);
                return fy_invalid;
            }

            idx++;
        }

        fy_generic result = fy_gb_mapping_create(gb, len, pairs);
        free(pairs);
        return result;
    }

    /* Unsupported type */
    PyErr_Format(PyExc_TypeError, "Cannot convert type '%s' to YAML",
                 Py_TYPE(obj)->tp_name);
    return fy_invalid;
}

/* dumps(obj, **options) - Serialize Python object to YAML/JSON string */
static PyObject *
libfyaml_dumps(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *obj;
    int compact = 0;
    int json_mode = 0;
    static char *kwlist[] = {"obj", "compact", "json", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|pp", kwlist,
                                     &obj, &compact, &json_mode))
        return NULL;

    /* Create generic builder */
    struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
    if (gb == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
        return NULL;
    }

    /* Convert Python object to fy_generic */
    fy_generic g = python_to_generic(gb, obj);
    if (!fy_generic_is_valid(g)) {
        fy_generic_builder_destroy(gb);
        /* Exception already set by python_to_generic */
        return NULL;
    }

    /* Determine emit flags based on options */
    unsigned int emit_flags = FYOPEF_DISABLE_DIRECTORY | FYOPEF_OUTPUT_TYPE_STRING;

    if (json_mode) {
        emit_flags |= FYOPEF_MODE_JSON;
        if (!compact)
            emit_flags |= FYOPEF_INDENT_2;
    } else {
        emit_flags |= FYOPEF_MODE_YAML_1_2;
        if (compact) {
            emit_flags |= FYOPEF_STYLE_FLOW;
        } else {
            emit_flags |= FYOPEF_STYLE_BLOCK;
        }
    }

    /* Emit to string using new API - returns a string generic! */
    fy_generic emitted = fy_gb_emit(gb, g, emit_flags, NULL);
    if (!fy_generic_is_valid(emitted)) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to emit YAML/JSON");
        return NULL;
    }

    /* Extract the C string from the generic */
    const char *yaml_str = fy_cast(emitted, "");
    if (yaml_str == NULL) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to extract string from emitted generic");
        return NULL;
    }

    /* Create Python string (makes a copy) */
    PyObject *result = PyUnicode_FromString(yaml_str);

    /* Clean up builder */
    fy_generic_builder_destroy(gb);

    return result;
}

/* from_python(obj, mutable=False) - Convert Python object to FyGeneric */
static PyObject *
libfyaml_from_python(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *obj;
    int mutable = 0;  /* Default to False (read-only) */
    static char *kwlist[] = {"obj", "mutable", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|p", kwlist, &obj, &mutable))
        return NULL;

    /* Create generic builder */
    struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
    if (gb == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
        return NULL;
    }

    /* Convert Python object to generic */
    fy_generic g = python_to_generic(gb, obj);
    if (!fy_generic_is_valid(g)) {
        fy_generic_builder_destroy(gb);
        return NULL;
    }

    /* Create root wrapper - transfers gb ownership to Python object */
    PyObject *result = FyGeneric_from_generic(g, gb, mutable);
    if (result == NULL) {
        fy_generic_builder_destroy(gb);
        return NULL;
    }

    return result;
}

/* load(file, mode='yaml', dedup=True, trim=True, mutable=False) - Load YAML/JSON from file object or path */
static PyObject *
libfyaml_load(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *file_obj;
    const char *mode = "yaml";
    int dedup = 1;  /* Default to True */
    int trim = 1;   /* Default to True */
    int mutable = 0;  /* Default to False (read-only) */
    static char *kwlist[] = {"file", "mode", "dedup", "trim", "mutable", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|sppp", kwlist, &file_obj, &mode, &dedup, &trim, &mutable))
        return NULL;

    /* Check if it's a string (file path) or file object */
    if (PyUnicode_Check(file_obj)) {
        /* It's a file path - use fy_gb_parse_file() with mmap support */
        const char *path = PyUnicode_AsUTF8(file_obj);
        if (path == NULL)
            return NULL;

        /* Create auto allocator with appropriate scenario based on dedup parameter */
        struct fy_auto_allocator_cfg auto_cfg;
        memset(&auto_cfg, 0, sizeof(auto_cfg));
        auto_cfg.scenario = dedup ? FYAST_PER_TAG_FREE_DEDUP : FYAST_PER_TAG_FREE;
        /* For files, we don't know the size upfront */
        auto_cfg.estimated_max_size = 1024 * 1024;  /* 1MB default estimate */

        struct fy_allocator *allocator = fy_allocator_create("auto", &auto_cfg);
        if (allocator == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to create allocator");
            return NULL;
        }

        /* Configure generic builder with allocator */
        struct fy_generic_builder_cfg gb_cfg;
        memset(&gb_cfg, 0, sizeof(gb_cfg));
        gb_cfg.allocator = allocator;
        gb_cfg.flags = FYGBCF_OWNS_ALLOCATOR;  /* Own allocator */

        /* Create generic builder */
        struct fy_generic_builder *gb = fy_generic_builder_create(&gb_cfg);
        if (gb == NULL) {
            fy_allocator_destroy(allocator);
            PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
            return NULL;
        }

        /* Determine parse flags based on mode */
        unsigned int parse_flags = FYOPPF_DISABLE_DIRECTORY;
        if (strcmp(mode, "json") == 0) {
            parse_flags |= FYOPPF_MODE_JSON;
        } else {
            parse_flags |= FYOPPF_MODE_YAML_1_2;
        }

        /* Parse from file using mmap (via fy_gb_parse_file) */
        fy_generic parsed = fy_gb_parse_file(gb, parse_flags, path);
        if (!fy_generic_is_valid(parsed)) {
            fy_generic_builder_destroy(gb);
            PyErr_Format(PyExc_ValueError, "Failed to parse YAML/JSON from file: %s", path);
            return NULL;
        }

        /* Create root wrapper - transfers gb ownership to Python object */
        PyObject *result = FyGeneric_from_generic(parsed, gb, mutable);
        if (result == NULL) {
            fy_generic_builder_destroy(gb);
            return NULL;
        }

        /* Auto-trim if requested (default behavior) */
        if (trim && gb) {
            fy_gb_trim(gb);
        }

        return result;
    } else {
        /* Assume it's a file object - read from it and use loads() */
        PyObject *content = PyObject_CallMethod(file_obj, "read", NULL);
        if (content == NULL)
            return NULL;

        /* Build kwargs dict for loads */
        PyObject *loads_kwargs = PyDict_New();
        if (loads_kwargs == NULL) {
            Py_DECREF(content);
            return NULL;
        }

        PyObject *mode_str = PyUnicode_FromString(mode);
        PyDict_SetItemString(loads_kwargs, "mode", mode_str);
        Py_DECREF(mode_str);

        PyObject *dedup_obj = PyBool_FromLong(dedup);
        PyDict_SetItemString(loads_kwargs, "dedup", dedup_obj);
        Py_DECREF(dedup_obj);

        PyObject *trim_obj = PyBool_FromLong(trim);
        PyDict_SetItemString(loads_kwargs, "trim", trim_obj);
        Py_DECREF(trim_obj);

        PyObject *mutable_obj = PyBool_FromLong(mutable);
        PyDict_SetItemString(loads_kwargs, "mutable", mutable_obj);
        Py_DECREF(mutable_obj);

        /* Call loads with the content */
        PyObject *loads_args = Py_BuildValue("(O)", content);
        PyObject *result = libfyaml_loads(self, loads_args, loads_kwargs);

        Py_DECREF(loads_args);
        Py_DECREF(loads_kwargs);
        Py_DECREF(content);
        return result;
    }
}

/* dump(file, obj, mode='yaml', compact=False) - Dump Python object to file object or path */
static PyObject *
libfyaml_dump(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *file_obj;
    PyObject *obj;
    const char *mode = "yaml";
    int compact = 0;
    static char *kwlist[] = {"file", "obj", "mode", "compact", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|sp", kwlist,
                                     &file_obj, &obj, &mode, &compact))
        return NULL;

    int json_mode = (strcmp(mode, "json") == 0);

    /* Check if it's a string (file path) or file object */
    if (PyUnicode_Check(file_obj)) {
        /* It's a file path - use fy_gb_emit_file() for direct file output */
        const char *path = PyUnicode_AsUTF8(file_obj);
        if (path == NULL)
            return NULL;

        /* Create generic builder */
        struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
        if (gb == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
            return NULL;
        }

        /* Convert Python object to fy_generic */
        fy_generic g = python_to_generic(gb, obj);
        if (!fy_generic_is_valid(g)) {
            fy_generic_builder_destroy(gb);
            /* Exception already set by python_to_generic */
            return NULL;
        }

        /* Determine emit flags based on options */
        unsigned int emit_flags = FYOPEF_DISABLE_DIRECTORY;

        if (json_mode) {
            emit_flags |= FYOPEF_MODE_JSON;
            if (!compact)
                emit_flags |= FYOPEF_INDENT_2;
        } else {
            emit_flags |= FYOPEF_MODE_YAML_1_2;
            if (compact) {
                emit_flags |= FYOPEF_STYLE_FLOW;
            } else {
                emit_flags |= FYOPEF_STYLE_BLOCK;
            }
        }

        /* Emit to file using new API - returns int 0 on success */
        fy_generic result_g = fy_gb_emit_file(gb, g, emit_flags, path);

        /* Check for success: should return integer 0 */
        if (!fy_generic_is_valid(result_g)) {
            fy_generic_builder_destroy(gb);
            PyErr_Format(PyExc_RuntimeError, "Failed to emit YAML/JSON to file: %s (invalid result)", path);
            return NULL;
        }

        if (!fy_generic_is_int_type(result_g)) {
            fy_generic_builder_destroy(gb);
            PyErr_Format(PyExc_RuntimeError, "Failed to emit YAML/JSON to file: %s (wrong type: %d)",
                        path, fy_get_type(result_g));
            return NULL;
        }

        int result_code = fy_cast(result_g, -1);
        fy_generic_builder_destroy(gb);

        if (result_code != 0) {
            PyErr_Format(PyExc_RuntimeError, "Failed to emit YAML/JSON to file: %s (error code: %d)", path, result_code);
            return NULL;
        }

        Py_RETURN_NONE;
    } else {
        /* Assume it's a file object - use dumps() and write */
        PyObject *dumps_kwargs = PyDict_New();
        if (dumps_kwargs == NULL)
            return NULL;

        PyObject *compact_bool = PyBool_FromLong(compact);
        PyObject *json_bool = PyBool_FromLong(json_mode);

        PyDict_SetItemString(dumps_kwargs, "compact", compact_bool);
        PyDict_SetItemString(dumps_kwargs, "json", json_bool);

        Py_DECREF(compact_bool);
        Py_DECREF(json_bool);

        /* Convert object to YAML string */
        PyObject *dumps_args = Py_BuildValue("(O)", obj);
        PyObject *yaml_str = libfyaml_dumps(self, dumps_args, dumps_kwargs);

        Py_DECREF(dumps_args);
        Py_DECREF(dumps_kwargs);

        if (yaml_str == NULL)
            return NULL;

        /* Write to file object */
        PyObject *result = PyObject_CallMethod(file_obj, "write", "O", yaml_str);
        Py_DECREF(yaml_str);

        if (result == NULL)
            return NULL;

        Py_DECREF(result);
        Py_RETURN_NONE;
    }
}

/* loads_all(string, mode='yaml', dedup=True, trim=True, mutable=False) - Parse multi-document YAML */
static PyObject *
libfyaml_loads_all(PyObject *self, PyObject *args, PyObject *kwargs)
{
    const char *yaml_str;
    Py_ssize_t yaml_len;
    const char *mode = "yaml";
    int dedup = 1;
    int trim = 1;
    int mutable = 0;
    static char *kwlist[] = {"s", "mode", "dedup", "trim", "mutable", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s#|sppp", kwlist, &yaml_str, &yaml_len, &mode, &dedup, &trim, &mutable))
        return NULL;

    /* Create auto allocator with appropriate scenario based on dedup parameter */
    struct fy_auto_allocator_cfg auto_cfg;
    memset(&auto_cfg, 0, sizeof(auto_cfg));
    auto_cfg.scenario = dedup ? FYAST_PER_TAG_FREE_DEDUP : FYAST_PER_TAG_FREE;
    auto_cfg.estimated_max_size = yaml_len * 2;

    struct fy_allocator *allocator = fy_allocator_create("auto", &auto_cfg);
    if (allocator == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create allocator");
        return NULL;
    }

    /* Configure generic builder with allocator */
    struct fy_generic_builder_cfg gb_cfg;
    memset(&gb_cfg, 0, sizeof(gb_cfg));
    gb_cfg.allocator = allocator;
    gb_cfg.estimated_max_size = yaml_len * 2;
    gb_cfg.flags = FYGBCF_OWNS_ALLOCATOR;

    struct fy_generic_builder *gb = fy_generic_builder_create(&gb_cfg);
    if (gb == NULL) {
        fy_allocator_destroy(allocator);
        PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
        return NULL;
    }

    /* Determine parse flags based on mode - add MULTI_DOCUMENT flag */
    unsigned int parse_flags = FYOPPF_DISABLE_DIRECTORY | FYOPPF_INPUT_TYPE_STRING | FYOPPF_MULTI_DOCUMENT;
    if (strcmp(mode, "json") == 0) {
        parse_flags |= FYOPPF_MODE_JSON;
    } else {
        parse_flags |= FYOPPF_MODE_YAML_1_2;
    }

    /* Parse YAML/JSON string - with MULTI_DOCUMENT, returns a sequence of documents */
    fy_generic parsed = fy_gb_parse(gb, yaml_str, parse_flags, NULL);

    if (!fy_generic_is_valid(parsed)) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_ValueError, "Failed to parse YAML/JSON");
        return NULL;
    }

    /* Return the sequence as a FyGeneric - user can iterate it */
    PyObject *result = FyGeneric_from_generic(parsed, gb, mutable);
    if (result == NULL) {
        fy_generic_builder_destroy(gb);
        return NULL;
    }

    /* Auto-trim if requested (default behavior) */
    if (trim && gb) {
        fy_gb_trim(gb);
    }

    return result;
}

/* load_all(file, mode='yaml', dedup=True, trim=True, mutable=False) - Parse multi-document from file */
static PyObject *
libfyaml_load_all(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *file_obj;
    const char *mode = "yaml";
    int dedup = 1;
    int trim = 1;
    int mutable = 0;
    static char *kwlist[] = {"file", "mode", "dedup", "trim", "mutable", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|sppp", kwlist,
                                     &file_obj, &mode, &dedup, &trim, &mutable))
        return NULL;

    /* Check if it's a string (file path) or file object */
    if (PyUnicode_Check(file_obj)) {
        /* It's a file path - use fy_gb_parse_file() for mmap-based loading */
        const char *path = PyUnicode_AsUTF8(file_obj);
        if (path == NULL)
            return NULL;

        /* Create auto allocator with appropriate scenario based on dedup parameter */
        struct fy_auto_allocator_cfg auto_cfg;
        memset(&auto_cfg, 0, sizeof(auto_cfg));
        auto_cfg.scenario = dedup ? FYAST_PER_TAG_FREE_DEDUP : FYAST_PER_TAG_FREE;
        auto_cfg.estimated_max_size = 1024 * 1024;  /* Estimate 1MB for file */

        struct fy_allocator *allocator = fy_allocator_create("auto", &auto_cfg);
        if (allocator == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to create allocator");
            return NULL;
        }

        /* Configure generic builder with allocator */
        struct fy_generic_builder_cfg gb_cfg;
        memset(&gb_cfg, 0, sizeof(gb_cfg));
        gb_cfg.allocator = allocator;
        gb_cfg.estimated_max_size = 1024 * 1024;
        gb_cfg.flags = FYGBCF_OWNS_ALLOCATOR;

        struct fy_generic_builder *gb = fy_generic_builder_create(&gb_cfg);
        if (gb == NULL) {
            fy_allocator_destroy(allocator);
            PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
            return NULL;
        }

        /* Determine parse flags based on mode - add MULTI_DOCUMENT flag */
        unsigned int parse_flags = FYOPPF_DISABLE_DIRECTORY | FYOPPF_MULTI_DOCUMENT;
        if (strcmp(mode, "json") == 0) {
            parse_flags |= FYOPPF_MODE_JSON;
        } else {
            parse_flags |= FYOPPF_MODE_YAML_1_2;
        }

        /* Parse from file using mmap - with MULTI_DOCUMENT, returns a sequence of documents */
        fy_generic parsed = fy_gb_parse_file(gb, parse_flags, path);

        if (!fy_generic_is_valid(parsed)) {
            fy_generic_builder_destroy(gb);
            PyErr_Format(PyExc_ValueError, "Failed to parse YAML/JSON file: %s", path);
            return NULL;
        }

        /* Return the sequence as a FyGeneric - user can iterate it */
        PyObject *result = FyGeneric_from_generic(parsed, gb, mutable);
        if (result == NULL) {
            fy_generic_builder_destroy(gb);
            return NULL;
        }

        /* Auto-trim if requested (default behavior) */
        if (trim && gb) {
            fy_gb_trim(gb);
        }

        return result;
    } else {
        /* Assume it's a file object - read and call loads_all() */
        PyObject *read_method = PyObject_GetAttrString(file_obj, "read");
        if (read_method == NULL)
            return NULL;

        PyObject *content = PyObject_CallObject(read_method, NULL);
        Py_DECREF(read_method);

        if (content == NULL)
            return NULL;

        /* Call loads_all with the content */
        PyObject *loads_all_kwargs = PyDict_New();
        if (loads_all_kwargs == NULL) {
            Py_DECREF(content);
            return NULL;
        }

        PyObject *mode_str = PyUnicode_FromString(mode);
        PyObject *dedup_bool = PyBool_FromLong(dedup);
        PyObject *trim_bool = PyBool_FromLong(trim);
        PyObject *mutable_bool = PyBool_FromLong(mutable);

        PyDict_SetItemString(loads_all_kwargs, "mode", mode_str);
        PyDict_SetItemString(loads_all_kwargs, "dedup", dedup_bool);
        PyDict_SetItemString(loads_all_kwargs, "trim", trim_bool);
        PyDict_SetItemString(loads_all_kwargs, "mutable", mutable_bool);

        Py_DECREF(mode_str);
        Py_DECREF(dedup_bool);
        Py_DECREF(trim_bool);
        Py_DECREF(mutable_bool);

        PyObject *loads_all_args = PyTuple_Pack(1, content);
        PyObject *result = libfyaml_loads_all(self, loads_all_args, loads_all_kwargs);

        Py_DECREF(loads_all_args);
        Py_DECREF(loads_all_kwargs);
        Py_DECREF(content);
        return result;
    }
}

/* dumps_all(documents, compact=False, json=False) - Dump FyGeneric sequence to multi-document string */
static PyObject *
libfyaml_dumps_all(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *documents;
    int compact = 0;
    int json_mode = 0;
    static char *kwlist[] = {"documents", "compact", "json", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|pp", kwlist,
                                     &documents, &compact, &json_mode))
        return NULL;

    /* documents must be a FyGeneric sequence */
    if (Py_TYPE(documents) != &FyGenericType) {
        PyErr_SetString(PyExc_TypeError, "documents must be a FyGeneric sequence (from load_all/loads_all)");
        return NULL;
    }

    FyGenericObject *fyobj = (FyGenericObject *)documents;
    if (!fy_generic_is_sequence(fyobj->fyg)) {
        PyErr_SetString(PyExc_TypeError, "documents must be a sequence");
        return NULL;
    }

    /* Create generic builder for emitting */
    struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
    if (gb == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
        return NULL;
    }

    /* Internalize the sequence into our builder */
    fy_generic doc_sequence = fy_gb_internalize(gb, fyobj->fyg);
    if (!fy_generic_is_valid(doc_sequence)) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to internalize document sequence");
        return NULL;
    }

    /* Determine emit flags based on options - add MULTI_DOCUMENT flag */
    unsigned int emit_flags = FYOPEF_DISABLE_DIRECTORY | FYOPEF_OUTPUT_TYPE_STRING | FYOPEF_MULTI_DOCUMENT;

    if (json_mode) {
        emit_flags |= FYOPEF_MODE_JSON;
        if (!compact)
            emit_flags |= FYOPEF_INDENT_2;
    } else {
        emit_flags |= FYOPEF_MODE_YAML_1_2;
        if (compact) {
            emit_flags |= FYOPEF_STYLE_FLOW;
        } else {
            emit_flags |= FYOPEF_STYLE_BLOCK;
        }
    }

    /* Emit the sequence of documents */
    fy_generic emitted = fy_gb_emit(gb, doc_sequence, emit_flags, NULL);

    if (!fy_generic_is_valid(emitted) || !fy_generic_is_string(emitted)) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to emit YAML/JSON documents");
        return NULL;
    }

    /* Get string from emitted result */
    const char *result_str;
    size_t result_len;
    result_str = fy_generic_get_string_size_alloca(emitted, &result_len);
    if (!result_str) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to get emitted string");
        return NULL;
    }

    PyObject *result = PyUnicode_FromStringAndSize(result_str, result_len);
    fy_generic_builder_destroy(gb);
    return result;
}

/* dump_all(file, documents, compact=False, json=False) - Dump FyGeneric sequence to file */
static PyObject *
libfyaml_dump_all(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *file_obj;
    PyObject *documents;
    int compact = 0;
    int json_mode = 0;
    static char *kwlist[] = {"file", "documents", "compact", "json", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|pp", kwlist,
                                     &file_obj, &documents, &compact, &json_mode))
        return NULL;

    /* documents must be a FyGeneric sequence */
    if (Py_TYPE(documents) != &FyGenericType) {
        PyErr_SetString(PyExc_TypeError, "documents must be a FyGeneric sequence (from load_all/loads_all)");
        return NULL;
    }

    FyGenericObject *fyobj = (FyGenericObject *)documents;
    if (!fy_generic_is_sequence(fyobj->fyg)) {
        PyErr_SetString(PyExc_TypeError, "documents must be a sequence");
        return NULL;
    }

    /* Check if it's a string (file path) or file object */
    if (PyUnicode_Check(file_obj)) {
        /* It's a file path - use fy_gb_emit_file() for direct file output */
        const char *path = PyUnicode_AsUTF8(file_obj);
        if (path == NULL)
            return NULL;

        /* Create generic builder for emitting */
        struct fy_generic_builder *gb = fy_generic_builder_create(NULL);
        if (gb == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
            return NULL;
        }

        /* Internalize the sequence into our builder */
        fy_generic doc_sequence = fy_gb_internalize(gb, fyobj->fyg);
        if (!fy_generic_is_valid(doc_sequence)) {
            fy_generic_builder_destroy(gb);
            PyErr_SetString(PyExc_RuntimeError, "Failed to internalize document sequence");
            return NULL;
        }

        /* Determine emit flags based on options - add MULTI_DOCUMENT flag */
        unsigned int emit_flags = FYOPEF_DISABLE_DIRECTORY | FYOPEF_MULTI_DOCUMENT;

        if (json_mode) {
            emit_flags |= FYOPEF_MODE_JSON;
            if (!compact)
                emit_flags |= FYOPEF_INDENT_2;
        } else {
            emit_flags |= FYOPEF_MODE_YAML_1_2;
            if (compact) {
                emit_flags |= FYOPEF_STYLE_FLOW;
            } else {
                emit_flags |= FYOPEF_STYLE_BLOCK;
            }
        }

        /* Emit to file using new API - returns int 0 on success */
        fy_generic result_g = fy_gb_emit_file(gb, doc_sequence, emit_flags, path);

        /* Check for success: should return integer 0 */
        if (!fy_generic_is_valid(result_g)) {
            fy_generic_builder_destroy(gb);
            PyErr_Format(PyExc_RuntimeError, "Failed to emit YAML/JSON to file: %s (invalid result)", path);
            return NULL;
        }

        if (!fy_generic_is_int_type(result_g)) {
            fy_generic_builder_destroy(gb);
            PyErr_Format(PyExc_RuntimeError, "Failed to emit YAML/JSON to file: %s (wrong type: %d)",
                        path, fy_get_type(result_g));
            return NULL;
        }

        int result_code = fy_cast(result_g, -1);
        fy_generic_builder_destroy(gb);

        if (result_code != 0) {
            PyErr_Format(PyExc_RuntimeError, "Failed to emit YAML/JSON to file: %s (error code: %d)", path, result_code);
            return NULL;
        }

        Py_RETURN_NONE;
    } else {
        /* Assume it's a file object - use dumps_all() and write */
        PyObject *dumps_all_kwargs = PyDict_New();
        if (dumps_all_kwargs == NULL)
            return NULL;

        PyObject *compact_bool = PyBool_FromLong(compact);
        PyObject *json_bool = PyBool_FromLong(json_mode);

        PyDict_SetItemString(dumps_all_kwargs, "compact", compact_bool);
        PyDict_SetItemString(dumps_all_kwargs, "json", json_bool);

        Py_DECREF(compact_bool);
        Py_DECREF(json_bool);

        PyObject *dumps_all_args = PyTuple_Pack(1, documents);
        PyObject *yaml_str = libfyaml_dumps_all(self, dumps_all_args, dumps_all_kwargs);

        Py_DECREF(dumps_all_args);
        Py_DECREF(dumps_all_kwargs);

        if (yaml_str == NULL)
            return NULL;

        /* Write the string to the file object */
        PyObject *write_method = PyObject_GetAttrString(file_obj, "write");
        if (write_method == NULL) {
            Py_DECREF(yaml_str);
            return NULL;
        }

        PyObject *write_args = PyTuple_Pack(1, yaml_str);
        PyObject *result = PyObject_CallObject(write_method, write_args);

        Py_DECREF(write_method);
        Py_DECREF(write_args);
        Py_DECREF(yaml_str);

        if (result == NULL)
            return NULL;

        Py_DECREF(result);
        Py_RETURN_NONE;
    }
}

/* Module method table */
static PyMethodDef module_methods[] = {
    {"loads", (PyCFunction)libfyaml_loads, METH_VARARGS | METH_KEYWORDS,
     "Load YAML/JSON from string"},
    {"dumps", (PyCFunction)libfyaml_dumps, METH_VARARGS | METH_KEYWORDS,
     "Dump Python object to YAML/JSON string"},
    {"load", (PyCFunction)libfyaml_load, METH_VARARGS | METH_KEYWORDS,
     "Load YAML/JSON from file (path or file object)"},
    {"dump", (PyCFunction)libfyaml_dump, METH_VARARGS | METH_KEYWORDS,
     "Dump Python object to file (path or file object)"},
    {"loads_all", (PyCFunction)libfyaml_loads_all, METH_VARARGS | METH_KEYWORDS,
     "Load multiple YAML/JSON documents from string"},
    {"load_all", (PyCFunction)libfyaml_load_all, METH_VARARGS | METH_KEYWORDS,
     "Load multiple YAML/JSON documents from file (path or file object)"},
    {"dumps_all", (PyCFunction)libfyaml_dumps_all, METH_VARARGS | METH_KEYWORDS,
     "Dump multiple Python objects to YAML/JSON string"},
    {"dump_all", (PyCFunction)libfyaml_dump_all, METH_VARARGS | METH_KEYWORDS,
     "Dump multiple Python objects to file (path or file object)"},
    {"from_python", (PyCFunction)libfyaml_from_python, METH_VARARGS | METH_KEYWORDS,
     "Convert Python object to FyGeneric"},
    {NULL}
};

/* Module definition */
static struct PyModuleDef libfyaml_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "_libfyaml",
    .m_doc = "Minimal libfyaml Python bindings prototype",
    .m_size = -1,
    .m_methods = module_methods,
};

/* Module initialization */
PyMODINIT_FUNC
PyInit__libfyaml(void)
{
    PyObject *m;

    /* Initialize types */
    if (PyType_Ready(&FyGenericType) < 0)
        return NULL;

    if (PyType_Ready(&FyGenericIteratorType) < 0)
        return NULL;

    /* Create module */
    m = PyModule_Create(&libfyaml_module);
    if (m == NULL)
        return NULL;

    /* Add types to module */
    Py_INCREF(&FyGenericType);
    if (PyModule_AddObject(m, "FyGeneric", (PyObject *)&FyGenericType) < 0) {
        Py_DECREF(&FyGenericType);
        Py_DECREF(m);
        return NULL;
    }

    return m;
}
