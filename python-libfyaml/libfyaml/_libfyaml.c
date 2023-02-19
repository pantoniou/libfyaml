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
#include <libfyaml/libfyaml-generic.h>

/* Forward declarations */
typedef struct FyDocumentStateObject FyDocumentStateObject;

/* ========== FyGeneric Type ========== */

typedef struct {
    PyObject_HEAD
    fy_generic fyg;         /* The generic value */
    PyObject *doc_state;    /* Reference to FyDocumentState (always non-NULL) */
    PyObject *path;         /* Tuple of path elements from root (NULL if root) */
} FyGenericObject;

/* Helper macros to access doc_state fields from FyGenericObject */
#define FYG_DOC_STATE(self) ((FyDocumentStateObject *)(self)->doc_state)
#define FYG_GB(self) (FYG_DOC_STATE(self)->gb)
#define FYG_VDS(self) (FYG_DOC_STATE(self)->vds)
#define FYG_MUTABLE(self) (FYG_DOC_STATE(self)->mutable)
#define FYG_ROOT_FYG(self) (FYG_DOC_STATE(self)->root_fyg)
#define FYG_IS_ROOT(self) ((self)->path == NULL)

static PyTypeObject FyGenericType;
static PyTypeObject FyGenericIteratorType;

/* Forward declarations */
static PyObject *FyGeneric_from_parent(fy_generic fyg, FyGenericObject *parent, PyObject *path_elem);

/* Singleton-aware reference counting helpers.
 *
 * Python 3.12+ has built-in immortality awareness in Py_INCREF/Py_DECREF
 * (PEP 683), so the standard macros are used directly there.
 * Python 3.10-3.11 has Py_NewRef() but no immortality in INCREF/DECREF.
 * Python 3.7-3.9: manual singleton guards throughout.
 *
 * In all cases Py_None/Py_True/Py_False are safe to leave unmodified. */

/* NULL-safe DECREF, singleton-safe on all supported Python versions */
static inline void _fy_py_xdecref_impl(PyObject *op)
{
#if PY_VERSION_HEX >= 0x030C0000
    Py_XDECREF(op);                 /* 3.12+: immortality handled internally */
#else
    if (op != NULL && op != Py_None && op != Py_True && op != Py_False)
        Py_DECREF(op);
#endif
}
static inline void _fy_py_xdecref_fyobj(FyGenericObject *op) { _fy_py_xdecref_impl((PyObject *)op); }
#define fy_py_xdecref(op) _Generic((op), \
    PyObject *:       _fy_py_xdecref_impl, \
    FyGenericObject *: _fy_py_xdecref_fyobj \
)(op)

/* INCREF, singleton-safe on all supported Python versions */
static inline void _fy_py_incref_impl(PyObject *op)
{
#if PY_VERSION_HEX >= 0x030C0000
    Py_INCREF(op);                  /* 3.12+: immortality handled internally */
#else
    if (op != Py_None && op != Py_True && op != Py_False)
        Py_INCREF(op);
#endif
}
static inline void _fy_py_incref_fyobj(FyGenericObject *op) { _fy_py_incref_impl((PyObject *)op); }
#define fy_py_incref(op) _Generic((op), \
    PyObject *:       _fy_py_incref_impl, \
    FyGenericObject *: _fy_py_incref_fyobj \
)(op)

/* Return a new owned reference, singleton-safe on all supported Python versions.
 * Uses Py_NewRef() on 3.10+ (which on 3.12+ also benefits from immortality).
 * Returns the same type as the argument — FyGenericObject* in, FyGenericObject* out. */
static inline PyObject *_fy_py_newref_impl(PyObject *op)
{
#if PY_VERSION_HEX >= 0x030A0000
    return Py_NewRef(op);           /* 3.10+: official API */
#else
    _fy_py_incref_impl(op);
    return op;
#endif
}
static inline FyGenericObject *_fy_py_newref_fyobj(FyGenericObject *op)
{
    _fy_py_incref_impl((PyObject *)op);
    return op;
}
#define fy_py_newref(op) _Generic((op), \
    PyObject *:       _fy_py_newref_impl, \
    FyGenericObject *: _fy_py_newref_fyobj \
)(op)

/* DECREF *ptr and set *ptr = NULL — the standard "release and clear" pattern */
static inline void fy_py_release(PyObject **op)
{
    if (!op)
        return;
    _fy_py_xdecref_impl(*op);
    *op = NULL;
}

/* Helper: Convert a fy_generic string value directly to a Python unicode object */
static PyObject *fy_szstr_to_pyunicode(fy_generic g)
{
    fy_generic_sized_string szstr = fy_cast(g, fy_szstr_empty);
    return PyUnicode_FromStringAndSize(szstr.data, szstr.size);
}

