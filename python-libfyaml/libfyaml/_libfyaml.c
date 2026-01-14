/* vim: set ts=4 sw=4 et: */
/**
 * _libfyaml.c - Python bindings for libfyaml generics
 *
 * Provides NumPy-like lazy conversion with the generic API.
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <math.h>
#include <assert.h>

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

/* Helper: Convert primitive fy_generic to Python object (for dict keys, iteration, etc.) */
static PyObject *fy_generic_to_python_primitive(fy_generic value)
{
    switch (fy_get_type(value)) {
    case FYGT_NULL:
        Py_INCREF(Py_None);
        return Py_None;
    case FYGT_BOOL:
        return PyBool_FromLong(fy_cast(value, (_Bool)false) ? 1 : 0);
    case FYGT_INT:
        return PyLong_FromLongLong(fy_cast(value, (long long)-1LL));
    case FYGT_FLOAT:
        return PyFloat_FromDouble(fy_cast(value, (double)0.0));
    case FYGT_STRING: {
        fy_generic_sized_string szstr = fy_cast(value, fy_szstr_empty);
        return PyUnicode_FromStringAndSize(szstr.data, szstr.size);
    }
    case FYGT_SEQUENCE:
        /* Sequences cannot be dict keys (unhashable in Python) */
        PyErr_SetString(PyExc_TypeError, "unhashable type: 'sequence'");
        return NULL;
    case FYGT_MAPPING:
        /* Mappings cannot be dict keys (unhashable in Python) */
        PyErr_SetString(PyExc_TypeError, "unhashable type: 'mapping'");
        return NULL;
    case FYGT_INDIRECT:
    case FYGT_ALIAS:
        /* These should be resolved before reaching here */
        PyErr_SetString(PyExc_RuntimeError, "unresolved indirect/alias type");
        return NULL;
    default:
        PyErr_Format(PyExc_TypeError, "unsupported type for conversion: %d", fy_get_type(value));
        return NULL;
    }
}

/* ========== FyGenericIterator Type ========== */

typedef struct {
    PyObject_HEAD
    FyGenericObject *generic_obj;  /* The FyGeneric being iterated */
    size_t index;                   /* Current position */
    enum fy_generic_type iter_type; /* SEQUENCE or MAPPING */
    union {
        fy_generic_sequence_handle seqh;
        fy_generic_mapping_handle maph;
    } u;
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
    PyObject *result = NULL;
    PyObject *key_obj = NULL;
    fy_generic key, item;

    switch (self->iter_type) {

    case FYGT_SEQUENCE:
        if (self->index >= self->u.seqh->count)
            goto out_stop;
        item = self->u.seqh->items[self->index];
        key_obj = PyLong_FromSize_t(self->index);
        if (key_obj == NULL)
            return NULL;

        result = FyGeneric_from_parent(item, self->generic_obj, key_obj);
        Py_DECREF(key_obj);
        break;

    case FYGT_MAPPING:
        if (self->index >= self->u.maph->count)
            goto out_stop;
        key = self->u.maph->pairs[self->index].key;
        item = self->u.maph->pairs[self->index].value;

        /* Convert key to Python object for path tracking */
        key_obj = fy_generic_to_python_primitive(key);
        if (key_obj == NULL)
            return NULL;

        /* Return the value (not the key) */
        result = FyGeneric_from_parent(item, self->generic_obj, key_obj);
        Py_DECREF(key_obj);
        break;

    default:
        return NULL;
    }

    self->index++;

    return result;

out_stop:
    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
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
    iter->iter_type = type;
    iter->index = 0;
    if (type == FYGT_SEQUENCE)
        iter->u.seqh = fy_cast(self->fyg, fy_seq_handle_null);
    else
        iter->u.maph = fy_cast(self->fyg, fy_map_handle_null);

    return (PyObject *)iter;
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
            fy_generic_sized_string szstr = fy_cast(self->fyg, fy_szstr_empty);
            return PyUnicode_FromStringAndSize(szstr.data, szstr.size);
        }

        case FYGT_INT:
            return PyUnicode_FromFormat("%lld", fy_cast(self->fyg, (long long)0));

        case FYGT_FLOAT: {
            PyObject *float_obj = PyFloat_FromDouble(fy_cast(self->fyg, (double)0.0));
            if (float_obj == NULL)
                return NULL;
            PyObject *str_obj = PyObject_Str(float_obj);
            Py_DECREF(float_obj);
            return str_obj;
        }

        case FYGT_BOOL:
            return PyUnicode_FromString(fy_cast(self->fyg, (_Bool)false) ? "True" : "False");

        case FYGT_NULL:
            return PyUnicode_FromString("None");

        case FYGT_SEQUENCE:
        case FYGT_MAPPING: {
            /* Emit collections as oneline flow */
            FyGenericObject *root_obj = (self->root == NULL) ? self : (FyGenericObject *)self->root;
            assert(root_obj->gb != NULL);

            unsigned int emit_flags = FYOPEF_DISABLE_DIRECTORY | FYOPEF_MODE_YAML_1_2 |
                                     FYOPEF_STYLE_ONELINE | FYOPEF_OUTPUT_TYPE_STRING;

            fy_generic emitted = fy_gb_emit(root_obj->gb, self->fyg, emit_flags, NULL);
            if (!fy_generic_is_valid(emitted)) {
                PyErr_SetString(PyExc_RuntimeError, "Failed to emit collection as string");
                return NULL;
            }

            fy_generic_sized_string szstr = fy_cast(emitted, fy_szstr_empty);
            if (szstr.data == NULL) {
                PyErr_SetString(PyExc_RuntimeError, "Failed to extract string from emitted collection");
                return NULL;
            }

            return PyUnicode_FromStringAndSize(szstr.data, szstr.size);
        }

        default:
            return FyGeneric_repr(self);
    }
}

/* FyGeneric: __int__ */
static PyObject *
FyGeneric_int(FyGenericObject *self)
{
    enum fy_generic_type type = fy_get_type(self->fyg);

    switch (type) {
        case FYGT_INT: {
            /* Get decorated int to check if it's unsigned and large */
            fy_generic_decorated_int dint = fy_cast(self->fyg, fy_dint_empty);
            if (dint.flags & FYGDIF_UNSIGNED_RANGE_EXTEND) {
                /* Unsigned value > LLONG_MAX */
                return PyLong_FromUnsignedLongLong(dint.uv);
            } else {
                /* Regular signed long long */
                return PyLong_FromLongLong(dint.sv);
            }
        }

        case FYGT_BOOL:
            /* true -> 1, false -> 0 */
            return PyLong_FromLong(fy_cast(self->fyg, (_Bool)false) ? 1 : 0);

        case FYGT_FLOAT: {
            /* Convert float to int (truncate like Python) */
            double val = fy_cast(self->fyg, (double)0.0);
            return PyLong_FromDouble(val);
        }

        case FYGT_STRING: {
            /* Parse string as integer using Python's int() */
            fy_generic_sized_string szstr = fy_cast(self->fyg, fy_szstr_empty);
            PyObject *str_obj = PyUnicode_FromStringAndSize(szstr.data, szstr.size);
            if (str_obj == NULL)
                return NULL;

            PyObject *int_obj = PyLong_FromUnicodeObject(str_obj, 10);
            Py_DECREF(str_obj);
            return int_obj;
        }

        case FYGT_NULL:
        case FYGT_SEQUENCE:
        case FYGT_MAPPING:
            PyErr_Format(PyExc_TypeError, "int() argument must be a string or a number, not '%.200s'",
                        Py_TYPE(self)->tp_name);
            return NULL;

        default:
            PyErr_SetString(PyExc_TypeError, "Cannot convert to int");
            return NULL;
    }
}

