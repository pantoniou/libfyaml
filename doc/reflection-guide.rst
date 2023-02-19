Reflection Guide
================

Reflection/meta-type support is the typed half of libfyaml 1.0-alpha1. It uses
type metadata plus YAML annotations to map YAML documents directly into native C
data structures and emit them back again.

The generic runtime covers value-oriented workflows. Reflection covers typed C
data.

Core Objects
------------

There are two core runtime objects:

``struct fy_reflection``
  Owns the type registry for a translation unit or packed blob.

``struct fy_type_context``
  Binds a reflection registry to one entry type and drives parse/emit for that
  typed root.

The normal flow is:

1. load or build a reflection registry
2. choose an entry type
3. create a type context
4. parse YAML into typed C data
5. emit the typed data back as YAML if needed
6. free the produced C data with the same type context

Metadata Annotations
--------------------

Annotations live in YAML-formatted comments in C source. They describe how YAML
collections map onto fields.

Example:

.. code-block:: c

   struct service_port {
       char *name;
       int port;
   };

   struct service_config {
       char *listen;
       int count;
       struct service_port *ports;   // yaml: { key: name, counter: count }
   };

That annotation maps a YAML mapping into an array of ``struct service_port``
and updates the count field.

Backends
--------

libfyaml supports two practical reflection workflows.

Libclang workflow
~~~~~~~~~~~~~~~~~

Use this workflow when reflection should read C headers directly.

* best for development and iteration
* no intermediate packed artifact
* requires a libfyaml build with libclang support

The example ``examples/reflection-libclang.c`` demonstrates this path.

Packed workflow
~~~~~~~~~~~~~~~

Use this workflow when deployment should not depend on runtime libclang.

* author metadata from C headers once
* export a packed blob
* ship the blob with the application
* load the blob at runtime and parse/emit typed data from it

The examples ``examples/reflection-export-packed.c`` and
``examples/reflection-packed.c`` demonstrate this path.

Choosing Between Them
---------------------

Use libclang directly when:

* you are iterating on types
* reflection metadata changes frequently
* you want the simplest authoring loop

Use packed blobs when:

* you want stable deployment artifacts
* runtime environments should not depend on libclang
* startup should consume precomputed metadata

From Generic Values to Typed Data
---------------------------------

Reflection complements the generic runtime. It fits data that already has a
stable C model.

A common progression looks like this:

* start with generics for exploratory parsing and transforms
* settle on a real C structure
* add annotations for collection mapping details
* switch the final parse/emit path to ``fy_type_context``

The workflow provides a direct path from value handling to native typed serdes.

Minimal Typed Parse/Emit Flow
-----------------------------

Create a reflection registry:

.. code-block:: c

   struct fy_reflection *rfl;

   rfl = fy_reflection_from_c_file_with_cflags(
       "reflection-config.h", "", false, true, NULL);

Create a type context:

.. code-block:: c

   struct fy_type_context_cfg cfg = {
       .rfl = rfl,
       .entry_type = "struct service_config",
   };
   struct fy_type_context *ctx = fy_type_context_create(&cfg);

Parse YAML into typed data:

.. code-block:: c

   void *data = NULL;
   fy_type_context_parse(ctx, fyp, &data);

Emit typed data back as YAML:

.. code-block:: c

   fy_type_context_emit(ctx, emit, data,
       FYTCEF_SS | FYTCEF_DS | FYTCEF_DE | FYTCEF_SE);

Then free the produced data with ``fy_type_context_free_data(ctx, data)``.

Introspection
-------------

Reflection is also useful when you need to inspect types. Public accessors such
as ``fy_type_info_lookup()``,
``fy_type_info_get_field_at()``, and ``fy_field_info_get_yaml_comment()`` let
applications inspect the registry and build tooling around it.

This is useful for:

* schema visualisation
* generators
* validation tools
* debug dumps
* authoring pipelines that export packed blobs

Examples
--------

The examples directory contains a complete companion workflow:

* ``reflection-config.h``: annotated schema source
* ``reflection-config.yaml``: sample input
* ``reflection-libclang.c``: direct-header workflow
* ``reflection-export-packed.c``: create packed blob from headers
* ``reflection-packed.c``: runtime load from packed blob

Alpha Caveats
-------------

Reflection is a major alpha feature. Keep the workflow explicit:

* keep authoring-time and deployment-time paths explicit
* prefer small focused annotations over clever ones
* validate the generated types with tests
* ship packed blobs if runtime environments should stay independent from
  libclang