/* Helper: Convert primitive fy_generic to Python object (for dict keys, iteration, etc.) */
static PyObject *fy_generic_to_python_primitive(fy_generic value)
{
    switch (fy_get_type(value)) {
    case FYGT_NULL:
        return fy_py_newref(Py_None);
    case FYGT_BOOL:
        return PyBool_FromLong(fy_cast(value, (_Bool)false) ? 1 : 0);
    case FYGT_INT:
        return PyLong_FromLongLong(fy_cast(value, (long long)-1LL));
    case FYGT_FLOAT:
        return PyFloat_FromDouble(fy_cast(value, (double)0.0));
    case FYGT_STRING:
        return fy_szstr_to_pyunicode(value);
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
    fy_py_xdecref(self->generic_obj);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* FyGenericIterator: __iter__ (returns self) */
static PyObject *
FyGenericIterator_iter(PyObject *self)
{
    return fy_py_newref(self);
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
        /* Empty sequence has NULL handle */
        if (self->u.seqh == NULL || self->index >= self->u.seqh->count)
            goto out_stop;
        item = self->u.seqh->items[self->index];
        key_obj = PyLong_FromSize_t(self->index);
        if (key_obj == NULL)
            return NULL;

        result = FyGeneric_from_parent(item, self->generic_obj, key_obj);
        fy_py_xdecref(key_obj);
        break;

    case FYGT_MAPPING:
        /* Empty mapping has NULL handle */
        if (self->u.maph == NULL || self->index >= self->u.maph->count)
            goto out_stop;
        key = self->u.maph->pairs[self->index].key;
        item = self->u.maph->pairs[self->index].value;

        /* Convert key to Python object for path tracking */
        key_obj = fy_generic_to_python_primitive(key);
        if (key_obj == NULL)
            return NULL;

        /* Return the value (not the key) */
        result = FyGeneric_from_parent(item, self->generic_obj, key_obj);
        fy_py_xdecref(key_obj);
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

    iter->generic_obj = fy_py_newref(self);
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

/* ========== FyDocumentState Type ========== */

struct FyDocumentStateObject {
    PyObject_HEAD
    fy_generic root_fyg;                /* Root generic value (updated on mutations) */
    fy_generic vds;                     /* VDS for metadata (fy_invalid if none) */
    struct fy_generic_builder *gb;      /* Builder (owned, NULL if shared) */
    int mutable;                        /* Whether mutation is allowed */
    PyObject *parent;                   /* Reference to parent doc_state (for sharing) */
};

static PyTypeObject FyDocumentStateType;

/* FyDocumentState: Deallocation */
static void
FyDocumentState_dealloc(FyDocumentStateObject *self)
{
    if (self->gb != NULL) {
        /* This doc_state owns the builder - destroy it */
        fy_generic_builder_destroy(self->gb);
    }
    fy_py_xdecref(self->parent);
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/* FyDocumentState: __repr__ */
static PyObject *
FyDocumentState_repr(FyDocumentStateObject *self)
{
    struct fy_document_state *fyds = fy_generic_vds_get_document_state(self->vds);
    if (fyds == NULL)
        return PyUnicode_FromString("<FyDocumentState: invalid>");

    const struct fy_version *vers = fy_document_state_version(fyds);
    if (vers)
        return PyUnicode_FromFormat("<FyDocumentState: YAML %d.%d>", vers->major, vers->minor);
    else
        return PyUnicode_FromString("<FyDocumentState>");
}

/* FyDocumentState: version property getter */
static PyObject *
FyDocumentState_get_version(FyDocumentStateObject *self, void *closure)
{
    struct fy_document_state *fyds = fy_generic_vds_get_document_state(self->vds);
    if (fyds == NULL)
        return fy_py_newref(Py_None);

    const struct fy_version *vers = fy_document_state_version(fyds);
    if (vers == NULL)
        return fy_py_newref(Py_None);

    return Py_BuildValue("(ii)", vers->major, vers->minor);
}

/* FyDocumentState: version_explicit property getter */
static PyObject *
FyDocumentState_get_version_explicit(FyDocumentStateObject *self, void *closure)
{
    struct fy_document_state *fyds = fy_generic_vds_get_document_state(self->vds);
    if (fyds == NULL)
        return fy_py_newref(Py_False);

    return PyBool_FromLong(fy_document_state_version_explicit(fyds));
}

/* FyDocumentState: tags property getter - returns list of dicts */
static PyObject *
FyDocumentState_get_tags(FyDocumentStateObject *self, void *closure)
{
    struct fy_document_state *fyds = fy_generic_vds_get_document_state(self->vds);
    if (fyds == NULL)
        return PyList_New(0);

    PyObject *result = PyList_New(0);
    if (result == NULL)
        return NULL;

    PyObject *tag_dict = NULL;
    PyObject *handle = NULL;
    PyObject *prefix = NULL;

    void *iter = NULL;
    const struct fy_tag *tag;
    while ((tag = fy_document_state_tag_directive_iterate(fyds, &iter)) != NULL) {
        tag_dict = PyDict_New();
        if (tag_dict == NULL)
            goto err_out;

        if (tag->handle) {
            handle = PyUnicode_FromString(tag->handle);
            if (handle == NULL)
                goto err_out;
        } else {
            handle = Py_None;
        }

        if (tag->prefix) {
            prefix = PyUnicode_FromString(tag->prefix);
            if (prefix == NULL)
                goto err_out;
        } else {
            prefix = Py_None;
        }

        PyDict_SetItemString(tag_dict, "handle", handle);
        PyDict_SetItemString(tag_dict, "prefix", prefix);
        fy_py_release(&handle);
        fy_py_release(&prefix);

        if (PyList_Append(result, tag_dict) < 0)
            goto err_out;

        fy_py_release(&tag_dict);
    }

    return result;

err_out:
    fy_py_xdecref(handle);
    fy_py_xdecref(prefix);
    fy_py_xdecref(tag_dict);
    fy_py_xdecref(result);
    return NULL;
}

/* FyDocumentState: tags_explicit property getter */
static PyObject *
FyDocumentState_get_tags_explicit(FyDocumentStateObject *self, void *closure)
{
    struct fy_document_state *fyds = fy_generic_vds_get_document_state(self->vds);
    if (fyds == NULL) {
        fy_py_incref(Py_False);
        return Py_False;
    }

    return PyBool_FromLong(fy_document_state_tags_explicit(fyds));
}

/* FyDocumentState: json_mode property getter */
static PyObject *
FyDocumentState_get_json_mode(FyDocumentStateObject *self, void *closure)
{
    struct fy_document_state *fyds = fy_generic_vds_get_document_state(self->vds);
    if (fyds == NULL) {
        fy_py_incref(Py_False);
        return Py_False;
    }

    return PyBool_FromLong(fy_document_state_json_mode(fyds));
}

/* FyDocumentState getsetters */
static PyGetSetDef FyDocumentState_getsetters[] = {
    {"version", (getter)FyDocumentState_get_version, NULL,
     "YAML version as (major, minor) tuple", NULL},
    {"version_explicit", (getter)FyDocumentState_get_version_explicit, NULL,
     "True if version was explicitly set via %YAML directive", NULL},
    {"tags", (getter)FyDocumentState_get_tags, NULL,
     "List of tag directives as dicts with 'handle' and 'prefix' keys", NULL},
    {"tags_explicit", (getter)FyDocumentState_get_tags_explicit, NULL,
     "True if tags were explicitly set via %TAG directives", NULL},
    {"json_mode", (getter)FyDocumentState_get_json_mode, NULL,
     "True if document was parsed as JSON", NULL},
    {NULL}  /* Sentinel */
};

/* FyDocumentState type object */
static PyTypeObject FyDocumentStateType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "libfyaml.FyDocumentState",
    .tp_doc = "Document state with version and tag directives",
    .tp_basicsize = sizeof(FyDocumentStateObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_dealloc = (destructor)FyDocumentState_dealloc,
    .tp_repr = (reprfunc)FyDocumentState_repr,
    .tp_getset = FyDocumentState_getsetters,
};

/* Helper: Create root FyDocumentState (owns builder) */
static PyObject *
FyDocumentState_create(fy_generic root_fyg, fy_generic vds, struct fy_generic_builder *gb, int mutable)
{
    FyDocumentStateObject *self = PyObject_New(FyDocumentStateObject, &FyDocumentStateType);
    if (self == NULL)
        return NULL;

    self->root_fyg = root_fyg;
    self->vds = vds;
    self->gb = gb;       /* Owns the builder */
    self->mutable = mutable;
    self->parent = NULL; /* No parent - this is the owner */

    return (PyObject *)self;
}

/* Helper: Create child FyDocumentState (shares parent's builder) */
static PyObject *
FyDocumentState_create_child(fy_generic root_fyg, fy_generic vds, FyDocumentStateObject *parent)
{
    FyDocumentStateObject *self = PyObject_New(FyDocumentStateObject, &FyDocumentStateType);
    if (self == NULL)
        return NULL;

    self->root_fyg = root_fyg;
    self->vds = vds;
    self->gb = NULL;              /* Child doesn't own the builder */
    self->mutable = parent->mutable;
    self->parent = fy_py_newref((PyObject *)parent);

    return (PyObject *)self;
}

/* ========== FyGeneric Type ========== */

/* FyGeneric: Deallocation */
static void
FyGeneric_dealloc(FyGenericObject *self)
{
    fy_py_xdecref(self->doc_state);
    fy_py_xdecref(self->path);
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
        case FYGT_STRING:
            return fy_szstr_to_pyunicode(self->fyg);

        case FYGT_INT:
            return PyUnicode_FromFormat("%lld", fy_cast(self->fyg, (long long)0));

        case FYGT_FLOAT: {
            PyObject *float_obj;
            PyObject *str_obj;
            float_obj = PyFloat_FromDouble(fy_cast(self->fyg, (double)0.0));
            if (float_obj == NULL)
                return NULL;
            str_obj = PyObject_Str(float_obj);
            fy_py_xdecref(float_obj);
            return str_obj;
        }

        case FYGT_BOOL:
            return PyUnicode_FromString(fy_cast(self->fyg, (_Bool)false) ? "True" : "False");

        case FYGT_NULL:
            return PyUnicode_FromString("None");

        case FYGT_SEQUENCE:
        case FYGT_MAPPING: {
            struct fy_generic_builder *gb;
            unsigned int emit_flags;
            fy_generic emitted;
            fy_generic_sized_string szstr;

            /* Emit collections as oneline flow */
            gb = FYG_GB(self);
            assert(gb != NULL);

            emit_flags = FYOPEF_DISABLE_DIRECTORY | FYOPEF_MODE_YAML_1_2 |
                         FYOPEF_STYLE_ONELINE | FYOPEF_OUTPUT_TYPE_STRING;

            emitted = fy_gb_emit(gb, self->fyg, emit_flags, NULL);
            if (!fy_generic_is_valid(emitted)) {
                PyErr_SetString(PyExc_RuntimeError, "Failed to emit collection as string");
                return NULL;
            }

            szstr = fy_cast(emitted, fy_szstr_empty);
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
            PyObject *str_obj = fy_szstr_to_pyunicode(self->fyg);
            if (str_obj == NULL)
                return NULL;

            PyObject *int_obj = PyLong_FromUnicodeObject(str_obj, 10);
            fy_py_xdecref(str_obj);
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
                fy_py_xdecref(py_int);
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
            PyObject *str_obj = fy_szstr_to_pyunicode(self->fyg);
            if (str_obj == NULL)
                return NULL;

            PyObject *float_obj = PyFloat_FromString(str_obj);
            fy_py_xdecref(str_obj);
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
            PyObject *str_obj;
            Py_ssize_t length;
            /* Return number of characters (not bytes, handles UTF-8) */
            str_obj = fy_szstr_to_pyunicode(self->fyg);
            if (str_obj == NULL)
                return -1;
            length = PyUnicode_GET_LENGTH(str_obj);
            fy_py_xdecref(str_obj);
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

    /* Share parent's doc_state */
    self->doc_state = parent->doc_state;
    fy_py_incref(self->doc_state);

    /* Build path from parent's path + this element */
    if (parent->path == NULL) {
        /* Parent is root - create new tuple with just this element */
        self->path = PyTuple_New(1);
        if (self->path == NULL) {
            fy_py_xdecref(self->doc_state);
            Py_TYPE(self)->tp_free((PyObject *)self);
            return NULL;
        }
        fy_py_incref(path_elem);
        PyTuple_SET_ITEM(self->path, 0, path_elem);
    } else {
        Py_ssize_t parent_len;
        Py_ssize_t i;
        /* Parent has path - copy and append */
        parent_len = PyTuple_Size(parent->path);
        self->path = PyTuple_New(parent_len + 1);
        if (self->path == NULL) {
            fy_py_xdecref(self->doc_state);
            Py_TYPE(self)->tp_free((PyObject *)self);
            return NULL;
        }

        /* Copy parent's path elements */
        for (i = 0; i < parent_len; i++) {
            PyObject *item = PyTuple_GET_ITEM(parent->path, i);
            fy_py_incref(item);
            PyTuple_SET_ITEM(self->path, i, item);
        }

        /* Append new element */
        fy_py_incref(path_elem);
        PyTuple_SET_ITEM(self->path, parent_len, path_elem);
    }

    return (PyObject *)self;
}

/* Helper: Create root FyGeneric wrapper (owns builder) */
static PyObject *
FyGeneric_from_generic(fy_generic fyg, struct fy_generic_builder *gb, int mutable)
{
    PyObject *doc_state;
    FyGenericObject *self;

    /* Create doc_state first (it owns the builder and root value) */
    doc_state = FyDocumentState_create(fyg, fy_invalid, gb, mutable);
    if (doc_state == NULL)
        return NULL;

    self = (FyGenericObject *)FyGenericType.tp_alloc(&FyGenericType, 0);
    if (self == NULL) {
        fy_py_xdecref(doc_state);
        return NULL;
    }

    self->fyg = fyg;
    self->doc_state = doc_state;  /* Takes ownership */
    self->path = NULL;            /* Root has no path */

    return (PyObject *)self;
}

/* Helper: Create root FyGeneric wrapper with VDS (owns builder) */
static PyObject *
FyGeneric_from_vds(fy_generic vds, struct fy_generic_builder *gb, int mutable)
{
    fy_generic fyg;
    PyObject *doc_state;
    FyGenericObject *self;

    /* Extract root from VDS */
    fyg = fy_generic_vds_get_root(vds);
    if (!fy_generic_is_valid(fyg))
        return NULL;

    /* Create doc_state first (it owns the builder, VDS, and root value) */
    doc_state = FyDocumentState_create(fyg, vds, gb, mutable);
    if (doc_state == NULL)
        return NULL;

    self = (FyGenericObject *)FyGenericType.tp_alloc(&FyGenericType, 0);
    if (self == NULL) {
        fy_py_xdecref(doc_state);
        return NULL;
    }

    self->fyg = fyg;
    self->doc_state = doc_state;  /* Takes ownership */
    self->path = NULL;            /* Root has no path */

    return (PyObject *)self;
}

/* FyGeneric: __getitem__ for sequences and mappings */
static PyObject *
FyGeneric_subscript(FyGenericObject *self, PyObject *key)
{
    enum fy_generic_type type = fy_get_type(self->fyg);
    Py_ssize_t index;
    size_t count;
    fy_generic item;
    PyObject *key_str;
    const char *key_cstr;
    fy_generic value;

    if (type == FYGT_SEQUENCE) {
        /* Sequence indexing */
        if (!PyLong_Check(key)) {
            PyErr_SetString(PyExc_TypeError, "Sequence indices must be integers");
            return NULL;
        }

        index = PyLong_AsSsize_t(key);
        if (index == -1 && PyErr_Occurred())
            return NULL;

        count = fy_len(self->fyg);

        /* Handle negative indices */
        if (index < 0)
            index += count;

        if (index < 0 || (size_t)index >= count) {
            PyErr_SetString(PyExc_IndexError, "Sequence index out of range");
            return NULL;
        }

        /* Use fy_get() to get item by index */
        item = fy_get(self->fyg, (int)index, fy_invalid);
        if (!fy_generic_is_valid(item)) {
            PyErr_SetString(PyExc_IndexError, "Invalid item at index");
            return NULL;
        }

        return FyGeneric_from_parent(item, self, key);

    } else if (type == FYGT_MAPPING) {
        /* Mapping key lookup */
        key_str = PyObject_Str(key);
        if (key_str == NULL)
            return NULL;

        key_cstr = PyUnicode_AsUTF8(key_str);
        if (key_cstr == NULL) {
            fy_py_xdecref(key_str);
            return NULL;
        }

        /* Use fy_get() with string key */
        value = fy_get(self->fyg, key_cstr, fy_invalid);
        fy_py_xdecref(key_str);

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
            _Bool value;
            value = fy_cast(self->fyg, (_Bool)0);
            if (value)
                Py_RETURN_TRUE;
            else
                Py_RETURN_FALSE;
        }

        case FYGT_INT: {
            fy_generic_decorated_int dint;
            /* Get decorated int to check if it's unsigned and large */
            dint = fy_cast(self->fyg, fy_dint_empty);
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

        case FYGT_STRING:
            return fy_szstr_to_pyunicode(self->fyg);

        case FYGT_SEQUENCE: {
            fy_generic_sequence_handle seqh;
            PyObject *list;
            size_t i;

            seqh = fy_cast(self->fyg, fy_seq_handle_null);
            if (!seqh)
                return PyList_New(0);  /* empty [] */

            list = PyList_New(seqh->count);
            if (list == NULL)
                return NULL;

            for (i = 0; i < seqh->count; i++) {
                // NOT VERY EFFICIENT
                PyObject *index_obj = PyLong_FromSize_t(i);
                if (index_obj == NULL)
                    goto seq_err;

                PyObject *item_obj = FyGeneric_from_parent(seqh->items[i], self, index_obj);
                fy_py_xdecref(index_obj);
                if (item_obj == NULL)
                    goto seq_err;

                PyObject *converted = FyGeneric_to_python((FyGenericObject *)item_obj, NULL);
                fy_py_xdecref(item_obj);
                if (converted == NULL)
                    goto seq_err;

                PyList_SET_ITEM(list, i, converted);
            }
            return list;

        seq_err:
            /* index_obj/item_obj/converted are always NULL here (consumed before each check) */
            fy_py_xdecref(list);
            return NULL;
        }

        case FYGT_MAPPING: {
            fy_generic_mapping_handle maph;
            PyObject *dict;
            PyObject *path_key;
            PyObject *conv_key;
            PyObject *conv_val;
            size_t i;

            maph = fy_cast(self->fyg, fy_map_handle_null);
            if (!maph)
                return PyDict_New();  /* empty {} */

            dict = PyDict_New();
            if (dict == NULL)
                return NULL;

            path_key = NULL;
            conv_key = NULL;
            conv_val = NULL;

            for (i = 0; i < maph->count; i++) {
                path_key = fy_generic_to_python_primitive(maph->pairs[i].key);
                if (!path_key)
                    goto map_err;

                PyObject *key_obj = FyGeneric_from_parent(maph->pairs[i].key, self, path_key);
                if (key_obj == NULL)
                    goto map_err;

                conv_key = FyGeneric_to_python((FyGenericObject *)key_obj, NULL);
                fy_py_xdecref(key_obj);
                if (conv_key == NULL)
                    goto map_err;

                PyObject *val_obj = FyGeneric_from_parent(maph->pairs[i].value, self, path_key);
                fy_py_xdecref(path_key);
                path_key = NULL;
                if (val_obj == NULL)
                    goto map_err;

                conv_val = FyGeneric_to_python((FyGenericObject *)val_obj, NULL);
                fy_py_xdecref(val_obj);
                if (conv_val == NULL)
                    goto map_err;

                if (PyDict_SetItem(dict, conv_key, conv_val) < 0)
                    goto map_err;

                fy_py_release(&conv_key);
                fy_py_release(&conv_val);
            }
            return dict;

        map_err:
            /* key_obj/val_obj are always NULL here (consumed before each check) */
            fy_py_xdecref(path_key);
            fy_py_xdecref(conv_key);
            fy_py_xdecref(conv_val);
            fy_py_xdecref(dict);
            return NULL;
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

static PyObject *
FyGeneric_is_indirect(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    return PyBool_FromLong(fy_generic_is_indirect(self->fyg));
}

/* Helper: Convert a generic metadata field (tag/anchor/comment) to Python str.
 * Returns None if null/invalid, raises RuntimeError if not a string. */
static PyObject *
fy_generic_metadata_to_pystr(fy_generic meta, const char *name)
{
    if (fy_generic_is_null(meta) || fy_generic_is_invalid(meta))
        Py_RETURN_NONE;
    if (!fy_generic_is_string(meta)) {
        PyErr_Format(PyExc_RuntimeError, "%s is not a string", name);
        return NULL;
    }
    return fy_szstr_to_pyunicode(meta);
}

/* FyGeneric: Tag and anchor access methods */
static PyObject *
FyGeneric_get_tag(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    return fy_generic_metadata_to_pystr(fy_generic_get_tag(self->fyg), "tag");
}

static PyObject *
FyGeneric_get_anchor(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    return fy_generic_metadata_to_pystr(fy_generic_get_anchor(self->fyg), "anchor");
}

static PyObject *
FyGeneric_has_tag(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    fy_generic tag = fy_generic_get_tag(self->fyg);
    return PyBool_FromLong(!fy_generic_is_null(tag) && !fy_generic_is_invalid(tag));
}

static PyObject *
FyGeneric_has_anchor(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    fy_generic anchor = fy_generic_get_anchor(self->fyg);
    return PyBool_FromLong(!fy_generic_is_null(anchor) && !fy_generic_is_invalid(anchor));
}

static PyObject *
FyGeneric_get_diag(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    fy_generic diag = fy_generic_get_diag(self->fyg);

    /* Check if diag is null or invalid - no diagnostics available */
    if (fy_generic_is_null(diag) || fy_generic_is_invalid(diag))
        Py_RETURN_NONE;

    /* Diag is a sequence of error mappings - wrap it as FyGeneric, sharing parent's doc_state */
    return FyGeneric_from_parent(diag, self, NULL);
}

static PyObject *
FyGeneric_has_diag(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    fy_generic diag = fy_generic_get_diag(self->fyg);
    return PyBool_FromLong(!fy_generic_is_null(diag) && !fy_generic_is_invalid(diag));
}

static PyObject *
FyGeneric_get_marker(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    fy_generic marker;
    size_t len;
    PyObject *tuple;
    size_t i;

    marker = fy_generic_get_marker(self->fyg);

    /* Check if marker is null or invalid - no marker available */
    if (fy_generic_is_null(marker) || fy_generic_is_invalid(marker))
        Py_RETURN_NONE;

    /* Marker is a sequence of 6 ints: start_byte, start_line, start_col, end_byte, end_line, end_col */
    if (!fy_generic_is_sequence(marker)) {
        PyErr_SetString(PyExc_RuntimeError, "marker is not a sequence");
        return NULL;
    }

    len = fy_generic_sequence_get_item_count(marker);
    if (len != 6) {
        PyErr_Format(PyExc_RuntimeError, "marker has %zu elements, expected 6", len);
        return NULL;
    }

    tuple = PyTuple_New(6);
    if (!tuple)
        return NULL;

    for (i = 0; i < 6; i++) {
        fy_generic item = fy_generic_sequence_get_item_generic(marker, i);
        long long val = fy_cast(item, (long long)-1LL);
        PyObject *pyval = PyLong_FromLongLong(val);
        if (!pyval) {
            fy_py_xdecref(tuple);
            return NULL;
        }
        PyTuple_SET_ITEM(tuple, i, pyval);
    }

    return tuple;
}

static PyObject *
FyGeneric_has_marker(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    fy_generic marker = fy_generic_get_marker(self->fyg);
    return PyBool_FromLong(!fy_generic_is_null(marker) && !fy_generic_is_invalid(marker));
}

static PyObject *
FyGeneric_get_comment(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    return fy_generic_metadata_to_pystr(fy_generic_get_comment(self->fyg), "comment");
}

static PyObject *
FyGeneric_has_comment(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    fy_generic comment = fy_generic_get_comment(self->fyg);
    return PyBool_FromLong(!fy_generic_is_null(comment) && !fy_generic_is_invalid(comment));
}

/* Comparison helper functions */

/* Macro: rich-compare two scalar values (a, b) for the given Python op.
 * 'cleanup' is evaluated before every return (use (void)0 if nothing to clean up).
 * Returns True, False, or NotImplemented. */
#define RICHCMP_SCALAR(a, b, op, cleanup) \
    do { \
        int _richcmp_result; \
        switch (op) { \
            case Py_EQ: _richcmp_result = ((a) == (b)); break; \
            case Py_NE: _richcmp_result = ((a) != (b)); break; \
            case Py_LT: _richcmp_result = ((a) <  (b)); break; \
            case Py_LE: _richcmp_result = ((a) <= (b)); break; \
            case Py_GT: _richcmp_result = ((a) >  (b)); break; \
            case Py_GE: _richcmp_result = ((a) >= (b)); break; \
            default: { cleanup; Py_RETURN_NOTIMPLEMENTED; } \
        } \
        { cleanup; } \
        if (_richcmp_result) Py_RETURN_TRUE; \
        else Py_RETURN_FALSE; \
    } while(0)

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
            other_pyobj = fy_py_newref(other);
        } else {
            /* Try to extract as long long */
            other_val = PyLong_AsLongLong(other);
            if (other_val == -1 && PyErr_Occurred()) {
                /* other is too large for long long - use Python comparison */
                PyErr_Clear();
                self_pyobj = PyLong_FromLongLong(self_val);
                if (!self_pyobj) return NULL;
                fy_py_incref(other);
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
                fy_py_xdecref(self_pyobj);
                return NULL;
            }

            PyObject *self_float = PyFloat_FromDouble(self_as_float);
            PyObject *other_float = self_float ? FyGeneric_float((FyGenericObject *)other) : NULL;
            PyObject *result = (self_float && other_float)
                ? PyObject_RichCompare(self_float, other_float, op) : NULL;
            fy_py_xdecref(self_float);
            fy_py_xdecref(other_float);
            fy_py_xdecref(self_pyobj);
            return result;
        }

        /* Convert other FyGeneric to int using __int__ (handles type conversion) */
        PyObject *other_int = FyGeneric_int((FyGenericObject *)other);
        if (!other_int) {
            fy_py_xdecref(self_pyobj);
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
                    fy_py_xdecref(other_int);
                    return NULL;
                }
                other_pyobj = other_int;
                use_python_cmp = 1;
            } else {
                fy_py_xdecref(other_int);
            }
        }
    } else {
        fy_py_xdecref(self_pyobj);
        Py_RETURN_NOTIMPLEMENTED;
    }

    /* Perform comparison */
    if (use_python_cmp) {
        cmp_result = PyObject_RichCompareBool(self_pyobj, other_pyobj, op);
        fy_py_xdecref(self_pyobj);
        fy_py_xdecref(other_pyobj);
        if (cmp_result < 0) return NULL;
        if (cmp_result) Py_RETURN_TRUE;
        else Py_RETURN_FALSE;
    } else {
        /* Regular C long long comparison */
        RICHCMP_SCALAR(self_val, other_val, op, (void)0);
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
        fy_py_xdecref(other_float);
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    RICHCMP_SCALAR(self_val, other_val, op, (void)0);
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
            fy_py_xdecref(other_str_obj);
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

    RICHCMP_SCALAR(cmp, 0, op, fy_py_xdecref(other_str_obj));
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
        fy_py_xdecref(self_int);
        return result;
    } else if (PyFloat_Check(other)) {
        /* Promote to float comparison: bool(true) == float(1.0) should be True */
        double self_as_float = self_val ? 1.0 : 0.0;
        PyObject *self_float = PyFloat_FromDouble(self_as_float);
        if (!self_float)
            return NULL;
        PyObject *result = PyObject_RichCompare(self_float, other, op);
        fy_py_xdecref(self_float);
        return result;
    } else if (Py_TYPE(other) == &FyGenericType) {
        enum fy_generic_type other_type = fy_get_type(((FyGenericObject *)other)->fyg);
        if (other_type == FYGT_INT) {
            /* Promote to int comparison */
            PyObject *self_int = PyLong_FromLongLong(self_val ? 1 : 0);
            PyObject *other_int = self_int ? FyGeneric_int((FyGenericObject *)other) : NULL;
            PyObject *result = (self_int && other_int)
                ? PyObject_RichCompare(self_int, other_int, op) : NULL;
            fy_py_xdecref(self_int);
            fy_py_xdecref(other_int);
            return result;
        } else if (other_type == FYGT_FLOAT) {
            /* Promote to float comparison */
            PyObject *self_float = PyFloat_FromDouble(self_val ? 1.0 : 0.0);
            PyObject *other_float = self_float ? FyGeneric_float((FyGenericObject *)other) : NULL;
            PyObject *result = (self_float && other_float)
                ? PyObject_RichCompare(self_float, other_float, op) : NULL;
            fy_py_xdecref(self_float);
            fy_py_xdecref(other_float);
            return result;
        } else if (other_type == FYGT_BOOL) {
            other_val = fy_cast(((FyGenericObject *)other)->fyg, (_Bool)0);
        } else {
            Py_RETURN_NOTIMPLEMENTED;
        }
    } else {
        Py_RETURN_NOTIMPLEMENTED;
    }

    RICHCMP_SCALAR(self_val, other_val, op, (void)0);
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

/* Helper: Build a list by iterating over mapping pairs.
 * item_fn is called once per pair and returns the element to store (new ref),
 * or NULL on error.  path_key is a borrowed reference (caller owns it). */
typedef PyObject *(*mapping_item_fn)(const fy_generic_map_pair *pair,
                                     FyGenericObject *parent,
                                     PyObject *path_key);

static PyObject *
fy_generic_mapping_collect(FyGenericObject *self, const char *method_name,
                            mapping_item_fn item_fn)
{
    if (!fy_generic_is_mapping(self->fyg)) {
        PyErr_Format(PyExc_TypeError, "%s requires a mapping", method_name);
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

        PyObject *item = item_fn(&pairs[i], self, path_key);
        fy_py_xdecref(path_key);
        if (item == NULL)
            break;

        PyList_SET_ITEM(result, i, item);
    }
    if (i < count) {
        fy_py_xdecref(result);
        return NULL;
    }

    return result;
}

static PyObject *
mapping_item_key(const fy_generic_map_pair *pair, FyGenericObject *parent, PyObject *path_key)
{
    return FyGeneric_from_parent(pair->key, parent, path_key);
}

static PyObject *
mapping_item_value(const fy_generic_map_pair *pair, FyGenericObject *parent, PyObject *path_key)
{
    return FyGeneric_from_parent(pair->value, parent, path_key);
}

static PyObject *
mapping_item_kv(const fy_generic_map_pair *pair, FyGenericObject *parent, PyObject *path_key)
{
    PyObject *key = FyGeneric_from_parent(pair->key, parent, path_key);
    if (!key)
        return NULL;
    PyObject *value = FyGeneric_from_parent(pair->value, parent, path_key);
    if (!value) {
        fy_py_xdecref(key);
        return NULL;
    }
    PyObject *tuple = PyTuple_Pack(2, key, value);
    fy_py_xdecref(key);
    fy_py_xdecref(value);
    return tuple;
}

/* FyGeneric: keys() - return list of keys for mappings */
static PyObject *
FyGeneric_keys(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    return fy_generic_mapping_collect(self, "keys()", mapping_item_key);
}

/* FyGeneric: values() - return list of values for mappings */
static PyObject *
FyGeneric_values(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    return fy_generic_mapping_collect(self, "values()", mapping_item_value);
}

/* FyGeneric: items() - return list of (key, value) tuples for mappings */
static PyObject *
FyGeneric_items(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    return fy_generic_mapping_collect(self, "items()", mapping_item_kv);
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
            return fy_py_newref(Py_None);

        case FYGT_BOOL:
            return fy_py_newref(fy_cast(self->fyg, (_Bool)0) ? Py_True : Py_False);

        case FYGT_INT:
            return PyLong_FromLongLong(fy_cast(self->fyg, (long long)0));

        case FYGT_FLOAT:
            return PyFloat_FromDouble(fy_cast(self->fyg, (double)0.0));

        case FYGT_STRING:
            return fy_szstr_to_pyunicode(self->fyg);

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
    fy_py_xdecref(py_obj);

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
    fy_py_xdecref(py_obj);

    return attr;
}

/* Forward declaration - needed for set_at_path() */
static fy_generic python_to_generic(struct fy_generic_builder *gb, PyObject *obj);

/* FyGeneric: trim() - trim allocator to release unused memory */
static PyObject *
FyGeneric_trim(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    struct fy_generic_builder *gb = FYG_GB(self);
    if (gb)
        fy_gb_trim(gb);
    Py_RETURN_NONE;
}

/* FyGeneric: clone() - Create a clone of this FyGeneric object */
static PyObject *
FyGeneric_clone(FyGenericObject *self, PyObject *Py_UNUSED(args))
{
    struct fy_generic_builder *gb;
    const struct fy_generic_builder_cfg *parent_cfg;
    struct fy_generic_builder_cfg cfg;
    struct fy_generic_builder *new_gb;
    fy_generic cloned;
    PyObject *result;

    gb = FYG_GB(self);
    assert(gb != NULL);

    /* Get parent's config and copy it, but set allocator to NULL for new instance */
    parent_cfg = fy_generic_builder_get_cfg(gb);
    assert(parent_cfg != NULL);  /* Can't fail if gb is not NULL */

    cfg = *parent_cfg;
    cfg.allocator = NULL;  /* Force creation of new allocator (and new tags) */
    cfg.parent = NULL;     /* Independent builder, no parent chain */

    new_gb = fy_generic_builder_create(&cfg);

    if (new_gb == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create builder for clone");
        return NULL;
    }

    /* Internalize (copy) the generic value from THIS object (not root) */
    /* This creates a new root starting from the current value */
    cloned = fy_gb_internalize(new_gb, self->fyg);
    if (fy_generic_is_invalid(cloned)) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to clone generic value");
        goto err_out;
    }

    /* Create a new FyGeneric Python object with the cloned value */
    result = FyGeneric_from_generic(cloned, new_gb, FYG_MUTABLE(self));
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
    if (FYG_IS_ROOT(self)) {
        return PyTuple_New(0);
    }

    /* Return the path tuple (always available for nested values) */
    if (self->path == NULL) {
        /* Should not happen since we always build paths now */
        Py_RETURN_NONE;
    }

    return fy_py_newref(self->path);
}

/* FyGeneric: get_at_path(path) - Get value at path (root only) */
static PyObject *
FyGeneric_get_at_path(FyGenericObject *self, PyObject *path_obj)
{
    struct fy_generic_builder *gb;
    Py_ssize_t path_len;
    fy_generic *path_array;
    fy_generic result;
    FyGenericObject *child;
    Py_ssize_t i;

    /* This method only works on root objects */
    if (!FYG_IS_ROOT(self)) {
        PyErr_SetString(PyExc_TypeError,
                      "get_at_path() can only be called on root FyGeneric objects");
        return NULL;
    }

    gb = FYG_GB(self);
    if (!gb) {
        PyErr_SetString(PyExc_RuntimeError, "No builder available");
        return NULL;
    }

    /* Convert Python path to fy_generic array */
    if (!PyList_Check(path_obj) && !PyTuple_Check(path_obj)) {
        PyErr_SetString(PyExc_TypeError, "Path must be a list or tuple");
        return NULL;
    }

    path_len = PySequence_Size(path_obj);
    if (path_len < 0)
        return NULL;

    path_array = alloca(sizeof(fy_generic) * path_len);

    /* Convert each path element */
    for (i = 0; i < path_len; i++) {
        PyObject *elem = PySequence_GetItem(path_obj, i);
        if (elem == NULL)
            return NULL;

        /* Check for None and reject it */
        if (elem == Py_None) {
            fy_py_xdecref(elem);
            PyErr_SetString(PyExc_TypeError, "Path elements cannot be None");
            return NULL;
        }
        /* Check for bool BEFORE int (bool is subclass of int in Python) */
        else if (PyBool_Check(elem)) {
            bool val = (elem == Py_True);
            fy_py_xdecref(elem);
            path_array[i] = fy_value(val);
        }
        else if (PyLong_Check(elem)) {
            long idx = PyLong_AsLong(elem);
            fy_py_xdecref(elem);
            if (idx == -1 && PyErr_Occurred())
                return NULL;
            path_array[i] = fy_value((int)idx);
        } else if (PyFloat_Check(elem)) {
            double val = PyFloat_AS_DOUBLE(elem);
            fy_py_xdecref(elem);
            path_array[i] = fy_value(val);
        } else if (PyUnicode_Check(elem)) {
            const char *str = PyUnicode_AsUTF8(elem);
            fy_py_xdecref(elem);
            if (str == NULL)
                return NULL;
            path_array[i] = fy_value(str);
        } else {
            fy_py_xdecref(elem);
            PyErr_SetString(PyExc_TypeError, "Path elements must be integers, floats, booleans, or strings");
            return NULL;
        }
    }

    /* Call GET_AT_PATH operation */
    result = fy_generic_op(gb, FYGBOPF_GET_AT_PATH, self->fyg,
                           path_len, path_array);

    if (fy_generic_is_invalid(result)) {
        PyErr_SetString(PyExc_KeyError, "Path not found");
        return NULL;
    }

    /* Convert result to Python - create a child FyGeneric */
    child = (FyGenericObject *)PyObject_New(FyGenericObject, &FyGenericType);
    if (child == NULL)
        return NULL;

    child->fyg = result;
    child->doc_state = self->doc_state;
    fy_py_incref(child->doc_state);

    /* Build the path as a Python tuple for the child */
    if (path_len > 0) {
        child->path = PyTuple_New(path_len);
        if (child->path == NULL) {
            fy_py_xdecref(child);
            return NULL;
        }

        for (i = 0; i < path_len; i++) {
            PyObject *elem = PySequence_GetItem(path_obj, i);
            if (elem == NULL) {
                fy_py_xdecref(child);
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
    Py_ssize_t path_len;
    PyObject *parts;
    Py_ssize_t i;
    PyObject *sep;
    PyObject *result;
    PyObject *slash;
    PyObject *final;

    if (!PyList_Check(path_list) && !PyTuple_Check(path_list)) {
        PyErr_SetString(PyExc_TypeError, "Path must be a list or tuple");
        return NULL;
    }

    path_len = PySequence_Size(path_list);
    if (path_len < 0)
        return NULL;

    /* Empty list returns "/" */
    if (path_len == 0) {
        return PyUnicode_FromString("/");
    }

    /* Build the Unix path string */
    parts = PyList_New(0);
    if (parts == NULL)
        return NULL;

    for (i = 0; i < path_len; i++) {
        PyObject *elem = PySequence_GetItem(path_list, i);
        if (elem == NULL) {
            fy_py_xdecref(parts);
            return NULL;
        }

        PyObject *str_elem;
        if (PyLong_Check(elem)) {
            /* Convert integer index to string */
            str_elem = PyObject_Str(elem);
            fy_py_xdecref(elem);
        } else if (PyUnicode_Check(elem)) {
            str_elem = elem;  /* Already a string, transfer ownership */
        } else {
            /* Unknown type - convert to string */
            str_elem = PyObject_Str(elem);
            fy_py_xdecref(elem);
        }

        if (str_elem == NULL) {
            fy_py_xdecref(parts);
            return NULL;
        }

        if (PyList_Append(parts, str_elem) < 0) {
            fy_py_xdecref(str_elem);
            fy_py_xdecref(parts);
            return NULL;
        }
        fy_py_xdecref(str_elem);
    }

    /* Join with "/" */
    sep = PyUnicode_FromString("/");
    if (sep == NULL) {
        fy_py_xdecref(parts);
        return NULL;
    }

    result = PyUnicode_Join(sep, parts);
    fy_py_xdecref(sep);
    fy_py_xdecref(parts);

    if (result == NULL)
        return NULL;

    /* Prepend "/" */
    slash = PyUnicode_FromString("/");
    if (slash == NULL) {
        fy_py_xdecref(result);
        return NULL;
    }

    final = PyUnicode_Concat(slash, result);
    fy_py_xdecref(slash);
    fy_py_xdecref(result);

    return final;
}

/* Internal: Convert Unix-style path string to path list */
static PyObject *
unix_path_to_path_list_internal(const char *path_cstr)
{
    PyObject *path_without_slash;
    PyObject *sep;
    PyObject *parts;
    Py_ssize_t parts_len;
    PyObject *path_list;
    Py_ssize_t i;

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
    path_without_slash = PyUnicode_FromString(path_cstr + 1);
    if (path_without_slash == NULL)
        return NULL;

    sep = PyUnicode_FromString("/");
    if (sep == NULL) {
        fy_py_xdecref(path_without_slash);
        return NULL;
    }

    parts = PyUnicode_Split(path_without_slash, sep, -1);
    fy_py_xdecref(sep);
    fy_py_xdecref(path_without_slash);

    if (parts == NULL)
        return NULL;

    /* Convert string parts to proper types (int if numeric, string otherwise) */
    parts_len = PyList_Size(parts);
    path_list = PyList_New(parts_len);
    if (path_list == NULL) {
        fy_py_xdecref(parts);
        return NULL;
    }

    for (i = 0; i < parts_len; i++) {
        PyObject *part = PyList_GET_ITEM(parts, i);
        const char *part_str = PyUnicode_AsUTF8(part);
        if (part_str == NULL) {
            fy_py_xdecref(path_list);
            fy_py_xdecref(parts);
            return NULL;
        }

        /* Try to parse as integer */
        char *endptr;
        long idx = strtol(part_str, &endptr, 10);
        if (*endptr == '\0' && *part_str != '\0') {
            /* Successfully parsed as integer */
            PyObject *idx_obj = PyLong_FromLong(idx);
            if (idx_obj == NULL) {
                fy_py_xdecref(path_list);
                fy_py_xdecref(parts);
                return NULL;
            }
            PyList_SET_ITEM(path_list, i, idx_obj);
        } else {
            /* Keep as string */
            fy_py_incref(part);
            PyList_SET_ITEM(path_list, i, part);
        }
    }

    fy_py_xdecref(parts);
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
    if (FYG_IS_ROOT(self)) {
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
    const char *path_cstr;
    PyObject *path_list;
    PyObject *result;

    /* This method only works on root objects */
    if (!FYG_IS_ROOT(self)) {
        PyErr_SetString(PyExc_TypeError,
                      "get_at_unix_path() can only be called on root FyGeneric objects");
        return NULL;
    }

    if (!PyUnicode_Check(path_str)) {
        PyErr_SetString(PyExc_TypeError, "Path must be a string");
        return NULL;
    }

    path_cstr = PyUnicode_AsUTF8(path_str);
    if (path_cstr == NULL)
        return NULL;

    /* Handle empty string or just "/" as root */
    if (path_cstr[0] == '\0' || (path_cstr[0] == '/' && path_cstr[1] == '\0'))
        return fy_py_newref((PyObject *)self);

    /* Convert Unix path to path list using internal converter */
    path_list = unix_path_to_path_list_internal(path_cstr);
    if (path_list == NULL)
        return NULL;

    /* Call get_at_path with the parsed path list */
    result = FyGeneric_get_at_path(self, path_list);
    fy_py_xdecref(path_list);

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
    if (!FYG_IS_ROOT(self)) {
        PyErr_SetString(PyExc_TypeError,
                      "set_at_path() can only be called on root FyGeneric objects");
        return NULL;
    }

    /* Check if mutation is allowed */
    if (!FYG_MUTABLE(self)) {
        PyErr_SetString(PyExc_TypeError,
                      "This FyGeneric object is read-only. Create with mutable=True to enable mutation.");
        return NULL;
    }

    struct fy_generic_builder *gb;
    Py_ssize_t path_len;
    fy_generic new_value;
    fy_generic *path_array;
    fy_generic new_root;
    Py_ssize_t i;

    gb = FYG_GB(self);
    if (!gb) {
        PyErr_SetString(PyExc_RuntimeError, "No builder available");
        return NULL;
    }

    /* Convert Python path to fy_generic array */
    if (!PyList_Check(path_obj) && !PyTuple_Check(path_obj)) {
        PyErr_SetString(PyExc_TypeError, "Path must be a list or tuple");
        return NULL;
    }

    path_len = PySequence_Size(path_obj);
    if (path_len < 0)
        return NULL;

    if (path_len == 0) {
        PyErr_SetString(PyExc_ValueError, "Path cannot be empty");
        return NULL;
    }

    /* Convert value to generic */
    new_value = python_to_generic(gb, value_obj);
    if (fy_generic_is_invalid(new_value)) {
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_ValueError, "Failed to convert value to generic");
        return NULL;
    }

    /* Allocate path array: path elements + value */
    path_array = alloca(sizeof(fy_generic) * (path_len + 1));

    /* Convert each path element */
    for (i = 0; i < path_len; i++) {
        PyObject *elem = PySequence_GetItem(path_obj, i);
        if (elem == NULL)
            return NULL;

        /* Check for None and reject it */
        if (elem == Py_None) {
            fy_py_xdecref(elem);
            PyErr_SetString(PyExc_TypeError, "Path elements cannot be None");
            return NULL;
        }
        /* Check for bool BEFORE int (bool is subclass of int in Python) */
        else if (PyBool_Check(elem)) {
            bool val = (elem == Py_True);
            fy_py_xdecref(elem);
            path_array[i] = fy_value(val);
        }
        else if (PyLong_Check(elem)) {
            long idx = PyLong_AsLong(elem);
            fy_py_xdecref(elem);
            if (idx == -1 && PyErr_Occurred())
                return NULL;
            path_array[i] = fy_value((int)idx);
        } else if (PyFloat_Check(elem)) {
            double val = PyFloat_AS_DOUBLE(elem);
            fy_py_xdecref(elem);
            path_array[i] = fy_value(val);
        } else if (PyUnicode_Check(elem)) {
            const char *str = PyUnicode_AsUTF8(elem);
            fy_py_xdecref(elem);
            if (str == NULL)
                return NULL;
            path_array[i] = fy_value(str);
        } else {
            fy_py_xdecref(elem);
            PyErr_SetString(PyExc_TypeError, "Path elements must be integers, floats, booleans, or strings");
            return NULL;
        }
    }

    /* Add value as last element */
    path_array[path_len] = new_value;

    /* Call SET_AT_PATH operation */
    new_root = fy_generic_op(gb, FYGBOPF_SET_AT_PATH, self->fyg,
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
    if (!FYG_IS_ROOT(self)) {
        PyErr_SetString(PyExc_TypeError,
                      "set_at_unix_path() can only be called on root FyGeneric objects");
        return NULL;
    }

    if (!PyUnicode_Check(path_str)) {
        PyErr_SetString(PyExc_TypeError, "Path must be a string");
        return NULL;
    }

    const char *path_cstr;
    PyObject *path_list;
    PyObject *set_args;

    path_cstr = PyUnicode_AsUTF8(path_str);
    if (path_cstr == NULL)
        return NULL;

    /* Cannot set at root */
    if (path_cstr[0] == '\0' || (path_cstr[0] == '/' && path_cstr[1] == '\0')) {
        PyErr_SetString(PyExc_ValueError, "Cannot set value at root path '/'");
        return NULL;
    }

    /* Convert Unix path to path list using internal converter */
    path_list = unix_path_to_path_list_internal(path_cstr);
    if (path_list == NULL)
        return NULL;

    /* Create tuple of (path_list, value_obj) for set_at_path */
    set_args = PyTuple_Pack(2, path_list, value_obj);
    fy_py_xdecref(path_list);

    if (set_args == NULL)
        return NULL;

    /* Call set_at_path with the converted path list */
    PyObject *result = FyGeneric_set_at_path(self, set_args);
    fy_py_xdecref(set_args);

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
    fy_py_xdecref(result);
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
    int json_mode;
    struct fy_generic_builder *gb;
    unsigned int emit_flags;
    fy_generic emitted;
    fy_generic_sized_string szstr;
    const char *path;
    fy_generic result_g;
    int result_code;
    PyObject *yaml_str;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|Ospp", kwlist,
                                     &file_obj, &mode, &compact, &strip_newline))
        return NULL;

    json_mode = (strcmp(mode, "json") == 0);

    /* Get builder access */
    gb = FYG_GB(self);
    if (!gb) {
        PyErr_SetString(PyExc_RuntimeError, "No builder available");
        return NULL;
    }

    /* Build emit flags */
    emit_flags = build_emit_flags(json_mode, compact, 0, strip_newline);

    /* If no file specified, return as string (like dumps()) */
    if (file_obj == NULL || file_obj == Py_None) {
        emit_flags |= FYOPEF_OUTPUT_TYPE_STRING;

        /* Emit to string */
        emitted = fy_gb_emit(gb, self->fyg, emit_flags, NULL);
        if (!fy_generic_is_valid(emitted)) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to emit YAML/JSON");
            return NULL;
        }

        /* Extract the sized string from the generic */
        szstr = fy_cast(emitted, fy_szstr_empty);
        if (szstr.data == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to extract string from emitted generic");
            return NULL;
        }

        /* Create Python string (makes a copy) */
        return PyUnicode_FromStringAndSize(szstr.data, szstr.size);
    }

    /* Check if it's a file path (string) */
    if (PyUnicode_Check(file_obj)) {
        path = PyUnicode_AsUTF8(file_obj);
        if (path == NULL)
            return NULL;

        /* Emit to file using fy_gb_emit_file() */
        result_g = fy_gb_emit_file(gb, self->fyg, emit_flags, path);

        /* Check for success: should return integer 0 */
        if (!fy_generic_is_valid(result_g) || !fy_generic_is_int_type(result_g)) {
            PyErr_Format(PyExc_RuntimeError, "Failed to emit YAML/JSON to file: %s", path);
            return NULL;
        }

        result_code = fy_cast(result_g, -1);
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

    emitted = fy_gb_emit(gb, self->fyg, emit_flags, NULL);
    if (!fy_generic_is_valid(emitted)) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to emit YAML/JSON");
        return NULL;
    }

    /* Extract the sized string from the generic */
    szstr = fy_cast(emitted, fy_szstr_empty);
    if (szstr.data == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to extract string from emitted generic");
        return NULL;
    }

    /* Create Python string */
    yaml_str = PyUnicode_FromStringAndSize(szstr.data, szstr.size);
    if (yaml_str == NULL)
        return NULL;

    /* Write to file object */
    if (write_to_file_object(file_obj, yaml_str) < 0) {
        fy_py_xdecref(yaml_str);
        return NULL;
    }

    fy_py_xdecref(yaml_str);
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
    {"is_indirect", _PyCFunction_CAST(FyGeneric_is_indirect), METH_NOARGS,
     "Check if value is indirect (has tag or anchor)"},
    {"get_tag", _PyCFunction_CAST(FyGeneric_get_tag), METH_NOARGS,
     "Get the tag of this value (or None if no tag)"},
    {"get_anchor", _PyCFunction_CAST(FyGeneric_get_anchor), METH_NOARGS,
     "Get the anchor of this value (or None if no anchor)"},
    {"has_tag", _PyCFunction_CAST(FyGeneric_has_tag), METH_NOARGS,
     "Check if value has a tag"},
    {"has_anchor", _PyCFunction_CAST(FyGeneric_has_anchor), METH_NOARGS,
     "Check if value has an anchor"},
    {"get_diag", _PyCFunction_CAST(FyGeneric_get_diag), METH_NOARGS,
     "Get diagnostic info for this value (or None if not available)"},
    {"has_diag", _PyCFunction_CAST(FyGeneric_has_diag), METH_NOARGS,
     "Check if value has diagnostic info"},
    {"get_marker", _PyCFunction_CAST(FyGeneric_get_marker), METH_NOARGS,
     "Get position marker (start_byte, start_line, start_col, end_byte, end_line, end_col) or None"},
    {"has_marker", _PyCFunction_CAST(FyGeneric_has_marker), METH_NOARGS,
     "Check if value has a position marker"},
    {"get_comment", _PyCFunction_CAST(FyGeneric_get_comment), METH_NOARGS,
     "Get comment associated with this value (or None)"},
    {"has_comment", _PyCFunction_CAST(FyGeneric_has_comment), METH_NOARGS,
     "Check if value has an associated comment"},
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
    struct fy_generic_builder *gb;
    fy_generic key_generic;
    fy_generic result;

    if (type != FYGT_MAPPING && type != FYGT_SEQUENCE) {
        /* For non-container types, return not implemented */
        PyErr_SetString(PyExc_TypeError, "argument of type 'FyGeneric' is not iterable");
        return -1;
    }

    /* Get builder access */
    gb = FYG_GB(self);
    assert(gb != NULL);

    /* Convert Python key to generic */
    key_generic = python_to_generic(gb, key);
    if (fy_generic_is_invalid(key_generic)) {
        /* Conversion failed */
        PyErr_Clear();
        return 0;
    }

    /* For mapping, check if key exists (OP_CONTAINS doesn't work for mappings) */
    if (type == FYGT_MAPPING) {
        result = fy_generic_mapping_get_generic_default(self->fyg, key_generic, fy_invalid);
        return fy_generic_is_invalid(result) ? 0 : 1;
    }

    /* For sequence, use OP_CONTAINS operation */
    result = fy_gb_contains(gb, self->fyg, key_generic);
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
    struct fy_generic_builder *gb;
    fy_generic new_value;
    Py_ssize_t existing_path_len;
    Py_ssize_t total_path_len;
    Py_ssize_t total_len;
    fy_generic *path_array;
    Py_ssize_t i;
    fy_generic new_root;
    Py_ssize_t index;
    size_t count;
    PyObject *key_str;
    const char *key_cstr;

    /* Check if mutation is allowed */
    if (!FYG_MUTABLE(self)) {
        PyErr_SetString(PyExc_TypeError,
                      "This FyGeneric object is read-only. Create with mutable=True to enable mutation.");
        return -1;
    }

    gb = FYG_GB(self);
    if (!gb) {
        PyErr_SetString(PyExc_RuntimeError, "No builder available for mutation");
        return -1;
    }

    if (value == NULL) {
        PyErr_SetString(PyExc_NotImplementedError, "Deletion not yet supported");
        return -1;
    }

    /* Convert Python value to generic */
    new_value = python_to_generic(gb, value);
    if (fy_generic_is_invalid(new_value)) {
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_ValueError, "Failed to convert value to generic");
        return -1;
    }

    /* Build full path: self->path + [key] + [value] */
    existing_path_len = self->path ? PyTuple_Size(self->path) : 0;
    total_path_len = existing_path_len + 1;  /* +1 for new key */
    total_len = total_path_len + 1;  /* +1 for value */

    /* Allocate path array on stack */
    path_array = alloca(sizeof(fy_generic) * total_len);

    /* Copy existing path elements */
    for (i = 0; i < existing_path_len; i++) {
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
    if (type == FYGT_SEQUENCE) {
        /* Sequence indexing */
        if (!PyLong_Check(key)) {
            PyErr_SetString(PyExc_TypeError, "Sequence indices must be integers");
            return -1;
        }

        index = PyLong_AsSsize_t(key);
        if (index == -1 && PyErr_Occurred())
            return -1;

        count = fy_len(self->fyg);

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
        key_str = PyObject_Str(key);
        if (key_str == NULL)
            return -1;

        key_cstr = PyUnicode_AsUTF8(key_str);
        if (key_cstr == NULL) {
            fy_py_xdecref(key_str);
            return -1;
        }

        path_array[existing_path_len] = fy_value(key_cstr);
        fy_py_xdecref(key_str);

    } else {
        PyErr_SetString(PyExc_TypeError, "Object does not support item assignment");
        return -1;
    }

    /* Add value as last element */
    path_array[total_path_len] = new_value;

    /* Call SET_AT_PATH on root with full path */
    new_root = fy_generic_op(gb, FYGBOPF_SET_AT_PATH, FYG_ROOT_FYG(self),
                            total_len, path_array);

    if (fy_generic_is_invalid(new_root)) {
        PyErr_SetString(PyExc_RuntimeError, "SET_AT_PATH operation failed");
        return -1;
    }

    /* Update the root's fyg in doc_state */
    FYG_DOC_STATE(self)->root_fyg = new_root;

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
                fy_py_incref(obj);
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
        fy_py_xdecref(left_py);
        return NULL;  /* Error already set */
    }
    if (rc == -2) {
        fy_py_xdecref(left_py);
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
        fy_py_xdecref(left_py);
        fy_py_xdecref(right_py);
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
            fy_py_xdecref(left_obj);
            fy_py_xdecref(right_obj);
            return result_obj;
        }

        long long result;
        if (__builtin_add_overflow(left_op.int_val, right_op.int_val, &result)) {
            /* Overflow - use Python's arbitrary precision */
            PyObject *left_obj = PyLong_FromLongLong(left_op.int_val);
            PyObject *right_obj = PyLong_FromLongLong(right_op.int_val);
            PyObject *result_obj = PyNumber_Add(left_obj, right_obj);
            fy_py_xdecref(left_obj);
            fy_py_xdecref(right_obj);
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
        fy_py_xdecref(left_py);
        return NULL;
    }
    if (rc == -2) {
        fy_py_xdecref(left_py);
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
        fy_py_xdecref(left_py);
        fy_py_xdecref(right_py);
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
            fy_py_xdecref(left_obj);
            fy_py_xdecref(right_obj);
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
        fy_py_xdecref(left_py);
        return NULL;
    }
    if (rc == -2) {
        fy_py_xdecref(left_py);
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
        fy_py_xdecref(left_py);
        fy_py_xdecref(right_py);
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
            fy_py_xdecref(left_obj);
            fy_py_xdecref(right_obj);
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
        fy_py_xdecref(left_py);
        return NULL;
    }
    if (rc == -2) {
        fy_py_xdecref(left_py);
        Py_RETURN_NOTIMPLEMENTED;
    }

    /* Convert both to double (true division always returns float) */
    double left_val, right_val;

    if (left_py) {
        left_val = PyLong_AsDouble(left_py);
        fy_py_xdecref(left_py);
    } else if (left_op.is_int) {
        left_val = left_op.is_unsigned_large ? (double)left_op.uint_val : (double)left_op.int_val;
    } else {
        left_val = left_op.float_val;
    }

    if (right_py) {
        right_val = PyLong_AsDouble(right_py);
        fy_py_xdecref(right_py);
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
        fy_py_xdecref(left_py);
        return NULL;
    }
    if (rc == -2) {
        fy_py_xdecref(left_py);
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
            fy_py_xdecref(left_py);
            fy_py_xdecref(right_py);
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
            fy_py_xdecref(left_py);
        } else if (left_op.is_int) {
            left_val = left_op.is_unsigned_large ? (double)left_op.uint_val : (double)left_op.int_val;
        } else {
            left_val = left_op.float_val;
        }

        if (right_py) {
            right_val = PyLong_AsDouble(right_py);
            fy_py_xdecref(right_py);
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
        fy_py_xdecref(left_py);
        return NULL;
    }
    if (rc == -2) {
        fy_py_xdecref(left_py);
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
            fy_py_xdecref(left_py);
            fy_py_xdecref(right_py);
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
            fy_py_xdecref(left_py);
        } else if (left_op.is_int) {
            left_val = left_op.is_unsigned_large ? (double)left_op.uint_val : (double)left_op.int_val;
        } else {
            left_val = left_op.float_val;
        }

        if (right_py) {
            right_val = PyLong_AsDouble(right_py);
            fy_py_xdecref(right_py);
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
        return fy_py_newref((PyObject *)Py_TYPE(Py_None));
    case FYGT_BOOL:
        return fy_py_newref((PyObject *)&PyBool_Type);
    case FYGT_INT:
        return fy_py_newref((PyObject *)&PyLong_Type);
    case FYGT_FLOAT:
        return fy_py_newref((PyObject *)&PyFloat_Type);
    case FYGT_STRING:
        return fy_py_newref((PyObject *)&PyUnicode_Type);
    case FYGT_SEQUENCE:
        return fy_py_newref((PyObject *)&PyList_Type);
    case FYGT_MAPPING:
        return fy_py_newref((PyObject *)&PyDict_Type);
    default:
        return fy_py_newref((PyObject *)&FyGenericType);
    }
}

/* FyGeneric: __class__ property setter - disallow changes */
static int
FyGeneric_set_class(FyGenericObject *Py_UNUSED(self), PyObject *Py_UNUSED(value), void *Py_UNUSED(closure))
{
    PyErr_SetString(PyExc_TypeError, "__class__ assignment not supported for FyGeneric");
    return -1;
}

/* FyGeneric: document_state property getter */
static PyObject *
FyGeneric_get_document_state(FyGenericObject *self, void *Py_UNUSED(closure))
{
    /* document_state is available for objects that have VDS stored */
    /* This includes both single-doc roots and multi-doc documents */

    fy_generic vds = FYG_VDS(self);
    if (!fy_generic_is_valid(vds))
        return fy_py_newref(Py_None);

    return fy_py_newref(self->doc_state);
}

/* FyGeneric getsetters */
static PyGetSetDef FyGeneric_getsetters[] = {
    {"__class__", (getter)FyGeneric_get_class, (setter)FyGeneric_set_class,
     "Dynamic class based on wrapped generic type", NULL},
    {"document_state", (getter)FyGeneric_get_document_state, NULL,
     "Document state with version and tag directives (None if not available)", NULL},
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
        fy_py_incref(temp_obj);
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

    case FYGT_STRING:
        /* Hash for strings */
        temp_obj = fy_szstr_to_pyunicode(self->fyg);
        break;

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
    fy_py_xdecref(temp_obj);

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

/* Helper: Parse mode string and return appropriate FYOPPF flags
 * Supported modes:
 *   - "yaml" or "yaml1.2" or "1.2" → YAML 1.2 (default)
 *   - "yaml1.1" or "1.1" → YAML 1.1 (supports merge keys)
 *   - "yaml1.1-pyyaml" or "pyyaml" → YAML 1.1 with PyYAML quirks
 *   - "json" → JSON mode
 * Returns 0 on error (with Python exception set)
 */
static unsigned int
parse_mode_flags(const char *mode)
{
    if (mode == NULL || strcmp(mode, "yaml") == 0 ||
        strcmp(mode, "yaml1.2") == 0 || strcmp(mode, "1.2") == 0) {
        return FYOPPF_MODE_YAML_1_2;
    } else if (strcmp(mode, "yaml1.1") == 0 || strcmp(mode, "1.1") == 0) {
        return FYOPPF_MODE_YAML_1_1;
    } else if (strcmp(mode, "yaml1.1-pyyaml") == 0 || strcmp(mode, "pyyaml") == 0) {
        return FYOPPF_MODE_YAML_1_1_PYYAML;
    } else if (strcmp(mode, "json") == 0) {
        return FYOPPF_MODE_JSON;
    } else {
        PyErr_Format(PyExc_ValueError,
            "Invalid mode '%s'. Supported modes: 'yaml', 'yaml1.1', 'yaml1.1-pyyaml', 'pyyaml', 'yaml1.2', '1.1', '1.2', 'json'",
            mode);
        return 0;
    }
}

/* loads(string, mode='yaml', dedup=True, trim=True, mutable=False, create_markers=False, keep_comments=False, keep_style=False) - Parse YAML/JSON from string
 *
 * mode can be:
 *   - 'yaml' or 'yaml1.2' or '1.2': YAML 1.2 (default)
 *   - 'yaml1.1' or '1.1': YAML 1.1 (supports merge keys <<)
 *   - 'json': JSON mode
 */
static PyObject *
libfyaml_loads(PyObject *self, PyObject *args, PyObject *kwargs)
{
    const char *yaml_str;
    Py_ssize_t yaml_len;
    const char *mode = "yaml";
    int dedup = 1;  /* Default to True */
    int trim = 1;   /* Default to True */
    int mutable = 0;  /* Default to False (read-only) */
    int collect_diag = 0;  /* Default to False */
    int create_markers = 0;  /* Default to False */
    int keep_comments = 0;  /* Default to False */
    int keep_style = 0;  /* Default to False */
    static char *kwlist[] = {"s", "mode", "dedup", "trim", "mutable", "collect_diag", "create_markers", "keep_comments", "keep_style", NULL};
    unsigned int mode_flags;
    struct fy_generic_builder *gb = NULL;
    unsigned int parse_flags;
    fy_generic vdir;
    int doc_count;
    fy_generic vds;
    PyObject *result;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s#|sppppppp", kwlist, &yaml_str, &yaml_len, &mode, &dedup, &trim, &mutable, &collect_diag, &create_markers, &keep_comments, &keep_style))
        return NULL;

    /* Parse mode string to flags */
    mode_flags = parse_mode_flags(mode);
    if (mode_flags == 0)
        return NULL;  /* Exception already set */

    /* Create generic builder using helper */
    gb = create_builder_with_config(dedup, yaml_len * 2);
    if (gb == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
        return NULL;
    }

    /* Parse - returns a directory (sequence of VDS) */
    parse_flags = FYOPPF_INPUT_TYPE_STRING | mode_flags;
    if (collect_diag)
        parse_flags |= FYOPPF_COLLECT_DIAG;
    if (create_markers)
        parse_flags |= FYOPPF_CREATE_MARKERS;
    if (keep_comments)
        parse_flags |= FYOPPF_KEEP_COMMENTS;
    if (keep_style)
        parse_flags |= FYOPPF_KEEP_STYLE;
    vdir = fy_gb_parse(gb, yaml_str, parse_flags, NULL);

    /* When collect_diag is enabled, errors return an indirect with diag attached */
    if (collect_diag) {
        fy_generic diag = fy_generic_get_diag(vdir);
        if (fy_generic_is_valid(diag) && !fy_generic_is_null(diag)) {
            /* Return the diag sequence directly - it contains the error info */
            PyObject *result = FyGeneric_from_generic(diag, gb, mutable);
            if (result == NULL) {
                fy_generic_builder_destroy(gb);
                return NULL;
            }
            return result;
        }
    }

    if (!fy_generic_is_valid(vdir)) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_ValueError, "Failed to parse YAML/JSON");
        return NULL;
    }

    /* Get document count - for loads() we expect exactly one */
    doc_count = fy_generic_dir_get_document_count(vdir);
    if (doc_count < 1) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_ValueError, "No documents found in input");
        return NULL;
    }
    if (doc_count > 1) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_ValueError, "Multiple documents found; use loads_all() instead");
        return NULL;
    }

    /* Get VDS for the single document */
    vds = fy_generic_dir_get_document_vds(vdir, 0);
    if (!fy_generic_is_valid(vds)) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to get document VDS");
        return NULL;
    }

    /* Create root wrapper with VDS - transfers gb ownership to Python object */
    result = FyGeneric_from_vds(vds, gb, mutable);
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
        Py_ssize_t len;
        fy_generic *items;
        Py_ssize_t i;
        fy_generic result;

        len = PySequence_Length(obj);
        if (len < 0)
            return fy_invalid;

        if (len == 0) {
            /* Empty sequence */
            return fy_gb_sequence_create(gb, 0, NULL);
        }

        /* Build array of fy_generic items */
        items = malloc(len * sizeof(fy_generic));
        if (items == NULL) {
            PyErr_NoMemory();
            return fy_invalid;
        }

        for (i = 0; i < len; i++) {
            PyObject *item = PySequence_GetItem(obj, i);
            if (item == NULL) {
                free(items);
                return fy_invalid;
            }

            items[i] = python_to_generic(gb, item);
            fy_py_xdecref(item);

            if (!fy_generic_is_valid(items[i])) {
                free(items);
                return fy_invalid;
            }
        }

        result = fy_gb_sequence_create(gb, len, items);
        free(items);
        return result;
    }

    if (PyDict_Check(obj)) {
        Py_ssize_t len;
        fy_generic *pairs;
        PyObject *key;
        PyObject *value;
        Py_ssize_t pos;
        Py_ssize_t idx;
        fy_generic result;

        len = PyDict_Size(obj);
        if (len < 0)
            return fy_invalid;

        if (len == 0) {
            /* Empty mapping */
            return fy_gb_mapping_create(gb, 0, NULL);
        }

        /* Build array of key-value pairs */
        pairs = malloc(len * 2 * sizeof(fy_generic));
        if (pairs == NULL) {
            PyErr_NoMemory();
            return fy_invalid;
        }

        pos = 0;
        idx = 0;

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

        result = fy_gb_mapping_create(gb, len, pairs);
        free(pairs);
        return result;
    }

    /* Unsupported type */
    PyErr_Format(PyExc_TypeError, "Cannot convert type '%s' to YAML",
                 Py_TYPE(obj)->tp_name);
    return fy_invalid;
}

