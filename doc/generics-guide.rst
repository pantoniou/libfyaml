Generics Guide
==============

The center of the generic runtime is ``fy_generic``.

``fy_generic`` is the sum-type value used by libfyaml to represent YAML and
JSON data in C. A ``fy_generic`` holds one value of one runtime type:

* null
* bool
* int
* float
* string
* sequence
* mapping
* YAML-specific wrappers such as anchors, tags, and aliases

The rest of the generic API is about working with ``fy_generic`` values:
creating them, storing them, parsing them, querying them, transforming them,
and emitting them again.

If you know Python ``dict`` / ``list`` / scalar workflows, or value-tree APIs
such as ``serde_json::Value``, this is the closest match in libfyaml.

``fy_generic`` As The Core Type
-------------------------------

Think of ``fy_generic`` as a tagged sum type and as the main application-facing
generic object:

* scalar ``fy_generic`` values stay typed
* sequence ``fy_generic`` values are ordered collections
* mapping ``fy_generic`` values are key/value collections
* the same ``fy_generic`` type represents YAML or JSON input
* ``fy_generic`` values are immutable, so operations return new values and
  leave the original value unchanged

The design goal is Python-like data handling in C with explicit allocation and
schema control.

In practice, most generic code is built around a small set of questions:

* how do I create a ``fy_generic`` value?
* how long does this ``fy_generic`` need to live?
* how do I read values out of it?
* how do I produce a new ``fy_generic`` from an existing one?

``fy_generic`` Layout
---------------------

``fy_generic`` is a single pointer-sized word. The low 3 bits carry the type
tag. The remaining bits hold either:

* the value itself, for small inline cases
* an aligned pointer to out-of-place storage

That layout is what makes the sum-type model practical in regular C. A large
part of the common scalar space fits directly in the word with no separate
allocation.

On 64-bit builds
~~~~~~~~~~~~~~~~

The 64-bit layout is the main target for the generic API:

* signed inline integers use 61 bits
* inline integer range is ``-1152921504606846976`` to
  ``1152921504606846975``
* inline strings use a 7-byte NUL-terminated slot in the word, which gives
  6 bytes of payload plus the trailing ``\\0``
* 32-bit floats can be stored inline; larger ``double`` values are stored
  out of place

In practice this means that values such as ``42``, ``true``, ``"abc"``, and a
large number of short keys and enum-like strings fit directly in the
``fy_generic`` word.

On 32-bit builds
~~~~~~~~~~~~~~~~

The same model applies on 32-bit builds with smaller inline ranges:

* signed inline integers use 29 bits
* inline integer range is ``-268435456`` to ``268435455``
* inline strings use a 3-byte NUL-terminated slot, which gives 2 bytes of
  payload plus the trailing ``\\0``
* floats are stored out of place

Special Inline Values
~~~~~~~~~~~~~~~~~~~~~

Some values are always represented directly in the word:

* ``fy_null``
* ``fy_false``
* ``fy_true``
* ``fy_invalid``
* empty sequence
* empty mapping

These are encoded as fixed tagged values, so they never need separate storage.

Out-Of-Place Values
~~~~~~~~~~~~~~~~~~~

When a value does not fit inline, the word stores an aligned pointer:

* scalar out-of-place storage uses 8-byte alignment
* sequence and mapping storage uses 16-byte alignment
* the low tag bits remain available because the pointed-to storage is aligned

This is how larger integers, larger strings, full ``double`` values,
sequences, mappings, and indirect YAML metadata wrappers are represented.

Public Constants
~~~~~~~~~~~~~~~~

The public header exposes the constants that define this layout:

* ``FYGT_GENERIC_BITS``
* ``FYGT_INT_INPLACE_BITS``
* ``FYGT_INT_INPLACE_MIN`` / ``FYGT_INT_INPLACE_MAX``
* ``FYGT_STRING_INPLACE_SIZE``

Those constants are the authoritative description of the current ``fy_generic``
encoding.

Three Lifetime Modes
--------------------

The API comes in three practical tiers for storing ``fy_generic`` values.

Stack-local literals
~~~~~~~~~~~~~~~~~~~~

Use these when the ``fy_generic`` value only needs to live inside the current
function.

.. code-block:: c

   fy_generic cfg = fy_mapping(
       "host", "localhost",
       "port", 8080,
       "tls", true);

This entry point maps directly to the Python binding model.

Builder-backed values
~~~~~~~~~~~~~~~~~~~~~

Use a ``struct fy_generic_builder`` when ``fy_generic`` values need to outlive
a stack frame, or when operations may allocate larger composite values.

