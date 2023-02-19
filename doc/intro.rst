Introduction
============

libfyaml is a standards-compliant YAML 1.2 processing library with JSON
support and compatibility with older YAML 1.1 behavior where required. The
library places correctness ahead of raw speed. Performance matters, but the
primary goals are correct behavior, stable interfaces, predictable memory use,
and safe operation in ordinary C programs.

The library is organized in three layers:

* the core library for parsing, emitting, event processing, document trees,
  composition, and path-based access
* the generic subsystem for compact value-oriented processing with
  ``fy_generic``
* the reflection subsystem for typed YAML <-> C serialization and
  deserialization

This structure reflects different classes of problems. The core library remains
the foundation. The generic and reflection subsystems exist because some
workloads require a different representation and a different level of
abstraction than the core document API alone.

Core Library Responsibilities
-----------------------------

The core library provides a comprehensive C interface for YAML processing. It
supports both event-based and document-based workflows, along with composition,
document building, path execution, and emission. This is the part of libfyaml
to use when the program needs explicit control over parsing and emission, or
when it needs to work directly with YAML structure in a conventional C style.

The core library is intentionally conservative in character. It is designed to
be stable, memory-conscious, and safe to embed in long-lived C software. It
does not attempt to hide the underlying work. The interfaces are explicit, and
that explicitness is part of the safety model.

The corresponding tradeoff is ergonomics. The core API is comprehensive and
reliable, but it is also verbose. That is expected in a conventional C
document-processing library, and it remains the right model when the program
needs detailed control over events, nodes, formatting, or the mechanics of
YAML processing itself.

For the examples below, assume ``intro-config.yaml`` contains:

.. code-block:: yaml

   host: localhost
   port: 80

A minimal document-based flow that parses this file, changes ``port`` to
``8080``, and emits the result looks like this:

.. code-block:: c

   #include <libfyaml.h>

   struct fy_document *fyd;

   fyd = fy_document_build_from_file(NULL, "intro-config.yaml");
   if (!fyd)
           return -1;

   if (fy_document_insert_at(
                   fyd, "/port", FY_NT,
                   fy_node_buildf(fyd, "8080"))) {
           fy_document_destroy(fyd);
           return -1;
   }

   if (fy_emit_document_to_fp(fyd, FYECF_DEFAULT, stdout)) {
           fy_document_destroy(fyd);
           return -1;
   }

   fy_document_destroy(fyd);

The corresponding example is ``examples/intro-core-update.c``.

Why The Generic Subsystem Exists
--------------------------------

The generic subsystem exists because the core document model is not the best
representation for every workload. It is possible to process data through the
document subsystem, but that model carries the cost of nodes, pointers, and the
other overheads needed to preserve a full YAML document structure. That is
appropriate for document processing. It is less appropriate for large
value-oriented workloads.

The center of the generic subsystem is ``fy_generic``, a compact sum-type
representation of YAML and JSON values. A small scalar can often be stored
directly in the machine word itself. For example, inline integer storage is one
of the reasons the generic subsystem can handle large data sets more
efficiently than the document tree representation. When an application is
concerned with values rather than document nodes, that difference matters.

The generic subsystem also lets the program keep only what is important. A
plain integer scalar does not need positional information, style, comments, or
other document metadata unless the application explicitly chooses to retain it.
That selective retention reduces memory use and keeps the representation close
to the actual data being processed.

A second reason for the generic subsystem is allocator behavior. The stacked
builders and arena-based allocators support deduplication, which reduces memory
consumption significantly on large repeated data sets while maintaining speed.
That makes the generic layer suitable for workloads where the document tree
would be too expensive in space.

A third reason is concurrency. The generic programming style is immutable and
functional. New values are produced from old values rather than modified in
place. That model makes multithreaded processing straightforward because read
sharing requires no locking. The document subsystem is not designed as a
multithreaded processing model at all; the library does not attempt to make
document-based processing a general parallel API.

The final reason is ergonomics. The generic subsystem has a close mapping to
sum-type and value-tree models that are common in Python, TypeScript, Rust, and
similar languages. A user does not need to learn the full surface of the core
document API to work effectively with YAML and JSON values. For many programs,
that is the right balance between performance and usability.

A minimal generic flow for the same ``intro-config.yaml`` file looks like this:

.. code-block:: c

   #include <libfyaml.h>
   #include <libfyaml/libfyaml-generic.h>

   fy_generic doc, updated, emitted;

   doc = fy_parse_file(FYOPPF_DISABLE_DIRECTORY, "intro-config.yaml");
   updated = fy_assoc(doc, "port", 8080);
   emitted = fy_emit(
           updated,
           FYOPEF_DISABLE_DIRECTORY |
           FYOPEF_MODE_YAML_1_2 |
           FYOPEF_STYLE_BLOCK,
           NULL);

   fputs(fy_cast(emitted, ""), stdout);