/* dumps(obj, **options) - Serialize Python object to YAML/JSON string
 *
 * style can be:
 *   - None (default): uses 'block' for YAML, auto for JSON
 *   - 'default' or 'original': preserves original style from parsed document
 *   - 'block': block style output
 *   - 'flow': flow style output
 *   - 'pretty': pretty output
 *   - 'compact': compact output
 *   - 'oneline': single-line output
 */
static PyObject *
libfyaml_dumps(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *obj;
    int compact = 0;
    int json_mode = 0;
    const char *style = NULL;
    int indent = 0;  /* 0 means default/unset */
    static char *kwlist[] = {"obj", "compact", "json", "style", "indent", NULL};
    struct fy_generic_builder *gb;
    fy_generic g;
    unsigned int emit_flags;
    fy_generic emitted;
    fy_generic_sized_string szstr;
    PyObject *result;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|ppzi", kwlist,
                                     &obj, &compact, &json_mode, &style, &indent))
        return NULL;

    /* Create generic builder */
    gb = fy_generic_builder_create(NULL);
    if (gb == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
        return NULL;
    }

    /* Convert Python object to fy_generic */
    g = python_to_generic(gb, obj);
    if (!fy_generic_is_valid(g)) {
        fy_generic_builder_destroy(gb);
        /* Exception already set by python_to_generic */
        return NULL;
    }

    /* Determine emit flags based on options */
    emit_flags = FYOPEF_DISABLE_DIRECTORY | FYOPEF_OUTPUT_TYPE_STRING;

    /* Apply indent flag based on requested indent value */
    if (indent > 0) {
        switch (indent) {
            case 1: emit_flags |= FYOPEF_INDENT_1; break;
            case 2: emit_flags |= FYOPEF_INDENT_2; break;
            case 3: emit_flags |= FYOPEF_INDENT_3; break;
            case 4: emit_flags |= FYOPEF_INDENT_4; break;
            case 5:
            case 6: emit_flags |= FYOPEF_INDENT_6; break;
            case 7:
            case 8:
            default: emit_flags |= FYOPEF_INDENT_8; break;
        }
    } else {
        /* Default indent is 2 */
        emit_flags |= FYOPEF_INDENT_2;
    }

    if (json_mode) {
        emit_flags |= FYOPEF_MODE_JSON;
    } else {
        emit_flags |= FYOPEF_MODE_YAML_1_2;
        /* Parse style string if provided */
        if (style != NULL) {
            if (strcmp(style, "default") == 0 || strcmp(style, "original") == 0) {
                emit_flags |= FYOPEF_STYLE_DEFAULT;
            } else if (strcmp(style, "block") == 0) {
                emit_flags |= FYOPEF_STYLE_BLOCK;
            } else if (strcmp(style, "flow") == 0) {
                emit_flags |= FYOPEF_STYLE_FLOW;
            } else if (strcmp(style, "pretty") == 0) {
                emit_flags |= FYOPEF_STYLE_PRETTY;
            } else if (strcmp(style, "compact") == 0) {
                emit_flags |= FYOPEF_STYLE_COMPACT;
            } else if (strcmp(style, "oneline") == 0) {
                emit_flags |= FYOPEF_STYLE_ONELINE;
            } else {
                fy_generic_builder_destroy(gb);
                PyErr_Format(PyExc_ValueError, "Unknown style: '%s'. Expected: default, original, block, flow, pretty, compact, or oneline", style);
                return NULL;
            }
        } else if (compact) {
            emit_flags |= FYOPEF_STYLE_FLOW;
        } else {
            emit_flags |= FYOPEF_STYLE_BLOCK;
        }
    }

    /* Emit to string using new API - returns a string generic! */
    emitted = fy_gb_emit(gb, g, emit_flags, NULL);
    if (!fy_generic_is_valid(emitted)) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to emit YAML/JSON");
        return NULL;
    }

    /* Extract the sized string from the generic */
    szstr = fy_cast(emitted, fy_szstr_empty);
    if (szstr.data == NULL) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to extract string from emitted generic");
        return NULL;
    }

    /* Create Python string (makes a copy) */
    result = PyUnicode_FromStringAndSize(szstr.data, szstr.size);

    /* Clean up builder */
    fy_generic_builder_destroy(gb);

    return result;  /* NULL propagates error to Python */
}