.. code-block:: c

   char storage[16384];
   struct fy_generic_builder *gb;

   gb = fy_generic_builder_create_in_place(
       FYGBCF_SCHEMA_AUTO | FYGBCF_SCOPE_LEADER,
       NULL, storage, sizeof(storage));

   fy_generic cfg = fy_gb_mapping(
       gb,
       "host", "localhost",
       "port", 8080);

Builders fit persistent results, repeated transforms, and application-owned
value stores.

Low-level storage control
~~~~~~~~~~~~~~~~~~~~~~~~~

The low-level helpers let you control the exact backing buffers used by
``fy_generic`` values. They are useful when you already have prebuilt item
arrays or you are optimizing memory placement.

Schemas Matter
--------------

``fy_generic`` is schema-less in shape, but scalar typing still depends on the
parse or builder schema:

* YAML 1.2 and JSON behave conservatively
* YAML 1.1 enables older implicit forms
* Python-compatible mode is useful when you want behavior that feels closer to
  the Python binding or PyYAML-style expectations

This matters for plain scalars such as ``yes``, ``on``, octal numbers, or
sexagesimal forms.

The example ``examples/generic-roundtrip.c`` demonstrates the same input under
different schema choices.

Constructing Values
-------------------

The core construction macros are:

* ``fy_value(...)`` for a single scalar or already-generic value
* ``fy_sequence(...)`` for list-shaped data
* ``fy_mapping(...)`` for dict-shaped data

Example:

.. code-block:: c

   fy_generic service = fy_mapping(
       "name", "api",
       "ports", fy_sequence(8080, 8443),
       "labels", fy_mapping("tier", "edge", "owner", "platform"));

This mirrors the Python binding closely:

.. code-block:: python

   service = {
       "name": "api",
       "ports": [8080, 8443],
       "labels": {"tier": "edge", "owner": "platform"},
   }

Access and Conversion
---------------------

Use ``fy_get()`` and casts to move between generic values and native C types.

.. code-block:: c

   fy_generic server = fy_get(doc, "server", fy_invalid);
   const char *host = fy_get(server, "host", "");
   long long port = fy_get(server, "port", 0LL);

For nested structures, prefer small intermediate values or ``fy_get_at_path()``
when it reads more naturally than repeated ``fy_get()`` calls.

Parsing and Emitting
--------------------

The generic API has its own parse/emit helpers:

* ``fy_parse()`` / ``fy_parse_file()``
* ``fy_emit()`` / ``fy_emit_file()``

Example:

.. code-block:: c

   fy_generic doc = fy_parse(
       "items: [1, 2, 3]",
       FYOPPF_DISABLE_DIRECTORY | FYOPPF_INPUT_TYPE_STRING,
       NULL);

   fy_generic yaml = fy_emit(
       doc,
       FYOPEF_DISABLE_DIRECTORY | FYOPEF_MODE_YAML_1_2 | FYOPEF_STYLE_BLOCK,
       NULL);

Emission returns a generic string value. This is convenient for testing,
transform pipelines, and bindings.

Functional Operations
---------------------

The generic runtime is value-oriented. Collection operations such as
``fy_filter()``, ``fy_map()``, and ``fy_reduce()`` support the same style used
in Python, functional collections, and sum-type/value-tree libraries.

There are parallel variants too: ``fy_pfilter()``, ``fy_pmap()``, and
``fy_preduce()``. These accept an explicit ``struct fy_thread_pool *`` so the
application controls how worker threads are created and reused.

Lambda forms are available for both serial and parallel operations:
``fy_filter_lambda()``, ``fy_map_lambda()``, ``fy_reduce_lambda()``,
``fy_pfilter_lambda()``, ``fy_pmap_lambda()``, and ``fy_preduce_lambda()``.
These are useful for short transforms where an inline expression reads more
clearly than a separate callback function.

Compiler support follows the generic API implementation:

* GCC uses nested-function lambdas
* Clang uses Blocks when built with Blocks support

The build system checks for GCC heap trampoline support and enables
``-ftrampoline-impl=heap`` automatically when it is available. Newer GCC
releases can therefore use the lambda APIs without the executable-stack warning
seen on older toolchains.

Typical flow:

* parse YAML into a generic
* select a sub-value
* transform a sequence
* reduce to a summary value
* install the new values back into a mapping
* emit the final result

See ``examples/generic-transform.c`` for a complete builder-backed example.

Examples
--------

The examples directory contains companion programs for this guide:

* ``generic-literals.c`` for Python-like construction in C
* ``generic-transform.c`` for value-oriented transforms
* ``generic-lambda-capture.c`` for explicit lambda usage with captured local
  variables
* ``generic-parallel-transform.c`` for serial and parallel lambda-based
  transforms over a larger sequence, with timing output and configurable
  item/thread counts
* ``generic-roundtrip.c`` for parse/emit and schema behavior
* ``generic-adoption-bridge.c`` for Python-binding-to-C mental mapping