This example does not check the result of each intermediate call. That is
intentional. ``fy_invalid`` is a no-op carrier in the generic API: operations
propagate it, and the final conversion or use site is where failure is usually
observed. That makes straight-line value transformations practical without
manual error handling after every step.

The corresponding example is ``examples/intro-generic-update.c``.

The detailed type layout, storage rules, and API model are covered in
:doc:`generics-guide`.

Why The Reflection Subsystem Exists
-----------------------------------

The generic subsystem is intentionally schema-less. It represents values, but
it does not assume that there is a declared schema outside the code that uses
those values. Reflection exists to address the other side of the problem:
typed data already described in C, or in an equivalent schema definition, that
should serialize and deserialize with as little handwritten glue code as
possible.

The goal of the reflection subsystem is straightforward. In the ideal case, the
user provides the C data structure and the library handles the rest. That
includes reading YAML into typed C data, emitting typed C data back to YAML,
and deriving part of the validation behavior from the schema that is already
present in the type description.

This matters because a large class of programs does not want a schema-less
value model at runtime. They already have a native representation, and what
they need from the YAML layer is reliable serdes with a low maintenance cost.
Reflection is the subsystem that makes that practical.

The reflection subsystem also covers a broad range of C language features,
including tagged unions and less trivial layout constructs. The fixtures under
``test/reflection-data/030JG6S/00/definition.h`` show this style in practice.
That example contains a compact schema-oriented C definition for a substantial
variant of the YAML 1.2 core schema.

The fixture itself is small enough to read directly:

.. literalinclude:: ../test/reflection-data/030JG6S/00/definition.h
   :language: c

For a small typed configuration, the same ``intro-config.yaml`` example can be
handled with a schema such as:

.. code-block:: c

   struct server_config {
           char *host;
           int port;
   };

The reflection-oriented parse, update, and emit flow then looks like this:

.. code-block:: c

   #include <libfyaml.h>
   #include <libfyaml/libfyaml-reflection.h>

   struct fy_parse_cfg parse_cfg = { .flags = FYPCF_QUIET };
   struct fy_reflection *rfl;
   struct fy_type_context_cfg ctx_cfg = { 0 };
   struct fy_type_context *ctx;
   struct fy_parser *fyp;
   struct fy_emitter_cfg emit_cfg = { .flags = FYECF_DEFAULT };
   struct fy_emitter *emit;
   struct server_config *cfg = NULL;

   rfl = fy_reflection_from_c_file_with_cflags(
           "reflection-intro-config.h", "", false, true, NULL);
   if (!rfl)
           return -1;

   ctx_cfg.rfl = rfl;
   ctx_cfg.entry_type = "struct server_config";
   ctx = fy_type_context_create(&ctx_cfg);
   if (!ctx)
           return -1;

   fyp = fy_parser_create(&parse_cfg);
   if (!fyp || fy_parser_set_input_file(fyp, "intro-config.yaml") != 0)
           return -1;

   if (fy_type_context_parse(ctx, fyp, (void **)&cfg) != 0 || !cfg)
           return -1;

   cfg->port = 8080;

   emit = fy_emitter_create(&emit_cfg);
   if (!emit)
           return -1;

   if (fy_type_context_emit(
                   ctx, emit, cfg,
                   FYTCEF_SS | FYTCEF_DS | FYTCEF_DE | FYTCEF_SE) != 0)
           return -1;

The corresponding example is ``examples/intro-reflection-update.c``.

The end-to-end workflow, including reflection metadata sources and packed
representations, is described in :doc:`reflection-guide`.

Choosing The Right Layer
------------------------

Use the core library when the program needs a comprehensive and safe C-style
YAML processing library with explicit control over parsing, events, documents,
and emission.

Use the generic subsystem when the program is value-oriented, when the user is
coming from sum-type languages and wants a familiar model, or when the
workload involves large amounts of data and needs a compact, efficient
representation.

Use the reflection subsystem when a schema already exists as C structures or a
schema definition, and the goal is low-friction serialization and
deserialization with a useful amount of validation derived from that schema.

Further Reading
---------------

The following pages provide the subsystem guides and API references that build
on this introduction:

* :doc:`libfyaml-core`
* :doc:`generics-guide`
* :doc:`reflection-guide`
* :doc:`libfyaml-generics`
* :doc:`libfyaml-reflection`