/* from_python(obj, tag=None, style=None, mutable=False, dedup=True) - Convert Python object to FyGeneric */
static PyObject *
libfyaml_from_python(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *obj;
    const char *tag = NULL;
    Py_ssize_t tag_len = 0;
    const char *style = NULL;
    int mutable = 0;  /* Default to False (read-only) */
    int dedup = 1;  /* Default to True, like loads/load */
    static char *kwlist[] = {"obj", "tag", "style", "mutable", "dedup", NULL};
    struct fy_generic_builder *gb;
    fy_generic g;
    enum fy_scalar_style scalar_style;
    fy_generic tag_generic;
    fy_generic style_generic;
    PyObject *result;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|z#zpp", kwlist, &obj, &tag, &tag_len, &style, &mutable, &dedup))
        return NULL;

    /* Create generic builder using helper (estimate 64KB for typical Python objects) */
    gb = create_builder_with_config(dedup, 64 * 1024);
    if (gb == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
        return NULL;
    }

    /* Convert Python object to generic */
    g = python_to_generic(gb, obj);
    if (!fy_generic_is_valid(g)) {
        fy_generic_builder_destroy(gb);
        return NULL;
    }

    /* Parse style string to scalar style enum */
    scalar_style = FYSS_ANY;
    if (style != NULL && style[0] != '\0') {
        if (strcmp(style, "|") == 0) {
            scalar_style = FYSS_LITERAL;
        } else if (strcmp(style, ">") == 0) {
            scalar_style = FYSS_FOLDED;
        } else if (strcmp(style, "'") == 0) {
            scalar_style = FYSS_SINGLE_QUOTED;
        } else if (strcmp(style, "\"") == 0) {
            scalar_style = FYSS_DOUBLE_QUOTED;
        } else if (strcmp(style, "") == 0 || strcmp(style, "plain") == 0) {
            scalar_style = FYSS_PLAIN;
        }
        /* else leave as FYSS_ANY */
    }

    /* If tag or style is provided, wrap in indirect */
    if ((tag != NULL && tag_len > 0) || scalar_style != FYSS_ANY) {
        uintptr_t flags = FYGIF_VALUE;
        tag_generic = fy_null;

        if (tag != NULL && tag_len > 0) {
            tag_generic = fy_gb_string_size_create(gb, tag, (size_t)tag_len);
            if (!fy_generic_is_valid(tag_generic)) {
                fy_generic_builder_destroy(gb);
                PyErr_SetString(PyExc_RuntimeError, "Failed to create tag string");
                return NULL;
            }
            flags |= FYGIF_TAG;
        }

        /* Create style generic if needed */
        style_generic = fy_null;
        if (scalar_style != FYSS_ANY) {
            style_generic.v = fy_generic_in_place_unsigned_int((unsigned int)scalar_style);
            flags |= FYGIF_STYLE;
        }

        struct fy_generic_indirect gi = {
            .flags = flags,
            .value = g,
            .anchor = fy_null,
            .tag = tag_generic,
            .diag = fy_null,
            .marker = fy_null,
            .comment = fy_null,
            .style = style_generic,
            .failsafe_str = fy_null
        };
        g = fy_gb_indirect_create(gb, &gi);
        if (!fy_generic_is_valid(g)) {
            fy_generic_builder_destroy(gb);
            PyErr_SetString(PyExc_RuntimeError, "Failed to create tagged generic");
            return NULL;
        }
    }

    /* Create root wrapper - transfers gb ownership to Python object */
    result = FyGeneric_from_generic(g, gb, mutable);
    if (result == NULL) {
        fy_generic_builder_destroy(gb);
        return NULL;
    }

    return result;
}