/* FyGeneric: __float__ */
static PyObject *
FyGeneric_float(FyGenericObject *self)
{
    enum fy_generic_type type = fy_get_type(self->fyg);

    switch (type) {
        case FYGT_FLOAT:
            /* Direct float conversion */
            return PyFloat_FromDouble(fy_cast(self->fyg, (double)0.0));

        case FYGT_INT: {
            /* Check if we have decorated int (large unsigned) */
            fy_generic_decorated_int dint = fy_cast(self->fyg, fy_dint_empty);

            if (dint.flags & FYGDIF_UNSIGNED_RANGE_EXTEND) {
                /* Large unsigned value - convert via Python int to preserve precision */
                PyObject *py_int = PyLong_FromUnsignedLongLong(dint.uv);
                if (!py_int)
                    return NULL;
                PyObject *py_float = PyNumber_Float(py_int);
                Py_DECREF(py_int);
                return py_float;
            } else {
                /* Regular int to float */
                return PyFloat_FromDouble((double)dint.sv);
            }
        }

        case FYGT_BOOL:
            /* true -> 1.0, false -> 0.0 */
            return PyFloat_FromDouble(fy_cast(self->fyg, (_Bool)false) ? 1.0 : 0.0);

        case FYGT_STRING: {
            /* Parse string as float using Python's float() */
            fy_generic_sized_string szstr = fy_cast(self->fyg, fy_szstr_empty);
            PyObject *str_obj = PyUnicode_FromStringAndSize(szstr.data, szstr.size);
            if (str_obj == NULL)
                return NULL;

            PyObject *float_obj = PyFloat_FromString(str_obj);
            Py_DECREF(str_obj);
            return float_obj;
        }

        case FYGT_NULL:
        case FYGT_SEQUENCE:
        case FYGT_MAPPING:
            PyErr_Format(PyExc_TypeError, "float() argument must be a string or a number, not '%.200s'",
                        Py_TYPE(self)->tp_name);
            return NULL;

        default:
            PyErr_SetString(PyExc_TypeError, "Cannot convert to float");
            return NULL;
    }
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
            fy_generic_sized_string szstr = fy_cast(self->fyg, fy_szstr_empty);
            return szstr.size > 0 ? 1 : 0;
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

    switch (type) {
        case FYGT_SEQUENCE:
        case FYGT_MAPPING:
            return (Py_ssize_t)fy_len(self->fyg);

        case FYGT_STRING: {
            /* Return number of characters (not bytes) */
            fy_generic_sized_string szstr = fy_cast(self->fyg, fy_szstr_empty);
            /* Create Python string to get character count (handles UTF-8) */
            PyObject *str_obj = PyUnicode_FromStringAndSize(szstr.data, szstr.size);
            if (str_obj == NULL)
                return -1;
            Py_ssize_t length = PyUnicode_GET_LENGTH(str_obj);
            Py_DECREF(str_obj);
            return length;
        }

        default:
            PyErr_SetString(PyExc_TypeError, "Object has no len()");
            return -1;
    }
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

    /* Build path from parent's path + this element */
    /* Path tracking is always enabled, regardless of mutability */
    /* Create tuple directly - no intermediate list needed */
    if (parent->path == NULL) {
        /* Parent is root - create new tuple with just this element */
        self->path = PyTuple_New(1);
        if (self->path == NULL) {
            Py_DECREF(self->root);
            Py_TYPE(self)->tp_free((PyObject *)self);
            return NULL;
        }
        Py_INCREF(path_elem);
        PyTuple_SET_ITEM(self->path, 0, path_elem);
    } else {
        /* Parent has path - copy and append */
        Py_ssize_t parent_len = PyTuple_Size(parent->path);
        self->path = PyTuple_New(parent_len + 1);
        if (self->path == NULL) {
            Py_DECREF(self->root);
            Py_TYPE(self)->tp_free((PyObject *)self);
            return NULL;
        }

        /* Copy parent's path elements */
        for (Py_ssize_t i = 0; i < parent_len; i++) {
            PyObject *item = PyTuple_GET_ITEM(parent->path, i);
            Py_INCREF(item);
            PyTuple_SET_ITEM(self->path, i, item);
        }

        /* Append new element */
        Py_INCREF(path_elem);
        PyTuple_SET_ITEM(self->path, parent_len, path_elem);
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

        case FYGT_INT: {
            /* Get decorated int to check if it's unsigned and large */
            fy_generic_decorated_int dint = fy_cast(self->fyg, fy_dint_empty);
            if (dint.flags & FYGDIF_UNSIGNED_RANGE_EXTEND) {
                /* Unsigned value > LLONG_MAX */
                return PyLong_FromUnsignedLongLong(dint.uv);
            } else {
                /* Regular signed long long */
                return PyLong_FromLongLong(dint.sv);
            }
        }

        case FYGT_FLOAT:
            return PyFloat_FromDouble(fy_cast(self->fyg, (double)0.0));

        case FYGT_STRING: {
            fy_generic_sized_string szstr = fy_cast(self->fyg, fy_szstr_empty);
            return PyUnicode_FromStringAndSize(szstr.data, szstr.size);
        }

        case FYGT_SEQUENCE: {

            fy_generic_sequence_handle seqh = fy_cast(self->fyg, fy_seq_handle_null);
            if (!seqh) {
                /* Empty flow-style sequence (e.g., []) - return empty list */
                return PyList_New(0);
            }

            PyObject *list = PyList_New(seqh->count);
            if (list == NULL)
                return NULL;

            PyObject *index_obj = NULL, *item_obj = NULL, *converted = NULL;

            size_t i;
            for (i = 0; i < seqh->count; i++) {
                fy_generic item = seqh->items[i];

                // NOT VERY EFFICIENT
                index_obj = PyLong_FromSize_t(i);
                if (index_obj == NULL)
                    break;
                item_obj = FyGeneric_from_parent(item, self, index_obj);
                Py_DECREF(index_obj);
                index_obj = NULL;
                if (item_obj == NULL)
                    break;

                converted = FyGeneric_to_python((FyGenericObject *)item_obj, NULL);
                Py_DECREF(item_obj);
                item_obj = NULL;
                if (converted == NULL)
                    break;

                PyList_SET_ITEM(list, i, converted);
                converted = NULL;
            }
            if (i < seqh->count) {
                Py_DECREF(list);
                if (item_obj)
                    Py_DECREF(item_obj);
                if (index_obj)
                    Py_DECREF(index_obj);
                return NULL;
            }
            return list;
        }

        case FYGT_MAPPING: {

            fy_generic_mapping_handle maph = fy_cast(self->fyg, fy_map_handle_null);
            if (!maph) {
                /* Empty flow-style mapping (e.g., {}) - return empty dict */
                return PyDict_New();
            }

            PyObject *dict = PyDict_New();
            if (dict == NULL)
                return NULL;

            PyObject *path_key = NULL, *conv_key = NULL, *val_obj = NULL, *conv_val = NULL;
            size_t i;

            for (i = 0; i < maph->count; i++) {
                /* First get the key as a Python object for path */
                fy_generic key = maph->pairs[i].key;

                path_key = fy_generic_to_python_primitive(key);
                if (!path_key)
                    break;

                /* Convert key */
                PyObject *key_obj = FyGeneric_from_parent(maph->pairs[i].key, self, path_key);
                if (key_obj == NULL)
                    break;

                conv_key = FyGeneric_to_python((FyGenericObject *)key_obj, NULL);
                Py_DECREF(key_obj);
                key_obj = NULL;
                if (conv_key == NULL)
                    break;

                /* Convert value */
                val_obj = FyGeneric_from_parent(maph->pairs[i].value, self, path_key);
                Py_DECREF(path_key);  /* Done with path_key */
                path_key = NULL;
                if (val_obj == NULL)
                    break;

                conv_val = FyGeneric_to_python((FyGenericObject *)val_obj, NULL);
                Py_DECREF(val_obj);
                val_obj = NULL;
                if (conv_val == NULL)
                    break;

                /* Add to dict */
                if (PyDict_SetItem(dict, conv_key, conv_val) < 0)
                    break;

                Py_DECREF(conv_key);
                conv_key = NULL;
                Py_DECREF(conv_val);
                conv_val = NULL;
            }

            if (i < maph->count) {
                Py_DECREF(dict);
                if (path_key)
                    Py_DECREF(path_key);
                if (conv_key)
                    Py_DECREF(conv_key);
                if (conv_val)
                    Py_DECREF(conv_val);
                return NULL;
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

/* Comparison helper functions */

/* Helper: Compare integers with support for large unsigned values */
static PyObject *
compare_int_helper(fy_generic self_fyg, PyObject *other, int op)
{
    /* Check if we have decorated int (large unsigned) */
    fy_generic_decorated_int self_dint = fy_cast(self_fyg, fy_dint_empty);
    PyObject *self_pyobj = NULL, *other_pyobj = NULL;
    int use_python_cmp = 0;
    int cmp_result;
    long long self_val, other_val;

    /* Check if self needs Python comparison (large unsigned) */
    if (self_dint.flags & FYGDIF_UNSIGNED_RANGE_EXTEND) {
        self_pyobj = PyLong_FromUnsignedLongLong(self_dint.uv);
        if (!self_pyobj) return NULL;
        use_python_cmp = 1;
    } else {
        self_val = self_dint.sv;
    }

    /* Handle other operand */
    if (PyLong_Check(other)) {
        if (use_python_cmp) {
            /* Self is large unsigned, just use other as-is */
            Py_INCREF(other);
            other_pyobj = other;
        } else {
            /* Try to extract as long long */
            other_val = PyLong_AsLongLong(other);
            if (other_val == -1 && PyErr_Occurred()) {
                /* other is too large for long long - use Python comparison */
                PyErr_Clear();
                self_pyobj = PyLong_FromLongLong(self_val);
                if (!self_pyobj) return NULL;
                Py_INCREF(other);
                other_pyobj = other;
                use_python_cmp = 1;
            }
        }
    } else if (Py_TYPE(other) == &FyGenericType) {
        /* Check if other is a float - if so, promote to float comparison */
        enum fy_generic_type other_type = fy_get_type(((FyGenericObject *)other)->fyg);
        if (other_type == FYGT_FLOAT) {
            /* Promote self to float and use float comparison */
            double self_as_float = use_python_cmp ? PyLong_AsDouble(self_pyobj) : (double)self_val;
            if (self_as_float == -1.0 && PyErr_Occurred()) {
                Py_XDECREF(self_pyobj);
                return NULL;
            }

            PyObject *self_float = PyFloat_FromDouble(self_as_float);
            if (!self_float) {
                Py_XDECREF(self_pyobj);
                return NULL;
            }

            PyObject *other_float = FyGeneric_float((FyGenericObject *)other);
            if (!other_float) {
                Py_DECREF(self_float);
                Py_XDECREF(self_pyobj);
                return NULL;
            }

            PyObject *result = PyObject_RichCompare(self_float, other_float, op);
            Py_DECREF(self_float);
            Py_DECREF(other_float);
            Py_XDECREF(self_pyobj);
            return result;
        }

        /* Convert other FyGeneric to int using __int__ (handles type conversion) */
        PyObject *other_int = FyGeneric_int((FyGenericObject *)other);
        if (!other_int) {
            Py_XDECREF(self_pyobj);
            return NULL;
        }

        if (use_python_cmp) {
            /* Self is large unsigned, just use converted int */
            other_pyobj = other_int;
        } else {
            /* Try to extract as long long */
            other_val = PyLong_AsLongLong(other_int);
            if (other_val == -1 && PyErr_Occurred()) {
                /* Other is too large for long long - use Python comparison */
                PyErr_Clear();
                self_pyobj = PyLong_FromLongLong(self_val);
                if (!self_pyobj) {
                    Py_DECREF(other_int);
                    return NULL;
                }
                other_pyobj = other_int;
                use_python_cmp = 1;
            } else {
                Py_DECREF(other_int);
            }
        }
    } else {
        Py_XDECREF(self_pyobj);
        Py_RETURN_NOTIMPLEMENTED;
    }

    /* Perform comparison */
    if (use_python_cmp) {
        cmp_result = PyObject_RichCompareBool(self_pyobj, other_pyobj, op);
        Py_DECREF(self_pyobj);
        Py_DECREF(other_pyobj);
        if (cmp_result < 0) return NULL;
        if (cmp_result) Py_RETURN_TRUE;
        else Py_RETURN_FALSE;
    } else {
        /* Regular C long long comparison */
        int result;
        switch (op) {
            case Py_EQ: result = (self_val == other_val); break;
            case Py_NE: result = (self_val != other_val); break;
            case Py_LT: result = (self_val < other_val); break;
            case Py_LE: result = (self_val <= other_val); break;
            case Py_GT: result = (self_val > other_val); break;
            case Py_GE: result = (self_val >= other_val); break;
            default: Py_RETURN_NOTIMPLEMENTED;
        }
        if (result) Py_RETURN_TRUE;
        else Py_RETURN_FALSE;
    }
}

/* Helper: Compare floats */
static PyObject *
compare_float_helper(fy_generic self_fyg, PyObject *other, int op)
{
    double self_val = fy_cast(self_fyg, (double)0.0);
    double other_val;

    if (PyFloat_Check(other)) {
        other_val = PyFloat_AsDouble(other);
    } else if (PyLong_Check(other)) {
        other_val = (double)PyLong_AsLongLong(other);
    } else if (Py_TYPE(other) == &FyGenericType) {
        /* Convert other FyGeneric to float using __float__ (handles type conversion) */
        PyObject *other_float = FyGeneric_float((FyGenericObject *)other);
        if (!other_float)
            return NULL;
        other_val = PyFloat_AsDouble(other_float);
        Py_DECREF(other_float);
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    int result;
    switch (op) {
        case Py_EQ: result = (self_val == other_val); break;
        case Py_NE: result = (self_val != other_val); break;
        case Py_LT: result = (self_val < other_val); break;
        case Py_LE: result = (self_val <= other_val); break;
        case Py_GT: result = (self_val > other_val); break;
        case Py_GE: result = (self_val >= other_val); break;
        default: Py_RETURN_NOTIMPLEMENTED;
    }
    if (result) Py_RETURN_TRUE;
    else Py_RETURN_FALSE;
}

/* Helper: Compare strings (binary-safe) */
static PyObject *
compare_string_helper(fy_generic self_fyg, PyObject *other, int op)
{
    fy_generic_sized_string self_szstr = fy_cast(self_fyg, fy_szstr_empty);
    const char *other_str = NULL;
    Py_ssize_t other_size = 0;
    int cmp;
    PyObject *other_str_obj = NULL;

    if (PyUnicode_Check(other)) {
        other_str = PyUnicode_AsUTF8AndSize(other, &other_size);
        if (!other_str) return NULL;
    } else if (Py_TYPE(other) == &FyGenericType) {
        /* Convert other FyGeneric to string using __str__ (handles type conversion) */
        other_str_obj = FyGeneric_str((FyGenericObject *)other);
        if (!other_str_obj)
            return NULL;
        other_str = PyUnicode_AsUTF8AndSize(other_str_obj, &other_size);
        if (!other_str) {
            Py_DECREF(other_str_obj);
            return NULL;
        }
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    /* Compare using memcmp for binary safety */
    size_t min_size = self_szstr.size < (size_t)other_size ? self_szstr.size : (size_t)other_size;
    cmp = memcmp(self_szstr.data, other_str, min_size);
    if (cmp == 0 && self_szstr.size != (size_t)other_size)
        cmp = self_szstr.size < (size_t)other_size ? -1 : 1;

    int result;
    switch (op) {
        case Py_EQ: result = (cmp == 0); break;
        case Py_NE: result = (cmp != 0); break;
        case Py_LT: result = (cmp < 0); break;
        case Py_LE: result = (cmp <= 0); break;
        case Py_GT: result = (cmp > 0); break;
        case Py_GE: result = (cmp >= 0); break;
        default:
            Py_XDECREF(other_str_obj);
            Py_RETURN_NOTIMPLEMENTED;
    }
    Py_XDECREF(other_str_obj);
    if (result) Py_RETURN_TRUE;
    else Py_RETURN_FALSE;
}

/* Helper: Compare booleans */
static PyObject *
compare_bool_helper(fy_generic self_fyg, PyObject *other, int op)
{
    _Bool self_val = fy_cast(self_fyg, (_Bool)0);
    _Bool other_val;

    if (PyBool_Check(other)) {
        other_val = (other == Py_True);
    } else if (PyLong_Check(other)) {
        /* Promote to int comparison: bool(true) == int(1) should be True */
        long long self_as_int = self_val ? 1 : 0;
        PyObject *self_int = PyLong_FromLongLong(self_as_int);
        if (!self_int)
            return NULL;
        PyObject *result = PyObject_RichCompare(self_int, other, op);
        Py_DECREF(self_int);
        return result;
    } else if (PyFloat_Check(other)) {
        /* Promote to float comparison: bool(true) == float(1.0) should be True */
        double self_as_float = self_val ? 1.0 : 0.0;
        PyObject *self_float = PyFloat_FromDouble(self_as_float);
        if (!self_float)
            return NULL;
        PyObject *result = PyObject_RichCompare(self_float, other, op);
        Py_DECREF(self_float);
        return result;
    } else if (Py_TYPE(other) == &FyGenericType) {
        enum fy_generic_type other_type = fy_get_type(((FyGenericObject *)other)->fyg);
        if (other_type == FYGT_INT) {
            /* Promote to int comparison */
            long long self_as_int = self_val ? 1 : 0;
            PyObject *self_int = PyLong_FromLongLong(self_as_int);
            if (!self_int)
                return NULL;
            PyObject *other_int = FyGeneric_int((FyGenericObject *)other);
            if (!other_int) {
                Py_DECREF(self_int);
                return NULL;
            }
            PyObject *result = PyObject_RichCompare(self_int, other_int, op);
            Py_DECREF(self_int);
            Py_DECREF(other_int);
            return result;
        } else if (other_type == FYGT_FLOAT) {
            /* Promote to float comparison */
            double self_as_float = self_val ? 1.0 : 0.0;
            PyObject *self_float = PyFloat_FromDouble(self_as_float);
            if (!self_float)
                return NULL;
            PyObject *other_float = FyGeneric_float((FyGenericObject *)other);
            if (!other_float) {
                Py_DECREF(self_float);
                return NULL;
            }
            PyObject *result = PyObject_RichCompare(self_float, other_float, op);
            Py_DECREF(self_float);
            Py_DECREF(other_float);
            return result;
        } else if (other_type == FYGT_BOOL) {
            other_val = fy_cast(((FyGenericObject *)other)->fyg, (_Bool)0);
        } else {
            Py_RETURN_NOTIMPLEMENTED;
        }
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    int result;
    switch (op) {
        case Py_EQ: result = (self_val == other_val); break;
        case Py_NE: result = (self_val != other_val); break;
        case Py_LT: result = (self_val < other_val); break;
        case Py_LE: result = (self_val <= other_val); break;
        case Py_GT: result = (self_val > other_val); break;
        case Py_GE: result = (self_val >= other_val); break;
        default: Py_RETURN_NOTIMPLEMENTED;
    }
    if (result) Py_RETURN_TRUE;
    else Py_RETURN_FALSE;
}

/* FyGeneric: __richcompare__ - implements ==, !=, <, <=, >, >= */
static PyObject *
FyGeneric_richcompare(PyObject *self, PyObject *other, int op)
{
    FyGenericObject *self_obj = (FyGenericObject *)self;
    enum fy_generic_type self_type = fy_get_type(self_obj->fyg);

    /* Dispatch to type-specific comparison helper */
    switch (self_type) {
        case FYGT_INT:
            return compare_int_helper(self_obj->fyg, other, op);
        case FYGT_FLOAT:
            return compare_float_helper(self_obj->fyg, other, op);
        case FYGT_STRING:
            return compare_string_helper(self_obj->fyg, other, op);
        case FYGT_BOOL:
            return compare_bool_helper(self_obj->fyg, other, op);
        default:
            Py_RETURN_NOTIMPLEMENTED;
    }
}

/* Mapping-specific methods */

/* FyGeneric: keys() - return list of keys for mappings */
static PyObject *
FyGeneric_keys(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    if (!fy_generic_is_mapping(self->fyg)) {
        PyErr_SetString(PyExc_TypeError, "keys() requires a mapping");
        return NULL;
    }

    size_t i, count;
    const fy_generic_map_pair *pairs = fy_generic_mapping_get_pairs(self->fyg, &count);

    PyObject *result = PyList_New(count);
    if (result == NULL)
        return NULL;

    for (i = 0; i < count; i++) {
        PyObject *path_key = fy_generic_to_python_primitive(pairs[i].key);
        if (path_key == NULL)
            break;

        PyObject *key = FyGeneric_from_parent(pairs[i].key, self, path_key);
        Py_DECREF(path_key);
        if (key == NULL)
            break;

        PyList_SET_ITEM(result, i, key);
    }
    if (i < count) {
        Py_DECREF(result);
        return NULL;
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

    size_t i, count;
    const fy_generic_map_pair *pairs = fy_generic_mapping_get_pairs(self->fyg, &count);

    PyObject *result = PyList_New(count);
    if (result == NULL)
        return NULL;

    for (i = 0; i < count; i++) {
        PyObject *path_key = fy_generic_to_python_primitive(pairs[i].key);
        if (path_key == NULL)
            break;

        PyObject *value = FyGeneric_from_parent(pairs[i].value, self, path_key);
        Py_DECREF(path_key);
        if (value == NULL)
            break;

        PyList_SET_ITEM(result, i, value);
    }
    if (i < count) {
        Py_DECREF(result);
        return NULL;
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

    size_t i, count;
    const fy_generic_map_pair *pairs = fy_generic_mapping_get_pairs(self->fyg, &count);

    PyObject *result = PyList_New(count);
    if (result == NULL)
        return NULL;

    PyObject *path_key = NULL, *key = NULL, *value = NULL, *tuple = NULL;

    for (i = 0; i < count; i++) {
        path_key = fy_generic_to_python_primitive(pairs[i].key);
        if (path_key == NULL)
            break;

        key = FyGeneric_from_parent(pairs[i].key, self, path_key);
        if (key == NULL)
            break;

        value = FyGeneric_from_parent(pairs[i].value, self, path_key);
        Py_DECREF(path_key);  /* Done with path_key */
        path_key = NULL;
        if (value == NULL)
            break;

        tuple = PyTuple_Pack(2, key, value);
        Py_DECREF(key);
        key = NULL;
        Py_DECREF(value);
        value = NULL;
        if (tuple == NULL)
            break;

        PyList_SET_ITEM(result, i, tuple);
    }
    if (i < count) {
        if (path_key)
            Py_DECREF(path_key);
        if (key)
            Py_DECREF(key);
        return NULL;
    }

    return result;
}

/* Helper: Convert FyGeneric primitive types to Python objects
 * Returns a new reference to a Python object for primitive types (null, bool, int, float, string)
 * Returns NULL for complex types (sequence, mapping) - caller should handle these separately
 */
static PyObject *
fy_generic_to_python_primitive_or_null(FyGenericObject *self)
{
    enum fy_generic_type type = fy_get_type(self->fyg);

    switch (type) {
        case FYGT_NULL:
            Py_INCREF(Py_None);
            return Py_None;

        case FYGT_BOOL: {
            PyObject *py_obj = fy_cast(self->fyg, (_Bool)0) ? Py_True : Py_False;
            Py_INCREF(py_obj);
            return py_obj;
        }

        case FYGT_INT:
            return PyLong_FromLongLong(fy_cast(self->fyg, (long long)0));

        case FYGT_FLOAT:
            return PyFloat_FromDouble(fy_cast(self->fyg, (double)0.0));

        case FYGT_STRING: {
            fy_generic_sized_string szstr = fy_cast(self->fyg, fy_szstr_empty);
            return PyUnicode_FromStringAndSize(szstr.data, szstr.size);
        }

        default:
            /* Not a primitive type */
            return NULL;
    }
}

/* FyGeneric: __format__ */
static PyObject *
FyGeneric_format(FyGenericObject *self, PyObject *format_spec)
{
    /* Try primitive conversion first */
    PyObject *py_obj = fy_generic_to_python_primitive_or_null(self);

    if (py_obj == NULL) {
        /* For complex types (sequence, mapping), use to_python() */
        py_obj = FyGeneric_to_python(self, NULL);
        if (py_obj == NULL)
            return NULL;
    }

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

    /* Try primitive conversion first */
    PyObject *py_obj = fy_generic_to_python_primitive_or_null(self);

    if (py_obj == NULL) {
        /* Handle complex types */
        enum fy_generic_type type = fy_get_type(self->fyg);

        switch (type) {
            case FYGT_SEQUENCE:
            case FYGT_MAPPING:
                /* Delegate to list/dict methods - convert to Python list/dict */
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
    }

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
    if (self->gb)
        fy_gb_trim(self->gb);
    Py_RETURN_NONE;
}

/* FyGeneric: clone() - Create a clone of this FyGeneric object */
static PyObject *
FyGeneric_clone(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    /* Get the root object for mutable flag and builder config */
    FyGenericObject *root_obj = self->root ? (FyGenericObject *)self->root : self;

    /* Root objects always have a valid builder */
    assert(root_obj->gb != NULL);

    /* Get parent's config and copy it, but set allocator to NULL for new instance */
    const struct fy_generic_builder_cfg *parent_cfg = fy_generic_builder_get_cfg(root_obj->gb);
    assert(parent_cfg != NULL);  /* Can't fail if gb is not NULL */

    struct fy_generic_builder_cfg cfg = *parent_cfg;
    cfg.allocator = NULL;  /* Force creation of new allocator (and new tags) */
    cfg.parent = NULL;     /* Independent builder, no parent chain */

    struct fy_generic_builder *new_gb = fy_generic_builder_create(&cfg);

    if (new_gb == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create builder for clone");
        return NULL;
    }

    /* Internalize (copy) the generic value from THIS object (not root) */
    /* This creates a new root starting from the current value */
    fy_generic cloned = fy_gb_internalize(new_gb, self->fyg);
    if (fy_generic_is_invalid(cloned)) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to clone generic value");
        goto err_out;
    }

    /* Create a new FyGeneric Python object with the cloned value */
    PyObject *result = FyGeneric_from_generic(cloned, new_gb, root_obj->mutable);
    if (result == NULL)
        goto err_out;

    return result;

err_out:
    fy_generic_builder_destroy(new_gb);
    return NULL;
}

/* FyGeneric: get_path() - Get the path from root to this object */
static PyObject *
FyGeneric_get_path(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    /* If this is root, return empty tuple */
    if (self->root == NULL) {
        return PyTuple_New(0);
    }

    /* Return the path tuple (always available for nested values) */
    if (self->path == NULL) {
        /* Should not happen since we always build paths now */
        Py_RETURN_NONE;
    }

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

        /* Check for None and reject it */
        if (elem == Py_None) {
            Py_DECREF(elem);
            PyErr_SetString(PyExc_TypeError, "Path elements cannot be None");
            return NULL;
        }
        /* Check for bool BEFORE int (bool is subclass of int in Python) */
        else if (PyBool_Check(elem)) {
            bool val = (elem == Py_True);
            Py_DECREF(elem);
            path_array[i] = fy_value(val);
        }
        else if (PyLong_Check(elem)) {
            long idx = PyLong_AsLong(elem);
            Py_DECREF(elem);
            if (idx == -1 && PyErr_Occurred())
                return NULL;
            path_array[i] = fy_value((int)idx);
        } else if (PyFloat_Check(elem)) {
            double val = PyFloat_AS_DOUBLE(elem);
            Py_DECREF(elem);
            path_array[i] = fy_value(val);
        } else if (PyUnicode_Check(elem)) {
            const char *str = PyUnicode_AsUTF8(elem);
            Py_DECREF(elem);
            if (str == NULL)
                return NULL;
            path_array[i] = fy_value(str);
        } else {
            Py_DECREF(elem);
            PyErr_SetString(PyExc_TypeError, "Path elements must be integers, floats, booleans, or strings");
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

    /* Build the path as a Python tuple for the child */
    /* Create tuple directly - no intermediate list needed */
    if (self->mutable && path_len > 0) {
        child->path = PyTuple_New(path_len);
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
            PyTuple_SET_ITEM(child->path, i, elem);
        }
    } else {
        child->path = NULL;
    }

    return (PyObject *)child;
}

/* Internal: Convert path list to Unix-style path string */
static PyObject *
path_list_to_unix_path_internal(PyObject *path_list)
{
    if (!PyList_Check(path_list) && !PyTuple_Check(path_list)) {
        PyErr_SetString(PyExc_TypeError, "Path must be a list or tuple");
        return NULL;
    }

    Py_ssize_t path_len = PySequence_Size(path_list);
    if (path_len < 0)
        return NULL;

    /* Empty list returns "/" */
    if (path_len == 0) {
        return PyUnicode_FromString("/");
    }

    /* Build the Unix path string */
    PyObject *parts = PyList_New(0);
    if (parts == NULL)
        return NULL;

    for (Py_ssize_t i = 0; i < path_len; i++) {
        PyObject *elem = PySequence_GetItem(path_list, i);
        if (elem == NULL) {
            Py_DECREF(parts);
            return NULL;
        }

        PyObject *str_elem;
        if (PyLong_Check(elem)) {
            /* Convert integer index to string */
            str_elem = PyObject_Str(elem);
            Py_DECREF(elem);
        } else if (PyUnicode_Check(elem)) {
            str_elem = elem;  /* Already a string, transfer ownership */
        } else {
            /* Unknown type - convert to string */
            str_elem = PyObject_Str(elem);
            Py_DECREF(elem);
        }

        if (str_elem == NULL) {
            Py_DECREF(parts);
            return NULL;
        }

        if (PyList_Append(parts, str_elem) < 0) {
            Py_DECREF(str_elem);
            Py_DECREF(parts);
            return NULL;
        }
        Py_DECREF(str_elem);
    }

    /* Join with "/" */
    PyObject *sep = PyUnicode_FromString("/");
    if (sep == NULL) {
        Py_DECREF(parts);
        return NULL;
    }

    PyObject *result = PyUnicode_Join(sep, parts);
    Py_DECREF(sep);
    Py_DECREF(parts);

    if (result == NULL)
        return NULL;

    /* Prepend "/" */
    PyObject *slash = PyUnicode_FromString("/");
    if (slash == NULL) {
        Py_DECREF(result);
        return NULL;
    }

    PyObject *final = PyUnicode_Concat(slash, result);
    Py_DECREF(slash);
    Py_DECREF(result);

    return final;
}

/* Internal: Convert Unix-style path string to path list */
static PyObject *
unix_path_to_path_list_internal(const char *path_cstr)
{
    /* Handle empty string or just "/" as root (empty list) */
    if (path_cstr[0] == '\0' || (path_cstr[0] == '/' && path_cstr[1] == '\0')) {
        return PyList_New(0);
    }

    /* Path must start with "/" */
    if (path_cstr[0] != '/') {
        PyErr_SetString(PyExc_ValueError, "Unix path must start with '/'");
        return NULL;
    }

    /* Skip leading "/" and split by "/" */
    PyObject *path_without_slash = PyUnicode_FromString(path_cstr + 1);
    if (path_without_slash == NULL)
        return NULL;

    PyObject *sep = PyUnicode_FromString("/");
    if (sep == NULL) {
        Py_DECREF(path_without_slash);
        return NULL;
    }

    PyObject *parts = PyUnicode_Split(path_without_slash, sep, -1);
    Py_DECREF(sep);
    Py_DECREF(path_without_slash);

    if (parts == NULL)
        return NULL;

    /* Convert string parts to proper types (int if numeric, string otherwise) */
    Py_ssize_t parts_len = PyList_Size(parts);
    PyObject *path_list = PyList_New(parts_len);
    if (path_list == NULL) {
        Py_DECREF(parts);
        return NULL;
    }

    for (Py_ssize_t i = 0; i < parts_len; i++) {
        PyObject *part = PyList_GET_ITEM(parts, i);
        const char *part_str = PyUnicode_AsUTF8(part);
        if (part_str == NULL) {
            Py_DECREF(path_list);
            Py_DECREF(parts);
            return NULL;
        }

        /* Try to parse as integer */
        char *endptr;
        long idx = strtol(part_str, &endptr, 10);
        if (*endptr == '\0' && *part_str != '\0') {
            /* Successfully parsed as integer */
            PyObject *idx_obj = PyLong_FromLong(idx);
            if (idx_obj == NULL) {
                Py_DECREF(path_list);
                Py_DECREF(parts);
                return NULL;
            }
            PyList_SET_ITEM(path_list, i, idx_obj);
        } else {
            /* Keep as string */
            Py_INCREF(part);
            PyList_SET_ITEM(path_list, i, part);
        }
    }

    Py_DECREF(parts);
    return path_list;
}

/* Module-level: path_list_to_unix_path(path_list) - Convert path list to Unix-style string */
static PyObject *
libfyaml_path_list_to_unix_path(PyObject *Py_UNUSED(self), PyObject *path_list)
{
    return path_list_to_unix_path_internal(path_list);
}

/* Module-level: unix_path_to_path_list(unix_path) - Convert Unix-style string to path list */
static PyObject *
libfyaml_unix_path_to_path_list(PyObject *Py_UNUSED(self), PyObject *unix_path)
{
    if (!PyUnicode_Check(unix_path)) {
        PyErr_SetString(PyExc_TypeError, "Path must be a string");
        return NULL;
    }

    const char *path_cstr = PyUnicode_AsUTF8(unix_path);
    if (path_cstr == NULL)
        return NULL;

    return unix_path_to_path_list_internal(path_cstr);
}

/* FyGeneric: get_unix_path() - Get the path as a Unix-style string */
static PyObject *
FyGeneric_get_unix_path(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    /* If this is root, return "/" */
    if (self->root == NULL) {
        return PyUnicode_FromString("/");
    }

    /* Get the path list */
    if (self->path == NULL) {
        /* Should not happen since we always build paths now */
        Py_RETURN_NONE;
    }

    /* Use internal converter */
    return path_list_to_unix_path_internal(self->path);
}

/* FyGeneric: get_at_unix_path(path_string) - Get value at Unix-style path (root only) */
static PyObject *
FyGeneric_get_at_unix_path(FyGenericObject *self, PyObject *path_str)
{
    /* This method only works on root objects */
    if (self->root != NULL) {
        PyErr_SetString(PyExc_TypeError,
                      "get_at_unix_path() can only be called on root FyGeneric objects");
        return NULL;
    }

    if (!PyUnicode_Check(path_str)) {
        PyErr_SetString(PyExc_TypeError, "Path must be a string");
        return NULL;
    }

    const char *path_cstr = PyUnicode_AsUTF8(path_str);
    if (path_cstr == NULL)
        return NULL;

    /* Handle empty string or just "/" as root */
    if (path_cstr[0] == '\0' || (path_cstr[0] == '/' && path_cstr[1] == '\0')) {
        Py_INCREF(self);
        return (PyObject *)self;
    }

    /* Convert Unix path to path list using internal converter */
    PyObject *path_list = unix_path_to_path_list_internal(path_cstr);
    if (path_list == NULL)
        return NULL;

    /* Call get_at_path with the parsed path list */
    PyObject *result = FyGeneric_get_at_path(self, path_list);
    Py_DECREF(path_list);

    return result;
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

        /* Check for None and reject it */
        if (elem == Py_None) {
            Py_DECREF(elem);
            PyErr_SetString(PyExc_TypeError, "Path elements cannot be None");
            return NULL;
        }
        /* Check for bool BEFORE int (bool is subclass of int in Python) */
        else if (PyBool_Check(elem)) {
            bool val = (elem == Py_True);
            Py_DECREF(elem);
            path_array[i] = fy_value(val);
        }
        else if (PyLong_Check(elem)) {
            long idx = PyLong_AsLong(elem);
            Py_DECREF(elem);
            if (idx == -1 && PyErr_Occurred())
                return NULL;
            path_array[i] = fy_value((int)idx);
        } else if (PyFloat_Check(elem)) {
            double val = PyFloat_AS_DOUBLE(elem);
            Py_DECREF(elem);
            path_array[i] = fy_value(val);
        } else if (PyUnicode_Check(elem)) {
            const char *str = PyUnicode_AsUTF8(elem);
            Py_DECREF(elem);
            if (str == NULL)
                return NULL;
            path_array[i] = fy_value(str);
        } else {
            Py_DECREF(elem);
            PyErr_SetString(PyExc_TypeError, "Path elements must be integers, floats, booleans, or strings");
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

/* FyGeneric: set_at_unix_path(path_string, value) - Set value at Unix-style path (root only) */
static PyObject *
FyGeneric_set_at_unix_path(FyGenericObject *self, PyObject *args)
{
    PyObject *path_str, *value_obj;

    if (!PyArg_ParseTuple(args, "OO", &path_str, &value_obj))
        return NULL;

    /* This method only works on root objects */
    if (self->root != NULL) {
        PyErr_SetString(PyExc_TypeError,
                      "set_at_unix_path() can only be called on root FyGeneric objects");
        return NULL;
    }

    if (!PyUnicode_Check(path_str)) {
        PyErr_SetString(PyExc_TypeError, "Path must be a string");
        return NULL;
    }

    const char *path_cstr = PyUnicode_AsUTF8(path_str);
    if (path_cstr == NULL)
        return NULL;

    /* Cannot set at root */
    if (path_cstr[0] == '\0' || (path_cstr[0] == '/' && path_cstr[1] == '\0')) {
        PyErr_SetString(PyExc_ValueError, "Cannot set value at root path '/'");
        return NULL;
    }

    /* Convert Unix path to path list using internal converter */
    PyObject *path_list = unix_path_to_path_list_internal(path_cstr);
    if (path_list == NULL)
        return NULL;

    /* Create tuple of (path_list, value_obj) for set_at_path */
    PyObject *set_args = PyTuple_Pack(2, path_list, value_obj);
    Py_DECREF(path_list);

    if (set_args == NULL)
        return NULL;

    /* Call set_at_path with the converted path list */
    PyObject *result = FyGeneric_set_at_path(self, set_args);
    Py_DECREF(set_args);

    return result;
}

/* FyGeneric method table */

/* Helper: Build emit flags for dump operations */
static unsigned int
build_emit_flags(int json_mode, int compact, int multi_document, int strip_newline)
{
    unsigned int emit_flags = FYOPEF_DISABLE_DIRECTORY;

    if (multi_document)
        emit_flags |= FYOPEF_MULTI_DOCUMENT;

    if (json_mode) {
        emit_flags |= FYOPEF_MODE_JSON;
        if (!compact)
            emit_flags |= FYOPEF_INDENT_2;
    } else {
        emit_flags |= FYOPEF_MODE_YAML_1_2;
        if (compact) {
            emit_flags |= multi_document ? FYOPEF_STYLE_FLOW : FYOPEF_STYLE_ONELINE;
        } else {
            emit_flags |= FYOPEF_STYLE_BLOCK;
        }
    }

    if (strip_newline)
        emit_flags |= FYOPEF_NO_ENDING_NEWLINE;

    return emit_flags;
}

/* Helper: Write string to file object */
static int
write_to_file_object(PyObject *file_obj, PyObject *content_str)
{
    PyObject *result = PyObject_CallMethod(file_obj, "write", "O", content_str);
    if (result == NULL)
        return -1;
    Py_DECREF(result);
    return 0;
}

/* FyGeneric: dump(file=None, mode='yaml', compact=False, strip_newline=False) - Dump to file or return string */
static PyObject *
FyGeneric_dump(FyGenericObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *file_obj = NULL;
    const char *mode = "yaml";
    int compact = 0;
    int strip_newline = 0;
    static char *kwlist[] = {"file", "mode", "compact", "strip_newline", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|Ospp", kwlist,
                                     &file_obj, &mode, &compact, &strip_newline))
        return NULL;

    int json_mode = (strcmp(mode, "json") == 0);

    /* Get root object for builder access */
    FyGenericObject *root_obj = (self->root == NULL) ? self : (FyGenericObject *)self->root;
    if (!root_obj->gb) {
        PyErr_SetString(PyExc_RuntimeError, "No builder available");
        return NULL;
    }

    /* Build emit flags */
    unsigned int emit_flags = build_emit_flags(json_mode, compact, 0, strip_newline);

    /* If no file specified, return as string (like dumps()) */
    if (file_obj == NULL || file_obj == Py_None) {
        emit_flags |= FYOPEF_OUTPUT_TYPE_STRING;

        /* Emit to string */
        fy_generic emitted = fy_gb_emit(root_obj->gb, self->fyg, emit_flags, NULL);
        if (!fy_generic_is_valid(emitted)) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to emit YAML/JSON");
            return NULL;
        }

        /* Extract the sized string from the generic */
        fy_generic_sized_string szstr = fy_cast(emitted, fy_szstr_empty);
        if (szstr.data == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to extract string from emitted generic");
            return NULL;
        }

        /* Create Python string (makes a copy) */
        return PyUnicode_FromStringAndSize(szstr.data, szstr.size);
    }

    /* Check if it's a file path (string) */
    if (PyUnicode_Check(file_obj)) {
        const char *path = PyUnicode_AsUTF8(file_obj);
        if (path == NULL)
            return NULL;

        /* Emit to file using fy_gb_emit_file() */
        fy_generic result_g = fy_gb_emit_file(root_obj->gb, self->fyg, emit_flags, path);

        /* Check for success: should return integer 0 */
        if (!fy_generic_is_valid(result_g) || !fy_generic_is_int_type(result_g)) {
            PyErr_Format(PyExc_RuntimeError, "Failed to emit YAML/JSON to file: %s", path);
            return NULL;
        }

        int result_code = fy_cast(result_g, -1);
        if (result_code != 0) {
            PyErr_Format(PyExc_RuntimeError, "Failed to emit YAML/JSON to file: %s (error code: %d)",
                        path, result_code);
            return NULL;
        }

        Py_RETURN_NONE;
    }

    /* Assume it's a file object */
    /* Note: stdout/stderr detection could be done via PyObject_AsFileDescriptor()
     * for future optimization with direct emitter API, but for now we emit to
     * string and write to the file object which works for all file-like objects */
    emit_flags |= FYOPEF_OUTPUT_TYPE_STRING;

    fy_generic emitted = fy_gb_emit(root_obj->gb, self->fyg, emit_flags, NULL);
    if (!fy_generic_is_valid(emitted)) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to emit YAML/JSON");
        return NULL;
    }

    /* Extract the sized string from the generic */
    fy_generic_sized_string szstr = fy_cast(emitted, fy_szstr_empty);
    if (szstr.data == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to extract string from emitted generic");
        return NULL;
    }

    /* Create Python string */
    PyObject *yaml_str = PyUnicode_FromStringAndSize(szstr.data, szstr.size);
    if (yaml_str == NULL)
        return NULL;

    /* Write to file object */
    if (write_to_file_object(file_obj, yaml_str) < 0) {
        Py_DECREF(yaml_str);
        return NULL;
    }

    Py_DECREF(yaml_str);
    Py_RETURN_NONE;
}

static PyMethodDef FyGeneric_methods[] = {
    {"to_python", _PyCFunction_CAST(FyGeneric_to_python), METH_NOARGS,
     "Convert to Python object (recursive)"},
    {"dump", _PyCFunction_CAST(FyGeneric_dump), METH_VARARGS | METH_KEYWORDS,
     "Dump to file or return as string"},
    {"trim", _PyCFunction_CAST(FyGeneric_trim), METH_NOARGS,
     "Trim allocator to release unused memory"},
    {"clone", _PyCFunction_CAST(FyGeneric_clone), METH_NOARGS,
     "Create a clone of this FyGeneric object"},
    {"get_path", _PyCFunction_CAST(FyGeneric_get_path), METH_NOARGS,
     "Get the path from root to this object"},
    {"get_at_path", _PyCFunction_CAST(FyGeneric_get_at_path), METH_O,
     "Get value at path (root only)"},
    {"get_unix_path", _PyCFunction_CAST(FyGeneric_get_unix_path), METH_NOARGS,
     "Get the path as a Unix-style string (e.g., '/server/host')"},
    {"get_at_unix_path", _PyCFunction_CAST(FyGeneric_get_at_unix_path), METH_O,
     "Get value at Unix-style path (root only)"},
    {"set_at_path", _PyCFunction_CAST(FyGeneric_set_at_path), METH_VARARGS,
     "Set value at path (root only)"},
    {"set_at_unix_path", _PyCFunction_CAST(FyGeneric_set_at_unix_path), METH_VARARGS,
     "Set value at Unix-style path (root only)"},
    {"__format__", _PyCFunction_CAST(FyGeneric_format), METH_O,
     "Format the value according to format specification"},
    {"is_null", _PyCFunction_CAST(FyGeneric_is_null), METH_NOARGS,
     "Check if value is null"},
    {"is_bool", _PyCFunction_CAST(FyGeneric_is_bool), METH_NOARGS,
     "Check if value is boolean"},
    {"is_int", _PyCFunction_CAST(FyGeneric_is_int), METH_NOARGS,
     "Check if value is integer"},
    {"is_float", _PyCFunction_CAST(FyGeneric_is_float), METH_NOARGS,
     "Check if value is float"},
    {"is_string", _PyCFunction_CAST(FyGeneric_is_string), METH_NOARGS,
     "Check if value is string"},
    {"is_sequence", _PyCFunction_CAST(FyGeneric_is_sequence), METH_NOARGS,
     "Check if value is sequence"},
    {"is_mapping", _PyCFunction_CAST(FyGeneric_is_mapping), METH_NOARGS,
     "Check if value is mapping"},
    {"keys", _PyCFunction_CAST(FyGeneric_keys), METH_NOARGS,
     "Return list of keys (for mappings)"},
    {"values", _PyCFunction_CAST(FyGeneric_values), METH_NOARGS,
     "Return list of values (for mappings)"},
    {"items", _PyCFunction_CAST(FyGeneric_items), METH_NOARGS,
     "Return list of (key, value) tuples (for mappings)"},
    {NULL}
};

/* FyGeneric: __contains__ - implements 'in' operator */
static int
FyGeneric_contains(FyGenericObject *self, PyObject *key)
{
    enum fy_generic_type type = fy_get_type(self->fyg);

    if (type != FYGT_MAPPING && type != FYGT_SEQUENCE) {
        /* For non-container types, return not implemented */
        PyErr_SetString(PyExc_TypeError, "argument of type 'FyGeneric' is not iterable");
        return -1;
    }

    /* Get root object for builder access */
    FyGenericObject *root_obj = (self->root == NULL) ? self : (FyGenericObject *)self->root;
    assert(root_obj->gb != NULL);

    /* Convert Python key to generic */
    fy_generic key_generic = python_to_generic(root_obj->gb, key);
    if (fy_generic_is_invalid(key_generic)) {
        /* Conversion failed */
        PyErr_Clear();
        return 0;
    }

    /* For mapping, check if key exists (OP_CONTAINS doesn't work for mappings) */
    if (type == FYGT_MAPPING) {
        fy_generic result = fy_generic_mapping_get_generic_default(self->fyg, key_generic, fy_invalid);
        return fy_generic_is_invalid(result) ? 0 : 1;
    }

    /* For sequence, use OP_CONTAINS operation */
    fy_generic result = fy_gb_contains(root_obj->gb, self->fyg, key_generic);
    if (fy_generic_is_invalid(result)) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to perform contains operation");
        return -1;
    }

    /* Extract boolean result */
    return fy_cast(result, (_Bool)false) ? 1 : 0;
}

/* FyGeneric as sequence */
static PySequenceMethods FyGeneric_as_sequence = {
    .sq_length = (lenfunc)FyGeneric_length,
    .sq_contains = (objobjproc)FyGeneric_contains,
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
    Py_ssize_t existing_path_len = self->path ? PyTuple_Size(self->path) : 0;
    Py_ssize_t total_path_len = existing_path_len + 1;  /* +1 for new key */
    Py_ssize_t total_len = total_path_len + 1;  /* +1 for value */

    /* Allocate path array on stack */
    fy_generic *path_array = alloca(sizeof(fy_generic) * total_len);

    /* Copy existing path elements */
    for (Py_ssize_t i = 0; i < existing_path_len; i++) {
        PyObject *elem = PyTuple_GET_ITEM(self->path, i);
        if (PyBool_Check(elem)) {
            bool val = (elem == Py_True);
            path_array[i] = fy_value(val);
        } else if (PyLong_Check(elem)) {
            long idx = PyLong_AsLong(elem);
            path_array[i] = fy_value((int)idx);
        } else if (PyFloat_Check(elem)) {
            double val = PyFloat_AS_DOUBLE(elem);
            path_array[i] = fy_value(val);
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

/* Structure to hold extracted numeric operand */
typedef struct {
    int is_int;              /* 1 if integer, 0 if float */
    int is_unsigned_large;   /* 1 if unsigned value > LLONG_MAX */
    long long int_val;       /* signed integer value (if is_int && !is_unsigned_large) */
    unsigned long long uint_val; /* unsigned integer value (if is_unsigned_large) */
    double float_val;        /* float value (if !is_int) */
} numeric_operand;

/* Helper: Extract numeric operand from FyGeneric or Python object
 * Returns: 0 on success, -1 on error (sets exception), -2 for NOTIMPLEMENTED, -3 for overflow (returns Python object) */
static int
extract_numeric_operand(PyObject *obj, numeric_operand *operand, const char *op_name, PyObject **py_obj)
{
    /* Initialize to zero */
    operand->is_int = 0;
    operand->is_unsigned_large = 0;
    operand->int_val = 0;
    operand->uint_val = 0;
    operand->float_val = 0.0;
    *py_obj = NULL;

    /* Handle FyGeneric objects */
    if (Py_TYPE(obj) == &FyGenericType) {
        enum fy_generic_type type = fy_get_type(((FyGenericObject *)obj)->fyg);
        if (type == FYGT_INT) {
            /* Get decorated int to check if it's unsigned and large */
            fy_generic_decorated_int dint = fy_cast(((FyGenericObject *)obj)->fyg, fy_dint_empty);

            if (dint.flags & FYGDIF_UNSIGNED_RANGE_EXTEND) {
                /* Unsigned value > LLONG_MAX - convert to Python for arbitrary precision */
                *py_obj = PyLong_FromUnsignedLongLong(dint.uv);
                if (!*py_obj)
                    return -1;
                return -3;  /* Overflow - use Python object */
            } else {
                /* Regular signed long long */
                operand->int_val = dint.sv;
                operand->is_int = 1;
                return 0;
            }
        } else if (type == FYGT_FLOAT) {
            operand->float_val = fy_cast(((FyGenericObject *)obj)->fyg, (double)0.0);
            operand->is_int = 0;
            return 0;
        } else if (type == FYGT_BOOL) {
            /* Convert bool to int: true=1, false=0 */
            operand->int_val = fy_cast(((FyGenericObject *)obj)->fyg, (_Bool)false) ? 1 : 0;
            operand->is_int = 1;
            return 0;
        } else {
            PyErr_Format(PyExc_TypeError, "unsupported operand type(s) for %s", op_name);
            return -1;
        }
    }

    /* Handle Python int */
    if (PyLong_Check(obj)) {
        /* Try to extract as long long first */
        long long val = PyLong_AsLongLong(obj);
        if (val == -1 && PyErr_Occurred()) {
            PyErr_Clear();
            /* Might be too large - try unsigned long long */
            unsigned long long uval = PyLong_AsUnsignedLongLong(obj);
            if (uval == (unsigned long long)-1 && PyErr_Occurred()) {
                /* Overflow even for unsigned long long - use Python's arbitrary precision */
                PyErr_Clear();
                *py_obj = obj;
                Py_INCREF(obj);
                return -3;  /* Overflow - use Python object */
            }
            /* Check if it fits in unsigned but not signed range */
            if (uval > (unsigned long long)LLONG_MAX) {
                operand->uint_val = uval;
                operand->is_int = 1;
                operand->is_unsigned_large = 1;
                return 0;
            }
            /* It fits in signed range despite overflow - shouldn't happen */
            operand->int_val = (long long)uval;
            operand->is_int = 1;
            return 0;
        }
        operand->int_val = val;
        operand->is_int = 1;
        return 0;
    }

    /* Handle Python float */
    if (PyFloat_Check(obj)) {
        operand->float_val = PyFloat_AsDouble(obj);
        operand->is_int = 0;
        return 0;
    }

    /* Unsupported type */
    return -2;  /* Special code for NOTIMPLEMENTED */
}

/* FyGeneric: __add__ */
static PyObject *
FyGeneric_add(PyObject *left, PyObject *right)
{
    numeric_operand left_op, right_op;
    PyObject *left_py = NULL, *right_py = NULL, *result = NULL;
    int rc;

    /* Extract left operand */
    rc = extract_numeric_operand(left, &left_op, "+", &left_py);
    if (rc == -1)
        return NULL;  /* Error already set */
    if (rc == -2)
        Py_RETURN_NOTIMPLEMENTED;

    /* Extract right operand */
    rc = extract_numeric_operand(right, &right_op, "+", &right_py);
    if (rc == -1) {
        Py_XDECREF(left_py);
        return NULL;  /* Error already set */
    }
    if (rc == -2) {
        Py_XDECREF(left_py);
        Py_RETURN_NOTIMPLEMENTED;
    }

    /* If either operand overflowed, use Python's arbitrary precision */
    if (left_py || right_py) {
        if (!left_py) {
            if (left_op.is_unsigned_large)
                left_py = PyLong_FromUnsignedLongLong(left_op.uint_val);
            else
                left_py = PyLong_FromLongLong(left_op.int_val);
        }
        if (!right_py) {
            if (right_op.is_unsigned_large)
                right_py = PyLong_FromUnsignedLongLong(right_op.uint_val);
            else
                right_py = PyLong_FromLongLong(right_op.int_val);
        }
        if (left_py && right_py) {
            result = PyNumber_Add(left_py, right_py);
        }
        Py_XDECREF(left_py);
        Py_XDECREF(right_py);
        return result;
    }

    /* Perform operation preserving type */
    if (left_op.is_int && right_op.is_int) {
        /* Integer addition with overflow check */
        if (left_op.is_unsigned_large || right_op.is_unsigned_large) {
            /* Use Python for unsigned large values */
            PyObject *left_obj = left_op.is_unsigned_large ?
                PyLong_FromUnsignedLongLong(left_op.uint_val) :
                PyLong_FromLongLong(left_op.int_val);
            PyObject *right_obj = right_op.is_unsigned_large ?
                PyLong_FromUnsignedLongLong(right_op.uint_val) :
                PyLong_FromLongLong(right_op.int_val);
            PyObject *result_obj = PyNumber_Add(left_obj, right_obj);
            Py_DECREF(left_obj);
            Py_DECREF(right_obj);
            return result_obj;
        }

        long long result;
        if (__builtin_add_overflow(left_op.int_val, right_op.int_val, &result)) {
            /* Overflow - use Python's arbitrary precision */
            PyObject *left_obj = PyLong_FromLongLong(left_op.int_val);
            PyObject *right_obj = PyLong_FromLongLong(right_op.int_val);
            PyObject *result_obj = PyNumber_Add(left_obj, right_obj);
            Py_DECREF(left_obj);
            Py_DECREF(right_obj);
            return result_obj;
        }
        return PyLong_FromLongLong(result);
    } else {
        /* Float arithmetic */
        double left_val, right_val;
        if (left_op.is_int) {
            left_val = left_op.is_unsigned_large ? (double)left_op.uint_val : (double)left_op.int_val;
        } else {
            left_val = left_op.float_val;
        }
        if (right_op.is_int) {
            right_val = right_op.is_unsigned_large ? (double)right_op.uint_val : (double)right_op.int_val;
        } else {
            right_val = right_op.float_val;
        }
        return PyFloat_FromDouble(left_val + right_val);
    }
}

/* FyGeneric: __sub__ */
static PyObject *
FyGeneric_sub(PyObject *left, PyObject *right)
{
    numeric_operand left_op, right_op;
    PyObject *left_py = NULL, *right_py = NULL, *result = NULL;
    int rc;

    /* Extract operands */
    rc = extract_numeric_operand(left, &left_op, "-", &left_py);
    if (rc == -1) return NULL;
    if (rc == -2) Py_RETURN_NOTIMPLEMENTED;

    rc = extract_numeric_operand(right, &right_op, "-", &right_py);
    if (rc == -1) {
        Py_XDECREF(left_py);
        return NULL;
    }
    if (rc == -2) {
        Py_XDECREF(left_py);
        Py_RETURN_NOTIMPLEMENTED;
    }

    /* If either operand overflowed, use Python's arbitrary precision */
    if (left_py || right_py || left_op.is_unsigned_large || right_op.is_unsigned_large) {
        if (!left_py) {
            left_py = left_op.is_unsigned_large ?
                PyLong_FromUnsignedLongLong(left_op.uint_val) :
                PyLong_FromLongLong(left_op.int_val);
        }
        if (!right_py) {
            right_py = right_op.is_unsigned_large ?
                PyLong_FromUnsignedLongLong(right_op.uint_val) :
                PyLong_FromLongLong(right_op.int_val);
        }
        if (left_py && right_py) {
            result = PyNumber_Subtract(left_py, right_py);
        }
        Py_XDECREF(left_py);
        Py_XDECREF(right_py);
        return result;
    }

    /* Perform operation preserving type */
    if (left_op.is_int && right_op.is_int) {
        /* Integer subtraction with overflow check */
        long long result;
        if (__builtin_sub_overflow(left_op.int_val, right_op.int_val, &result)) {
            /* Overflow - use Python's arbitrary precision */
            PyObject *left_obj = PyLong_FromLongLong(left_op.int_val);
            PyObject *right_obj = PyLong_FromLongLong(right_op.int_val);
            PyObject *result_obj = PyNumber_Subtract(left_obj, right_obj);
            Py_DECREF(left_obj);
            Py_DECREF(right_obj);
            return result_obj;
        }
        return PyLong_FromLongLong(result);
    } else {
        /* Float arithmetic */
        double left_val = left_op.is_int ? (double)left_op.int_val : left_op.float_val;
        double right_val = right_op.is_int ? (double)right_op.int_val : right_op.float_val;
        return PyFloat_FromDouble(left_val - right_val);
    }
}

/* FyGeneric: __mul__ */
static PyObject *
FyGeneric_mul(PyObject *left, PyObject *right)
{
    numeric_operand left_op, right_op;
    PyObject *left_py = NULL, *right_py = NULL, *result = NULL;
    int rc;

    /* Extract operands */
    rc = extract_numeric_operand(left, &left_op, "*", &left_py);
    if (rc == -1) return NULL;
    if (rc == -2) Py_RETURN_NOTIMPLEMENTED;

    rc = extract_numeric_operand(right, &right_op, "*", &right_py);
    if (rc == -1) {
        Py_XDECREF(left_py);
        return NULL;
    }
    if (rc == -2) {
        Py_XDECREF(left_py);
        Py_RETURN_NOTIMPLEMENTED;
    }

    /* If either operand overflowed, use Python's arbitrary precision */
    if (left_py || right_py || left_op.is_unsigned_large || right_op.is_unsigned_large) {
        if (!left_py) {
            left_py = left_op.is_unsigned_large ?
                PyLong_FromUnsignedLongLong(left_op.uint_val) :
                PyLong_FromLongLong(left_op.int_val);
        }
        if (!right_py) {
            right_py = right_op.is_unsigned_large ?
                PyLong_FromUnsignedLongLong(right_op.uint_val) :
                PyLong_FromLongLong(right_op.int_val);
        }
        if (left_py && right_py) {
            result = PyNumber_Multiply(left_py, right_py);
        }
        Py_XDECREF(left_py);
        Py_XDECREF(right_py);
        return result;
    }

    /* Perform operation preserving type */
    if (left_op.is_int && right_op.is_int) {
        /* Integer multiplication with overflow check */
        long long result;
        if (__builtin_mul_overflow(left_op.int_val, right_op.int_val, &result)) {
            /* Overflow - use Python's arbitrary precision */
            PyObject *left_obj = PyLong_FromLongLong(left_op.int_val);
            PyObject *right_obj = PyLong_FromLongLong(right_op.int_val);
            PyObject *result_obj = PyNumber_Multiply(left_obj, right_obj);
            Py_DECREF(left_obj);
            Py_DECREF(right_obj);
            return result_obj;
        }
        return PyLong_FromLongLong(result);
    } else {
        /* Float arithmetic */
        double left_val = left_op.is_int ? (double)left_op.int_val : left_op.float_val;
        double right_val = right_op.is_int ? (double)right_op.int_val : right_op.float_val;
        return PyFloat_FromDouble(left_val * right_val);
    }
}

/* FyGeneric: __truediv__ */
static PyObject *
FyGeneric_truediv(PyObject *left, PyObject *right)
{
    numeric_operand left_op, right_op;
    PyObject *left_py = NULL, *right_py = NULL;
    int rc;

    /* Extract operands */
    rc = extract_numeric_operand(left, &left_op, "/", &left_py);
    if (rc == -1) return NULL;
    if (rc == -2) Py_RETURN_NOTIMPLEMENTED;

    rc = extract_numeric_operand(right, &right_op, "/", &right_py);
    if (rc == -1) {
        Py_XDECREF(left_py);
        return NULL;
    }
    if (rc == -2) {
        Py_XDECREF(left_py);
        Py_RETURN_NOTIMPLEMENTED;
    }

    /* Convert both to double (true division always returns float) */
    double left_val, right_val;

    if (left_py) {
        left_val = PyLong_AsDouble(left_py);
        Py_DECREF(left_py);
    } else if (left_op.is_int) {
        left_val = left_op.is_unsigned_large ? (double)left_op.uint_val : (double)left_op.int_val;
    } else {
        left_val = left_op.float_val;
    }

    if (right_py) {
        right_val = PyLong_AsDouble(right_py);
        Py_DECREF(right_py);
    } else if (right_op.is_int) {
        right_val = right_op.is_unsigned_large ? (double)right_op.uint_val : (double)right_op.int_val;
    } else {
        right_val = right_op.float_val;
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
    numeric_operand left_op, right_op;
    PyObject *left_py = NULL, *right_py = NULL, *result = NULL;
    int rc;

    /* Extract operands */
    rc = extract_numeric_operand(left, &left_op, "//", &left_py);
    if (rc == -1) return NULL;
    if (rc == -2) Py_RETURN_NOTIMPLEMENTED;

    rc = extract_numeric_operand(right, &right_op, "//", &right_py);
    if (rc == -1) {
        Py_XDECREF(left_py);
        return NULL;
    }
    if (rc == -2) {
        Py_XDECREF(left_py);
        Py_RETURN_NOTIMPLEMENTED;
    }

    /* Perform operation */
    if ((left_op.is_int || left_py) && (right_op.is_int || right_py)) {
        /* Integer floor division - use Python for large or overflow values */
        if (left_py || right_py || left_op.is_unsigned_large || right_op.is_unsigned_large) {
            if (!left_py) {
                left_py = left_op.is_unsigned_large ?
                    PyLong_FromUnsignedLongLong(left_op.uint_val) :
                    PyLong_FromLongLong(left_op.int_val);
            }
            if (!right_py) {
                right_py = right_op.is_unsigned_large ?
                    PyLong_FromUnsignedLongLong(right_op.uint_val) :
                    PyLong_FromLongLong(right_op.int_val);
            }
            if (left_py && right_py) {
                result = PyNumber_FloorDivide(left_py, right_py);
            }
            Py_XDECREF(left_py);
            Py_XDECREF(right_py);
            return result;
        }

        /* Integer floor division */
        if (right_op.int_val == 0) {
            PyErr_SetString(PyExc_ZeroDivisionError, "integer division or modulo by zero");
            return NULL;
        }
        /* Python floor division semantics */
        long long result = left_op.int_val / right_op.int_val;
        if ((left_op.int_val ^ right_op.int_val) < 0 && left_op.int_val % right_op.int_val != 0)
            result--;
        return PyLong_FromLongLong(result);
    } else {
        /* Float floor division */
        double left_val, right_val;

        if (left_py) {
            left_val = PyLong_AsDouble(left_py);
            Py_DECREF(left_py);
        } else if (left_op.is_int) {
            left_val = left_op.is_unsigned_large ? (double)left_op.uint_val : (double)left_op.int_val;
        } else {
            left_val = left_op.float_val;
        }

        if (right_py) {
            right_val = PyLong_AsDouble(right_py);
            Py_DECREF(right_py);
        } else if (right_op.is_int) {
            right_val = right_op.is_unsigned_large ? (double)right_op.uint_val : (double)right_op.int_val;
        } else {
            right_val = right_op.float_val;
        }

        if (right_val == 0.0) {
            PyErr_SetString(PyExc_ZeroDivisionError, "float floor division by zero");
            return NULL;
        }
        return PyFloat_FromDouble(floor(left_val / right_val));
    }
}

/* FyGeneric: __mod__ */
static PyObject *
FyGeneric_mod(PyObject *left, PyObject *right)
{
    numeric_operand left_op, right_op;
    PyObject *left_py = NULL, *right_py = NULL, *result = NULL;
    int rc;

    /* Extract operands */
    rc = extract_numeric_operand(left, &left_op, "%", &left_py);
    if (rc == -1) return NULL;
    if (rc == -2) Py_RETURN_NOTIMPLEMENTED;

    rc = extract_numeric_operand(right, &right_op, "%", &right_py);
    if (rc == -1) {
        Py_XDECREF(left_py);
        return NULL;
    }
    if (rc == -2) {
        Py_XDECREF(left_py);
        Py_RETURN_NOTIMPLEMENTED;
    }

    /* Perform operation */
    if ((left_op.is_int || left_py) && (right_op.is_int || right_py)) {
        /* Integer modulo - use Python for large or overflow values */
        if (left_py || right_py || left_op.is_unsigned_large || right_op.is_unsigned_large) {
            if (!left_py) {
                left_py = left_op.is_unsigned_large ?
                    PyLong_FromUnsignedLongLong(left_op.uint_val) :
                    PyLong_FromLongLong(left_op.int_val);
            }
            if (!right_py) {
                right_py = right_op.is_unsigned_large ?
                    PyLong_FromUnsignedLongLong(right_op.uint_val) :
                    PyLong_FromLongLong(right_op.int_val);
            }
            if (left_py && right_py) {
                result = PyNumber_Remainder(left_py, right_py);
            }
            Py_XDECREF(left_py);
            Py_XDECREF(right_py);
            return result;
        }

        /* Integer modulo */
        if (right_op.int_val == 0) {
            PyErr_SetString(PyExc_ZeroDivisionError, "integer division or modulo by zero");
            return NULL;
        }
        /* Python modulo semantics (same sign as divisor) */
        long long result = left_op.int_val % right_op.int_val;
        if (result != 0 && (left_op.int_val ^ right_op.int_val) < 0)
            result += right_op.int_val;
        return PyLong_FromLongLong(result);
    } else {
        /* Float modulo */
        double left_val, right_val;

        if (left_py) {
            left_val = PyLong_AsDouble(left_py);
            Py_DECREF(left_py);
        } else if (left_op.is_int) {
            left_val = left_op.is_unsigned_large ? (double)left_op.uint_val : (double)left_op.int_val;
        } else {
            left_val = left_op.float_val;
        }

        if (right_py) {
            right_val = PyLong_AsDouble(right_py);
            Py_DECREF(right_py);
        } else if (right_op.is_int) {
            right_val = right_op.is_unsigned_large ? (double)right_op.uint_val : (double)right_op.int_val;
        } else {
            right_val = right_op.float_val;
        }

        if (right_val == 0.0) {
            PyErr_SetString(PyExc_ZeroDivisionError, "float modulo");
            return NULL;
        }
        return PyFloat_FromDouble(fmod(left_val, right_val));
    }
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

/* FyGeneric: __class__ property getter - returns appropriate Python type */
static PyObject *
FyGeneric_get_class(FyGenericObject *self, void *Py_UNUSED(closure))
{
    enum fy_generic_type type = fy_get_type(self->fyg);

    switch (type) {
    case FYGT_NULL:
        /* None's type */
        Py_INCREF(Py_TYPE(Py_None));
        return (PyObject *)Py_TYPE(Py_None);
    case FYGT_BOOL:
        Py_INCREF(&PyBool_Type);
        return (PyObject *)&PyBool_Type;
    case FYGT_INT:
        Py_INCREF(&PyLong_Type);
        return (PyObject *)&PyLong_Type;
    case FYGT_FLOAT:
        Py_INCREF(&PyFloat_Type);
        return (PyObject *)&PyFloat_Type;
    case FYGT_STRING:
        Py_INCREF(&PyUnicode_Type);
        return (PyObject *)&PyUnicode_Type;
    case FYGT_SEQUENCE:
        Py_INCREF(&PyList_Type);
        return (PyObject *)&PyList_Type;
    case FYGT_MAPPING:
        Py_INCREF(&PyDict_Type);
        return (PyObject *)&PyDict_Type;
    default:
        /* For unknown types, return the actual FyGeneric type */
        Py_INCREF(&FyGenericType);
        return (PyObject *)&FyGenericType;
    }
}

/* FyGeneric: __class__ property setter - disallow changes */
static int
FyGeneric_set_class(FyGenericObject *Py_UNUSED(self), PyObject *Py_UNUSED(value), void *Py_UNUSED(closure))
{
    PyErr_SetString(PyExc_TypeError, "__class__ assignment not supported for FyGeneric");
    return -1;
}

/* FyGeneric getsetters */
static PyGetSetDef FyGeneric_getsetters[] = {
    {"__class__", (getter)FyGeneric_get_class, (setter)FyGeneric_set_class,
     "Dynamic class based on wrapped generic type", NULL},
    {NULL}  /* Sentinel */
};

/* FyGeneric: __hash__ - compute hash of wrapped value */
static Py_hash_t
FyGeneric_hash(FyGenericObject *self)
{
    enum fy_generic_type type = fy_get_type(self->fyg);
    PyObject *temp_obj = NULL;
    Py_hash_t hash_value;

    /* Only hashable types can be hashed */
    switch (type) {
    case FYGT_NULL:
        /* Hash for None */
        temp_obj = Py_None;
        Py_INCREF(temp_obj);
        break;

    case FYGT_BOOL:
        /* Hash for True/False */
        temp_obj = PyBool_FromLong(fy_cast(self->fyg, (_Bool)false) ? 1 : 0);
        break;

    case FYGT_INT: {
        /* Hash for integers - handle decorated ints */
        fy_generic_decorated_int dint = fy_cast(self->fyg, fy_dint_empty);
        if (dint.flags & FYGDIF_UNSIGNED_RANGE_EXTEND) {
            /* Large unsigned value */
            temp_obj = PyLong_FromUnsignedLongLong(dint.uv);
        } else {
            /* Regular signed value */
            temp_obj = PyLong_FromLongLong(dint.sv);
        }
        break;
    }

    case FYGT_FLOAT:
        /* Hash for floats */
        temp_obj = PyFloat_FromDouble(fy_cast(self->fyg, (double)0.0));
        break;

    case FYGT_STRING: {
        /* Hash for strings */
        fy_generic_sized_string szstr = fy_cast(self->fyg, fy_szstr_empty);
        temp_obj = PyUnicode_FromStringAndSize(szstr.data, szstr.size);
        break;
    }

    case FYGT_SEQUENCE:
        /* Sequences are not hashable */
        PyErr_SetString(PyExc_TypeError, "unhashable type: 'sequence'");
        return -1;

    case FYGT_MAPPING:
        /* Mappings are not hashable */
        PyErr_SetString(PyExc_TypeError, "unhashable type: 'mapping'");
        return -1;

    case FYGT_INDIRECT:
    case FYGT_ALIAS:
        /* These should not appear here */
        PyErr_SetString(PyExc_TypeError, "unhashable type: indirect/alias");
        return -1;

    default:
        /* Unknown types are not hashable */
        PyErr_Format(PyExc_TypeError, "unhashable type: %d", type);
        return -1;
    }

    if (!temp_obj)
        return -1;

    /* Compute hash of the Python object */
    hash_value = PyObject_Hash(temp_obj);
    Py_DECREF(temp_obj);

    return hash_value;
}

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
    .tp_hash = (hashfunc)FyGeneric_hash,
    .tp_str = (reprfunc)FyGeneric_str,
    .tp_getattro = (getattrofunc)FyGeneric_getattro,
    .tp_richcompare = FyGeneric_richcompare,
    .tp_iter = (getiterfunc)FyGeneric_iter,
    .tp_methods = FyGeneric_methods,
    .tp_getset = FyGeneric_getsetters,
};

/* ========== Module Functions ========== */

/* Helper: Create a generic builder with allocator and dedup support */
static struct fy_generic_builder *
create_builder_with_config(int dedup, size_t estimated_size)
{
    /* Create auto allocator with appropriate scenario based on dedup parameter */
    struct fy_auto_allocator_cfg auto_cfg;
    memset(&auto_cfg, 0, sizeof(auto_cfg));
    auto_cfg.scenario = dedup ? FYAST_PER_TAG_FREE_DEDUP : FYAST_PER_TAG_FREE;
    auto_cfg.estimated_max_size = estimated_size;

    struct fy_allocator *allocator = fy_allocator_create("auto", &auto_cfg);
    if (allocator == NULL)
        return NULL;

    /* Configure generic builder with allocator and size estimate */
    struct fy_generic_builder_cfg gb_cfg;
    memset(&gb_cfg, 0, sizeof(gb_cfg));
    gb_cfg.allocator = allocator;
    gb_cfg.estimated_max_size = estimated_size;
    gb_cfg.flags = FYGBCF_OWNS_ALLOCATOR;  /* Own allocator */

    /* Create generic builder */
    struct fy_generic_builder *gb = fy_generic_builder_create(&gb_cfg);
    if (gb == NULL) {
        fy_allocator_destroy(allocator);
        return NULL;
    }

    return gb;
}

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

    /* Create generic builder using helper */
    struct fy_generic_builder *gb = create_builder_with_config(dedup, yaml_len * 2);
    if (gb == NULL) {
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
        /* Try to extract as long long first */
        long long val = PyLong_AsLongLong(obj);
        if (val == -1 && PyErr_Occurred()) {
            PyErr_Clear();
            /* Might be too large - try unsigned long long */
            unsigned long long uval = PyLong_AsUnsignedLongLong(obj);
            if (uval == (unsigned long long)-1 && PyErr_Occurred()) {
                /* Overflow even for unsigned long long - Python int too large */
                return fy_invalid;
            }
            /* Check if it exceeds LLONG_MAX - need decorated int */
            if (uval > (unsigned long long)LLONG_MAX) {
                /* Create decorated int with unsigned range extend flag */
                fy_generic_decorated_int dint = {
                    .uv = uval,
                    .flags = FYGDIF_UNSIGNED_RANGE_EXTEND
                };
                return fy_gb_dint_type_create_out_of_place(gb, dint);
            }
            /* Fits in signed range despite PyLong_AsLongLong overflow */
            return fy_gb_long_long_create(gb, (long long)uval);
        }
        /* Normal signed long long */
        return fy_gb_long_long_create(gb, val);
    }

    if (PyFloat_Check(obj)) {
        double val = PyFloat_AsDouble(obj);
        if (val == -1.0 && PyErr_Occurred())
            return fy_invalid;
        return fy_gb_double_create(gb, val);
    }

    if (PyUnicode_Check(obj)) {
        Py_ssize_t size;
        const char *str = PyUnicode_AsUTF8AndSize(obj, &size);
        if (str == NULL)
            return fy_invalid;
        return fy_gb_string_size_create(gb, str, (size_t)size);
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

    /* Extract the sized string from the generic */
    fy_generic_sized_string szstr = fy_cast(emitted, fy_szstr_empty);
    if (szstr.data == NULL) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to extract string from emitted generic");
        return NULL;
    }

    /* Create Python string (makes a copy) */
    PyObject *result = PyUnicode_FromStringAndSize(szstr.data, szstr.size);

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
    int dedup = 1;  /* Default to True, like loads/load */
    static char *kwlist[] = {"obj", "mutable", "dedup", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|pp", kwlist, &obj, &mutable, &dedup))
        return NULL;

    /* Create generic builder using helper (estimate 64KB for typical Python objects) */
    struct fy_generic_builder *gb = create_builder_with_config(dedup, 64 * 1024);
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

        /* Create generic builder using helper (1MB default estimate for files) */
        struct fy_generic_builder *gb = create_builder_with_config(dedup, 1024 * 1024);
        if (gb == NULL) {
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

    /* Extract the sized string from the generic */
    fy_generic_sized_string szstr = fy_cast(emitted, fy_szstr_empty);
    if (szstr.data == NULL) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to extract string from emitted generic");
        return NULL;
    }

    PyObject *result = PyUnicode_FromStringAndSize(szstr.data, szstr.size);
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

        /* Build emit flags */
        unsigned int emit_flags = build_emit_flags(json_mode, compact, 1, 0);

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

        /* Write to file object */
        if (write_to_file_object(file_obj, yaml_str) < 0) {
            Py_DECREF(yaml_str);
            return NULL;
        }

        Py_DECREF(yaml_str);
        Py_RETURN_NONE;
    }
}

/* Module method table */
static PyMethodDef module_methods[] = {
    {"loads", _PyCFunction_CAST(libfyaml_loads), METH_VARARGS | METH_KEYWORDS,
     "Load YAML/JSON from string"},
    {"dumps", _PyCFunction_CAST(libfyaml_dumps), METH_VARARGS | METH_KEYWORDS,
     "Dump Python object to YAML/JSON string"},
    {"load", _PyCFunction_CAST(libfyaml_load), METH_VARARGS | METH_KEYWORDS,
     "Load YAML/JSON from file (path or file object)"},
    {"dump", _PyCFunction_CAST(libfyaml_dump), METH_VARARGS | METH_KEYWORDS,
     "Dump Python object to file (path or file object)"},
    {"loads_all", _PyCFunction_CAST(libfyaml_loads_all), METH_VARARGS | METH_KEYWORDS,
     "Load multiple YAML/JSON documents from string"},
    {"load_all", _PyCFunction_CAST(libfyaml_load_all), METH_VARARGS | METH_KEYWORDS,
     "Load multiple YAML/JSON documents from file (path or file object)"},
    {"dumps_all", _PyCFunction_CAST(libfyaml_dumps_all), METH_VARARGS | METH_KEYWORDS,
     "Dump multiple Python objects to YAML/JSON string"},
    {"dump_all", _PyCFunction_CAST(libfyaml_dump_all), METH_VARARGS | METH_KEYWORDS,
     "Dump multiple Python objects to file (path or file object)"},
    {"from_python", _PyCFunction_CAST(libfyaml_from_python), METH_VARARGS | METH_KEYWORDS,
     "Convert Python object to FyGeneric"},
    {"path_list_to_unix_path", _PyCFunction_CAST(libfyaml_path_list_to_unix_path), METH_O,
     "Convert path list (e.g., ['server', 'host']) to Unix-style path string (e.g., '/server/host')"},
    {"unix_path_to_path_list", _PyCFunction_CAST(libfyaml_unix_path_to_path_list), METH_O,
     "Convert Unix-style path string (e.g., '/server/host') to path list (e.g., ['server', 'host'])"},
    {NULL}
};

/* Module definition */
static struct PyModuleDef libfyaml_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "_libfyaml",
    .m_doc = "Python bindings for libfyaml generic type system",
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