/* load(file, mode='yaml', dedup=True, trim=True, mutable=False, collect_diag=False, keep_style=False) - Load YAML/JSON from file object or path */
static PyObject *
libfyaml_load(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *file_obj;
    const char *mode = "yaml";
    int dedup = 1;  /* Default to True */
    int trim = 1;   /* Default to True */
    int mutable = 0;  /* Default to False (read-only) */
    int collect_diag = 0;  /* Default to False */
    int create_markers = 0;  /* Default to False */
    int keep_comments = 0;  /* Default to False */
    int keep_style = 0;  /* Default to False */
    static char *kwlist[] = {"file", "mode", "dedup", "trim", "mutable", "collect_diag", "create_markers", "keep_comments", "keep_style", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|sppppppp", kwlist, &file_obj, &mode, &dedup, &trim, &mutable, &collect_diag, &create_markers, &keep_comments, &keep_style))
        return NULL;

    /* Check if it's a string (file path) or file object */
    if (PyUnicode_Check(file_obj)) {
        /* It's a file path - use fy_gb_parse_file() with mmap support */
        const char *path;
        struct fy_generic_builder *gb;
        unsigned int mode_flags;
        unsigned int parse_flags;
        fy_generic vdir;
        int doc_count;
        fy_generic vds;
        PyObject *result;

        path = PyUnicode_AsUTF8(file_obj);
        if (path == NULL)
            return NULL;

        /* Create generic builder using helper (1MB default estimate for files) */
        gb = create_builder_with_config(dedup, 1024 * 1024);
        if (gb == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
            return NULL;
        }

        /* Parse mode string to flags */
        mode_flags = parse_mode_flags(mode);
        if (mode_flags == 0)
            return NULL;  /* Exception already set */

        /* Determine parse flags */
        parse_flags = mode_flags;
        if (collect_diag)
            parse_flags |= FYOPPF_COLLECT_DIAG;
        if (create_markers)
            parse_flags |= FYOPPF_CREATE_MARKERS;
        if (keep_comments)
            parse_flags |= FYOPPF_KEEP_COMMENTS;
        if (keep_style)
            parse_flags |= FYOPPF_KEEP_STYLE;

        /* Parse from file - returns a directory (sequence of VDS) */
        vdir = fy_gb_parse_file(gb, parse_flags, path);
        if (!fy_generic_is_valid(vdir)) {
            fy_generic_builder_destroy(gb);
            PyErr_Format(PyExc_ValueError, "Failed to parse YAML/JSON from file: %s", path);
            return NULL;
        }

        /* Get document count - for load() we expect exactly one */
        doc_count = fy_generic_dir_get_document_count(vdir);
        if (doc_count < 1) {
            fy_generic_builder_destroy(gb);
            PyErr_SetString(PyExc_ValueError, "No documents found in file");
            return NULL;
        }
        if (doc_count > 1) {
            fy_generic_builder_destroy(gb);
            PyErr_SetString(PyExc_ValueError, "Multiple documents found; use load_all() instead");
            return NULL;
        }

        /* Get VDS for the single document */
        vds = fy_generic_dir_get_document_vds(vdir, 0);
        if (!fy_generic_is_valid(vds)) {
            fy_generic_builder_destroy(gb);
            PyErr_SetString(PyExc_RuntimeError, "Failed to get document VDS");
            return NULL;
        }

        /* Create root wrapper with VDS - transfers gb ownership to Python object */
        result = FyGeneric_from_vds(vds, gb, mutable);
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
            fy_py_xdecref(content);
            return NULL;
        }

        PyObject *mode_str = PyUnicode_FromString(mode);
        if (mode_str == NULL) {
            fy_py_xdecref(loads_kwargs);
            fy_py_xdecref(content);
            return NULL;
        }
        PyDict_SetItemString(loads_kwargs, "mode", mode_str);
        fy_py_xdecref(mode_str);

        PyObject *dedup_obj = PyBool_FromLong(dedup);
        PyDict_SetItemString(loads_kwargs, "dedup", dedup_obj);
        fy_py_xdecref(dedup_obj);

        PyObject *trim_obj = PyBool_FromLong(trim);
        PyDict_SetItemString(loads_kwargs, "trim", trim_obj);
        fy_py_xdecref(trim_obj);

        PyObject *mutable_obj = PyBool_FromLong(mutable);
        PyDict_SetItemString(loads_kwargs, "mutable", mutable_obj);
        fy_py_xdecref(mutable_obj);

        PyObject *markers_obj = PyBool_FromLong(create_markers);
        PyDict_SetItemString(loads_kwargs, "create_markers", markers_obj);
        fy_py_xdecref(markers_obj);

        PyObject *comments_obj = PyBool_FromLong(keep_comments);
        PyDict_SetItemString(loads_kwargs, "keep_comments", comments_obj);
        fy_py_xdecref(comments_obj);

        PyObject *style_obj = PyBool_FromLong(keep_style);
        PyDict_SetItemString(loads_kwargs, "keep_style", style_obj);
        fy_py_xdecref(style_obj);

        /* Call loads with the content */
        PyObject *loads_args = Py_BuildValue("(O)", content);
        PyObject *result = libfyaml_loads(self, loads_args, loads_kwargs);

        fy_py_xdecref(loads_args);
        fy_py_xdecref(loads_kwargs);
        fy_py_xdecref(content);
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

        fy_py_xdecref(compact_bool);
        fy_py_xdecref(json_bool);

        /* Convert object to YAML string */
        PyObject *dumps_args = Py_BuildValue("(O)", obj);
        PyObject *yaml_str = libfyaml_dumps(self, dumps_args, dumps_kwargs);

        fy_py_xdecref(dumps_args);
        fy_py_xdecref(dumps_kwargs);

        if (yaml_str == NULL)
            return NULL;

        /* Write to file object */
        PyObject *result = PyObject_CallMethod(file_obj, "write", "O", yaml_str);
        fy_py_xdecref(yaml_str);

        if (result == NULL)
            return NULL;

        fy_py_xdecref(result);
        Py_RETURN_NONE;
    }
}

/* Helper: Create FyGeneric from VDS with reference to parent's doc_state (for multi-doc) */
static PyObject *
FyGeneric_from_vds_with_parent(fy_generic vds, FyGenericObject *parent, int mutable)
{
    fy_generic fyg;
    FyDocumentStateObject *parent_ds;
    PyObject *doc_state;
    FyGenericObject *self;

    /* Extract root from VDS */
    fyg = fy_generic_vds_get_root(vds);
    if (!fy_generic_is_valid(fyg))
        return NULL;

    /* Create child doc_state that references parent's doc_state */
    parent_ds = (FyDocumentStateObject *)parent->doc_state;
    doc_state = FyDocumentState_create_child(fyg, vds, parent_ds);
    if (doc_state == NULL)
        return NULL;

    self = (FyGenericObject *)FyGenericType.tp_alloc(&FyGenericType, 0);
    if (self == NULL) {
        fy_py_xdecref(doc_state);
        return NULL;
    }

    self->fyg = fyg;
    self->doc_state = doc_state;  /* Takes ownership */
    self->path = NULL;            /* Root of this document */

    return (PyObject *)self;
}

/* loads_all(string, mode='yaml', dedup=True, trim=True, mutable=False, collect_diag=False, create_markers=False, keep_comments=False, keep_style=False) - Parse multi-document YAML */
static PyObject *
libfyaml_loads_all(PyObject *self, PyObject *args, PyObject *kwargs)
{
    const char *yaml_str;
    Py_ssize_t yaml_len;
    const char *mode = "yaml";
    int dedup = 1;
    int trim = 1;
    int mutable = 0;
    int collect_diag = 0;
    int create_markers = 0;
    int keep_comments = 0;
    int keep_style = 0;
    static char *kwlist[] = {"s", "mode", "dedup", "trim", "mutable", "collect_diag", "create_markers", "keep_comments", "keep_style", NULL};
    struct fy_generic_builder *gb;
    unsigned int mode_flags;
    unsigned int parse_flags;
    fy_generic vdir;
    int doc_count;
    PyObject *holder_doc_state;
    FyGenericObject *holder;
    PyObject *result;
    int i;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s#|sppppppp", kwlist, &yaml_str, &yaml_len, &mode, &dedup, &trim, &mutable, &collect_diag, &create_markers, &keep_comments, &keep_style))
        return NULL;

    /* Create generic builder using helper */
    gb = create_builder_with_config(dedup, yaml_len * 2);
    if (gb == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
        return NULL;
    }

    /* Parse mode string to flags */
    mode_flags = parse_mode_flags(mode);
    if (mode_flags == 0)
        return NULL;  /* Exception already set */

    /* Parse flags: MULTI_DOCUMENT for multiple docs */
    parse_flags = FYOPPF_INPUT_TYPE_STRING | FYOPPF_MULTI_DOCUMENT | mode_flags;
    if (collect_diag)
        parse_flags |= FYOPPF_COLLECT_DIAG;
    if (create_markers)
        parse_flags |= FYOPPF_CREATE_MARKERS;
    if (keep_comments)
        parse_flags |= FYOPPF_KEEP_COMMENTS;
    if (keep_style)
        parse_flags |= FYOPPF_KEEP_STYLE;

    /* Parse - returns a directory (sequence of VDS) */
    vdir = fy_gb_parse(gb, yaml_str, parse_flags, NULL);
    if (!fy_generic_is_valid(vdir)) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_ValueError, "Failed to parse YAML/JSON");
        return NULL;
    }

    /* Get document count */
    doc_count = fy_generic_dir_get_document_count(vdir);

    /* Create a holder FyGeneric that owns the builder (invisible to user) */
    /* Create doc_state first (it owns the builder) */
    holder_doc_state = FyDocumentState_create(vdir, fy_invalid, gb, mutable);
    if (holder_doc_state == NULL) {
        fy_generic_builder_destroy(gb);
        return NULL;
    }

    holder = (FyGenericObject *)FyGenericType.tp_alloc(&FyGenericType, 0);
    if (holder == NULL) {
        fy_py_xdecref(holder_doc_state);
        return NULL;
    }
    holder->fyg = vdir;
    holder->doc_state = holder_doc_state;
    holder->path = NULL;

    /* Create Python list to hold all documents */
    result = PyList_New(doc_count);
    if (result == NULL) {
        fy_py_xdecref(holder);
        return NULL;
    }

    /* Create FyGeneric for each document from its VDS */
    for (i = 0; i < doc_count; i++) {
        fy_generic vds = fy_generic_dir_get_document_vds(vdir, i);
        if (!fy_generic_is_valid(vds)) {
            fy_py_xdecref(result);
            fy_py_xdecref(holder);
            PyErr_SetString(PyExc_RuntimeError, "Failed to get document VDS");
            return NULL;
        }

        PyObject *doc = FyGeneric_from_vds_with_parent(vds, holder, mutable);
        if (doc == NULL) {
            fy_py_xdecref(result);
            fy_py_xdecref(holder);
            return NULL;
        }

        PyList_SET_ITEM(result, i, doc);  /* Steals reference */
    }

    /* Release our reference to holder - the docs in the list keep it alive */
    fy_py_xdecref(holder);

    /* Auto-trim if requested (default behavior) */
    if (trim && gb) {
        fy_gb_trim(gb);
    }

    return result;
}

/* load_all(file, mode='yaml', dedup=True, trim=True, mutable=False, collect_diag=False, create_markers=False, keep_comments=False, keep_style=False) - Parse multi-document from file */
static PyObject *
libfyaml_load_all(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *file_obj;
    const char *mode = "yaml";
    int dedup = 1;
    int trim = 1;
    int mutable = 0;
    int collect_diag = 0;
    int create_markers = 0;
    int keep_comments = 0;
    int keep_style = 0;
    static char *kwlist[] = {"file", "mode", "dedup", "trim", "mutable", "collect_diag", "create_markers", "keep_comments", "keep_style", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|sppppppp", kwlist,
                                     &file_obj, &mode, &dedup, &trim, &mutable, &collect_diag, &create_markers, &keep_comments, &keep_style))
        return NULL;

    /* Check if it's a string (file path) or file object */
    if (PyUnicode_Check(file_obj)) {
        /* It's a file path - use fy_gb_parse_file() for mmap-based loading */
        const char *path;
        struct fy_generic_builder *gb;
        unsigned int mode_flags;
        unsigned int parse_flags;
        fy_generic vdir;
        int doc_count;
        PyObject *holder_doc_state;
        FyGenericObject *holder;
        PyObject *result;
        int i;

        path = PyUnicode_AsUTF8(file_obj);
        if (path == NULL)
            return NULL;

        /* Create generic builder using helper (1MB estimate for files) */
        gb = create_builder_with_config(dedup, 1024 * 1024);
        if (gb == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
            return NULL;
        }

        /* Parse mode string to flags */
        mode_flags = parse_mode_flags(mode);
        if (mode_flags == 0)
            return NULL;  /* Exception already set */

        /* Parse flags: MULTI_DOCUMENT for multiple docs */
        parse_flags = FYOPPF_MULTI_DOCUMENT | mode_flags;
        if (collect_diag)
            parse_flags |= FYOPPF_COLLECT_DIAG;
        if (create_markers)
            parse_flags |= FYOPPF_CREATE_MARKERS;
        if (keep_comments)
            parse_flags |= FYOPPF_KEEP_COMMENTS;
        if (keep_style)
            parse_flags |= FYOPPF_KEEP_STYLE;

        /* Parse from file - returns a directory (sequence of VDS) */
        vdir = fy_gb_parse_file(gb, parse_flags, path);
        if (!fy_generic_is_valid(vdir)) {
            fy_generic_builder_destroy(gb);
            PyErr_Format(PyExc_ValueError, "Failed to parse YAML/JSON file: %s", path);
            return NULL;
        }

        /* Get document count */
        doc_count = fy_generic_dir_get_document_count(vdir);

        /* Create a holder FyGeneric that owns the builder */
        /* Create doc_state first (it owns the builder) */
        holder_doc_state = FyDocumentState_create(vdir, fy_invalid, gb, mutable);
        if (holder_doc_state == NULL) {
            fy_generic_builder_destroy(gb);
            return NULL;
        }

        holder = (FyGenericObject *)FyGenericType.tp_alloc(&FyGenericType, 0);
        if (holder == NULL) {
            fy_py_xdecref(holder_doc_state);
            return NULL;
        }
        holder->fyg = vdir;
        holder->doc_state = holder_doc_state;
        holder->path = NULL;

        /* Create Python list to hold all documents */
        result = PyList_New(doc_count);
        if (result == NULL) {
            fy_py_xdecref(holder);
            return NULL;
        }

        /* Create FyGeneric for each document from its VDS */
        for (i = 0; i < doc_count; i++) {
            fy_generic vds = fy_generic_dir_get_document_vds(vdir, i);
            if (!fy_generic_is_valid(vds)) {
                fy_py_xdecref(result);
                fy_py_xdecref(holder);
                PyErr_SetString(PyExc_RuntimeError, "Failed to get document VDS");
                return NULL;
            }

            PyObject *doc = FyGeneric_from_vds_with_parent(vds, holder, mutable);
            if (doc == NULL) {
                fy_py_xdecref(result);
                fy_py_xdecref(holder);
                return NULL;
            }

            PyList_SET_ITEM(result, i, doc);
        }

        /* Release our reference to holder - docs keep it alive */
        fy_py_xdecref(holder);

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
        fy_py_xdecref(read_method);

        if (content == NULL)
            return NULL;

        /* Call loads_all with the content */
        PyObject *loads_all_kwargs = PyDict_New();
        if (loads_all_kwargs == NULL) {
            fy_py_xdecref(content);
            return NULL;
        }

        PyObject *mode_str = PyUnicode_FromString(mode);
        if (mode_str == NULL) {
            fy_py_xdecref(loads_all_kwargs);
            fy_py_xdecref(content);
            return NULL;
        }
        PyObject *dedup_bool = PyBool_FromLong(dedup);
        PyObject *trim_bool = PyBool_FromLong(trim);
        PyObject *mutable_bool = PyBool_FromLong(mutable);
        PyObject *markers_bool = PyBool_FromLong(create_markers);
        PyObject *comments_bool = PyBool_FromLong(keep_comments);
        PyObject *style_bool = PyBool_FromLong(keep_style);

        PyDict_SetItemString(loads_all_kwargs, "mode", mode_str);
        PyDict_SetItemString(loads_all_kwargs, "dedup", dedup_bool);
        PyDict_SetItemString(loads_all_kwargs, "trim", trim_bool);
        PyDict_SetItemString(loads_all_kwargs, "mutable", mutable_bool);
        PyDict_SetItemString(loads_all_kwargs, "create_markers", markers_bool);
        PyDict_SetItemString(loads_all_kwargs, "keep_comments", comments_bool);
        PyDict_SetItemString(loads_all_kwargs, "keep_style", style_bool);

        fy_py_xdecref(mode_str);
        fy_py_xdecref(dedup_bool);
        fy_py_xdecref(trim_bool);
        fy_py_xdecref(mutable_bool);
        fy_py_xdecref(markers_bool);
        fy_py_xdecref(comments_bool);
        fy_py_xdecref(style_bool);

        PyObject *loads_all_args = PyTuple_Pack(1, content);
        PyObject *result = libfyaml_loads_all(self, loads_all_args, loads_all_kwargs);

        fy_py_xdecref(loads_all_args);
        fy_py_xdecref(loads_all_kwargs);
        fy_py_xdecref(content);
        return result;
    }
}

/* dumps_all(documents, compact=False, json=False, style=None) - Dump FyGeneric sequence to multi-document string */
static PyObject *
libfyaml_dumps_all(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *documents;
    int compact = 0;
    int json_mode = 0;
    const char *style = NULL;
    static char *kwlist[] = {"documents", "compact", "json", "style", NULL};
    struct fy_generic_builder *gb = NULL;
    fy_generic doc_sequence;
    unsigned int emit_flags;
    fy_generic emitted;
    fy_generic_sized_string szstr;
    PyObject *result;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|ppz", kwlist,
                                     &documents, &compact, &json_mode, &style))
        return NULL;

    /* Create generic builder for emitting */
    gb = fy_generic_builder_create(NULL);
    if (gb == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
        return NULL;
    }

    /* Accept either a FyGeneric sequence or a Python list of FyGeneric objects */
    if (Py_TYPE(documents) == &FyGenericType) {
        /* FyGeneric sequence - internalize it */
        FyGenericObject *fyobj = (FyGenericObject *)documents;
        if (!fy_generic_is_sequence(fyobj->fyg)) {
            fy_generic_builder_destroy(gb);
            PyErr_SetString(PyExc_TypeError, "documents must be a sequence");
            return NULL;
        }
        doc_sequence = fy_gb_internalize(gb, fyobj->fyg);
    } else if (PyList_Check(documents)) {
        /* Python list - build a sequence from it */
        Py_ssize_t count = PyList_Size(documents);
        fy_generic *items = alloca(count * sizeof(fy_generic));
        Py_ssize_t i;

        for (i = 0; i < count; i++) {
            PyObject *item = PyList_GET_ITEM(documents, i);
            FyGenericObject *fyitem;
            if (Py_TYPE(item) != &FyGenericType) {
                fy_generic_builder_destroy(gb);
                PyErr_SetString(PyExc_TypeError, "all documents must be FyGeneric objects");
                return NULL;
            }
            fyitem = (FyGenericObject *)item;
            items[i] = fy_gb_internalize(gb, fyitem->fyg);
        }
        doc_sequence = fy_gb_sequence_create(gb, count, items);
    } else {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_TypeError, "documents must be a list or FyGeneric sequence");
        return NULL;
    }

    if (!fy_generic_is_valid(doc_sequence)) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to create document sequence");
        return NULL;
    }

    /* Determine emit flags based on options - add MULTI_DOCUMENT flag */
    emit_flags = FYOPEF_DISABLE_DIRECTORY | FYOPEF_OUTPUT_TYPE_STRING | FYOPEF_MULTI_DOCUMENT;

    if (json_mode) {
        emit_flags |= FYOPEF_MODE_JSON;
        if (!compact)
            emit_flags |= FYOPEF_INDENT_2;
    } else {
        emit_flags |= FYOPEF_MODE_YAML_1_2;
        /* Parse style string if provided */
        if (style != NULL) {
            if (strcmp(style, "default") == 0 || strcmp(style, "original") == 0) {
                emit_flags |= FYOPEF_STYLE_DEFAULT;
            } else if (strcmp(style, "block") == 0) {
                emit_flags |= FYOPEF_STYLE_BLOCK;
            } else if (strcmp(style, "flow") == 0) {
                emit_flags |= FYOPEF_STYLE_FLOW;
            } else if (strcmp(style, "pretty") == 0) {
                emit_flags |= FYOPEF_STYLE_PRETTY;
            } else if (strcmp(style, "compact") == 0) {
                emit_flags |= FYOPEF_STYLE_COMPACT;
            } else if (strcmp(style, "oneline") == 0) {
                emit_flags |= FYOPEF_STYLE_ONELINE;
            } else {
                fy_generic_builder_destroy(gb);
                PyErr_Format(PyExc_ValueError, "Unknown style: '%s'. Expected: default, original, block, flow, pretty, compact, or oneline", style);
                return NULL;
            }
        } else if (compact) {
            emit_flags |= FYOPEF_STYLE_FLOW;
        } else {
            emit_flags |= FYOPEF_STYLE_BLOCK;
        }
    }

    /* Emit the sequence of documents */
    emitted = fy_gb_emit(gb, doc_sequence, emit_flags, NULL);

    if (!fy_generic_is_valid(emitted) || !fy_generic_is_string(emitted)) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to emit YAML/JSON documents");
        return NULL;
    }

    /* Extract the sized string from the generic */
    szstr = fy_cast(emitted, fy_szstr_empty);
    if (szstr.data == NULL) {
        fy_generic_builder_destroy(gb);
        PyErr_SetString(PyExc_RuntimeError, "Failed to extract string from emitted generic");
        return NULL;
    }

    result = PyUnicode_FromStringAndSize(szstr.data, szstr.size);
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
    FyGenericObject *fyobj;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|pp", kwlist,
                                     &file_obj, &documents, &compact, &json_mode))
        return NULL;

    /* documents must be a FyGeneric sequence */
    if (Py_TYPE(documents) != &FyGenericType) {
        PyErr_SetString(PyExc_TypeError, "documents must be a FyGeneric sequence (from load_all/loads_all)");
        return NULL;
    }

    fyobj = (FyGenericObject *)documents;
    if (!fy_generic_is_sequence(fyobj->fyg)) {
        PyErr_SetString(PyExc_TypeError, "documents must be a sequence");
        return NULL;
    }

    /* Check if it's a string (file path) or file object */
    if (PyUnicode_Check(file_obj)) {
        /* It's a file path - use fy_gb_emit_file() for direct file output */
        const char *path = PyUnicode_AsUTF8(file_obj);
        struct fy_generic_builder *gb = NULL;
        fy_generic doc_sequence;
        unsigned int emit_flags;
        fy_generic result_g;
        int result_code;
        if (path == NULL)
            return NULL;

        /* Create generic builder for emitting */
        gb = fy_generic_builder_create(NULL);
        if (gb == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to create generic builder");
            return NULL;
        }

        /* Internalize the sequence into our builder */
        doc_sequence = fy_gb_internalize(gb, fyobj->fyg);
        if (!fy_generic_is_valid(doc_sequence)) {
            fy_generic_builder_destroy(gb);
            PyErr_SetString(PyExc_RuntimeError, "Failed to internalize document sequence");
            return NULL;
        }

        /* Build emit flags */
        emit_flags = build_emit_flags(json_mode, compact, 1, 0);

        /* Emit to file using new API - returns int 0 on success */
        result_g = fy_gb_emit_file(gb, doc_sequence, emit_flags, path);

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

        result_code = fy_cast(result_g, -1);
        fy_generic_builder_destroy(gb);

        if (result_code != 0) {
            PyErr_Format(PyExc_RuntimeError, "Failed to emit YAML/JSON to file: %s (error code: %d)", path, result_code);
            return NULL;
        }

        Py_RETURN_NONE;
    } else {
        /* Assume it's a file object - use dumps_all() and write */
        PyObject *dumps_all_kwargs = PyDict_New();
        PyObject *compact_bool;
        PyObject *json_bool;
        PyObject *dumps_all_args;
        PyObject *yaml_str;
        if (dumps_all_kwargs == NULL)
            return NULL;

        compact_bool = PyBool_FromLong(compact);
        json_bool = PyBool_FromLong(json_mode);

        PyDict_SetItemString(dumps_all_kwargs, "compact", compact_bool);
        PyDict_SetItemString(dumps_all_kwargs, "json", json_bool);

        fy_py_xdecref(compact_bool);
        fy_py_xdecref(json_bool);

        dumps_all_args = PyTuple_Pack(1, documents);
        yaml_str = libfyaml_dumps_all(self, dumps_all_args, dumps_all_kwargs);

        fy_py_xdecref(dumps_all_args);
        fy_py_xdecref(dumps_all_kwargs);

        if (yaml_str == NULL)
            return NULL;

        /* Write to file object */
        if (write_to_file_object(file_obj, yaml_str) < 0) {
            fy_py_xdecref(yaml_str);
            return NULL;
        }

        fy_py_xdecref(yaml_str);
        Py_RETURN_NONE;
    }
}

/* ========== Streaming API: _parse(), _scan(), _emit() ========== */

/* Helper: convert mode string to fy_parse_cfg_flags for the low-level parser API */
static enum fy_parse_cfg_flags
parse_mode_to_parser_flags(const char *mode)
{
    if (mode == NULL || strcmp(mode, "yaml1.1") == 0 || strcmp(mode, "1.1") == 0) {
        return FYPCF_DEFAULT_VERSION_1_1 | FYPCF_SLOPPY_FLOW_INDENTATION | FYPCF_ALLOW_DUPLICATE_KEYS;
    } else if (strcmp(mode, "yaml1.1-pyyaml") == 0 || strcmp(mode, "pyyaml") == 0) {
        return FYPCF_DEFAULT_VERSION_1_1 | FYPCF_SLOPPY_FLOW_INDENTATION | FYPCF_ALLOW_DUPLICATE_KEYS;
    } else if (strcmp(mode, "yaml") == 0 || strcmp(mode, "yaml1.2") == 0 || strcmp(mode, "1.2") == 0) {
        return FYPCF_DEFAULT_VERSION_AUTO;
    } else if (strcmp(mode, "json") == 0) {
        return FYPCF_JSON_FORCE;
    } else {
        PyErr_Format(PyExc_ValueError,
            "Invalid mode '%s'. Use 'yaml1.1', 'yaml1.1-pyyaml', 'yaml1.2', or 'json'", mode);
        return (enum fy_parse_cfg_flags)-1;
    }
}

/* Helper: build a mark tuple (line, column, index) from fy_mark, or Py_None */
static PyObject *
mark_to_tuple(const struct fy_mark *m)
{
    if (!m)
        return fy_py_newref(Py_None);
    return Py_BuildValue("(iin)", m->line, m->column, (Py_ssize_t)m->input_pos);
}

/* Helper: get tag string from a tag token, resolving handles to full URIs */
static PyObject *
tag_token_to_pystring(struct fy_token *tag_token)
{
    if (!tag_token)
        return fy_py_newref(Py_None);

    const char *handle = fy_tag_token_handle0(tag_token);
    const char *suffix = fy_tag_token_suffix0(tag_token);

    if (!handle && !suffix)
        return fy_py_newref(Py_None);

    /* If handle is "!" or "!!" or a named handle, combine with suffix */
    if (handle && suffix) {
        /* Resolve standard handles */
        if (strcmp(handle, "!!") == 0) {
            /* !! maps to tag:yaml.org,2002: */
            return PyUnicode_FromFormat("tag:yaml.org,2002:%s", suffix);
        }
        return PyUnicode_FromFormat("%s%s", handle, suffix);
    } else if (suffix) {
        return PyUnicode_FromString(suffix);
    } else if (handle) {
        return PyUnicode_FromString(handle);
    }

    return fy_py_newref(Py_None);
}

/* _parse(string, mode='yaml1.1') -> list of event tuples */
static PyObject *
libfyaml_stream_parse(PyObject *self, PyObject *args, PyObject *kwargs)
{
    const char *yaml_str;
    Py_ssize_t yaml_len;
    const char *mode = "yaml1.1";
    static char *kwlist[] = {"s", "mode", NULL};
    enum fy_parse_cfg_flags flags;
    struct fy_parse_cfg cfg = {0};
    struct fy_parser *fyp = NULL;
    PyObject *result = NULL;
    /* Declared here so parse_error: can clean them up on any mid-case goto */
    PyObject *py_sm = NULL, *py_em = NULL;
    struct fy_event *fye;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s#|s", kwlist,
                                     &yaml_str, &yaml_len, &mode))
        return NULL;

    flags = parse_mode_to_parser_flags(mode);
    if ((int)flags == -1)
        return NULL;

    cfg.flags = flags;
    fyp = fy_parser_create(&cfg);
    if (!fyp) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create parser");
        return NULL;
    }

    if (fy_parser_set_string(fyp, yaml_str, (size_t)yaml_len) != 0) {
        fy_parser_destroy(fyp);
        PyErr_SetString(PyExc_RuntimeError, "Failed to set parser input");
        return NULL;
    }

    result = PyList_New(0);
    if (!result) {
        fy_parser_destroy(fyp);
        return NULL;
    }

    while ((fye = fy_parser_parse(fyp)) != NULL) {
        PyObject *evt = NULL;
        enum fy_event_type type = fy_event_get_type(fye);

        py_sm = mark_to_tuple(fy_event_start_mark(fye));
        py_em = mark_to_tuple(fy_event_end_mark(fye));
        if (!py_sm || !py_em)
            goto parse_error;

        switch (type) {
        case FYET_STREAM_START:
            /* (1, start_mark, end_mark) */
            evt = Py_BuildValue("(iOO)", 1, py_sm, py_em);
            break;

        case FYET_STREAM_END:
            /* (2, start_mark, end_mark) */
            evt = Py_BuildValue("(iOO)", 2, py_sm, py_em);
            break;

        case FYET_DOCUMENT_START: {
            /* (3, implicit, version_tuple_or_None, tags_list_or_None, start_mark, end_mark) */
            int implicit = fye->document_start.implicit ? 1 : 0;
            /* Version: borrowed Py_None or newly built tuple */
            PyObject *py_ver = Py_None;
            const struct fy_version *ver = fy_document_start_event_version(fye);
            /* Tag directives: borrowed Py_None or newly built dict */
            PyObject *py_tags = Py_None;
            struct fy_document_state *fyds = fye->document_start.document_state;
            if (ver) {
                py_ver = Py_BuildValue("(ii)", ver->major, ver->minor);
                if (!py_ver)
                    goto parse_error;
            }
            if (fyds) {
                void *iter = NULL;
                const struct fy_tag *tag;
                int has_non_default = 0;

                while ((tag = fy_document_state_tag_directive_iterate(fyds, &iter)) != NULL) {
                    if (!fy_document_state_tag_is_default(fyds, tag)) {
                        has_non_default = 1;
                        break;
                    }
                }

                if (has_non_default) {
                    py_tags = PyDict_New();
                    if (!py_tags) {
                        fy_py_xdecref(py_ver);
                        goto parse_error;
                    }
                    iter = NULL;
                    while ((tag = fy_document_state_tag_directive_iterate(fyds, &iter)) != NULL) {
                        if (tag->handle && tag->prefix) {
                            PyObject *key = PyUnicode_FromString(tag->handle);
                            PyObject *val = PyUnicode_FromString(tag->prefix);
                            if (key && val)
                                PyDict_SetItem(py_tags, key, val);
                            fy_py_xdecref(key);
                            fy_py_xdecref(val);
                        }
                    }
                }
            }

            evt = Py_BuildValue("(iiOOOO)", 3, implicit, py_ver, py_tags, py_sm, py_em);
            fy_py_xdecref(py_ver);
            fy_py_xdecref(py_tags);
            break;
        }

        case FYET_DOCUMENT_END: {
            /* (4, implicit, start_mark, end_mark) */
            int implicit = fye->document_end.implicit ? 1 : 0;
            evt = Py_BuildValue("(iiOO)", 4, implicit, py_sm, py_em);
            break;
        }

        case FYET_MAPPING_START: {
            /* (5, anchor, tag, implicit, flow_style, start_mark, end_mark) */
            struct fy_token *anchor_tok = fy_event_get_anchor_token(fye);
            struct fy_token *tag_tok = fy_event_get_tag_token(fye);
            /* py_anchor: borrowed Py_None or new string */
            PyObject *py_anchor = Py_None;
            PyObject *py_tag = NULL;
            int implicit = 0;
            enum fy_node_style ns = FYNS_ANY;
            PyObject *py_flow = Py_None;
            if (anchor_tok) {
                const char *anchor_text = fy_token_get_text0(anchor_tok);
                if (anchor_text) {
                    py_anchor = PyUnicode_FromString(anchor_text);
                    if (!py_anchor) goto parse_error;
                }
            }

            py_tag = tag_token_to_pystring(tag_tok);
            if (!py_tag) { fy_py_xdecref(py_anchor); goto parse_error; }

            implicit = (tag_tok == NULL) ? 1 : 0;

            /* py_flow: borrowed singleton (Py_True/Py_False/Py_None) */
            ns = fy_event_get_node_style(fye);
            py_flow = (ns == FYNS_FLOW) ? Py_True :
                      (ns == FYNS_BLOCK) ? Py_False : Py_None;

            evt = Py_BuildValue("(iOOiOOO)", 5, py_anchor, py_tag, implicit, py_flow, py_sm, py_em);
            fy_py_xdecref(py_anchor);
            fy_py_xdecref(py_tag);
            break;
        }

        case FYET_MAPPING_END:
            /* (6, start_mark, end_mark) */
            evt = Py_BuildValue("(iOO)", 6, py_sm, py_em);
            break;

        case FYET_SEQUENCE_START: {
            /* (7, anchor, tag, implicit, flow_style, start_mark, end_mark) */
            struct fy_token *anchor_tok = fy_event_get_anchor_token(fye);
            struct fy_token *tag_tok = fy_event_get_tag_token(fye);
            /* py_anchor: borrowed Py_None or new string */
            PyObject *py_anchor = Py_None;
            PyObject *py_tag = NULL;
            int implicit = 0;
            enum fy_node_style ns = FYNS_ANY;
            PyObject *py_flow = Py_None;
            if (anchor_tok) {
                const char *anchor_text = fy_token_get_text0(anchor_tok);
                if (anchor_text) {
                    py_anchor = PyUnicode_FromString(anchor_text);
                    if (!py_anchor) goto parse_error;
                }
            }

            py_tag = tag_token_to_pystring(tag_tok);
            if (!py_tag) { fy_py_xdecref(py_anchor); goto parse_error; }

            implicit = (tag_tok == NULL) ? 1 : 0;

            /* py_flow: borrowed singleton */
            ns = fy_event_get_node_style(fye);
            py_flow = (ns == FYNS_FLOW) ? Py_True :
                      (ns == FYNS_BLOCK) ? Py_False : Py_None;

            evt = Py_BuildValue("(iOOiOOO)", 7, py_anchor, py_tag, implicit, py_flow, py_sm, py_em);
            fy_py_xdecref(py_anchor);
            fy_py_xdecref(py_tag);
            break;
        }

        case FYET_SEQUENCE_END:
            /* (8, start_mark, end_mark) */
            evt = Py_BuildValue("(iOO)", 8, py_sm, py_em);
            break;

        case FYET_SCALAR: {
            /* (9, anchor, tag, implicit_tuple, value, style, start_mark, end_mark) */
            struct fy_token *anchor_tok = fye->scalar.anchor;
            struct fy_token *tag_tok = fye->scalar.tag;
            struct fy_token *value_tok = fye->scalar.value;
            /* py_anchor: borrowed Py_None or new string */
            PyObject *py_anchor = Py_None;
            PyObject *py_tag = NULL;
            size_t val_len = 0;
            const char *val_text = NULL;
            PyObject *py_value = NULL;
            enum fy_scalar_style ss = FYSS_ANY;
            PyObject *py_style = Py_None;
            int plain_implicit = 0;
            int non_plain_implicit = 0;
            PyObject *py_implicit = NULL;
            if (anchor_tok) {
                const char *anchor_text = fy_token_get_text0(anchor_tok);
                if (anchor_text) {
                    py_anchor = PyUnicode_FromString(anchor_text);
                    if (!py_anchor) goto parse_error;
                }
            }

            py_tag = tag_token_to_pystring(tag_tok);
            if (!py_tag) { fy_py_xdecref(py_anchor); goto parse_error; }

            /* Scalar value: fall back to empty string on invalid UTF-8 */
            val_text = fy_token_get_text(value_tok, &val_len);
            py_value = val_text
                ? PyUnicode_FromStringAndSize(val_text, val_len)
                : PyUnicode_FromString("");
            if (!py_value) {
                PyErr_Clear();
                py_value = PyUnicode_FromString("");
            }

            /* py_style: borrowed Py_None for plain, new string otherwise */
            ss = fy_token_scalar_style(value_tok);
            switch (ss) {
            case FYSS_SINGLE_QUOTED: py_style = PyUnicode_FromString("'");  break;
            case FYSS_DOUBLE_QUOTED: py_style = PyUnicode_FromString("\""); break;
            case FYSS_LITERAL:       py_style = PyUnicode_FromString("|");  break;
            case FYSS_FOLDED:        py_style = PyUnicode_FromString(">");  break;
            default:                 py_style = Py_None;                    break;
            }
            if (!py_style) {
                fy_py_xdecref(py_anchor);
                fy_py_xdecref(py_tag);
                fy_py_xdecref(py_value);
                goto parse_error;
            }

            /* Implicit tuple: (plain_implicit, non_plain_implicit) */
            plain_implicit = (tag_tok == NULL && ss == FYSS_PLAIN) ? 1 : 0;
            non_plain_implicit = (tag_tok == NULL && ss != FYSS_PLAIN) ? 1 : 0;
            if (fye->scalar.tag_implicit)
                non_plain_implicit = 1;
            py_implicit = Py_BuildValue("(ii)", plain_implicit, non_plain_implicit);

            evt = Py_BuildValue("(iOOOOOOO)", 9, py_anchor, py_tag, py_implicit,
                                py_value, py_style, py_sm, py_em);
            fy_py_xdecref(py_anchor);
            fy_py_xdecref(py_tag);
            fy_py_xdecref(py_implicit);
            fy_py_xdecref(py_value);
            fy_py_xdecref(py_style);
            break;
        }

        case FYET_ALIAS: {
            /* (10, anchor, start_mark, end_mark) */
            struct fy_token *anchor_tok = fye->alias.anchor;
            PyObject *py_anchor = Py_None;
            if (anchor_tok) {
                const char *anchor_text = fy_token_get_text0(anchor_tok);
                if (anchor_text) {
                    py_anchor = PyUnicode_FromString(anchor_text);
                    if (!py_anchor) goto parse_error;
                }
            }
            evt = Py_BuildValue("(iOOO)", 10, py_anchor, py_sm, py_em);
            fy_py_xdecref(py_anchor);
            break;
        }

        default:
            /* Skip unknown event types */
            fy_parser_event_free(fyp, fye);
            fy_py_release(&py_sm);
            fy_py_release(&py_em);
            continue;
        }

        fy_py_release(&py_sm);
        fy_py_release(&py_em);

        if (!evt)
            goto parse_error;

        if (PyList_Append(result, evt) < 0) {
            fy_py_xdecref(evt);
            goto parse_error;
        }
        fy_py_xdecref(evt);

        fy_parser_event_free(fyp, fye);
    }

    fy_parser_destroy(fyp);
    return result;

parse_error:
    fy_py_xdecref(py_sm);
    fy_py_xdecref(py_em);
    fy_py_xdecref(result);
    fy_parser_destroy(fyp);
    if (!PyErr_Occurred())
        PyErr_SetString(PyExc_RuntimeError, "Error during YAML parsing");
    return NULL;
}

/* _scan(string, mode='yaml1.1') -> list of token tuples */
static PyObject *
libfyaml_stream_scan(PyObject *self, PyObject *args, PyObject *kwargs)
{
    const char *yaml_str;
    Py_ssize_t yaml_len;
    const char *mode = "yaml1.1";
    static char *kwlist[] = {"s", "mode", NULL};
    enum fy_parse_cfg_flags flags;
    struct fy_parse_cfg cfg = {0};
    struct fy_parser *fyp = NULL;
    PyObject *result = NULL;
    /* Declared here so scan_error: can clean them up on any mid-case goto */
    PyObject *py_sm = NULL, *py_em = NULL;
    struct fy_token *fyt;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s#|s", kwlist,
                                     &yaml_str, &yaml_len, &mode))
        return NULL;

    flags = parse_mode_to_parser_flags(mode);
    if ((int)flags == -1)
        return NULL;

    cfg.flags = flags;
    fyp = fy_parser_create(&cfg);
    if (!fyp) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create parser");
        return NULL;
    }

    if (fy_parser_set_string(fyp, yaml_str, (size_t)yaml_len) != 0) {
        fy_parser_destroy(fyp);
        PyErr_SetString(PyExc_RuntimeError, "Failed to set parser input");
        return NULL;
    }

    result = PyList_New(0);
    if (!result) {
        fy_parser_destroy(fyp);
        return NULL;
    }

    while ((fyt = fy_scan(fyp)) != NULL) {
        PyObject *tok = NULL;
        enum fy_token_type type = fy_token_get_type(fyt);

        py_sm = mark_to_tuple(fy_token_start_mark(fyt));
        py_em = mark_to_tuple(fy_token_end_mark(fyt));
        if (!py_sm || !py_em)
            goto scan_error;

        switch (type) {
        case FYTT_STREAM_START:
            /* (1, encoding, start_mark, end_mark) - encoding always 'utf-8' */
            tok = Py_BuildValue("(isOO)", 1, "utf-8", py_sm, py_em);
            break;

        case FYTT_STREAM_END:
            tok = Py_BuildValue("(iOO)", 2, py_sm, py_em);
            break;

        case FYTT_VERSION_DIRECTIVE: {
            /* py_ver: borrowed Py_None or newly built tuple */
            const struct fy_version *ver = fy_version_directive_token_version(fyt);
            PyObject *py_ver = Py_None;
            if (ver) {
                py_ver = Py_BuildValue("(ii)", ver->major, ver->minor);
                /* if py_ver is NULL, Py_BuildValue put it in tok via "O" which
                 * handles NULL gracefully — tok will also be NULL */
            }
            tok = Py_BuildValue("(i(sO)OO)", 3, "YAML", py_ver, py_sm, py_em);
            fy_py_xdecref(py_ver);
            break;
        }

        case FYTT_TAG_DIRECTIVE: {
            /* py_handle/py_prefix: borrowed Py_None or new strings */
            const struct fy_tag *tag = fy_tag_directive_token_tag(fyt);
            PyObject *py_handle = Py_None;
            PyObject *py_prefix = Py_None;
            if (tag) {
                if (tag->handle) {
                    py_handle = PyUnicode_FromString(tag->handle);
                    if (!py_handle) goto scan_error;
                }
                if (tag->prefix) {
                    py_prefix = PyUnicode_FromString(tag->prefix);
                    if (!py_prefix) { fy_py_xdecref(py_handle); goto scan_error; }
                }
            }
            tok = Py_BuildValue("(i(s(OO))OO)", 4, "TAG", py_handle, py_prefix, py_sm, py_em);
            fy_py_xdecref(py_handle);
            fy_py_xdecref(py_prefix);
            break;
        }

        case FYTT_DOCUMENT_START:
            tok = Py_BuildValue("(iOO)", 5, py_sm, py_em);
            break;

        case FYTT_DOCUMENT_END:
            tok = Py_BuildValue("(iOO)", 6, py_sm, py_em);
            break;

        case FYTT_BLOCK_SEQUENCE_START:
            tok = Py_BuildValue("(iOO)", 7, py_sm, py_em);
            break;

        case FYTT_BLOCK_MAPPING_START:
            tok = Py_BuildValue("(iOO)", 8, py_sm, py_em);
            break;

        case FYTT_BLOCK_END:
            tok = Py_BuildValue("(iOO)", 9, py_sm, py_em);
            break;

        case FYTT_FLOW_SEQUENCE_START:
            tok = Py_BuildValue("(iOO)", 10, py_sm, py_em);
            break;

        case FYTT_FLOW_SEQUENCE_END:
            tok = Py_BuildValue("(iOO)", 11, py_sm, py_em);
            break;

        case FYTT_FLOW_MAPPING_START:
            tok = Py_BuildValue("(iOO)", 12, py_sm, py_em);
            break;

        case FYTT_FLOW_MAPPING_END:
            tok = Py_BuildValue("(iOO)", 13, py_sm, py_em);
            break;

        case FYTT_BLOCK_ENTRY:
            tok = Py_BuildValue("(iOO)", 14, py_sm, py_em);
            break;

        case FYTT_FLOW_ENTRY:
            tok = Py_BuildValue("(iOO)", 15, py_sm, py_em);
            break;

        case FYTT_KEY:
            tok = Py_BuildValue("(iOO)", 16, py_sm, py_em);
            break;

        case FYTT_VALUE:
            tok = Py_BuildValue("(iOO)", 17, py_sm, py_em);
            break;

        case FYTT_ALIAS: {
            const char *val = fy_token_get_text0(fyt);
            tok = Py_BuildValue("(isOO)", 18, val ? val : "", py_sm, py_em);
            break;
        }

        case FYTT_ANCHOR: {
            const char *val = fy_token_get_text0(fyt);
            tok = Py_BuildValue("(isOO)", 19, val ? val : "", py_sm, py_em);
            break;
        }

        case FYTT_TAG: {
            const char *handle = fy_tag_token_handle0(fyt);
            const char *suffix = fy_tag_token_suffix0(fyt);
            tok = Py_BuildValue("(i(ss)OO)", 20,
                                handle ? handle : "",
                                suffix ? suffix : "",
                                py_sm, py_em);
            break;
        }

        case FYTT_SCALAR: {
            size_t val_len = 0;
            const char *val = fy_token_get_text(fyt, &val_len);
            enum fy_scalar_style ss = fy_scalar_token_get_style(fyt);
            int plain = (ss == FYSS_PLAIN || ss == FYSS_ANY) ? 1 : 0;
            /* py_style: borrowed Py_None for plain, new string otherwise */
            PyObject *py_style = Py_None;
            PyObject *py_val = NULL;
            switch (ss) {
            case FYSS_SINGLE_QUOTED: py_style = PyUnicode_FromString("'");  break;
            case FYSS_DOUBLE_QUOTED: py_style = PyUnicode_FromString("\""); break;
            case FYSS_LITERAL:       py_style = PyUnicode_FromString("|");  break;
            case FYSS_FOLDED:        py_style = PyUnicode_FromString(">");  break;
            default:                 py_style = Py_None;                    break;
            }

            py_val = val ? PyUnicode_FromStringAndSize(val, val_len)
                         : PyUnicode_FromString("");
            if (!py_val) {
                PyErr_Clear();
                py_val = PyUnicode_FromString("");
            }
            tok = Py_BuildValue("(iOiOOO)", 21, py_val, plain, py_style, py_sm, py_em);
            fy_py_xdecref(py_val);
            fy_py_xdecref(py_style);
            break;
        }

        default:
            /* Skip non-YAML tokens (path expressions etc.) */
            fy_scan_token_free(fyp, fyt);
            fy_py_release(&py_sm);
            fy_py_release(&py_em);
            continue;
        }

        fy_py_release(&py_sm);
        fy_py_release(&py_em);

        if (!tok)
            goto scan_error;

        if (PyList_Append(result, tok) < 0) {
            fy_py_xdecref(tok);
            goto scan_error;
        }
        fy_py_xdecref(tok);

        fy_scan_token_free(fyp, fyt);
    }

    fy_parser_destroy(fyp);
    return result;

scan_error:
    fy_py_xdecref(py_sm);
    fy_py_xdecref(py_em);
    fy_py_xdecref(result);
    fy_parser_destroy(fyp);
    if (!PyErr_Occurred())
        PyErr_SetString(PyExc_RuntimeError, "Error during YAML scanning");
    return NULL;
}

/* _emit(events_list, canonical=False, indent=None, width=None, allow_unicode=True, line_break=None) -> string */
static PyObject *
libfyaml_stream_emit(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *events_list;
    int canonical = 0;
    PyObject *indent_obj = Py_None;
    PyObject *width_obj = Py_None;
    int allow_unicode = 1;
    const char *line_break = NULL;
    static char *kwlist[] = {"events", "canonical", "indent", "width",
                             "allow_unicode", "line_break", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|iOOis", kwlist,
                                     &events_list, &canonical, &indent_obj,
                                     &width_obj, &allow_unicode, &line_break))
        return NULL;

    enum fy_emitter_cfg_flags emit_flags = FYECF_MODE_ORIGINAL | FYECF_WIDTH_INF;
    struct fy_emitter *emit = NULL;
    Py_ssize_t n;
    Py_ssize_t i;
    size_t output_size;
    char *output;
    PyObject *result;

    if (!PyList_Check(events_list) && !PyTuple_Check(events_list)) {
        PyErr_SetString(PyExc_TypeError, "events must be a list or tuple");
        return NULL;
    }

    /* Build emitter flags */
    if (indent_obj != Py_None) {
        long indent_val = PyLong_AsLong(indent_obj);
        if (indent_val == -1 && PyErr_Occurred())
            return NULL;
        if (indent_val >= 1 && indent_val <= 9)
            emit_flags |= FYECF_INDENT(indent_val);
    }

    if (width_obj != Py_None) {
        long width_val = PyLong_AsLong(width_obj);
        if (width_val == -1 && PyErr_Occurred())
            return NULL;
        /* Clear the default INF width and set new one */
        emit_flags &= ~(FYECF_WIDTH_MASK << FYECF_WIDTH_SHIFT);
        if (width_val <= 0 || width_val > 255)
            emit_flags |= FYECF_WIDTH_INF;
        else
            emit_flags |= FYECF_WIDTH(width_val);
    }

    emit = fy_emit_to_string(emit_flags);
    if (!emit) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create emitter");
        return NULL;
    }

    n = PySequence_Length(events_list);
    for (i = 0; i < n; i++) {
        PyObject *evt_tuple = PySequence_GetItem(events_list, i);
        int evt_type;
        struct fy_event *fye;
        int rc;
        if (!evt_tuple)
            goto emit_error;

        if (!PyTuple_Check(evt_tuple)) {
            fy_py_xdecref(evt_tuple);
            PyErr_SetString(PyExc_TypeError, "Each event must be a tuple");
            goto emit_error;
        }

        evt_type = (int)PyLong_AsLong(PyTuple_GET_ITEM(evt_tuple, 0));
        fye = NULL;

        switch (evt_type) {
        case 1: /* STREAM_START */
            fye = fy_emit_event_create(emit, FYET_STREAM_START);
            break;

        case 2: /* STREAM_END */
            fye = fy_emit_event_create(emit, FYET_STREAM_END);
            break;

        case 3: { /* DOCUMENT_START: (3, implicit, version, tags, sm, em) */
            int implicit = (int)PyLong_AsLong(PyTuple_GET_ITEM(evt_tuple, 1));
            /* Version */
            struct fy_version version_struct;
            const struct fy_version *vers_ptr = NULL;
            PyObject *py_ver = PyTuple_GET_ITEM(evt_tuple, 2);
            /* Tags - build NULL-terminated array */
            PyObject *py_tags = PyTuple_GET_ITEM(evt_tuple, 3);
            struct fy_tag **tags_array = NULL;
            struct fy_tag *tag_storage = NULL;
            if (py_ver != Py_None && PyTuple_Check(py_ver)) {
                version_struct.major = (int)PyLong_AsLong(PyTuple_GET_ITEM(py_ver, 0));
                version_struct.minor = (int)PyLong_AsLong(PyTuple_GET_ITEM(py_ver, 1));
                vers_ptr = &version_struct;
            }

            if (py_tags != Py_None && PyDict_Check(py_tags)) {
                Py_ssize_t tag_count = PyDict_Size(py_tags);
                PyObject *key, *value;
                Py_ssize_t pos = 0;
                Py_ssize_t idx = 0;
                tags_array = (struct fy_tag **)calloc(tag_count + 1, sizeof(struct fy_tag *));
                if (!tags_array) {
                    PyErr_NoMemory();
                    goto emit_error;
                }
                tag_storage = (struct fy_tag *)calloc(tag_count, sizeof(struct fy_tag));
                if (!tag_storage) {
                    free(tags_array);
                    PyErr_NoMemory();
                    goto emit_error;
                }
                while (PyDict_Next(py_tags, &pos, &key, &value)) {
                    tag_storage[idx].handle = PyUnicode_AsUTF8(key);
                    tag_storage[idx].prefix = PyUnicode_AsUTF8(value);
                    tags_array[idx] = &tag_storage[idx];
                    idx++;
                }
                tags_array[tag_count] = NULL;
            }

            fye = fy_emit_event_create(emit, FYET_DOCUMENT_START, implicit,
                                        vers_ptr, (const struct fy_tag * const *)tags_array);
            free(tags_array);
            free(tag_storage);
            break;
        }

        case 4: { /* DOCUMENT_END: (4, implicit, sm, em) */
            int implicit = (int)PyLong_AsLong(PyTuple_GET_ITEM(evt_tuple, 1));
            fye = fy_emit_event_create(emit, FYET_DOCUMENT_END, implicit);
            break;
        }

        case 5: { /* MAPPING_START: (5, anchor, tag, implicit, flow_style, sm, em) */
            PyObject *py_anchor = PyTuple_GET_ITEM(evt_tuple, 1);
            PyObject *py_tag = PyTuple_GET_ITEM(evt_tuple, 2);
            PyObject *py_flow = PyTuple_GET_ITEM(evt_tuple, 4);

            const char *anchor = (py_anchor != Py_None) ? PyUnicode_AsUTF8(py_anchor) : NULL;
            const char *tag = (py_tag != Py_None) ? PyUnicode_AsUTF8(py_tag) : NULL;

            enum fy_node_style style;
            if (py_flow == Py_True)
                style = FYNS_FLOW;
            else if (py_flow == Py_False)
                style = FYNS_BLOCK;
            else
                style = FYNS_ANY;

            fye = fy_emit_event_create(emit, FYET_MAPPING_START, style, anchor, tag);
            break;
        }

        case 6: /* MAPPING_END */
            fye = fy_emit_event_create(emit, FYET_MAPPING_END);
            break;

        case 7: { /* SEQUENCE_START: (7, anchor, tag, implicit, flow_style, sm, em) */
            PyObject *py_anchor = PyTuple_GET_ITEM(evt_tuple, 1);
            PyObject *py_tag = PyTuple_GET_ITEM(evt_tuple, 2);
            PyObject *py_flow = PyTuple_GET_ITEM(evt_tuple, 4);

            const char *anchor = (py_anchor != Py_None) ? PyUnicode_AsUTF8(py_anchor) : NULL;
            const char *tag = (py_tag != Py_None) ? PyUnicode_AsUTF8(py_tag) : NULL;

            enum fy_node_style style;
            if (py_flow == Py_True)
                style = FYNS_FLOW;
            else if (py_flow == Py_False)
                style = FYNS_BLOCK;
            else
                style = FYNS_ANY;

            fye = fy_emit_event_create(emit, FYET_SEQUENCE_START, style, anchor, tag);
            break;
        }

        case 8: /* SEQUENCE_END */
            fye = fy_emit_event_create(emit, FYET_SEQUENCE_END);
            break;

        case 9: { /* SCALAR: (9, anchor, tag, implicit, value, style, sm, em) */
            PyObject *py_anchor = PyTuple_GET_ITEM(evt_tuple, 1);
            PyObject *py_tag = PyTuple_GET_ITEM(evt_tuple, 2);
            PyObject *py_value = PyTuple_GET_ITEM(evt_tuple, 4);
            PyObject *py_style = PyTuple_GET_ITEM(evt_tuple, 5);

            const char *anchor = (py_anchor != Py_None) ? PyUnicode_AsUTF8(py_anchor) : NULL;
            const char *tag = (py_tag != Py_None) ? PyUnicode_AsUTF8(py_tag) : NULL;

            Py_ssize_t val_len;
            const char *value = PyUnicode_AsUTF8AndSize(py_value, &val_len);

            enum fy_scalar_style ss = FYSS_ANY;
            if (py_style != Py_None && PyUnicode_Check(py_style)) {
                const char *style_str = PyUnicode_AsUTF8(py_style);
                if (style_str && style_str[0] == '\'')
                    ss = FYSS_SINGLE_QUOTED;
                else if (style_str && style_str[0] == '"')
                    ss = FYSS_DOUBLE_QUOTED;
                else if (style_str && style_str[0] == '|')
                    ss = FYSS_LITERAL;
                else if (style_str && style_str[0] == '>')
                    ss = FYSS_FOLDED;
                else
                    ss = FYSS_PLAIN;
            }

            fye = fy_emit_event_create(emit, FYET_SCALAR, ss, value, (size_t)val_len, anchor, tag);
            break;
        }

        case 10: { /* ALIAS: (10, anchor, sm, em) */
            PyObject *py_anchor = PyTuple_GET_ITEM(evt_tuple, 1);
            const char *anchor = (py_anchor != Py_None) ? PyUnicode_AsUTF8(py_anchor) : "";
            fye = fy_emit_event_create(emit, FYET_ALIAS, anchor);
            break;
        }

        default:
            fy_py_xdecref(evt_tuple);
            PyErr_Format(PyExc_ValueError, "Unknown event type: %d", evt_type);
            goto emit_error;
        }

        fy_py_xdecref(evt_tuple);

        if (!fye) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to create emit event");
            goto emit_error;
        }

        rc = fy_emit_event(emit, fye);
        if (rc != 0) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to emit event");
            goto emit_error;
        }
    }

    /* Collect the output */
    output_size = 0;
    output = fy_emit_to_string_collect(emit, &output_size);
    fy_emitter_destroy(emit);

    if (!output) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to collect emitter output");
        return NULL;
    }

    result = PyUnicode_FromStringAndSize(output, output_size);
    free(output);
    return result;

emit_error:
    fy_emitter_destroy(emit);
    return NULL;
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
     "Convert Python object to FyGeneric (with optional tag)"},
    {"path_list_to_unix_path", _PyCFunction_CAST(libfyaml_path_list_to_unix_path), METH_O,
     "Convert path list (e.g., ['server', 'host']) to Unix-style path string (e.g., '/server/host')"},
    {"unix_path_to_path_list", _PyCFunction_CAST(libfyaml_unix_path_to_path_list), METH_O,
     "Convert Unix-style path string (e.g., '/server/host') to path list (e.g., ['server', 'host'])"},
    {"_parse", _PyCFunction_CAST(libfyaml_stream_parse), METH_VARARGS | METH_KEYWORDS,
     "Parse YAML string and return list of event tuples"},
    {"_scan", _PyCFunction_CAST(libfyaml_stream_scan), METH_VARARGS | METH_KEYWORDS,
     "Scan YAML string and return list of token tuples"},
    {"_emit", _PyCFunction_CAST(libfyaml_stream_emit), METH_VARARGS | METH_KEYWORDS,
     "Emit list of event tuples to YAML string"},
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

    if (PyType_Ready(&FyDocumentStateType) < 0)
        return NULL;

    /* Create module */
    m = PyModule_Create(&libfyaml_module);
    if (m == NULL)
        return NULL;

    /* Add types to module */
    fy_py_incref((PyObject *)&FyGenericType);
    if (PyModule_AddObject(m, "FyGeneric", (PyObject *)&FyGenericType) < 0) {
        fy_py_xdecref((PyObject *)&FyGenericType);
        fy_py_xdecref(m);
        return NULL;
    }

    fy_py_incref((PyObject *)&FyDocumentStateType);
    if (PyModule_AddObject(m, "FyDocumentState", (PyObject *)&FyDocumentStateType) < 0) {
        fy_py_xdecref((PyObject *)&FyDocumentStateType);
        fy_py_xdecref(m);
        return NULL;
    }

    return m;
}
